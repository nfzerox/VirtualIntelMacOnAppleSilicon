// Establish native final-release behavior before adding Mojave callbacks.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <Block.h>
static unsigned notifications;
int main(int argc,char **argv) {
    setbuf(stdout,NULL);
    void *image=argc==2?dlopen(argv[1],RTLD_NOW|RTLD_LOCAL):RTLD_DEFAULT;
    if(!image&&argc==2){fprintf(stderr,"%s\n",dlerror());return 7;}
    CGLPixelFormatAttribute attrs[]={0};CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 8;
    void *(*registerCallback)(void (^)(CGLPBufferObj))=dlsym(image,"CGLRegisterDestroyPBufferCallback");
    void (*unregisterCallback)(void *)=dlsym(image,"CGLUnregisterDestroyPBufferCallback");
    void *token=NULL;
    if(registerCallback&&unregisterCallback) token=registerCallback(^(CGLPBufferObj p){notifications++;printf("final callback=%p\n",p);});
    CGLPBufferObj p=NULL;CGLError error=CGLCreatePBuffer(16,16,GL_TEXTURE_2D,GL_RGBA,0,&p);
    printf("create=%d pointer=%p callbacks=%s\n",error,p,token?"available":"absent");
    if(error||!p)return 2;
    printf("initial refs=%d\n",CGLGetPBufferRetainCount(p));
    CGLRetainPBuffer(p);CGLReleasePBuffer(p);
    if(CGLGetPBufferRetainCount(p)!=1||notifications)return 3;
    error=CGLDestroyPBuffer(p);
    if(error|| (token&&notifications!=1))return 4;
    p=NULL;error=CGLCreatePBuffer(16,16,GL_TEXTURE_2D,GL_RGBA,0,&p);
    if(error||!p)return 5;
    CGLReleasePBuffer(p);
    if(token&&notifications!=2)return 6;
    if(token)unregisterCallback(token);
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);
    printf("lifetime PASS notifications=%u\n",notifications);return 0;
}
