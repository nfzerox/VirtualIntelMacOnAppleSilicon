// Reversible private-buffer trial. Donor prepare first devolatilizes, then
// checks host validity; synchronize first checks guest validity.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <stdint.h>
static IMP old_prepare, old_sync;
static Ivar host_valid, guest_valid;
static int enabled = 1;
void PVGPrivateValidityGuard(int value) { __atomic_store_n(&enabled, value, __ATOMIC_RELAXED); }
static BOOL valid(id object, Ivar ivar) {
    return *(uint8_t *)((char *)(void *)object + ivar_getOffset(ivar)) != 0;
}
static void prepare(id self, SEL cmd, id encoder) {
    if (__atomic_load_n(&enabled, __ATOMIC_RELAXED)) {
        ((void (*)(id, SEL))objc_msgSend)(self, sel_registerName("devolatilize"));
        if (valid(self, host_valid)) return;
    }
    ((void (*)(id, SEL, id))old_prepare)(self, cmd, encoder);
}
static void synchronize(id self, SEL cmd, id encoder) {
    if (__atomic_load_n(&enabled, __ATOMIC_RELAXED) && valid(self, guest_valid)) return;
    ((void (*)(id, SEL, id))old_sync)(self, cmd, encoder);
}
__attribute__((constructor)) static void install(void) {
    Class cls = NSClassFromString(@"PGPrivateBufferResource");
    host_valid = class_getInstanceVariable(cls, "_hostValid");
    guest_valid = class_getInstanceVariable(cls, "_guestValid");
    Method p = class_getInstanceMethod(cls, sel_registerName("prepareInEncoder:"));
    Method s = class_getInstanceMethod(cls, sel_registerName("synchronizeInEncoder:"));
    if (!p || !s || !host_valid || !guest_valid) abort();
    old_prepare = method_getImplementation(p); old_sync = method_getImplementation(s);
    class_replaceMethod(cls, sel_registerName("prepareInEncoder:"), (IMP)prepare, method_getTypeEncoding(p));
    class_replaceMethod(cls, sel_registerName("synchronizeInEncoder:"), (IMP)synchronize, method_getTypeEncoding(s));
}
