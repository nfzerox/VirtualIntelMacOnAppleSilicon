/* Finite native PVG drawable API inventory in each guest architecture.
 * No Metal framework, renderer registration, system patch or display write.
 * Signatures agree with the existing PVGGLDrawable companion and native
 * IOAccelDevice probe; successful object creation is not presentation proof. */
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../PassthroughOpenGL/pvg-drawable.h"
int main(int argc,char **argv)
{
    if(argc>2 || (argc==2&&strcmp(argv[1],"--wire")))return 64;
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,
        IOServiceMatching("AppleParavirtAccelerator"));
    if(!service){puts("No PVG accelerator; no API qualification");return 77;}
    if(argc==2){
        io_connect_t connection=0;IOReturn status=VIMPVGDrawableOpen(service,&connection);
        printf("pointer-bits=%u native drawable type9 open=%x\n",(unsigned)(sizeof(void*)*8),status);
        VIMPVGDrawableClose(connection);IOObjectRelease(service);return status?1:0;
    }
    void *library=dlopen("/System/Library/PrivateFrameworks/IOAccelerator.framework/Versions/A/IOAccelerator",
                        RTLD_NOW|RTLD_LOCAL);
    if(!library){printf("pointer-bits=%u IOAccelerator unavailable: %s\n",
        (unsigned)(sizeof(void*)*8),dlerror());IOObjectRelease(service);return 77;}
    const char *names[]={"IOAccelDeviceCreate","IOAccelDeviceRelease",
        "IOAccelGLDrawableCreate","IOAccelGLDrawableClearDrawable",
        "IOAccelGLDrawableSetDrawable","IOAccelGLDrawableSetSwapRect",
        "IOAccelGLDrawableSetSwapInterval","IOAccelGLDrawableLookupIOSurfaceFromIndex"};
    void *functions[sizeof(names)/sizeof(names[0])];unsigned missing=0;
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i){
        functions[i]=dlsym(library,names[i]);missing+=!functions[i];
        printf("pointer-bits=%u %s=%s\n",(unsigned)(sizeof(void*)*8),names[i],functions[i]?"present":"absent");
    }
    int result=77;
    if(!missing){
        void *(*create)(io_service_t)=(void *)functions[0];
        void (*release)(void *)=(void *)functions[1];
        CFTypeRef (*drawable)(void *)=(void *)functions[2];
        void *device=create(service);
        CFTypeRef object=device?drawable(device):NULL;
        printf("device=%s drawable=%s\n",device?"created":"unavailable",object?"created":"unavailable");
        if(object){CFRelease(object);result=0;}
        if(device)release(device);
    }
    IOObjectRelease(service);dlclose(library);return result;
}
