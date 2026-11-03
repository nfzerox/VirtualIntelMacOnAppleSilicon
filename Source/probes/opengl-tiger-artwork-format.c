/* Exact public CGL request from Tiger iTunes Artwork +defaultPixelFormat,
 * independently decompiled from8R4088's native i386 saver at0xcc00. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
int main(void)
{
    CGLPixelFormatAttribute a[]={kCGLPFANoRecovery,kCGLPFAAccelerated,kCGLPFAColorSize,24,
        kCGLPFAAlphaSize,8,kCGLPFADepthSize,0,kCGLPFAStencilSize,8,kCGLPFAAccumSize,0,kCGLPFAWindow,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0,accelerated=0,accum=-1;
    CGLError error=CGLChoosePixelFormat(a,&pf,&count);
    printf("choose=%d count=%d format=%p\n",error,(int)count,(void *)pf);fflush(stdout);
    if(error||!pf)return 1;
    CGLDescribePixelFormat(pf,0,kCGLPFAAccelerated,&accelerated);CGLDescribePixelFormat(pf,0,kCGLPFAAccumSize,&accum);
    error=CGLCreateContext(pf,NULL,&c);printf("create=%d context=%p accelerated=%d accum=%d\n",error,(void *)c,(int)accelerated,(int)accum);
    int result=1;
    if(!error&&c&&!CGLSetCurrentContext(c)){
        glDisable(GL_TEXTURE_2D);GLenum e=glGetError();
        printf("renderer=%s disable_error=%x\n",glGetString(GL_RENDERER),(unsigned)e);
        result=!accelerated||accum!=0||e;
    }
    CGLSetCurrentContext(NULL);if(c)CGLDestroyContext(c);CGLDestroyPixelFormat(pf);return result;
}
