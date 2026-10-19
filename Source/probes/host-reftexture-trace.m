// Local bounded diagnostic; preserves donor preparation and synchronization.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
static IMP original_prepare;
static unsigned observations;
void PVGRearmRefTextureTrace(void) { __atomic_store_n(&observations, 0, __ATOMIC_RELAXED); }
static void prepare(id self, SEL cmd, id encoder) {
    unsigned n = __atomic_fetch_add(&observations, 1, __ATOMIC_RELAXED);
    if (n < 64) {
        Ivar ivar = class_getInstanceVariable(object_getClass(self), "_backing");
        id backing = ivar ? object_getIvar(self, ivar) : nil;
        fprintf(stderr, "PVG ref backing self=%p backing=%p class=%s\n",
            self, backing, backing ? object_getClassName(backing) : "nil");
    }
    ((void (*)(id, SEL, id))original_prepare)(self, cmd, encoder);
}
__attribute__((constructor)) static void install(void) {
    Class cls = NSClassFromString(@"PGRefTextureResource");
    SEL selector = sel_registerName("prepareInEncoder:");
    Method method = class_getInstanceMethod(cls, selector);
    if (!method || !class_getInstanceVariable(cls, "_backing")) abort();
    original_prepare = method_getImplementation(method);
    class_replaceMethod(cls, selector, (IMP)prepare, method_getTypeEncoding(method));
}
