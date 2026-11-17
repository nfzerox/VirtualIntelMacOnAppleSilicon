// Local-only Metal import diagnostic; never linked into the shipping helper.
// Preserve the original result. At most eight rejected imports are described;
// prefix controls alias the same memory without reading it or running GPU work.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <execinfo.h>
#include <sys/sysctl.h>
#include <unistd.h>
#import "metal-buffer-import-pages.h"

static IMP originalImport;
static unsigned reports;
static unsigned hardwarePage;
typedef id (*Import)(id,SEL,const void *,NSUInteger,MTLResourceOptions,
                     void (^)(void *,NSUInteger));

static id TraceImport(id device,SEL selector,const void *bytes,NSUInteger length,
                      MTLResourceOptions options,void (^deallocator)(void *,NSUInteger)) {
    id result=((Import)originalImport)(device,selector,bytes,length,options,deallocator);
    if(result)return result;
    unsigned sequence=__atomic_fetch_add(&reports,1,__ATOMIC_RELAXED);
    if(sequence>=8)return result;
    if(!sequence)VIMMetalBufferImportPages(device,^id<MTLBuffer>(void *control,NSUInteger size) {
        return ((Import)originalImport)(device,selector,control,size,MTLResourceStorageModeShared,nil);
    });
    fprintf(stderr,"PVG buffer import: pointer=%p length=0x%lx options=0x%lx page=%d hardware-page=%u\n",
        bytes,(unsigned long)length,(unsigned long)options,getpagesize(),hardwarePage);
    const NSUInteger sizes[]={4096,16384,65536};
    for(unsigned i=0;bytes&&i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        if(sizes[i]>=length)continue;
        id prefix=((Import)originalImport)(device,selector,bytes,sizes[i],options,nil);
        fprintf(stderr,"PVG buffer prefix: length=0x%lx imported=%d\n",(unsigned long)sizes[i],prefix!=nil);
        [prefix release];
    }
    void *stack[18];int count=backtrace(stack,18);backtrace_symbols_fd(stack,count,2);
    return result;
}

__attribute__((constructor)) static void InstallTrace(void) {@autoreleasepool {
    size_t size=sizeof(hardwarePage);
    if(sysctlbyname("hw.pagesize",&hardwarePage,&size,NULL,0))hardwarePage=0;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    Class cls=object_getClass(device);
    SEL selector=sel_registerName("newBufferWithBytesNoCopy:length:options:deallocator:");
    Method method=class_getInstanceMethod(cls,selector);
    if(method) {
        originalImport=method_getImplementation(method);
        class_replaceMethod(cls,selector,(IMP)TraceImport,method_getTypeEncoding(method));
        fprintf(stderr,"PVG buffer import trace: %s\n",class_getName(cls));
    }
    [device release];
}}
