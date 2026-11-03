/* Finite ordinary window-capable context admission, using Chess's measured
 * native9J3050 non-multisample fallback. No window, surface or display change.
 * Compare ordinary recovery against an explicit --no-recovery control. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc>2||(argc==2&&strcmp(argv[1],"--no-recovery")))return 64;
    CGLPixelFormatAttribute attributes[]={kCGLPFADoubleBuffer,kCGLPFAWindow,
        kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,kCGLPFADepthSize,16,
        kCGLPFAStencilSize,1,argc==2?kCGLPFANoRecovery:0,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0,screen=0;
    CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
    printf("choose=%d screens=%d pointer-bits=%u recovery=%d\n",error,count,(unsigned)(sizeof(void*)*8),argc==1);
    if(error||!format)return 1;
    const CGLPixelFormatAttribute keys[]={kCGLPFAWindow,kCGLPFADoubleBuffer,
        kCGLPFAAccelerated,kCGLPFACompliant,kCGLPFARendererID,kCGLPFADisplayMask};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i){
        GLint value=0;CGLError status=CGLDescribePixelFormat(format,0,keys[i],&value);
        printf("format-key=%u value=%x status=%d\n",(unsigned)keys[i],value,status);
    }
    error=CGLCreateContext(format,NULL,&context);
    printf("create=%d context=%d\n",error,context!=NULL);
    if(!error&&context){
        error=CGLGetVirtualScreen(context,&screen);printf("virtual-screen=%d status=%d\n",screen,error);
        error=CGLSetCurrentContext(context);printf("make-current=%d\n",error);
        if(!error)printf("renderer=%s GL-error=%x\n",glGetString(GL_RENDERER),glGetError());
        CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    }
    CGLDestroyPixelFormat(format);return error||!context?2:0;
}
