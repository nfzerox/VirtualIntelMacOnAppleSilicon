/* Native10.4 public-CGL setup gate, not a benchmark or conformance claim.
 * Never admit software fallback or infer WindowServer acceleration from an
 * application context. No private layout/address or screen capture needed. */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <IOKit/IOKitLib.h>
#include <objc/objc.h>
#include "opengl-guest-machine.h"
#include <sys/utsname.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    struct utsname kernel;
    if(argc!=2||strcmp(argv[1],"--desktop"))return 64;
    if(uname(&kernel)||strncmp(kernel.release,"8.",2)||!VIMOpenGLGuestMachine())return 77;
    setvbuf(stdout,NULL,_IOLBF,0);
    int accelerated=CGDisplayUsesOpenGLAcceleration(CGMainDisplayID());
    printf("desktop accelerated=%d\n",accelerated);
    if(!accelerated)return 4;
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    if(!service)return 77;IOObjectRelease(service);
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;CGLPBufferObj buffer=NULL;
    GLint count=0,hardware=0;int result=1;
    CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
    if(!error&&format)error=CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&hardware);
    if(error||!format||count<1||!hardware)goto done;
    if((error=CGLCreateContext(format,NULL,&context))||!context||
       (error=CGLSetCurrentContext(context)))goto done;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s hardware=%d\n",renderer?renderer:"none",(int)hardware);
    if(!renderer||strstr(renderer,"Software")||strstr(renderer,"Float"))goto done;
    if((error=CGLCreatePBuffer(17,13,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,0,&buffer))||
       !buffer||(error=CGLSetPBuffer(context,buffer,0,0,0)))goto done;
    glViewport(0,0,17,13);glClearColor(.25f,.5f,.75f,1.f);glClear(GL_COLOR_BUFFER_BIT);
    unsigned char pixels[17*13*4]={0};
    glReadPixels(0,0,17,13,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum glError=glGetError();printf("pixel=%u,%u,%u,%u GL=%x\n",pixels[0],pixels[1],pixels[2],pixels[3],(unsigned)glError);
    if(glError)goto done;
    for(unsigned i=0;i<sizeof(pixels);i+=4)
        if(pixels[i]<63||pixels[i]>65||pixels[i+1]<127||pixels[i+1]>129||
           pixels[i+2]<190||pixels[i+2]>192||pixels[i+3]!=255)goto done;
    result=0;
done:
    CGLSetCurrentContext(NULL);
    if(context)CGLDestroyContext(context);if(buffer)CGLDestroyPBuffer(buffer);if(format)CGLDestroyPixelFormat(format);
    if(error)fprintf(stderr,"Native graphics check CGL error=%d\n",(int)error);
    if(!result)puts("PASS native Tiger accelerated desktop and public pbuffer pixels");
    return result;
}
