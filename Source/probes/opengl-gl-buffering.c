/* Actual native CGL double-buffer/fence behavior, not capability bits alone. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
static int check(GLenum buffer,unsigned r,unsigned g,unsigned b)
{
    unsigned char pixel[4]={0};glReadBuffer(buffer);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    printf("buffer=%x pixel=%u,%u,%u expected=%u,%u,%u\n",buffer,pixel[0],pixel[1],pixel[2],r,g,b);
    return pixel[0]==r && pixel[1]==g && pixel[2]==b;
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFADoubleBuffer,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&n);
    if(!e && pf) e=CGLCreateContext(pf,NULL,&ctx);
    if(!ctx || e) { printf("create failed=%d\n",e);return 1; }
    CGLSetCurrentContext(ctx);glViewport(0,0,64,64);
    glDrawBuffer(GL_FRONT);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glDrawBuffer(GL_BACK);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    int pass=check(GL_FRONT,255,0,0) && check(GL_BACK,0,255,0);
    CGLFlushDrawable(ctx);
    pass=check(GL_FRONT,0,255,0) && check(GL_BACK,0,255,0) && pass;
    glDrawBuffer(GL_BACK);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    pass=check(GL_FRONT,0,255,0) && check(GL_BACK,0,0,255) && pass;
    GLenum buffering_error=glGetError();printf("buffering error=%x\n",buffering_error);
    GLuint fence=0;glGenFencesAPPLE(1,&fence);
    GLboolean generated_is=glIsFenceAPPLE(fence),generated_test=glTestFenceAPPLE(fence);
    GLenum generated_error=glGetError();
    printf("generated fence=%u is=%u test=%u error=%x\n",fence,generated_is,generated_test,generated_error);
    pass=!generated_is && generated_test && pass;
    glSetFenceAPPLE(fence);glFinishFenceAPPLE(fence);
    pass=glIsFenceAPPLE(fence) && glTestFenceAPPLE(fence) && pass;
    glDeleteFencesAPPLE(1,&fence);pass=!glIsFenceAPPLE(fence) && pass;
    GLenum error=glGetError();pass=pass && !buffering_error && !generated_error;
    printf("error=%x %s\n",error,pass && !error ? "PASS" : "FAIL");
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);
    return !pass || error;
}
