// Run Apple's donor AIR downgrade on captured IR without submitting GPU work.
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2 && !(argc == 3 && (!strcmp(argv[2], "--upgrade") || !strcmp(argv[2], "--roundtrip")))) return 64;
    void *llvm=dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/31001/Libraries/libLLVM.dylib",RTLD_NOW|RTLD_LOCAL);
    void *gpu=dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/31001/Libraries/libGPUCompiler.dylib",RTLD_NOW|RTLD_LOCAL);
    if(!llvm||!gpu) { fprintf(stderr,"%s\n",dlerror());return 1; }
#define API(type,name,image) type name = (void *)dlsym(image,#name); if(!name) return 2
    typedef void *(*Create)(void);
    typedef int (*Read)(const char *,void **,char **);
    typedef int (*Parse)(void *,void *,void **,char **);
    typedef bool (*Downgrade)(void *,uint64_t,uint32_t);
    typedef char *(*Print)(void *);
    typedef void (*Dispose)(void *);
    API(Create,LLVMContextCreate,llvm);
    API(Read,LLVMCreateMemoryBufferWithContentsOfFile,llvm);
    API(Parse,LLVMParseIRInContext,llvm);
    API(Downgrade,MTLDowngradeAIRModule,gpu);
    API(Downgrade,MTLUpgradeAIRModule,gpu);
    API(Print,LLVMPrintModuleToString,llvm);
    API(Dispose,LLVMDisposeMessage,llvm);
    API(Dispose,LLVMDisposeModule,llvm);
    API(Dispose,LLVMContextDispose,llvm);
    void *ctx=LLVMContextCreate(),*buffer=NULL,*module=NULL;char *error=NULL;
    if(LLVMCreateMemoryBufferWithContentsOfFile(argv[1],&buffer,&error)||LLVMParseIRInContext(ctx,buffer,&module,&error)) {
        fprintf(stderr,"%s\n",error?:"parse failed"); return 1;
    }
    // Verified immediate arguments in donor AppleParavirtCompiler::buildRequest.
    // The upgrade wrapper has the same module/u64/u32 ABI (verified in
    // libGPUCompiler 31001); Big Sur supports AIR 2.3.
    bool ok=argc==3 ? MTLUpgradeAIRModule(module,UINT64_C(0x300000002),0) :
        MTLDowngradeAIRModule(module,UINT64_C(0x200000002),0);
    if (ok && argc==3 && !strcmp(argv[2],"--roundtrip")) ok=MTLDowngradeAIRModule(module,UINT64_C(0x200000002),0);
    fprintf(stderr,"%s=%d\n",argc==3?"upgrade":"downgrade",ok);
    char *text=LLVMPrintModuleToString(module); puts(text); LLVMDisposeMessage(text);
    LLVMDisposeModule(module); LLVMContextDispose(ctx);return ok?0:1;
}
