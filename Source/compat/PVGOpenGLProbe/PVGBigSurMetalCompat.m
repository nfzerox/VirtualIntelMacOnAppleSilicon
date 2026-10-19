#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include "PVGBigSurTextureUsage.h"

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

// Loaded by the native guest's copied Metal plugin in /Library/GPUBundles.
// A category +load runs after the plugin's classes are registered; a dylib
// constructor would run too early. No global injection or system-volume edits.
@interface AppleParavirtDevice : NSObject
@end
@implementation AppleParavirtDevice (VIMBigSurTextureMetadata)
+ (void)load {
    if(NSProcessInfo.processInfo.operatingSystemVersion.majorVersion!=11)return;
    pvgInstallBigSurTextureUsage();
    Class compiler=objc_getClass("MTLCompiler");
    SEL selector=sel_registerName("initWithTargetData:cacheUUID:pluginPath:device:compilerFlags:");
    Method method=class_getInstanceMethod(compiler,selector);
    if(method&&method_getNumberOfArguments(method)==7) {
        originalCompilerInit=method_getImplementation(method);
        class_replaceMethod(compiler,selector,(IMP)nativeCompilerPath,method_getTypeEncoding(method));
    }
}
@end
