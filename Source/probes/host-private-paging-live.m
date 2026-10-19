// Attach only the missing private-buffer hooks to an already loaded shadow
// experiment. Supply that image's transfer() address from LLDB symbol lookup;
// do not reload the whole shim (its associated-object keys would differ).
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>
static void (*transfer_existing)(id, id, BOOL);
static IMP original_prepare, original_sync;
static Ivar staging;
static void prepare(id self, SEL cmd, id encoder) {
    ((void (*)(id, SEL))objc_msgSend)(self, sel_registerName("ensurePhysical"));
    transfer_existing(object_getIvar(self, staging), encoder, YES);
    ((void (*)(id, SEL, id))original_prepare)(self, cmd, encoder);
}
static void sync_buffer(id self, SEL cmd, id encoder) {
    ((void (*)(id, SEL, id))original_sync)(self, cmd, encoder);
    transfer_existing(object_getIvar(self, staging), encoder, NO);
}
int pvg_private_paging_install(void *transfer_function) {
    if (!transfer_function || transfer_existing) return 1;
    Class cls = NSClassFromString(@"PGPrivateBufferResource");
    staging = class_getInstanceVariable(cls, "_transferBuffer");
    Method p = class_getInstanceMethod(cls, sel_registerName("prepareInEncoder:"));
    Method s = class_getInstanceMethod(cls, sel_registerName("synchronizeInEncoder:"));
    if (!staging || !p || !s) return 2;
    transfer_existing = transfer_function;
    original_prepare = method_setImplementation(p, (IMP)prepare);
    original_sync = method_setImplementation(s, (IMP)sync_buffer);
    fprintf(stderr, "PVG live private paging installed\n");
    return 0;
}
