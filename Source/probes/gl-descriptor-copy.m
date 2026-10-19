#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <stdio.h>
int main(int argc,char **argv) {
    if(argc!=2)return 64;
    @autoreleasepool {
        if(!dlopen(argv[1],RTLD_NOW|RTLD_LOCAL)){fprintf(stderr,"%s\n",dlerror());return 2;}
        NSArray *objects=@[[MTLRenderPassDescriptor renderPassDescriptor],[MTLRenderPipelineDescriptor new],
            [MTLTextureDescriptor new],[NSClassFromString(@"MTLCommandQueueDescriptorInternal") new]];
        const char *setters[]={"setDitherEnabled:","setOpenGLModeEnabled:","setWriteSwizzleEnabled:","setIsOpenGLQueue:"};
        const char *getters[]={"isDitherEnabled","openGLModeEnabled","writeSwizzleEnabled","isOpenGLQueue"};
        // Mojave command-queue descriptors return nil from native copy;
        // preserve that native contract rather than inventing a new one.
        for(unsigned i=0;i<3;i++) {
            id object=objects[i];SEL set=sel_registerName(setters[i]),get=sel_registerName(getters[i]);
            ((void(*)(id,SEL,BOOL))objc_msgSend)(object,set,YES);
            id copy=[object copy];
            ((void(*)(id,SEL,BOOL))objc_msgSend)(object,set,NO);
            BOOL copied=((BOOL(*)(id,SEL))objc_msgSend)(copy,get);
            BOOL changed=((BOOL(*)(id,SEL))objc_msgSend)(object,get);
            printf("%s copied=%d original=%d classes=%s/%s same=%d\n",getters[i],copied,changed,
                object_getClassName(object),object_getClassName(copy),copy==object);
            [copy release];if(!copied||changed)return 1;
        }
    }
    return 0;
}
