// Bounded live diagnostic for the Rosetta shadow experiment; no guest changes.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <stdint.h>

typedef struct { uint64_t (**vtable)(void *, uint64_t); uint64_t count; } List;
typedef struct { List base; List *source; id task; } TraceList;
static IMP original;
static unsigned observations;
// Re-arm immediately before a bounded UI reproducer via debugger expression.
void PVGRearmResourceTrace(void) {
    __atomic_store_n(&observations, 0, __ATOMIC_RELAXED);
}
static uint64_t entry(void *opaque, uint64_t index) {
    TraceList *list = opaque;
    uint64_t value = list->source->vtable[0](list->source, index);
#ifdef PVG_TRACE_FORCE_SHARED_SYNC
    // Live qualification of the production shadow fix, without a VM reboot.
    id shared = ((id (*)(id, SEL, uint32_t))objc_msgSend)(list->task,
        sel_registerName("getHostResource:"), (uint32_t)value);
    if ([shared isKindOfClass:NSClassFromString(@"PGSharedBufferResource")])
        value |= UINT64_C(4) << 32;
#endif
    unsigned n = __atomic_fetch_add(&observations, 1, __ATOMIC_RELAXED);
    if (n < 256) {
        id resource = ((id (*)(id, SEL, uint32_t))objc_msgSend)(list->task,
            sel_registerName("getExistingHostResource:"), (uint32_t)value);
        fprintf(stderr, "PVG resource trace task=%p index=%llu id=%u flags=%x class=%s\n",
            list->task, index, (uint32_t)value, (uint32_t)(value >> 32),
            resource ? object_getClassName(resource) : "not-created");
    }
    return value;
}
static void execute(id self, SEL cmd, uint32_t count, void *commands,
                    List *resources, id event, uint64_t *value, id completion) {
    uint64_t (*vtable[])(void *, uint64_t) = {entry};
    TraceList list = {{vtable, resources->count}, resources, self};
    ((void (*)(id, SEL, uint32_t, void *, void *, id, uint64_t *, id))original)(
        self, cmd, count, commands, &list, event, value, completion);
}
__attribute__((constructor)) static void install(void) {
    Class cls = NSClassFromString(@"PGLocalTask");
    SEL sel = sel_registerName("doExecIndirect:cmdBuffers:resourceList:fifoEvent:fifoEventValue:completionHandler:");
    Method method = class_getInstanceMethod(cls, sel);
    if (!method) abort();
    original = method_getImplementation(method);
    class_replaceMethod(cls, sel, (IMP)execute, method_getTypeEncoding(method));
    fprintf(stderr, "PVG bounded resource trace installed\n");
}
