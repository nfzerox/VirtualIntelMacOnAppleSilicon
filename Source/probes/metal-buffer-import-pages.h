#import <Metal/Metal.h>
#include <sys/mman.h>
#include <unistd.h>

// Shared by the standalone control and the bounded live helper diagnostic.
// The caller supplies the real device/import entry, never another GPU context.
static BOOL VIMMetalBufferImportPages(id<MTLDevice> device,
    id<MTLBuffer> (^import)(void *,NSUInteger)) {
    const size_t span=3*16384;
    void *memory=mmap(NULL,span,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(memory==MAP_FAILED)return NO;
    uintptr_t aligned=((uintptr_t)memory+16383)&~(uintptr_t)16383;
    const size_t offsets[]={0,0,4096,4096};
    const size_t lengths[]={4096,16384,4096,16384};
    fprintf(stderr,"Metal page control: device=%s process-page=%d\n",device.name.UTF8String,getpagesize());
    BOOL control=NO;
    for(unsigned i=0;i<4;i++) {
        id<MTLBuffer> buffer=import((void *)(aligned+offsets[i]),lengths[i]);
        fprintf(stderr,"Metal page control: offset=%zu length=%zu imported=%d\n",offsets[i],lengths[i],buffer!=nil);
        if(i==1)control=buffer!=nil;
        [buffer release];
    }
    if(!mprotect((void *)aligned,16384,PROT_READ)) {
        id<MTLBuffer> readonly=import((void *)aligned,4096);
        fprintf(stderr,"Metal page control: readonly length=4096 imported=%d\n",readonly!=nil);
        [readonly release];
    }
    munmap(memory,span);return control;
}
