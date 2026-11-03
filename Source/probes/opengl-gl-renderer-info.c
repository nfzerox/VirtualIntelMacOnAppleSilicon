/* Actual public renderer capabilities; runs on host or unmodified reference. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--context")) {
        /* Period-compatible public CGL: no 10.7 profile selector on Snow.
         * This reports the actual native/forwarded string, not SDK defines. */
        CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
        CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
        CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
        if(!error)error=CGLCreateContext(format,NULL,&context);
        if(format)CGLDestroyPixelFormat(format);
        if(!error)error=CGLSetCurrentContext(context);
        if(error){printf("context error=%d\n",error);if(context)CGLDestroyContext(context);return 1;}
        printf("renderer=%s\nversion=%s\nextensions=%s\n",glGetString(GL_RENDERER),
            glGetString(GL_VERSION),glGetString(GL_EXTENSIONS));
        CGLSetCurrentContext(NULL);CGLDestroyContext(context);return 0;
    }
    if(argc!=1)return 64;
    CGLRendererInfoObj info=NULL;GLint n=0;
    CGLError e=CGLQueryRendererInfo(~0U,&info,&n);
    printf("renderers error=%d count=%d\n",e,n);if(e) return 1;
    CGLRendererProperty keys[]={kCGLRPRendererID,kCGLRPAccelerated,kCGLRPOnline,kCGLRPDisplayMask,
        kCGLRPVideoMemoryMegabytes,kCGLRPTextureMemoryMegabytes};
    const char *names[]={"id","accelerated","online","mask","video_MB","texture_MB"};
    for(GLint i=0;i<n;++i) {
        printf("renderer %d",i);
        for(unsigned k=0;k<sizeof(keys)/sizeof(keys[0]);++k) { GLint v=0;e=CGLDescribeRenderer(info,i,keys[k],&v);printf(" %s=%d(error=%d)",names[k],v,e); }
        puts("");
    }
    CGLDestroyRendererInfo(info);return 0;
}
