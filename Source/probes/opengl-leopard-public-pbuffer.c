/* Finite period public-CGL attachment control, without an Intel-only guard:
 * Leopard's supplied reference uses NVIDIA9400M, Tiger uses Intel GMA950.
 * No window, preferences, foreign surface or system changes; not a perf test. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
int main(void)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj f=NULL;CGLContextObj c=NULL;CGLPBufferObj p=NULL;GLint n=0;
    CGLError e=CGLChoosePixelFormat(a,&f,&n);
    if(!e)e=CGLCreateContext(f,NULL,&c);
    if(!e)e=CGLSetCurrentContext(c);
    printf("context=%d bits=%u renderer=%s\n",e,(unsigned)(8*sizeof(void*)),c?glGetString(GL_RENDERER):(const GLubyte *)"none");
    if(!e)e=CGLCreatePBuffer(17,13,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,0,&p);
    printf("create=%d\n",e);
    if(!e){e=CGLSetPBuffer(c,p,0,0,0);printf("attach=%d\n",e);}
    if(!e){glViewport(0,0,17,13);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        unsigned char pixel[4]={0};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        GLenum error=glGetError();printf("pixel=%u,%u,%u,%u GL=%x\n",pixel[0],pixel[1],pixel[2],pixel[3],(unsigned)error);
        if(error||pixel[0]!=255||pixel[1]||pixel[2]||pixel[3]!=255)e=kCGLBadState;
    }
    CGLSetCurrentContext(NULL);if(c)CGLDestroyContext(c);if(p)CGLDestroyPBuffer(p);if(f)CGLDestroyPixelFormat(f);
    return e?1:0;
}
