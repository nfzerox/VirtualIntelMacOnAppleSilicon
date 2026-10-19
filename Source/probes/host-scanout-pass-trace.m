// Bounded diagnostic of full-screen render-pass preservation.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
static IMP original;
static unsigned count;
static unsigned preserve;
void PVGScanoutPreserve(unsigned enabled) { __atomic_store_n(&preserve, enabled, __ATOMIC_RELAXED); }
static id encoder(id self, SEL cmd, MTLRenderPassDescriptor *pass) {
    MTLRenderPassColorAttachmentDescriptor *a = pass.colorAttachments[0];
    id<MTLTexture> t = a.texture;
    if (t.width == 2560 && t.height == 1600 &&
        __atomic_fetch_add(&count, 1, __ATOMIC_RELAXED) < 100) {
        NSLog(@"PVG scanout pass texture=%p parent=%p surface=%p load=%lu store=%lu clear=%g,%g,%g,%g",
              t, t.parentTexture, t.iosurface, a.loadAction, a.storeAction,
              a.clearColor.red, a.clearColor.green, a.clearColor.blue, a.clearColor.alpha);
    }
    MTLLoadAction saved = a.loadAction;
    if (__atomic_load_n(&preserve, __ATOMIC_RELAXED) && t.iosurface &&
        t.width == 2560 && t.height == 1600 && saved == MTLLoadActionDontCare) {
        a.loadAction = MTLLoadActionLoad;
    }
    id result = ((id (*)(id, SEL, id))original)(self, cmd, pass);
    a.loadAction = saved;
    return result;
}
__attribute__((constructor)) static void install(void) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id buffer = [queue commandBuffer];
    Class cls = object_getClass(buffer);
    SEL sel = @selector(renderCommandEncoderWithDescriptor:);
    Method method = class_getInstanceMethod(cls, sel);
    if (!method) abort();
    original = method_getImplementation(method);
    class_replaceMethod(cls, sel, (IMP)encoder, method_getTypeEncoding(method));
    [queue release]; [device release];
}
