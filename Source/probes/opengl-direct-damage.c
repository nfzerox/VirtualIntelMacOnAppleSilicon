/* Direct transport diagnostic, not an acceleration-status test. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
extern int DGDrawableDamage(int32_t *,int);
static int check(const char *name,int x,int y,int width,int height)
{
    int32_t d[4]={0};int good=DGDrawableDamage(d,1) &&
        d[0]==x && d[1]==y && d[2]==width && d[3]==height;
    printf("%s %s damage=%d,%d %dx%d\n",good?"PASS":"FAIL",name,d[0],d[1],d[2],d[3]);return good;
}
int main(void)
{
    setenv("VIM_GL_DAMAGE_PRESENT","1",1);
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n) || CGLCreateContext(pf,NULL,&c) || CGLSetCurrentContext(c)) return 2;
    void *pixels=calloc(64*64,4),*other=calloc(64*64,4);if(!pixels || !other) return 3;
    CGLSetOffScreen(c,64,64,256,pixels);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    int good=check("full color clear",0,0,64,64);
    glFlush();good=check("no-op flush has no damage",0,0,0,0) && good;
    glEnable(GL_SCISSOR_TEST);glScissor(4,5,6,7);glClear(GL_COLOR_BUFFER_BIT);
    glScissor(20,24,3,4);glClear(GL_COLOR_BUFFER_BIT);
    good=check("union of submitted scissor regions",4,5,19,23) && good;
    glScissor(-10,-20,15,26);glClear(GL_COLOR_BUFFER_BIT);
    good=check("clamped drawable bounds",0,0,5,6) && good;
    glClear(GL_DEPTH_BUFFER_BIT);good=check("depth-only clear does not publish color",0,0,0,0) && good;
    glScissor(1,2,3,4);glClear(GL_COLOR_BUFFER_BIT);
    CGLSetOffScreen(c,64,64,256,other);
    good=check("damage belongs to drawable",0,0,0,0) && good;
    CGLSetOffScreen(c,64,64,256,pixels);
    good=check("damage survives drawable switches",1,2,3,4) && good;
    GLuint fbo=0,texture=0;glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    glClear(GL_COLOR_BUFFER_BIT);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    good=check("offscreen FBO does not dirty window",0,0,0,0) && good;
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    GLenum error=glGetError();printf("%s final GL error=%x\n",error?"FAIL":"PASS",error);good=good && !error;
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(pf);
    free(pixels);free(other);return !good;
}
