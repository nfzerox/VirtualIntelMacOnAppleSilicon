/* Read-only public byte-budget/renderer query on the actual period CGL.
 * Optional iWork library calls are its exported read-only VRAM queries. */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <dlfcn.h>
#include <stdio.h>
int main(void)
{
    CGDirectDisplayID displays[32];CGDisplayCount count=0;
    CGError error=CGGetActiveDisplayList(32,displays,&count);
    if(error)return 1;
    for(unsigned i=0;i<count;++i){
        unsigned mask=CGDisplayIDToOpenGLDisplayMask(displays[i]);
        CGLRendererInfoObj info=NULL;GLint n=0;CGLError e=CGLQueryRendererInfo(mask,&info,&n);
        printf("display=%u mask=%x size=%zux%zu query=%d count=%d\n",(unsigned)displays[i],mask,
            CGDisplayPixelsWide(displays[i]),CGDisplayPixelsHigh(displays[i]),e,(int)n);
        for(GLint r=0;!e&&r<n;++r){
            const CGLRendererProperty keys[]={kCGLRPRendererID,kCGLRPAccelerated,kCGLRPVideoMemory,kCGLRPTextureMemory};
            printf(" renderer=%d",(int)r);
            for(unsigned k=0;k<4;++k){GLint v=0;CGLError result=CGLDescribeRenderer(info,r,keys[k],&v);
                printf(" key%u=%d/%#x(error=%d)",(unsigned)keys[k],(int)v,(unsigned)v,result);}
            puts("");
        }
        if(info)CGLDestroyRendererInfo(info);
    }
    void *lib=dlopen("/Library/Application Support/iWork '09/Frameworks/SFAnimation.framework/SFAnimation",RTLD_LAZY|RTLD_LOCAL);
    if(lib){
        unsigned (*total)(void)=dlsym(lib,"SFXGLRendererTotalVRAM");
        unsigned (*display)(CGDirectDisplayID)=dlsym(lib,"SFXGLVRAMForDisplay");
        if(total)printf("iWork total VRAM=%u\n",total());
        for(unsigned i=0;display&&i<count;++i)printf("iWork display%u VRAM=%u\n",(unsigned)displays[i],display(displays[i]));
        dlclose(lib);
    }
    CGLPixelFormatAttribute attributes[]={kCGLPFAFullScreen,kCGLPFADoubleBuffer,
        kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint formats=0;
    CGLError e=CGLChoosePixelFormat(attributes,&format,&formats);
    printf("fullscreen choose=%d formats=%d present=%d\n",e,(int)formats,format!=NULL);
    void *engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_LAZY|RTLD_LOCAL);
    if(engine){
        struct Format {struct Format *next;unsigned renderer,flags,buffers;};
        int (*choose)(struct Format **,const GLint *)=dlsym(engine,"gliChoosePixelFormat");
        int (*destroy)(struct Format *)=dlsym(engine,"gliDestroyPixelFormat");
        if(choose&&destroy){
            const GLint window[]={4,kCGLPFAWindow,kCGLPFADoubleBuffer,0};
            const GLint fullscreen[]={4,kCGLPFAFullScreen,kCGLPFADoubleBuffer,0};
            const GLint combined[]={4,kCGLPFAWindow,kCGLPFAFullScreen,kCGLPFADoubleBuffer,0};
            const GLint *cases[]={window,fullscreen,combined};
            const char *names[]={"window","fullscreen","window+fullscreen"};
            for(unsigned i=0;i<3;++i){
                struct Format *raw=NULL;int result=choose(&raw,cases[i]);
                printf("GLI %s choose=%d renderer=%x flags=%x buffers=%x\n",names[i],result,
                    raw?raw->renderer:0,raw?raw->flags:0,raw?raw->buffers:0);
                if(raw)destroy(raw);
            }
        }
        dlclose(engine);
    }
    if(!format)return 2;
    if(!e&&format)e=CGLCreateContext(format,NULL,&context);
    printf("fullscreen create=%d\n",e);
    if(!e&&context)e=CGLSetCurrentContext(context);
    if(!e&&context)e=CGLSetFullScreen(context);
    printf("fullscreen attach=%d (%s)\n",e,CGLErrorString(e));
    if(context){CGLClearDrawable(context);CGLSetCurrentContext(NULL);CGLDestroyContext(context);}
    if(format)CGLDestroyPixelFormat(format);
    return 0;
}
