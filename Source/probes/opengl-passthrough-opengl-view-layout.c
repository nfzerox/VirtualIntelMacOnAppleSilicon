/* Distinguish shared-allocation identity from logical-view pixel layout.
 * The dimensions come from a live CA trace (784 backing / 770 logical).
 * All pixels belong to this fixture; no desktop image is captured. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-surfaces.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
enum { W=784,H=483,V=770 };
static int inspect(const char *phase,const unsigned char *expected)
{
    const int points[][2]={{0,0},{1,1},{153,83},{431,95},{383,241},{769,482}};
    int good=1;
    for(unsigned i=0;i<sizeof(points)/sizeof(*points);++i) {
        int x=points[i][0],y=points[i][1];unsigned char p[4]={0},alpha[4]={0};
        const unsigned char *e=expected+((size_t)y*W+x)*4;
        glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        glReadPixels(x,y,1,1,GL_ALPHA,GL_UNSIGNED_BYTE,alpha);GLenum error=glGetError();
        int pass=!error && p[0]==e[2] && p[1]==e[1] && p[2]==e[0] && p[3]==e[3] && alpha[0]==e[3];
        printf("%s %s point=%d,%d actual=%u,%u,%u,%u alpha-only=%u expected=%u,%u,%u,%u error=%x\n",
            pass?"PASS":"FAIL",phase,x,y,p[0],p[1],p[2],p[3],alpha[0],e[2],e[1],e[0],e[3],error);
        good=pass && good;
    }
    return good;
}
static void sample(GLuint fbo,GLuint texture)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0,0,W,H);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(W,0);glVertex2f(1,-1);
    glTexCoord2f(W,H);glVertex2f(1,1);glTexCoord2f(0,H);glVertex2f(-1,1);glEnd();
}
int main(void)
{
    unsigned char *cpu=malloc(W*H*4),*shadow=calloc(W*H,4);if(!cpu || !shadow)return 2;
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
        unsigned char *p=cpu+((size_t)y*W+x)*4;
        p[0]=(x*3+y)%256;p[1]=(x+y*5)%256;p[2]=(x*7+y*11)%256;p[3]=(x*13+y*17)%256;
    }
    void *context=DGCreateContext(NULL,NULL);
    if(!context || !DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,W,H))return 2;
    uint64_t key=0;arc4random_buf(&key,sizeof(key));if(!key)key=1;
    uint32_t lease=DGAcquireSurface(key,1,W,H);
    int good=lease && DGUploadSurface(lease,cpu,W,H,W*4,1) && DGAttachSurfaceDrawable(lease);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glReadBuffer(GL_FRONT);glDrawBuffer(GL_FRONT);
    good=inspect("canonical drawing allocation",cpu) && good;
    GLuint output=0,fbo=0,textures[2]={0};glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    glGenTextures(2,textures);
    for(unsigned cropped=0;cropped<2;++cropped) {
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[cropped]);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        good=DGBindSurfaceView(lease,GL_RGBA,cropped?V:W,H) && good;
        sample(fbo,textures[cropped]);good=inspect(cropped?"cropped logical alias":"full logical alias",cpu) && good;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);glEnable(GL_SCISSOR_TEST);glScissor(153,83,279,14);
    glClearColor(1,0,1,.5f);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);glFlush();
    for(int y=83;y<97;++y)for(int x=153;x<432;++x) {
        unsigned char *p=cpu+((size_t)y*W+x)*4;p[0]=255;p[1]=0;p[2]=255;p[3]=128;
    }
    good=inspect("later GPU drawing",cpu) && good;
    for(unsigned cropped=0;cropped<2;++cropped) {
        sample(fbo,textures[cropped]);good=inspect(cropped?"cropped live GPU alias":"full live GPU alias",cpu) && good;
    }
    // Earlier tests read back the producer before sampling the alias. That
    // can hide render/alias hazards. Draw new pixels and immediately sample
    // them IN PLACE, with no readback/finish/flush/copy between the draws.
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);glColor4ub(0,255,255,64);
    glViewport(0,0,W,H);glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    GLfloat x0=2.f*153/W-1,x1=2.f*432/W-1,y0=2.f*83/H-1,y1=2.f*97/H-1;
    glBegin(GL_QUADS);glVertex2f(x0,y0);glVertex2f(x1,y0);glVertex2f(x1,y1);glVertex2f(x0,y1);glEnd();
    for(int y=83;y<97;++y)for(int x=153;x<432;++x) {
        unsigned char *p=cpu+((size_t)y*W+x)*4;p[0]=255;p[1]=255;p[2]=0;p[3]=64;
    }
    const char *sync=getenv("VIM_VIEW_SYNC");
    if(sync && !strcmp(sync,"flush"))glFlush();
    else if(sync && !strcmp(sync,"barrier"))glTextureBarrierNV();
    else if(sync && !strcmp(sync,"finish"))glFinish();
    else if(sync && !strcmp(sync,"read"))good=inspect("geometry readback control",cpu) && good;
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[1]);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(V,0);glVertex2f(2.f*V/W-1,-1);
    glTexCoord2f(V,H);glVertex2f(2.f*V/W-1,1);glTexCoord2f(0,H);glVertex2f(-1,1);glEnd();
    good=inspect("in-place alias after queued geometry without readback",cpu) && good;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteTextures(1,&textures[0]);glDeleteTextures(1,&output);
    glDeleteFramebuffersEXT(1,&fbo);
    unsigned char *shared_shadow=calloc(W*H,4);void *shared=DGCreateContext(NULL,context);
    if(!shared_shadow || !shared || !DGMakeCurrent(shared,shared_shadow,GL_UNSIGNED_BYTE,W,H))return 2;
    uint32_t shared_lease=DGAcquireSurface(key,1,W,H);
    good=shared_lease && DGAttachSurfaceDrawable(shared_lease) && good;
    DGDestroyContext(context);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    glViewport(0,0,W,H);glDisable(GL_BLEND);glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glColor4ub(255,255,0,192);glBegin(GL_QUADS);
    glVertex2f(x0,y0);glVertex2f(x1,y0);glVertex2f(x1,y1);glVertex2f(x0,y1);glEnd();
    for(int y=83;y<97;++y)for(int x=153;x<432;++x) {
        unsigned char *p=cpu+((size_t)y*W+x)*4;p[0]=0;p[1]=255;p[2]=255;p[3]=192;
    }
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[1]);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(V,0);glVertex2f(2.f*V/W-1,-1);
    glTexCoord2f(V,H);glVertex2f(2.f*V/W-1,1);glTexCoord2f(0,H);glVertex2f(-1,1);glEnd();
    good=inspect("shared GL alias survives original context destruction",cpu) && good;
    glDeleteTextures(1,&textures[1]);DGReleaseSurface(shared_lease);DGDestroyContext(shared);
    free(shared_shadow);free(cpu);free(shadow);
    return good?0:1;
}
