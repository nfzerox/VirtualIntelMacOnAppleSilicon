// Exercise the real Intel donor's original validator and the shared adapter.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include "../vendor/qemu/hw/display/apple-gfx-packed422.h"
#include <dlfcn.h>
#include <assert.h>
#include <string.h>
int main(int argc,char **argv) {@autoreleasepool {
    if(argc!=2)return 64;
    assert(dlopen(argv[1],RTLD_NOW|RTLD_LOCAL));
    Class cls=object_getClass(NSClassFromString(@"PGBackingResource"));
    SEL selector=sel_registerName("validateBackingDescriptor:srcPlanes:destPlanes:planeCount:backingAllocationLength:");
    Method method=class_getInstanceMethod(cls,selector);assert(method);
    typedef BOOL(*Validate)(id,SEL,const void *,const void *,void *,uint32_t,uint64_t);
    Validate original=(Validate)method_getImplementation(method);
    id receiver=NSClassFromString(@"PGBackingResource");
    VIMPackedBackingDescriptor descriptor={0x385000,0xfc03,0x76323136,1};
    VIMPackedBackingPlane source={0,1280,720,0x08001400},destination={0};
    assert(!original(receiver,selector,&descriptor,&source,&destination,1,descriptor.length));
    VIMInstallPacked422Backing();
    Validate adapted=(Validate)class_getMethodImplementation(cls,selector);assert(adapted!=original);
    assert(adapted(receiver,selector,&descriptor,&source,&destination,1,descriptor.length));
    assert(source.width==1280&&destination.width==640&&destination.height==720&&destination.strideAndElement==source.strideAndElement);
    assert(!adapted(receiver,selector,&descriptor,&source,&destination,1,5120*719));
    source.width=640;
    assert(adapted(receiver,selector,&descriptor,&source,&destination,1,descriptor.length)&&!memcmp(&source,&destination,sizeof(source)));
    source.width=1280;source.strideAndElement=0x04001400;descriptor.pixelFormat=0x42475241;
    assert(adapted(receiver,selector,&descriptor,&source,&destination,1,descriptor.length)&&!memcmp(&source,&destination,sizeof(source)));
    source.strideAndElement=0x08001400;
    assert(!adapted(receiver,selector,&descriptor,&source,&destination,1,descriptor.length));
    puts("PASS: real donor rejects captured layout, adapter accepts packed texels, original allocation/other-format checks remain active");
    return 0;
}}
