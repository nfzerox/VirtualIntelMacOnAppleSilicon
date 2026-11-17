// Read-only ABI/ivar inventory for the original loaded PVG framework.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <dlfcn.h>

int main(void) {@autoreleasepool {
    if(!dlopen("/System/Library/Frameworks/ParavirtualizedGraphics.framework/ParavirtualizedGraphics",RTLD_NOW|RTLD_LOCAL))return 1;
    for(NSString *name in @[@"PGTask",@"PGBufferResource",@"PGBackingResource"]) {
        Class cls=NSClassFromString(name);if(!cls)continue;
        printf("CLASS %s size=%zu\n",name.UTF8String,class_getInstanceSize(cls));
        unsigned count=0;Ivar *ivars=class_copyIvarList(cls,&count);
        for(unsigned i=0;i<count;i++)printf("IVAR %s offset=%td type=%s\n",ivar_getName(ivars[i]),
            ivar_getOffset(ivars[i]),ivar_getTypeEncoding(ivars[i]));
        free(ivars);
        for(NSString *method in @[@"runBlock:",@"prepareResources:count:event:value:",
            @"completeResources:count:sync:completionHandler:",@"synchronizeResources:count:discard:completionHandler:",
            @"needsPrepare",@"needsSync",@"needsComputePaging",@"prepareInComputeEncoder:",
            @"synchronizeInComputeEncoder:",@"prepareInEncoder:",@"synchronizeInEncoder:",@"mtlDevice",@"device"]) {
            Method found=class_getInstanceMethod(cls,NSSelectorFromString(method));if(!found)continue;
            const char *types=method_getTypeEncoding(found);
            NSMethodSignature *signature=[NSMethodSignature signatureWithObjCTypes:types];
            printf("METHOD %s return-size=%lu encoding=%s\n",method.UTF8String,
                (unsigned long)signature.methodReturnLength,types);
        }
    }
    return 0;
}}
