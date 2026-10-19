#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <objc/runtime.h>
#include "../compat/PVGMetalCompat/PVGCompletionSite.h"
int main(void) {
    @autoreleasepool {
        // Mojave installs the bundled Catalina client; the stock client's
        // completion layout is deliberately not the one used after install.
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101500
        void *image=dlopen("/System/Library/PrivateFrameworks/IOAccelerator.framework/Versions/A/IOAccelerator",RTLD_NOW|RTLD_LOCAL);
        void *symbol=image?dlsym(image,"IOAccelDisplayPipeSetTransactionNotifyPort"):NULL;
        Dl_info info;
        if(!symbol || !dladdr(symbol,&info) || !pvgFindCompletionSite(info.dli_fbase)) {
            fprintf(stderr,"Unsupported IOAccelerator completion layout; no system files changed.\n");return 1;
        }
#endif
        Class texture=objc_getClass("MTLIOAccelTexture");
        Ivar usage=texture?class_getInstanceVariable(texture,"_usage"):NULL;
        if(!usage || strcmp(ivar_getTypeEncoding(usage),"Q") || ivar_getOffset(usage)<=0) {
            fprintf(stderr,"Unsupported Metal texture metadata layout.\n");return 2;
        }
        void *llvm=dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib",RTLD_NOW|RTLD_LOCAL);
        const char *apis[]={"LLVMWriteBitcodeToMemoryBuffer","LLVMGetBufferStart","LLVMGetBufferSize","LLVMDisposeMemoryBuffer"};
        for(unsigned i=0;i<4;i++) if(!llvm || !dlsym(llvm,apis[i])) {
            fprintf(stderr,"Unsupported native compiler family: missing %s.\n",apis[i]);return 3;
        }
        puts("PASS: target-specific interface checks, texture usage ivar, and LLVM 3902 APIs.");
        puts("These are structural checks, not certification of every private ABI or workload.");
    }
    return 0;
}
