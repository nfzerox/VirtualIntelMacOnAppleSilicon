/* Same-GPU drawable sampling: no publication/readback before the snapshot.
 * Reproduces Quartz's ordered render -> sample -> effect transaction. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include "opengl-gl-feedback.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
extern int DGSnapshotDrawableTexture(GLenum,GLint,int,int,int);
extern int DGBindDrawableTexture(unsigned,int,int);
extern const char *DGTransportName(void);
int main(void)
{
    unsigned char *shadow=calloc(128*22,4);
    void *context=DGCreateContext(NULL,NULL);
    if(!context || !shadow || !DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,128,22)) return 2;
    printf("renderer=%s transport=%s\n",glGetString(GL_RENDERER),DGTransportName());
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,11,128,11);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    GLuint other=0,otherTexture=0,snapshot=0;
    glGenTextures(1,&otherTexture);glBindTexture(GL_TEXTURE_2D,otherTexture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&other);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,other);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,otherTexture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glGenTextures(1,&snapshot);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,snapshot);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,23);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_TRUE);
    int live=getenv("VIM_GL_DRAWABLE_ALIAS")!=NULL;
    if(live)glEnable(0xffffffffu);
    if(!live) { glPixelTransferf(GL_RED_BIAS,.25f);glPixelTransferf(GL_ALPHA_SCALE,0);glPixelTransferf(GL_ALPHA_BIAS,.25f); }
    int good=live?DGBindDrawableTexture(GL_RGBA8,126,22):DGSnapshotDrawableTexture(GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,128,22,0);
    if(!live) { glPixelTransferf(GL_RED_BIAS,0);glPixelTransferf(GL_ALPHA_SCALE,1);glPixelTransferf(GL_ALPHA_BIAS,0); }
    if(live) {
        GLenum prior=glGetError();good=prior==GL_INVALID_ENUM && good;
        printf("%s drawing-view import retains earlier caller error=%x\n",prior==GL_INVALID_ENUM?"PASS":"FAIL",prior);
    }
    GLint read=0,draw=0,texture=0,row=0,skip=0,swap=0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip);
    glGetIntegerv(GL_UNPACK_SWAP_BYTES,&swap);
    good=good && read==(GLint)other && draw==0 && texture==(GLint)snapshot && row==23 && skip==3 && swap;
    unsigned unchanged=0;for(unsigned i=0;i<128*22*4;++i) unchanged|=shadow[i];
    good=good && !unchanged;
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,snapshot,0);
    unsigned char bottom[4]={0},top[4]={0};
    glReadPixels(32,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bottom);
    glReadPixels(32,19,1,1,GL_RGBA,GL_UNSIGNED_BYTE,top);
    GLenum error=glGetError();
    good=good && bottom[0]==255 && !bottom[1] && !bottom[2] && bottom[3]==255 &&
        !top[0] && !top[1] && top[2]==255 && top[3]==255 && !error;
    printf("%s unpublished GPU snapshot/state: bottom=%u,%u,%u,%u top=%u,%u,%u,%u error=%x CPU-shadow=%u\n",
        good ? "PASS" : "FAIL",bottom[0],bottom[1],bottom[2],bottom[3],top[0],top[1],top[2],top[3],error,unchanged);
    if(live) {
        GLint width=0,height=0;glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_HEIGHT,&height);
        good=width==126 && height==22 && good;
        printf("%s drawing-view alias has logical dimensions=%d,%d\n",width==126&&height==22?"PASS":"FAIL",width,height);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);
        glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,snapshot);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,other);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glReadPixels(32,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bottom);error=glGetError();
        int changed=!error && !bottom[0] && bottom[1]==255 && !bottom[2] && bottom[3]==255;
        printf("%s drawing-view alias sees later GPU draw without reimport RGBA=%u,%u,%u,%u error=%x\n",
            changed?"PASS":"FAIL",bottom[0],bottom[1],bottom[2],bottom[3],error);
        good=changed && good;
        if(getenv("VIM_GL_SELF_SAMPLE")) {
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glReadBuffer(GL_FRONT);glDrawBuffer(GL_FRONT);
            glViewport(0,0,126,22);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,snapshot);glEnable(GL_TEXTURE_RECTANGLE_ARB);
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
            glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
            if(getenv("VIM_GL_TEXTURE_BARRIER"))glTextureBarrierNV();
            glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(126,0);glVertex2f(1,-1);
            glTexCoord2f(126,22);glVertex2f(1,1);glTexCoord2f(0,22);glVertex2f(-1,1);glEnd();
            glDisable(GL_TEXTURE_RECTANGLE_ARB);glReadPixels(32,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bottom);error=glGetError();
            int feedback=!error && !bottom[0] && bottom[1]==255 && !bottom[2] && bottom[3]==255;
            printf("%s private drawing-view self-sample barrier=%d RGBA=%u,%u,%u,%u error=%x\n",feedback?"PASS":"FAIL",
                getenv("VIM_GL_TEXTURE_BARRIER")!=NULL,bottom[0],bottom[1],bottom[2],bottom[3],error);
            good=feedback && good;
        }
        if(getenv("VIM_GL_LINEAR_FEEDBACK"))good=VIMLinearFeedback(snapshot,126,22) && good;
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,snapshot);
        int oversized=DGBindDrawableTexture(GL_RGBA8,129,22);error=glGetError();
        printf("%s oversized drawing-view import rejected error=%x\n",!oversized&&error==GL_INVALID_VALUE?"PASS":"FAIL",error);
        good=!oversized && error==GL_INVALID_VALUE && good;
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,126,22,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        error=glGetError();good=error==GL_INVALID_OPERATION && good;
        printf("%s live drawing-view storage replacement rejected error=%x\n",error==GL_INVALID_OPERATION?"PASS":"FAIL",error);
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&other);
    glDeleteTextures(1,&snapshot);glDeleteTextures(1,&otherTexture);
    DGDestroyContext(context);free(shadow);return !good;
}
