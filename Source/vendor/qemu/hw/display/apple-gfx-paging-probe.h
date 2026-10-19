/* Local experiment. Transfer and verbose diagnostics are independently opt-in. */
#import <objc/runtime.h>
#include <stdatomic.h>
#include <execinfo.h>
#include "apple-gfx-paging-encoder.h"
#include "apple-gfx-msaa-transfer.h"

static __thread unsigned apple_gfx_paging_scope;
static bool apple_gfx_msaa_transfer_enabled;
static bool apple_gfx_paging_trace_enabled;
static IMP apple_gfx_original_prepare_resources;
typedef struct AppleGFXPrepareSync {
    bool first, second;
    id event;
    uint64_t value;
} AppleGFXPrepareSync;

static id apple_gfx_paging_blit(id self, SEL selector)
{
    if (apple_gfx_paging_scope) {
        return [[[AppleGFXPagingEncoder alloc] initWithCommandBuffer:self] autorelease];
    }
    return ((id (*)(id, SEL))apple_gfx_paging_native_blit)(self, selector);
}

static AppleGFXPrepareSync apple_gfx_prepare_resources(id self, SEL selector,
                                                      void *resources, id event,
                                                      uint64_t value)
{
    apple_gfx_paging_scope++;
    @try {
        return ((AppleGFXPrepareSync (*)(id, SEL, void *, id, uint64_t))
            apple_gfx_original_prepare_resources)(self, selector, resources, event, value);
    } @finally {
        apple_gfx_paging_scope--;
    }
}

static IMP apple_gfx_original_page_texture;
static atomic_uint apple_gfx_page_trace_count;
static atomic_uint apple_gfx_depth_page_trace_count;
static IMP apple_gfx_original_guest_valid;
static atomic_uint apple_gfx_valid_trace_count;
static atomic_uint apple_gfx_download_trace_count;
static atomic_uint apple_gfx_sync_trace_count;
static IMP apple_gfx_original_sync_resources;
static char apple_gfx_seen_msaa_page_key;

/* ABI verified from runtime metadata and the framework's completion block. */
static void apple_gfx_trace_sync_resources(id self, SEL selector,
                                         const uint32_t *resources,
                                         uint32_t count, BOOL discard,
                                         void (^completion)(uint32_t, NSError *))
{
    unsigned sequence = apple_gfx_paging_trace_enabled ?
        atomic_fetch_add(&apple_gfx_sync_trace_count, 1) : 0;
    bool trace = apple_gfx_paging_trace_enabled && sequence < 120;
    if (trace) {
        fprintf(stderr, "PVG sync begin sequence=%u count=%u discard=%d\n",
                sequence, count, discard);
    }
    if (apple_gfx_msaa_transfer_enabled) apple_gfx_paging_scope++;
    @try {
    if (!apple_gfx_paging_trace_enabled) {
        ((void (*)(id, SEL, const uint32_t *, uint32_t, BOOL, id))
            apple_gfx_original_sync_resources)(self, selector, resources, count,
                                              discard, completion);
        return;
    }
    ((void (*)(id, SEL, const uint32_t *, uint32_t, BOOL, id))
        apple_gfx_original_sync_resources)(self, selector, resources, count,
        discard, ^(uint32_t status, NSError *error) {
            if (trace || error) {
                fprintf(stderr, "PVG sync complete sequence=%u status=%u error=%s\n",
                        sequence, status, error.description.UTF8String ?: "none");
            }
            completion(status, error);
        });
    } @finally {
        if (apple_gfx_msaa_transfer_enabled) apple_gfx_paging_scope--;
    }
    if (trace) {
        fprintf(stderr, "PVG sync returned sequence=%u\n", sequence);
    }
}

/* Observe validity transitions without changing their ordering or value. */
static void apple_gfx_trace_guest_valid(id self, SEL selector, BOOL valid)
{
    ((void (*)(id, SEL, BOOL))apple_gfx_original_guest_valid)(self, selector, valid);
    if (!valid) {
        return;
    }
    Ivar texture_ivar = class_getInstanceVariable(object_getClass(self), "_texture");
    id<MTLTexture> texture = texture_ivar ? object_getIvar(self, texture_ivar) : nil;
    if (![texture conformsToProtocol:@protocol(MTLTexture)] ||
        [(id<MTLTexture>)texture pixelFormat] != MTLPixelFormatDepth32Float_Stencil8 ||
        atomic_fetch_add(&apple_gfx_valid_trace_count, 1) >= 40) {
        return;
    }
    void *frames[16];
    int count = backtrace(frames, 16);
    flockfile(stderr);
    fprintf(stderr, "PVG guest-valid object=%p size=%lux%lu samples=%lu\n",
            self, (unsigned long)[texture width], (unsigned long)[texture height],
            (unsigned long)[texture sampleCount]);
    backtrace_symbols_fd(frames, count, fileno(stderr));
    funlockfile(stderr);
}

static void apple_gfx_trace_page_texture(id self, SEL selector, id encoder,
                                       BOOL download)
{
    unsigned sequence = apple_gfx_paging_trace_enabled ?
        atomic_fetch_add(&apple_gfx_page_trace_count, 1) : 0;
    Ivar texture_ivar = class_getInstanceVariable(object_getClass(self), "_texture");
    id<MTLTexture> page_texture = texture_ivar ? object_getIvar(self, texture_ivar) : nil;
    bool packed_depth = page_texture &&
        page_texture.pixelFormat == MTLPixelFormatDepth32Float_Stencil8;
    if (apple_gfx_paging_trace_enabled && (sequence < 3 || (download &&
        atomic_fetch_add(&apple_gfx_download_trace_count, 1) < 120) || (packed_depth &&
        atomic_fetch_add(&apple_gfx_depth_page_trace_count, 1) < 80))) {
        flockfile(stderr);
        fprintf(stderr, "PVG page sequence=%u object=%p download=%d\n",
                sequence, self, download);
        /* Header/level offsets verified in pageTextureInEncoder disassembly.
         * Read only the first level, which the original method will consume.
         */
        Ivar dimensions = class_getInstanceVariable(object_getClass(self), "_dimension");
        if (dimensions) {
            const uint8_t *bytes = NULL;
            memcpy(&bytes, (uint8_t *)(void *)self + ivar_getOffset(dimensions), sizeof(bytes));
            if (bytes) {
                uint16_t flags, arrays;
                memcpy(&flags, bytes, 2);
                memcpy(&arrays, bytes + 2, 2);
                if ((flags & 0x3fff) && arrays) {
                    uint64_t base, slice_stride, level_offset, level_length, row_stride;
                    uint32_t width, height, depth;
                    memcpy(&base, bytes + 4, 8);
                    memcpy(&slice_stride, bytes + 12, 8);
                    memcpy(&level_offset, bytes + 24, 8);
                    memcpy(&level_length, bytes + 32, 8);
                    memcpy(&row_stride, bytes + 40, 8);
                    memcpy(&width, bytes + 48, 4);
                    memcpy(&height, bytes + 52, 4);
                    memcpy(&depth, bytes + 56, 4);
                    fprintf(stderr, "  paging flags=%x arrays=%u base=%llu slice=%llu levelOffset=%llu levelLength=%llu row=%llu size=%ux%ux%u blocks=%u,%u,%u,%u\n",
                            flags, arrays, (unsigned long long)base,
                            (unsigned long long)slice_stride, (unsigned long long)level_offset,
                            (unsigned long long)level_length, (unsigned long long)row_stride,
                            width, height, depth, bytes[20], bytes[21], bytes[22], bytes[23]);
                }
            }
        }
        for (Class cls = object_getClass(self); cls && cls != [NSObject class];
             cls = class_getSuperclass(cls)) {
            unsigned count = 0;
            Ivar *ivars = class_copyIvarList(cls, &count);
            for (unsigned i = 0; i < count; i++) {
                const char *type = ivar_getTypeEncoding(ivars[i]);
                const char *name = ivar_getName(ivars[i]);
                if (type[0] == '@') {
                    id value = object_getIvar(self, ivars[i]);
                    fprintf(stderr, "  %s=%p class=%s", name, value,
                            value ? object_getClassName(value) : "nil");
                    if ([value conformsToProtocol:@protocol(MTLTexture)]) {
                        id<MTLTexture> texture = value;
                        fprintf(stderr, " format=%lu size=%lux%lu storage=%lu type=%lu samples=%lu",
                                (unsigned long)texture.pixelFormat,
                                (unsigned long)texture.width,
                                (unsigned long)texture.height,
                                (unsigned long)texture.storageMode,
                                (unsigned long)texture.textureType,
                                (unsigned long)texture.sampleCount);
                    }
                    fputc('\n', stderr);
                } else if (strchr("QqIiBc^", type[0])) {
                    NSUInteger size = 0;
                    NSGetSizeAndAlignment(type, &size, NULL);
                    uint64_t value = 0;
                    memcpy(&value, (uint8_t *)(void *)self + ivar_getOffset(ivars[i]),
                           MIN(size, sizeof(value)));
                    fprintf(stderr, "  %s type=%s value=0x%llx\n", name, type,
                            (unsigned long long)value);
                }
            }
            free(ivars);
        }
        funlockfile(stderr);
    }
    if (apple_gfx_msaa_transfer_enabled && packed_depth) {
        Class cls = object_getClass(self);
        id<MTLTexture> texture = object_getIvar(self, class_getInstanceVariable(cls, "_texture"));
        // The same interleaved guest representation is invalid for a raw
        // Metal blit at 1x too (Preview's selection layer uses that case).
        if (texture.sampleCount >= 1) {
            if (![encoder isKindOfClass:[AppleGFXPagingEncoder class]]) {
                [NSException raise:NSInternalInconsistencyException format:@"MSAA paging outside ordered adapter"];
            }
            Ivar dim = class_getInstanceVariable(cls, "_dimension");
            const uint8_t *bytes = NULL;
            if (!dim) {
                [NSException raise:NSInternalInconsistencyException format:@"Missing MSAA dimension metadata"];
            }
            memcpy(&bytes, (uint8_t *)(void *)self + ivar_getOffset(dim), sizeof(bytes));
            if (!bytes) {
                [NSException raise:NSInvalidArgumentException format:@"Missing MSAA dimension record"];
            }
            uint16_t flags, arrays;
            uint64_t base, level, row;
            uint32_t width, height, depth;
            memcpy(&flags, bytes, 2); memcpy(&arrays, bytes + 2, 2);
            memcpy(&base, bytes + 4, 8); memcpy(&level, bytes + 24, 8);
            memcpy(&row, bytes + 40, 8); memcpy(&width, bytes + 48, 4);
            memcpy(&height, bytes + 52, 4); memcpy(&depth, bytes + 56, 4);
            if (flags != 1 || arrays != 1 || width != texture.width ||
                height != texture.height || depth != 1 || level > UINT64_MAX - base) {
                [NSException raise:NSInvalidArgumentException format:@"Unsupported MSAA dimension record"];
            }
            Ivar buffer_ivar = class_getInstanceVariable(cls, "_transferBuffer");
            id<MTLBuffer> buffer = buffer_ivar ? object_getIvar(self, buffer_ivar) : nil;
            if (!buffer) {
                [NSException raise:NSInvalidArgumentException format:@"Missing MSAA transfer buffer"];
            }
            [(AppleGFXPagingEncoder *)encoder performTransfer:^(id<MTLCommandBuffer> commands) {
                [[AppleGFXMSAATransfer forDevice:texture.device] encode:commands texture:texture
                    buffer:buffer offset:base + level row:row download:download];
            }];
            if (apple_gfx_paging_trace_enabled) {
                fprintf(stderr, "PVG MSAA transfer object=%p download=%d size=%ux%u samples=%lu\n",
                        self, download, width, height, (unsigned long)texture.sampleCount);
            }
            return;
        }
    }
    /* Controlled clear-only experiment, NOT a general paging implementation.
     * Private multisample content cannot be faithfully uploaded by a raw
     * buffer blit. Skip only for the explicitly opted-in diagnostic run.
     * Never enable this for application validation or as a production fix.
     */
    if (getenv("PVG_TEST_SKIP_MSAA_UPLOAD")) {
        Ivar texture_ivar = class_getInstanceVariable(object_getClass(self), "_texture");
        id<MTLTexture> texture = texture_ivar ? object_getIvar(self, texture_ivar) : nil;
        if (texture && texture.storageMode == MTLStorageModePrivate && texture.sampleCount > 1) {
            bool seen = objc_getAssociatedObject(self, &apple_gfx_seen_msaa_page_key) != nil;
            objc_setAssociatedObject(self, &apple_gfx_seen_msaa_page_key, @YES,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            if (!download && !seen) {
                fprintf(stderr, "PVG EXPERIMENT skipped FIRST private MSAA upload object=%p\n", self);
                return;
            }
            fprintf(stderr, "PVG EXPERIMENT forwarding later MSAA paging object=%p download=%d\n", self, download);
        }
    }
    ((void (*)(id, SEL, id, BOOL))apple_gfx_original_page_texture)
        (self, selector, encoder, download);
}

static void apple_gfx_install_paging_probe(id<MTLDevice> device)
{
    if ((!getenv("PVG_TRACE_PAGING") && !getenv("PVG_TEST_MSAA_TRANSFER")) ||
        apple_gfx_original_page_texture) {
        return;
    }
    apple_gfx_paging_trace_enabled = getenv("PVG_TRACE_PAGING") != NULL;
    if (!apple_gfx_paging_trace_enabled) {
        /* The MSAA transfer adapter targets the newer framework's task ABI.
         * Older frameworks (macOS 11-14) lack it: keep their native paging. */
        Method prepare = class_getInstanceMethod(objc_getClass("PGLocalTask"),
            sel_registerName("prepareResources:event:value:"));
        Method sync = class_getInstanceMethod(objc_getClass("PGBaseTask"),
            sel_registerName("synchronizeResources:count:discard:completionHandler:"));
        if (!prepare || !sync || strcmp(method_getTypeEncoding(prepare),
                "{PGTaskPrepareSync_s=BB@Q}40@0:8^{PGResourceList=^^?Q}16@24Q32") ||
            strcmp(method_getTypeEncoding(sync), "v40@0:8r^{?=I}16I24B28@?32")) {
            fprintf(stderr, "PVG paging: MSAA transfer adapter unsupported by this framework; skipped\n");
            return;
        }
    }
    Class cls = objc_getClass("PGSinglePlaneTextureResource");
    SEL selector = sel_registerName("pageTextureInEncoder:isDownload:");
    Method method = class_getInstanceMethod(cls, selector);
    if (!method) {
        return;
    }
    char result[16], argument[16];
    method_getReturnType(method, result, sizeof(result));
    method_getArgumentType(method, 3, argument, sizeof(argument));
    if (strcmp(result, "v") || (strcmp(argument, "c") && strcmp(argument, "B"))) {
        fprintf(stderr, "PVG paging diagnostic: unexpected method ABI %s\n",
                method_getTypeEncoding(method));
        return;
    }
    apple_gfx_original_page_texture = method_getImplementation(method);
    method_setImplementation(method, (IMP)apple_gfx_trace_page_texture);
    Method sync = class_getInstanceMethod(objc_getClass("PGBaseTask"),
        sel_registerName("synchronizeResources:count:discard:completionHandler:"));
    if (sync && !strcmp(method_getTypeEncoding(sync), "v40@0:8r^{?=I}16I24B28@?32")) {
        apple_gfx_original_sync_resources = method_getImplementation(sync);
        method_setImplementation(sync, (IMP)apple_gfx_trace_sync_resources);
    }
    Method validity = class_getInstanceMethod(objc_getClass("PGResource"),
                                             sel_registerName("setIsGuestValid:"));
    if (validity && apple_gfx_paging_trace_enabled) {
        method_getReturnType(validity, result, sizeof(result));
        method_getArgumentType(validity, 2, argument, sizeof(argument));
        if (!strcmp(result, "v") && (!strcmp(argument, "c") || !strcmp(argument, "B"))) {
            apple_gfx_original_guest_valid = method_getImplementation(validity);
            method_setImplementation(validity, (IMP)apple_gfx_trace_guest_valid);
        }
    }
    if (getenv("PVG_TEST_MSAA_TRANSFER")) {
        Method prepare = class_getInstanceMethod(objc_getClass("PGLocalTask"),
            sel_registerName("prepareResources:event:value:"));
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        Method factory = class_getInstanceMethod([commands class], @selector(blitCommandEncoder));
        if (!prepare || strcmp(method_getTypeEncoding(prepare),
                "{PGTaskPrepareSync_s=BB@Q}40@0:8^{PGResourceList=^^?Q}16@24Q32") ||
            !factory || !apple_gfx_original_sync_resources || getenv("PVG_TEST_SKIP_MSAA_UPLOAD")) {
            [queue release];
            [NSException raise:NSInternalInconsistencyException format:@"Unsupported MSAA paging hook ABI/configuration"];
        }
        apple_gfx_original_prepare_resources = method_getImplementation(prepare);
        apple_gfx_paging_native_blit = method_getImplementation(factory);
        method_setImplementation(prepare, (IMP)apple_gfx_prepare_resources);
        class_replaceMethod([commands class], @selector(blitCommandEncoder),
                            (IMP)apple_gfx_paging_blit, method_getTypeEncoding(factory));
        apple_gfx_msaa_transfer_enabled = true;
        [queue release];
    }
}
