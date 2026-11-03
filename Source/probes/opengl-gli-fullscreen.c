/* Read-only fullscreen attachment qualification: maps the actual framebuffer
 * through the research driver's reported layout, but never draws or presents. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib) { puts(dlerror());return 2; }
    int (*choose)(void **,const int *)=dlsym(lib,"gliChoosePixelFormat");
    int (*create)(GLIContext *,void *,GLIContext,GLIFunctionDispatch *,GLIFunctionDispatch *,unsigned)=dlsym(lib,"gliCreateContext");
    int (*attach)(GLIContext,unsigned,const void *,unsigned)=dlsym(lib,"gliAttachDrawable");
    int (*destroy)(GLIContext)=dlsym(lib,"gliDestroyContext");
    int (*destroyPF)(void *)=dlsym(lib,"gliDestroyPixelFormat");
    if(!choose || !create || !attach || !destroy || !destroyPF) return 2;
    const int attrs[]={4,8,24,11,8,0};void *pf=NULL;GLIContext ctx=NULL;
    GLIFunctionDispatch *disp=calloc(1,sizeof(*disp)),*fallback=calloc(1,sizeof(*fallback));
    if(!disp || !fallback) return 2;
    int e=choose(&pf,attrs);printf("choose=%d\n",e);
    if(!e) { e=create(&ctx,pf,NULL,disp,fallback,0);printf("create=%d\n",e); }
    const unsigned record[5]={0};
    if(!e) { e=attach(ctx,kCGLPFAFullScreen,record,0);printf("fullscreen attach=%d (no draw performed)\n",e); }
    if(ctx) destroy(ctx);if(pf) destroyPF(pf);free(disp);free(fallback);
    return e ? 1 : 0;
}
