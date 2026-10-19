#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc,char **argv) {
    if(argc!=2)return 64;
    void *h=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    IOReturn (*create)(io_service_t,uint32_t,uint32_t,void **)=h?dlsym(h,"IOAccelCreateSurface"):NULL;
    if(!create){fprintf(stderr,"%s\n",dlerror());return 2;}
    io_service_t gpu=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AppleParavirtAccelerator"));
    if(!gpu)return 3;
    void *surface=NULL;
    IOReturn status=create(gpu,0x70000000u|(uint32_t)getpid(),0x4c24,&surface);
    printf("create=0x%x surface=%p\n",status,surface);
    if(surface)IOServiceClose((io_connect_t)(uintptr_t)surface);
    IOObjectRelease(gpu);
    return status?1:0;
}
