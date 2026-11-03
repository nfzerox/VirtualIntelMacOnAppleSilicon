/* Compare OS acceleration status and renderer selection on guest/reference. */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
int main(void)
{
    printf("display accelerated=%d\n",CGDisplayUsesOpenGLAcceleration(CGMainDisplayID()));
    CGLRendererInfoObj info=NULL;GLint n=0;
    CGLError error=CGLQueryRendererInfo(1,&info,&n);
    printf("renderers error=%d count=%d\n",error,n);
    for(int i=0;!error && i<n;++i) {
        const CGLRendererProperty keys[]={kCGLRPRendererID,kCGLRPAccelerated,kCGLRPWindow,
            kCGLRPBackingStore,kCGLRPVideoMemoryMegabytes,kCGLRPTextureMemoryMegabytes};
        for(unsigned j=0;j<sizeof(keys)/sizeof(*keys);++j) {
            GLint v=-1;CGLError e=CGLDescribeRenderer(info,i,keys[j],&v);
            printf("renderer[%d] property=%d value=%d (0x%x) error=%d\n",i,keys[j],v,v,e);
        }
    }
    if(info) CGLDestroyRendererInfo(info);
    for(int dbl=0;dbl<2;++dbl) {
        CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
            kCGLPFAColorSize,24,kCGLPFADepthSize,24,kCGLPFADoubleBuffer,0};
        if(!dbl) attrs[6]=0;
        CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;
        error=CGLChoosePixelFormat(attrs,&pf,&n);
        if(!error && pf) error=CGLCreateContext(pf,NULL,&ctx);
        if(!error && ctx) {
            CGLSetCurrentContext(ctx);
            GLboolean double_buffer=0;glGetBooleanv(GL_DOUBLEBUFFER,&double_buffer);
            printf("requested double=%d actual=%d renderer=%s\n",dbl,double_buffer,glGetString(GL_RENDERER));
            CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);
        } else printf("requested double=%d selection error=%d formats=%d\n",dbl,error,n);
        if(pf) CGLDestroyPixelFormat(pf);
    }
    return 0;
}
