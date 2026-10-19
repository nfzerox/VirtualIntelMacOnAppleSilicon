#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <syslog.h>

// Diagnostic only: the first host protection fault after opening a Launchpad
// folder followed five 64x64 BGRA mipmap generations in one WindowServer
// commit. Suppress only that exact operation to test causality. This is not a
// production rendering fix: later sampling can require the missing mip levels.
static void (*originalGenerateMipmaps)(id, SEL, id);
static unsigned skippedMipmaps;

static void pvgGenerateMipmaps(id self, SEL selector, id texture) {
    SEL widthSelector = sel_registerName("width");
    SEL heightSelector = sel_registerName("height");
    SEL pixelFormatSelector = sel_registerName("pixelFormat");
    if (texture && [texture respondsToSelector:widthSelector] &&
        [texture respondsToSelector:heightSelector] &&
        [texture respondsToSelector:pixelFormatSelector]) {
        NSUInteger width = ((NSUInteger (*)(id, SEL))objc_msgSend)(
            texture, widthSelector);
        NSUInteger height = ((NSUInteger (*)(id, SEL))objc_msgSend)(
            texture, heightSelector);
        NSUInteger pixelFormat = ((NSUInteger (*)(id, SEL))objc_msgSend)(
            texture, pixelFormatSelector);
        if (width == 64 && height == 64 && pixelFormat == 80) {
            unsigned count = __sync_add_and_fetch(&skippedMipmaps, 1);
            if (count <= 16)
                syslog(LOG_NOTICE, "PVG mipmap probe skipped 64x64 BGRA texture %p count=%u",
                       texture, count);
            return;
        }
    }
    originalGenerateMipmaps(self, selector, texture);
}

__attribute__((visibility("default")))
void PVGInstallWindowServerMipmapProbe(void) {
    if (strcmp(getprogname(), "WindowServer") != 0)
        return;
    Class encoder = objc_getClass("MTLSerializerBlitCommandEncoder");
    SEL selector = sel_registerName("generateMipmapsForTexture:");
    Method method = encoder ? class_getInstanceMethod(encoder, selector) : NULL;
    if (!method) {
        syslog(LOG_ERR, "PVG mipmap probe could not find blit encoder method");
        return;
    }
    originalGenerateMipmaps =
        (void (*)(id, SEL, id))method_getImplementation(method);
    method_setImplementation(method, (IMP)pvgGenerateMipmaps);
    syslog(LOG_NOTICE, "PVG mipmap probe installed in WindowServer");
}
