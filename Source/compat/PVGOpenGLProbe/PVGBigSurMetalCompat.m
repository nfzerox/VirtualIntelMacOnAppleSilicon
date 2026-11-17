#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include "PVGBigSurTextureUsage.h"
#include "PVGTiledBackingCompat.h"

static IMP originalCompilerInit;
static id nativeCompilerPath(id self,SEL selector,id data,const void *cacheUUID,
                             NSString *path,id device,NSUInteger flags) {
    // setupCompiler derives this path from its own relocated NSBundle.
    // The unmodified compiler dylib is cache-resident at its original system
    // path, not a loose file to copy out of the plugin's Resources folder.
    if([NSStringFromClass([device class]) isEqualToString:@"AppleParavirtDevice"]&&
       [path isEqualToString:@"/Library/GPUBundles/AppleParavirtGPUMetal.bundle/Contents/Resources/libAppleParavirtCompilerPlugin.dylib"])
        path=@"/System/Library/Extensions/AppleParavirtGPUMetal.bundle/Contents/Resources/libAppleParavirtCompilerPlugin.dylib";
    return ((id(*)(id,SEL,id,const void *,id,id,NSUInteger))originalCompilerInit)
        (self,selector,data,cacheUUID,path,device,flags);
}

// The paravirtual device inherits Metal's targetDeviceArchitecture, which is
// nil. Tahoe's RenderBox (icons, Liquid Glass) opens precompiled binary
// archives, and Metal aborts the process when the device names no
// architecture to pick a slice by. Real x86 GPUs name theirs and simply
// match no Apple-GPU slice, so RenderBox compiles its AIR library instead.
// Name a GPU type no archive slice uses, for the same miss. ABI64 | 0xff.
static id pvgTargetArchitecture(id self,SEL selector) {
    (void)self;(void)selector;
    static id architecture;static dispatch_once_t once;
    dispatch_once(&once,^{
        Class cls=objc_getClass("MTLTargetDeviceArchitecture");
        id value=[cls new];
        if([value respondsToSelector:@selector(setCpuType:)]&&[value respondsToSelector:@selector(setSubType:)]) {
            ((void(*)(id,SEL,uint32_t))objc_msgSend)(value,@selector(setCpuType:),0x010000ffu);
            ((void(*)(id,SEL,uint32_t))objc_msgSend)(value,@selector(setSubType:),0);
            architecture=value;
        }
    });
    return architecture;
}
static void pvgInstallTargetArchitecture(Class device) {
    SEL selector=sel_registerName("targetDeviceArchitecture");
    Method inherited=class_getInstanceMethod(device,selector);
    // Only where the device has no answer of its own.
    if(inherited&&objc_getClass("MTLTargetDeviceArchitecture"))
        class_addMethod(device,selector,(IMP)pvgTargetArchitecture,method_getTypeEncoding(inherited));
}

// Loaded by the native guest's copied Metal plugin in /Library/GPUBundles.
// A category +load runs after the plugin's classes are registered; a dylib
// constructor would run too early. No global injection or system-volume edits.
@interface AppleParavirtDevice : NSObject
@end
@implementation AppleParavirtDevice (VIMBigSurTextureMetadata)
+ (void)load {
    if(NSProcessInfo.processInfo.operatingSystemVersion.majorVersion<11)return;
    pvgInstallBigSurTextureUsage();
    pvgInstallTargetArchitecture(self);
    pvgEnableSkyLightClientStorage();
    Class compiler=objc_getClass("MTLCompiler");
    SEL selector=sel_registerName("initWithTargetData:cacheUUID:pluginPath:device:compilerFlags:");
    Method method=class_getInstanceMethod(compiler,selector);
    if(method&&method_getNumberOfArguments(method)==7) {
        originalCompilerInit=method_getImplementation(method);
        class_replaceMethod(compiler,selector,(IMP)nativeCompilerPath,method_getTypeEncoding(method));
    }
}
@end
