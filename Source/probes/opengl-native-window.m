// Native Big Sur control: stock OpenGL/Metal/driver/compiler; only select
// the renderer's Intel desktop capability profile in this test process.
#define main PVGWindowMain
#include "opengl-window.m"
#undef main
#import <Metal/Metal.h>
#import <objc/runtime.h>
static id nativeVendor(id self,SEL selector) {
    (void)self;(void)selector;return @"Intel (PVG native control)";
}
int main(void) {
    @autoreleasepool {
        BOOL found=NO;
        NSArray *devices=MTLCopyAllDevices();
        for(id device in devices) {
            if(![NSStringFromClass([device class]) isEqualToString:@"AppleParavirtDevice"])continue;
            SEL selector=sel_registerName("vendorName");
            Method method=class_getInstanceMethod([device class],selector);
            if(!method)return 3;
            class_replaceMethod([device class],selector,(IMP)nativeVendor,method_getTypeEncoding(method));
            found=YES;
        }
        [devices release];
        return found?PVGWindowMain():3;
    }
}
