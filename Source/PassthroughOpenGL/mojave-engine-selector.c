/* Mojave's native CGL loads this entry library once. Each VM boot selects
 * exactly one engine by the presence of our non-display PCI transport.
 * gliCreateContext returns that engine's own dispatch table: GL drawing never
 * traverses this selector and existing contexts never change engines. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <OpenGL/OpenGL.h>
#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "keynote-screen-compatibility.h"

#define EXPORT __attribute__((visibility("default")))
#define ENTRIES(E,V) \
 E(unsigned char,gliGetVersion,(GLint *a,GLint *b,GLint *c),(a,b,c),0) \
 V(gliInitializeLibrary,(void *a,void *b,unsigned c,void *d,void *e,void *f,unsigned g),(a,b,c,d,e,f,g)) \
 V(gliTerminateLibrary,(void),()) \
 E(CGLError,gliChoosePixelFormat,(void **a,const GLint *b),(a,b),kCGLBadState) \
 E(CGLError,gliDestroyPixelFormat,(void *a),(a),kCGLBadState) \
 E(CGLError,gliQueryRendererInfo,(void **a,unsigned b),(a,b),kCGLBadState) \
 E(CGLError,gliDestroyRendererInfo,(void *a),(a),kCGLBadState) \
 E(CGLError,gliCreateContext,(void **a,const void *b,void *c,void *d,void *e,unsigned f),(a,b,c,d,e,f),kCGLBadState) \
 E(CGLError,gliCreateContextWithShared,(void **a,const void *b,void *c,void *d,void *e,unsigned f),(a,b,c,d,e,f),kCGLBadState) \
 E(CGLError,gliDestroyContext,(void *a),(a),kCGLBadState) \
 E(CGLError,gliGetInteger,(void *a,unsigned b,GLint *c),(a,b,c),kCGLBadState) \
 E(CGLError,gliSetInteger,(void *a,unsigned b,const GLint *c),(a,b,c),kCGLBadState) \
 E(CGLError,gliAttachDrawable,(void *a,unsigned b,const void *c,unsigned d),(a,b,c,d),kCGLBadState) \
 E(CGLError,gliAttachDrawableWithOptions,(void *a,unsigned b,const void *c,unsigned d,unsigned e),(a,b,c,d,e),kCGLBadState) \
 V(gliSwapBuffers,(void *a),(a)) \
 V(gliGetAttribute,(void *a,unsigned b,void *c),(a,b,c)) \
 V(gliSetAttribute,(void *a,unsigned b,const void *c),(a,b,c)) \
 V(gliCopyAttributes,(void *a,void *b),(a,b)) \
 E(CGLError,gliGetMemoryAnnotationList,(void *a,void *b),(a,b),kCGLBadState) \
 E(IOReturn,VIMGLINotifyCPUSurfaceCopy,(uint32_t a,const void *b,int32_t c,int32_t d),(a,b,c,d),kIOReturnUnsupported) \
 E(uint32_t,VIMGLITryGPUFramebufferCopy,(io_connect_t a,uint32_t b,const void *c,int32_t d,int32_t e,IOReturn *f),(a,b,c,d,e,f),0)
#define FIELD(R,N,P,A,F) R (*N) P;
#define VOID_FIELD(N,P,A) void (*N) P;
static struct { ENTRIES(FIELD,VOID_FIELD) } engine;
#undef FIELD
#undef VOID_FIELD
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void *image; // Engine and its dispatch addresses live for the process.
static int passthrough;

static uint32_t PCIProperty(io_service_t service,CFStringRef key)
{
    CFTypeRef value=IORegistryEntryCreateCFProperty(service,key,kCFAllocatorDefault,0);
    uint32_t result=0;
    if(value&&CFGetTypeID(value)==CFDataGetTypeID()&&CFDataGetLength(value)>=4)
        memcpy(&result,CFDataGetBytePtr(value),4);
    if(value)CFRelease(value);
    return CFSwapInt32LittleToHost(result);
}

static int HasTransport(void)
{
    io_iterator_t iterator=0;
    if(IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("IOPCIDevice"),&iterator))return 0;
    io_service_t service;int found=0;
    while((service=IOIteratorNext(iterator))) {
        found=PCIProperty(service,CFSTR("vendor-id"))==0x1b36&&
              PCIProperty(service,CFSTR("device-id"))==0x10f0;
        IOObjectRelease(service);
        if(found)break;
    }
    IOObjectRelease(iterator);
    return found;
}

static void SelectEngine(void)
{
    VIMApplyKeynoteScreenCompatibility();
    Dl_info own={0};char path[PATH_MAX];
    passthrough=HasTransport();
    if(!dladdr((const void *)&SelectEngine,&own)||!own.dli_fname)return;
    const char *last=strrchr(own.dli_fname,'/');
    if(!last)return;
    size_t directory=(size_t)(last-own.dli_fname)+1;
    const char *name=passthrough?"GLEnginePassthrough":"GLEngineApple";
    if(directory+strlen(name)>=sizeof(path))return;
    memcpy(path,own.dli_fname,directory);strcpy(path+directory,name);
    image=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!image){syslog(LOG_ERR,"Virtual Intel Mac OpenGL: cannot load selected engine: %s",dlerror());return;}
#define LOAD(R,N,P,A,F) engine.N=dlsym(image,#N);
#define VOID_LOAD(N,P,A) engine.N=dlsym(image,#N);
    ENTRIES(LOAD,VOID_LOAD)
#undef LOAD
#undef VOID_LOAD
}

// Stable identity for idempotent preparation and finite bringup diagnostics.
EXPORT int VIMOpenGLBootSelection(void){pthread_once(&once,SelectEngine);return image?passthrough:-1;}
#define FORWARD(R,N,P,A,F) EXPORT R N P {pthread_once(&once,SelectEngine);if(!engine.N)return F;return engine.N A;}
#define VOID_FORWARD(N,P,A) EXPORT void N P {pthread_once(&once,SelectEngine);if(engine.N)engine.N A;}
ENTRIES(FORWARD,VOID_FORWARD)
