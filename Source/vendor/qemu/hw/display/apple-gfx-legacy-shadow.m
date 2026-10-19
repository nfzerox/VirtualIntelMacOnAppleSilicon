/* Local Rosetta bring-up experiment; not an upstream contribution.
 * macOS 26 Intel PVG's 4K fragmented mappings cannot always be imported by
 * Apple Silicon Metal. Preserve zero-copy when possible; stage rejected maps.
 * Standalone diagnostic build uses -DAPPLE_GFX_SHADOW_STANDALONE. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <stdint.h>
#include <string.h>

/* Local Intel-on-Apple-GPU compatibility experiment. Catalina's incremental
 * scanout passes can request DontCare while relying on untouched pixels.
 * Preserve IOSurface color attachments only during guest command decoding;
 * never alter explicit clears, depth/stencil, or host presentation passes. */
static __thread unsigned guest_render_depth;
static IMP original_render_encoder;
static IMP original_library_data;
extern dispatch_data_t PVGHostRepairLibrary(dispatch_data_t) __attribute__((weak_import));
static unsigned library_capture_sequence;
static id capture_library_data(id self, SEL cmd, dispatch_data_t data, NSError **error)
{
    /* Opt-in, bounded diagnostic capture before the compiler sees the bytes.
     * Not enabled by normal launch and never changes the submitted library. */
    unsigned sequence = __atomic_fetch_add(&library_capture_sequence, 1, __ATOMIC_RELAXED);
    if (getenv("PVG_CAPTURE_LIBRARIES") && sequence < 256) {
        const void *bytes = NULL;
        size_t size = 0;
        dispatch_data_t mapped = dispatch_data_create_map(data, &bytes, &size);
        if (mapped) {
            NSString *path = [NSString stringWithFormat:@"%s/library-%d-%03u.metallib",
                getenv("PVG_CAPTURE_LIBRARIES"), getpid(), sequence];
            BOOL saved = [[NSData dataWithBytesNoCopy:(void *)bytes length:size
                                         freeWhenDone:NO] writeToFile:path atomically:YES];
            fprintf(stderr, "PVG library capture %u bytes=%zu saved=%d\n", sequence, size, saved);
            dispatch_release(mapped);
        }
    }
    dispatch_data_t repaired = PVGHostRepairLibrary ? PVGHostRepairLibrary(data) : NULL;
    id library = ((id (*)(id, SEL, dispatch_data_t, NSError **))original_library_data)(self, cmd, repaired ?: data, error);
    if (repaired) dispatch_release(repaired);
    return library;
}
static id preserving_render_encoder(id self, SEL cmd, MTLRenderPassDescriptor *pass)
{
    MTLRenderPassDescriptor *adjusted = nil;
    if (guest_render_depth) {
        for (NSUInteger i = 0; i < 8; i++) {
            MTLRenderPassColorAttachmentDescriptor *a = pass.colorAttachments[i];
            if (a.loadAction == MTLLoadActionDontCare && a.texture.iosurface &&
                a.texture.storageMode != MTLStorageModeMemoryless) {
                if (!adjusted) adjusted = [pass copy];
                adjusted.colorAttachments[i].loadAction = MTLLoadActionLoad;
            }
        }
    }
    id result = ((id (*)(id, SEL, id))original_render_encoder)(
        self, cmd, adjusted ?: pass);
    [adjusted release];
    return result;
}

@interface AppleGFXLegacyShadow : NSObject {
@public
    void *guest;
    NSUInteger length;
    NSArray *pages;
    id<MTLBuffer> baseline;
}
@end
@implementation AppleGFXLegacyShadow
- (void)dealloc { [baseline release]; [pages release]; [super dealloc]; }
@end

static char shadow_key;
static char order_key;
static char pipeline_key;
@interface AppleGFXShadowCompletion : NSObject {
@public
    BOOL donorDone, gpuDone;
    uint32_t result;
    NSError *failure;
    void (^callback)(uint32_t, NSError *);
}
- (void)finishDonor:(BOOL)donor result:(uint32_t)value error:(NSError *)error;
@end
@implementation AppleGFXShadowCompletion
- (void)finishDonor:(BOOL)donor result:(uint32_t)value error:(NSError *)error
{
    void (^ready)(uint32_t, NSError *) = nil;
    @synchronized (self) {
        if (donor) { donorDone = YES; result = value; } else gpuDone = YES;
        if (error && !failure) failure = [error retain];
        if (donorDone && gpuDone) { ready = callback; callback = nil; }
    }
    if (ready) { ready(result, failure); [ready release]; }
}
- (void)dealloc { [callback release]; [failure release]; [super dealloc]; }
@end
@interface AppleGFXShadowOrder : NSObject {
@public
    id<MTLEvent> event;
    uint64_t value;
    uint64_t completedValue;
}
@end
@implementation AppleGFXShadowOrder
- (void)dealloc { [event release]; [super dealloc]; }
@end
static IMP original_needs_prepare, original_exec;
static IMP original_backing_prepare, original_backing_sync;
static IMP original_private_prepare, original_private_sync;
static Ivar shared_buffer;
static id send_id(id obj, const char *name)
{
    return ((id (*)(id, SEL))objc_msgSend)(obj, sel_registerName(name));
}

static id legacy_buffer(id self, SEL cmd, uint32_t page, uint64_t length)
{
    void *bytes = ((void *(*)(id, SEL, uint32_t, uint64_t))objc_msgSend)(
        self, sel_registerName("mappedAddressForPageNumber:length:"), page, length);
    if (!bytes) [NSException raise:NSInternalInconsistencyException
                            format:@"PVG legacy mapping failed"];
    id<MTLDevice> device = send_id(self, "mtlDevice");
    id<MTLBuffer> buffer = [device newBufferWithBytesNoCopy:bytes length:length
        options:MTLResourceStorageModeShared deallocator:nil];
    if (buffer) return buffer;
    buffer = [device newBufferWithBytes:bytes length:length
        options:MTLResourceStorageModeShared];
    if (!buffer) [NSException raise:NSMallocException
                             format:@"PVG legacy shadow allocation failed"];
    AppleGFXLegacyShadow *shadow = [AppleGFXLegacyShadow new];
    shadow->guest = bytes;
    shadow->length = length;
    shadow->baseline = [device newBufferWithBytes:bytes length:length
        options:MTLResourceStorageModeShared];
    if (!shadow->baseline) abort();
    NSMutableArray *pages = [NSMutableArray array];
    BOOL coalesce = getenv("PVG_SHADOW_COALESCE") != NULL;
    NSUInteger maximumChunk = 65536;
    const char *chunkSetting = getenv("PVG_SHADOW_MAX_CHUNK");
    if (chunkSetting) {
        char *end;
        unsigned long parsed = strtoul(chunkSetting, &end, 10);
        if (end != chunkSetting && !*end && parsed >= 4096 &&
            parsed <= 1048576 && !(parsed & (parsed - 1))) maximumChunk = parsed;
    }
    NSUInteger nextChunk = maximumChunk;
    for (NSUInteger offset = 0; offset < length;) {
        // Bounded speculative import, never infer compatibility from virtual
        // adjacency. Metal must accept the actual no-copy range. Fall back to
        // the existing single-page path on fragmented backing.
        NSUInteger chunk = coalesce ? nextChunk : 4096;
        while (chunk > length - offset && chunk > 4096) chunk /= 2;
        id<MTLBuffer> pageBuffer = nil;
        for (;;) {
            pageBuffer = [device newBufferWithBytesNoCopy:(char *)bytes + offset
                length:chunk options:MTLResourceStorageModeShared deallocator:nil];
            if (pageBuffer || chunk == 4096) break;
            chunk /= 2;
        }
        if (!pageBuffer) [NSException raise:NSInternalInconsistencyException
            format:@"PVG single-page Metal import failed"];
        [pages addObject:pageBuffer];
        [pageBuffer release];
        offset += chunk;
        /* Probe growth after a success, but do not retry all large sizes
         * for every 4K page in a persistently fractured region. */
        nextChunk = MIN(maximumChunk, chunk * 2);
    }
    shadow->pages = [pages copy];
    objc_setAssociatedObject(buffer, &shadow_key, shadow,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [shadow release];
    static unsigned count;
    if (__atomic_fetch_add(&count, 1, __ATOMIC_RELAXED) < 12)
        fprintf(stderr, "PVG legacy shadow: page=%u length=%llu runs=%lu coalesce=%d\n",
                page, length, (unsigned long)pages.count, coalesce);
    return buffer;
}

static BOOL needs_prepare(id self, SEL cmd)
{
    id buffer = object_getIvar(self, shared_buffer);
    if (objc_getAssociatedObject(buffer, &shadow_key)) return YES;
    return ((BOOL (*)(id, SEL))original_needs_prepare)(self, cmd);
}

static void transfer(id<MTLBuffer> buffer, id<MTLBlitCommandEncoder> encoder, BOOL upload)
{
    AppleGFXLegacyShadow *shadow = objc_getAssociatedObject(buffer, &shadow_key);
    if (!shadow) return;
    NSUInteger offset = 0;
    for (id<MTLBuffer> page in shadow->pages) {
        NSUInteger size = MIN(page.length, shadow->length - offset);
        if (upload) [encoder copyFromBuffer:page sourceOffset:0 toBuffer:buffer
            destinationOffset:offset size:size];
        else [encoder copyFromBuffer:buffer sourceOffset:offset toBuffer:page
            destinationOffset:0 size:size];
        offset += size;
    }
    if (upload) [encoder copyFromBuffer:buffer sourceOffset:0
        toBuffer:shadow->baseline destinationOffset:0 size:shadow->length];
}

static BOOL cpu_paging(id self, SEL cmd) { return NO; }
static BOOL needs_sync(id self, SEL cmd)
{
    return objc_getAssociatedObject(object_getIvar(self, shared_buffer), &shadow_key) != nil;
}
static void prepare(id self, SEL cmd, id encoder)
{
    send_id(self, "ensurePhysical");
    transfer(object_getIvar(self, shared_buffer), encoder, YES);
}
static void synchronize(id self, SEL cmd, id encoder)
{
    /* Shared storage can be CPU-written in disjoint regions while the GPU is
     * using it. Whole-buffer scatter clobbers those writes. The execution tail
     * selectively scatters GPU-changed bytes before publishing completion. */
}

static id<MTLComputePipelineState> scatter_pipeline(id<MTLDevice> device)
{
    @synchronized (device) {
        id pipeline = objc_getAssociatedObject(device, &pipeline_key);
        if (pipeline) return pipeline;
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:
            @"#include <metal_stdlib>\nusing namespace metal;\n"
             "kernel void scatter(device const uchar *after [[buffer(0)]],"
             "device const uchar *before [[buffer(1)]],"
             "device uchar *guest [[buffer(2)]], uint i [[thread_position_in_grid]])"
             "{ if (after[i] != before[i]) guest[i] = after[i]; }"
            options:nil error:&error];
        id function = [library newFunctionWithName:@"scatter"];
        pipeline = function ? [device newComputePipelineStateWithFunction:function
            error:&error] : nil;
        [function release]; [library release];
        if (!pipeline) [NSException raise:NSInternalInconsistencyException
            format:@"PVG selective scatter compile failed: %@", error];
        objc_setAssociatedObject(device, &pipeline_key, pipeline,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [pipeline release];
        return pipeline;
    }
}
static void backing_prepare(id self, SEL cmd, id encoder)
{
    send_id(self, "ensurePhysical");
    Ivar ivar = class_getInstanceVariable(object_getClass(self), "_transferBuffer");
    transfer(object_getIvar(self, ivar), encoder, YES);
    ((void (*)(id, SEL, id))original_backing_prepare)(self, cmd, encoder);
}
static void backing_sync(id self, SEL cmd, id encoder)
{
    ((void (*)(id, SEL, id))original_backing_sync)(self, cmd, encoder);
    Ivar ivar = class_getInstanceVariable(object_getClass(self), "_transferBuffer");
    transfer(object_getIvar(self, ivar), encoder, NO);
}

/* PGPrivateBufferResource owns its own transfer buffer and does not inherit
 * PGBackingResource's paging methods. The donor's ensurePhysical creates that
 * legacy mapping; mirror it around the donor's ordinary private-buffer blits.
 * Unlike shared buffers, managed/private staging follows explicit ownership. */
static void private_prepare(id self, SEL cmd, id encoder)
{
    send_id(self, "ensurePhysical");
    Ivar ivar = class_getInstanceVariable(object_getClass(self), "_transferBuffer");
    transfer(object_getIvar(self, ivar), encoder, YES);
    ((void (*)(id, SEL, id))original_private_prepare)(self, cmd, encoder);
}
static void private_sync(id self, SEL cmd, id encoder)
{
    ((void (*)(id, SEL, id))original_private_sync)(self, cmd, encoder);
    Ivar ivar = class_getInstanceVariable(object_getClass(self), "_transferBuffer");
    transfer(object_getIvar(self, ivar), encoder, NO);
}

/* These texture classes inherit PGTextureResource, not PGBackingResource.
 * Hook the actual paging operation, inside the donor's validity checks, so
 * already-valid GPU textures are not overwritten by stale guest bytes.
 * Compute paging is deliberately excluded: a compute encoder cannot blit. */
static const char *texture_classes[] = {
    "PGSinglePlaneTextureResource",
    "PGDualPlaneBlitTextureResource",
    "PGSinglePlaneSharedTextureBackingResource",
    "PGDualPlaneSharedTextureBackingResource",
};
static IMP original_texture_page[4];
static void texture_page(unsigned index, id self, SEL cmd, id encoder,
                         BOOL download)
{
    Ivar ivar = class_getInstanceVariable(object_getClass(self), "_transferBuffer");
    if (!ivar) abort();
    id buffer = object_getIvar(self, ivar);
    if (!download) transfer(buffer, encoder, YES);
    ((void (*)(id, SEL, id, BOOL))original_texture_page[index])(
        self, cmd, encoder, download);
    if (download) transfer(object_getIvar(self, ivar), encoder, NO);
}
#define TEXTURE_PAGE_WRAPPER(N) \
static void texture_page_##N(id self, SEL cmd, id encoder, BOOL download) \
{ texture_page(N, self, cmd, encoder, download); }
TEXTURE_PAGE_WRAPPER(0)
TEXTURE_PAGE_WRAPPER(1)
TEXTURE_PAGE_WRAPPER(2)
TEXTURE_PAGE_WRAPPER(3)
#undef TEXTURE_PAGE_WRAPPER

/* Verified donor PGResourceList vtable returns id | flags << 32. */
typedef struct LegacyResourceList {
    uint64_t (**vtable)(void *, uint64_t);
    uint64_t count;
} LegacyResourceList;
typedef struct ShadowResourceList {
    LegacyResourceList base;
    LegacyResourceList *original;
    id task;
} ShadowResourceList;
static uint64_t get_entry(void *list, uint64_t index)
{
    ShadowResourceList *wrapper = list;
    uint64_t entry = wrapper->original->vtable[0](wrapper->original, index);
    // Resolve only when the donor enumerates: doExecIndirect first refreshes
    // the object list. Eager resolution before that initialization is invalid.
    id resource = ((id (*)(id, SEL, uint32_t))objc_msgSend)(wrapper->task,
        sel_registerName("getHostResource:"), (uint32_t)entry);
    /* Shared storage relies on coherence: even GPU-written buffers can arrive
     * with flags=0 (verified with a compute output buffer). Request download
     * for every shared buffer, not only those explicitly marked GPU-written.
     * First enumeration precedes ensurePhysical, so do not require an existing
     * shadow here. needsSync filters zero-copy maps after initialization. */
    if ([resource isKindOfClass:NSClassFromString(@"PGSharedBufferResource")])
        entry |= UINT64_C(4) << 32;
    return entry;
}

static void execute(id self, SEL cmd, uint32_t count, void *commands,
                    LegacyResourceList *resources, id event, uint64_t *value,
                    void (^completion)(uint32_t, NSError *))
{
    uint64_t (*vtable[])(void *, uint64_t) = {get_entry};
    ShadowResourceList list = {{vtable, resources->count}, resources, self};
    @synchronized (self) {
        AppleGFXShadowOrder *order = objc_getAssociatedObject(self, &order_key);
        if (!order) {
            order = [AppleGFXShadowOrder new];
            order->event = [send_id(self, "mtlDevice") newEvent];
            if (!order->event) abort();
            objc_setAssociatedObject(self, &order_key, order,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            [order release];
        }
        id device = send_id(self, "device");
        uint64_t completed;
        @synchronized (order) { completed = order->completedValue; }
        if (order->value > completed) {
            /* Donor FIFO event signals rendering completion, not completion
             * of our additional scatter on blitDownQueue. Fence both paging
             * and execution before the next submission touches shadow data. */
            Ivar paging = class_getInstanceVariable(object_getClass(self), "_pagingQueue");
            if (!paging) abort();
            id<MTLCommandQueue> queues[] = {object_getIvar(self, paging),
                                           send_id(device, "execQueue")};
            for (unsigned i = 0; i < 2; i++) {
                id<MTLCommandBuffer> barrier = [queues[i] commandBuffer];
                [barrier encodeWaitForEvent:order->event value:order->value];
                [barrier commit];
            }
        }
        AppleGFXShadowCompletion *done = [AppleGFXShadowCompletion new];
        done->callback = [completion copy];
        guest_render_depth++;
        @try {
        ((void (*)(id, SEL, uint32_t, void *, void *, id, uint64_t *, id))
            original_exec)(self, cmd, count, commands, &list, event, value,
                ^(uint32_t result, NSError *error) {
                    [done finishDonor:YES result:result error:error];
                });
        } @finally {
            guest_render_depth--;
        }
        NSMutableSet *buffers = [NSMutableSet set];
        for (uint64_t i = 0; i < resources->count; i++) {
            uint64_t entry = resources->vtable[0](resources, i);
            id resource = ((id (*)(id, SEL, uint32_t))objc_msgSend)(self,
                sel_registerName("getExistingHostResource:"), (uint32_t)entry);
            if (![resource isKindOfClass:NSClassFromString(@"PGSharedBufferResource")]) continue;
            id buffer = object_getIvar(resource, shared_buffer);
            if (objc_getAssociatedObject(buffer, &shadow_key)) [buffers addObject:buffer];
        }
        if (!buffers.count) {
            /* No extra GPU writes: donor completion is sufficient. Keep the
             * last real scatter fence for later submissions, but do not add
             * an empty blitDown submission or advance its sequence. */
            [done finishDonor:NO result:0 error:nil];
            [done release];
            return;
        }
        id<MTLCommandBuffer> tail = [send_id(device, "blitDownQueue") commandBuffer];
        [tail encodeWaitForEvent:event value:*value];
        if (buffers.count) {
            id<MTLComputeCommandEncoder> compute = [tail computeCommandEncoder];
            [compute setComputePipelineState:scatter_pipeline(send_id(self, "mtlDevice"))];
            for (id<MTLBuffer> buffer in buffers) {
                AppleGFXLegacyShadow *shadow = objc_getAssociatedObject(buffer, &shadow_key);
                NSUInteger offset = 0;
                for (id<MTLBuffer> page in shadow->pages) {
                    NSUInteger size = MIN(page.length, shadow->length - offset);
                    [compute setBuffer:buffer offset:offset atIndex:0];
                    [compute setBuffer:shadow->baseline offset:offset atIndex:1];
                    [compute setBuffer:page offset:0 atIndex:2];
                    [compute dispatchThreads:MTLSizeMake(size, 1, 1)
                        threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                    offset += size;
                }
            }
            [compute endEncoding];
        }
        uint64_t submitted = ++order->value;
        [tail encodeSignalEvent:order->event value:submitted];
        [tail addCompletedHandler:^(id<MTLCommandBuffer> command) {
            /* Only successful GPU completion retires a dependency. The
             * callback retains order; publish under its own short lock, not
             * the task lock held by submission. Never erase an in-flight
             * newer fence, and keep error paths conservatively fenced. */
            if (command.status == MTLCommandBufferStatusCompleted) {
                @synchronized (order) {
                    order->completedValue = MAX(order->completedValue, submitted);
                }
            }
            [done finishDonor:NO result:0 error:command.error];
        }];
        [tail commit];
        [done release];
    }
}

static IMP replace(Class cls, const char *name, IMP replacement)
{
    SEL selector = sel_registerName(name);
    Method method = class_getInstanceMethod(cls, selector);
    if (!method) abort();
    IMP old = method_getImplementation(method);
    class_replaceMethod(cls, selector, replacement, method_getTypeEncoding(method));
    return old;
}

#ifdef APPLE_GFX_SHADOW_STANDALONE
__attribute__((constructor))
#endif
void apple_gfx_legacy_shadow_install(void)
{
    Class task = NSClassFromString(@"PGLocalTask");
    Class shared = NSClassFromString(@"PGSharedBufferResource");
    if (!getenv("PVG_LEGACY_SHADOW")) return;
    if (getenv("PVG_CAPTURE_LIBRARIES") || (PVGHostRepairLibrary && getenv("PVG_HOST_AIR_LLVM"))) {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        original_library_data = replace(object_getClass(device),
            "newLibraryWithData:error:", (IMP)capture_library_data);
        [device release];
    }
    const char *preserve = getenv("PVG_PRESERVE_IOSURFACE");
    if (preserve && strcmp(preserve, "0") != 0) {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id buffer = [queue commandBuffer];
        original_render_encoder = replace(object_getClass(buffer),
            "renderCommandEncoderWithDescriptor:", (IMP)preserving_render_encoder);
        [queue release];
        [device release];
    }
    /* The Metal hooks above apply to every framework. The shadow itself needs
     * the task/resource classes of newer frameworks; macOS 13's has neither. */
    if (!task || !shared) {
        fprintf(stderr, "PVG legacy shadow: framework has no legacy task classes; shadow disabled\n");
        return;
    }
    shared_buffer = class_getInstanceVariable(shared, "_buffer");
    if (!shared_buffer) abort();
    replace(task, "newLegacyBufferForVirtualPage:length:", (IMP)legacy_buffer);
    replace(shared, "prepareInEncoder:", (IMP)prepare);
    replace(shared, "synchronizeInEncoder:", (IMP)synchronize);
    /* macOS 26 adds CPU paging for shared buffers; keep the encoder path we
     * shadow. Earlier frameworks (macOS 15) only have the encoder path. */
    if (class_getInstanceMethod(shared, sel_registerName("needsCPUPaging")))
        replace(shared, "needsCPUPaging", (IMP)cpu_paging);
    replace(shared, "needsSync", (IMP)needs_sync);
    original_needs_prepare = replace(shared, "needsPrepare", (IMP)needs_prepare);
    Class backing = NSClassFromString(@"PGBackingResource");
    original_backing_prepare = replace(backing, "prepareInEncoder:", (IMP)backing_prepare);
    original_backing_sync = replace(backing, "synchronizeInEncoder:", (IMP)backing_sync);
    if (getenv("PVG_SHADOW_PRIVATE_PAGING")) {
        Class private = NSClassFromString(@"PGPrivateBufferResource");
        if (!private || !class_getInstanceVariable(private, "_transferBuffer")) abort();
        original_private_prepare = replace(private, "prepareInEncoder:", (IMP)private_prepare);
        original_private_sync = replace(private, "synchronizeInEncoder:", (IMP)private_sync);
    }
    if (getenv("PVG_SHADOW_TEXTURE_PAGING")) {
        IMP hooks[] = {(IMP)texture_page_0, (IMP)texture_page_1,
                       (IMP)texture_page_2, (IMP)texture_page_3};
        for (unsigned i = 0; i < 4; i++) {
            Class cls = objc_getClass(texture_classes[i]);
            if (!cls || !class_getInstanceVariable(cls, "_transferBuffer")) abort();
            original_texture_page[i] = replace(cls,
                "pageTextureInEncoder:isDownload:", hooks[i]);
        }
    }
    original_exec = replace(task,
        "doExecIndirect:cmdBuffers:resourceList:fifoEvent:fifoEventValue:completionHandler:",
        (IMP)execute);
    fprintf(stderr, "PVG legacy shadow experiment enabled\n");
}
