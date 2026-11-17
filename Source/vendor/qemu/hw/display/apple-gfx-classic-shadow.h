/* Local downstream experiment, not an upstream contribution.
 * Older PVG uses a combined buffer resource and separate preparation/completion
 * methods. Keep its native queues/callbacks, sharing the existing page imports
 * and changed-byte scatter with the newer framework adapter. */
typedef struct {
    uint32_t identifier;
    uint8_t upload, written, readback, reserved;
    uint64_t opaque0, opaque1;
} AppleGFXClassicResource;
typedef struct {
    bool wait, download;
    id event;
    uint64_t value;
} AppleGFXClassicPrepare;
typedef struct {
    bool download;
    id event;
    uint64_t value;
} AppleGFXClassicComplete;
_Static_assert(sizeof(AppleGFXClassicResource) == 24, "PVG resource wire ABI");
_Static_assert(sizeof(AppleGFXClassicPrepare) == 24, "PVG prepare return ABI");
_Static_assert(sizeof(AppleGFXClassicComplete) == 24, "PVG completion argument ABI");

static Class classic_buffer_class;
static Ivar classic_buffer, classic_private, classic_transfer, classic_paging;
static IMP classic_original_prepare, classic_original_sync, classic_original_compute;
static IMP classic_original_prepare_compute, classic_original_sync_compute;
static IMP classic_original_needs_prepare, classic_original_needs_sync;
static IMP classic_original_prepare_resources, classic_original_complete_resources;
static IMP classic_original_run;
static char classic_gather_key;

static bool classic_is_private(id resource)
{
    bool value;
    memcpy(&value, (char *)resource + ivar_getOffset(classic_private), sizeof(value));
    return value;
}

static id classic_shared_buffer(id resource)
{
    return classic_is_private(resource) ? nil : object_getIvar(resource, classic_buffer);
}

static id<MTLComputePipelineState> classic_gather_pipeline(id<MTLDevice> device)
{
    @synchronized (device) {
        id pipeline = objc_getAssociatedObject(device, &classic_gather_key);
        if (pipeline) return pipeline;
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:
            @"#include <metal_stdlib>\nusing namespace metal;\n"
             "kernel void gather(device uchar *after [[buffer(0)]],"
             "device uchar *before [[buffer(1)]],"
             "device const uchar *guest [[buffer(2)]], uint i [[thread_position_in_grid]])"
             "{ uchar value=guest[i]; after[i]=value; before[i]=value; }"
            options:nil error:&error];
        id function = [library newFunctionWithName:@"gather"];
        pipeline = function ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
        [function release]; [library release];
        if (!pipeline) [NSException raise:NSInternalInconsistencyException
            format:@"PVG shared gather compile failed: %@", error];
        objc_setAssociatedObject(device, &classic_gather_key, pipeline, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [pipeline release];
        return pipeline;
    }
}

static void classic_compute_transfer(id<MTLBuffer> buffer,
                                      id<MTLComputeCommandEncoder> encoder, bool download)
{
    AppleGFXLegacyShadow *shadow = objc_getAssociatedObject(buffer, &shadow_key);
    if (!shadow) return;
    [encoder setComputePipelineState:download ? scatter_pipeline(buffer.device) :
                                               classic_gather_pipeline(buffer.device)];
    NSUInteger offset = 0;
    for (id<MTLBuffer> page in shadow->pages) {
        NSUInteger size = MIN(page.length, shadow->length - offset);
        [encoder setBuffer:buffer offset:offset atIndex:0];
        [encoder setBuffer:shadow->baseline offset:offset atIndex:1];
        [encoder setBuffer:page offset:0 atIndex:2];
        [encoder dispatchThreads:MTLSizeMake(size, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
        offset += size;
    }
}

static BOOL classic_needs_prepare(id self, SEL cmd)
{
    if (!classic_is_private(self)) {
        send_id(self, "ensurePhysical");
        if (objc_getAssociatedObject(object_getIvar(self, classic_buffer), &shadow_key)) return YES;
    }
    return ((BOOL (*)(id, SEL))classic_original_needs_prepare)(self, cmd);
}

static BOOL classic_needs_sync(id self, SEL cmd)
{
    if (objc_getAssociatedObject(classic_shared_buffer(self), &shadow_key)) return YES;
    return ((BOOL (*)(id, SEL))classic_original_needs_sync)(self, cmd);
}

static BOOL classic_needs_compute(id self, SEL cmd)
{
    if (objc_getAssociatedObject(classic_shared_buffer(self), &shadow_key)) return YES;
    return ((BOOL (*)(id, SEL))classic_original_compute)(self, cmd);
}

static void classic_prepare_compute(id self, SEL cmd, id encoder)
{
    id buffer = classic_shared_buffer(self);
    if (objc_getAssociatedObject(buffer, &shadow_key)) classic_compute_transfer(buffer, encoder, false);
    else ((void (*)(id, SEL, id))classic_original_prepare_compute)(self, cmd, encoder);
}

static void classic_sync_compute(id self, SEL cmd, id encoder)
{
    id buffer = classic_shared_buffer(self);
    if (objc_getAssociatedObject(buffer, &shadow_key)) classic_compute_transfer(buffer, encoder, true);
    else ((void (*)(id, SEL, id))classic_original_sync_compute)(self, cmd, encoder);
}

static void classic_prepare_blit(id self, SEL cmd, id encoder)
{
    send_id(self, "ensurePhysical");
    if (classic_is_private(self)) transfer(object_getIvar(self, classic_transfer), encoder, YES);
    ((void (*)(id, SEL, id))classic_original_prepare)(self, cmd, encoder);
}

static void classic_sync_blit(id self, SEL cmd, id encoder)
{
    ((void (*)(id, SEL, id))classic_original_sync)(self, cmd, encoder);
    if (classic_is_private(self)) transfer(object_getIvar(self, classic_transfer), encoder, NO);
}

static NSMutableData *classic_readback_resources(id task, const AppleGFXClassicResource *resources,
                                                uint32_t count, bool prepare)
{
    NSMutableData *copy = nil;
    for (uint32_t i = 0; i < count; i++) {
        id resource = ((id (*)(id, SEL, uint32_t))objc_msgSend)(task,
            sel_registerName(prepare ? "getHostResource:" : "getExistingHostResource:"), resources[i].identifier);
        if (![resource isKindOfClass:classic_buffer_class] || classic_is_private(resource)) continue;
        if (prepare) send_id(resource, "ensurePhysical");
        if (!objc_getAssociatedObject(object_getIvar(resource, classic_buffer), &shadow_key)) continue;
        if (!copy) copy = [NSMutableData dataWithBytes:resources length:(NSUInteger)count * sizeof(*resources)];
        ((AppleGFXClassicResource *)copy.mutableBytes)[i].readback = 1;
    }
    return copy;
}

static AppleGFXClassicPrepare classic_prepare_resources(id self, SEL cmd,
    const AppleGFXClassicResource *resources, uint32_t count, id event, uint64_t value)
{
    NSMutableData *copy = classic_readback_resources(self, resources, count, true);
    return ((AppleGFXClassicPrepare (*)(id, SEL, const void *, uint32_t, id, uint64_t))
        classic_original_prepare_resources)(self, cmd, copy ? copy.bytes : resources, count, event, value);
}

static void classic_complete_resources(id self, SEL cmd, const AppleGFXClassicResource *resources,
    uint32_t count, AppleGFXClassicComplete sync, id completion)
{
    NSMutableData *copy = classic_readback_resources(self, resources, count, false);
    ((void (*)(id, SEL, const void *, uint32_t, AppleGFXClassicComplete, id))
        classic_original_complete_resources)(self, cmd, copy ? copy.bytes : resources, count, sync, completion);
    if (!copy || !sync.download) return;
    // The donor's completion already includes our scatter. This same-queue
    // tail only fences later submissions on the other two native queues.
    AppleGFXShadowOrder *order = objc_getAssociatedObject(self, &order_key);
    if (!order) {
        order = [AppleGFXShadowOrder new];
        order->event = [send_id(self, "mtlDevice") newEvent];
        if (!order->event) [NSException raise:NSMallocException format:@"PVG shared-buffer event allocation failed"];
        objc_setAssociatedObject(self, &order_key, order, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [order release];
    }
    id<MTLCommandBuffer> tail = [send_id(send_id(self, "device"), "blitDownQueue") commandBuffer];
    uint64_t submitted = ++order->value;
    [tail encodeSignalEvent:order->event value:submitted];
    [tail addCompletedHandler:^(id<MTLCommandBuffer> command) {
        if (command.status == MTLCommandBufferStatusCompleted) @synchronized (order) {
            order->completedValue = MAX(order->completedValue, submitted);
        }
    }];
    [tail commit];
}

static void classic_run(id self, SEL cmd, id block)
{
    @synchronized (self) {
        AppleGFXShadowOrder *order = objc_getAssociatedObject(self, &order_key);
        uint64_t completed = 0;
        if (order) @synchronized (order) { completed = order->completedValue; }
        if (order && order->value > completed) {
            id<MTLCommandQueue> queues[] = {object_getIvar(self, classic_paging),
                send_id(send_id(self, "device"), "execQueue")};
            for (unsigned i = 0; i < 2; i++) {
                id<MTLCommandBuffer> barrier = [queues[i] commandBuffer];
                [barrier encodeWaitForEvent:order->event value:order->value];
                [barrier commit];
            }
        }
        guest_render_depth++;
        @try { ((void (*)(id, SEL, id))classic_original_run)(self, cmd, block); }
        @finally { guest_render_depth--; }
    }
}

static bool classic_shadow_install(void)
{
    Class task = NSClassFromString(@"PGTask"), buffers = NSClassFromString(@"PGBufferResource");
    Class backing = NSClassFromString(@"PGBackingResource");
    if (!task || !buffers || !backing || !class_getInstanceVariable(backing, "_transferBuffer") ||
        !class_getInstanceMethod(backing, sel_registerName("prepareInEncoder:")) ||
        !class_getInstanceMethod(backing, sel_registerName("synchronizeInEncoder:"))) return false;
    Method prep = class_getInstanceMethod(task, sel_registerName("prepareResources:count:event:value:"));
    Method done = class_getInstanceMethod(task, sel_registerName("completeResources:count:sync:completionHandler:"));
    // Check semantic layouts, not an OS/build pin or a struct's spelling.
    if (!prep || !done || !strstr(method_getTypeEncoding(prep), "=BB@Q}") ||
        !strstr(method_getTypeEncoding(prep), "=ICCCCQQ}") ||
        !strstr(method_getTypeEncoding(done), "=B@Q}")) return false;
    classic_buffer = class_getInstanceVariable(buffers, "_buffer");
    classic_transfer = class_getInstanceVariable(buffers, "_transferBuffer");
    classic_private = class_getInstanceVariable(buffers, "_isPrivate");
    classic_paging = class_getInstanceVariable(task, "_pagingQueue");
    if (!classic_buffer || !classic_transfer || !classic_private || !classic_paging) return false;
    const char *taskMethods[] = {"newBufferForVirtualPage:length:", "runBlock:",
        "getHostResource:", "getExistingHostResource:", "mtlDevice", "device"};
    const char *bufferMethods[] = {"ensurePhysical", "needsPrepare", "needsSync", "needsComputePaging",
        "prepareInEncoder:", "synchronizeInEncoder:", "prepareInComputeEncoder:", "synchronizeInComputeEncoder:"};
    for (unsigned i = 0; i < sizeof(taskMethods) / sizeof(taskMethods[0]); i++)
        if (!class_getInstanceMethod(task, sel_registerName(taskMethods[i]))) return false;
    for (unsigned i = 0; i < sizeof(bufferMethods) / sizeof(bufferMethods[0]); i++)
        if (!class_getInstanceMethod(buffers, sel_registerName(bufferMethods[i]))) return false;
    classic_buffer_class = buffers;
    replace(task, "newBufferForVirtualPage:length:", (IMP)legacy_buffer);
    classic_original_needs_prepare = replace(buffers, "needsPrepare", (IMP)classic_needs_prepare);
    classic_original_needs_sync = replace(buffers, "needsSync", (IMP)classic_needs_sync);
    classic_original_compute = replace(buffers, "needsComputePaging", (IMP)classic_needs_compute);
    classic_original_prepare = replace(buffers, "prepareInEncoder:", (IMP)classic_prepare_blit);
    classic_original_sync = replace(buffers, "synchronizeInEncoder:", (IMP)classic_sync_blit);
    classic_original_prepare_compute = replace(buffers, "prepareInComputeEncoder:", (IMP)classic_prepare_compute);
    classic_original_sync_compute = replace(buffers, "synchronizeInComputeEncoder:", (IMP)classic_sync_compute);
    classic_original_prepare_resources = replace(task, "prepareResources:count:event:value:", (IMP)classic_prepare_resources);
    classic_original_complete_resources = replace(task, "completeResources:count:sync:completionHandler:", (IMP)classic_complete_resources);
    classic_original_run = replace(task, "runBlock:", (IMP)classic_run);
    original_backing_prepare = replace(backing, "prepareInEncoder:", (IMP)backing_prepare);
    original_backing_sync = replace(backing, "synchronizeInEncoder:", (IMP)backing_sync);
    fprintf(stderr, "PVG classic shared-buffer adapter enabled\n");
    return true;
}
