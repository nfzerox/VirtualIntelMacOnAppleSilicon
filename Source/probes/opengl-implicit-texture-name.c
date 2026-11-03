/* Legacy OpenGL may create an unused texture name by binding it, without
 * glGenTextures. Renderer-private FBO textures must not occupy that namespace.
 * Own context only; no window, preferences or installed-file changes. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attrs,&format,&count);
    if(!error)error=CGLCreateContext(format,NULL,&context);
    if(!error)error=CGLSetCurrentContext(context);
    int failed=1;
    if(!error){
        GLenum prior=glGetError();
        glBindTexture(GL_TEXTURE_2D,1);GLenum bound=glGetError();
        GLint name=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&name);GLenum queried=glGetError();
        printf("renderer=%s prior=%x bind=%x queried=%x name=%d\n",glGetString(GL_RENDERER),
            (unsigned)prior,(unsigned)bound,(unsigned)queried,(int)name);
        failed=prior||bound||queried||name!=1;
    }else printf("CGL error=%d\n",error);
    CGLSetCurrentContext(NULL);if(context)CGLDestroyContext(context);if(format)CGLDestroyPixelFormat(format);
    return failed;
}
