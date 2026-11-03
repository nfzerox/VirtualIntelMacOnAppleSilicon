/* Shared render-target ownership, ordering and retirement. No guest image or
 * system process is touched. This exercises negotiated private primitives. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-surfaces.h"
#include "opengl-gl-feedback.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
enum { W=64,H=32 };
static int check(const char *name,int good)
{ printf("%s %s\n",good?"PASS":"FAIL",name);return good; }
static int pixel(const char *name,unsigned r,unsigned g,unsigned b,unsigned a)
{
    unsigned char p[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum error=glGetError();
    int good=!error && p[0]==r && p[1]==g && p[2]==b && p[3]==a;
    printf("%s %s RGBA=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",name,p[0],p[1],p[2],p[3],error);return good;
}
static int sample(GLuint texture,unsigned r,unsigned g,unsigned b,unsigned a,const char *name)
{
    glViewport(0,0,W,H);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(W,0);glVertex2f(1,-1);
    glTexCoord2f(W,H);glVertex2f(1,1);glTexCoord2f(0,H);glVertex2f(-1,1);glEnd();
    glDisable(GL_TEXTURE_RECTANGLE_ARB);return pixel(name,r,g,b,a);
}
static void fill(unsigned char *bytes,unsigned b,unsigned g,unsigned r,unsigned a)
{ for(unsigned i=0;i<W*H;++i) { bytes[4*i]=b;bytes[4*i+1]=g;bytes[4*i+2]=r;bytes[4*i+3]=a; } }
static int run(void)
{
    unsigned char shadows[2][W*H*4]={0},cpu[W*H*4];uint64_t key=0;arc4random_buf(&key,sizeof(key));if(!key)key=1;
    int attrs[]={0x30,24,0x31,8,0};void *producer=DGCreateContext(attrs,NULL),*consumer=DGCreateContext(NULL,NULL);
    if(!producer || !consumer || !DGMakeCurrent(producer,shadows[0],GL_UNSIGNED_BYTE,W,H))return 2;
    if(!DGSupportsSurfaceDrawables())return 2;
    uint32_t lease=DGAcquireSurface(key,1,W,H);fill(cpu,255,0,0,255);
    glPixelTransferf(GL_RED_BIAS,.25f);glPixelTransferf(GL_ALPHA_SCALE,0);glPixelTransferf(GL_ALPHA_BIAS,.25f);
    int good=lease && DGUploadSurface(lease,cpu,W,H,W*4,1);
    glPixelTransferf(GL_RED_BIAS,0);glPixelTransferf(GL_ALPHA_SCALE,1);glPixelTransferf(GL_ALPHA_BIAS,0);
    GLuint other=0,binding=0;glGenFramebuffersEXT(1,&other);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,other);
    glGenTextures(1,&binding);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,binding);glEnable(GL_SCISSOR_TEST);glScissor(2,3,0,0);
    glEnable(0xffffffffu);int attached=DGAttachSurfaceDrawable(lease);GLenum prior=glGetError();
    good=check("attach preserves earlier caller error",attached && prior==GL_INVALID_ENUM) && good;
    GLint read=0,draw=0,texture=0,box[4]={0};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);glGetIntegerv(GL_SCISSOR_BOX,box);
    good=check("attach preserves independent framebuffer, texture and scissor state",read==(GLint)other && !draw &&
        texture==(GLint)binding && glIsEnabled(GL_SCISSOR_TEST) && box[0]==2 && box[1]==3 && !box[2] && !box[3] && !glGetError()) && good;
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&other);glDisable(GL_SCISSOR_TEST);
    good=pixel("default buffer is seeded retained storage",0,0,255,255) && good;
    good=DGBindSurface(lease,GL_RGBA8) && VIMLinearFeedback(binding,W,H) && good;
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();
    good=DGPublishSurfaceEpoch(lease,0,0,2) && pixel("same-storage publication needs no self-blit",0,255,0,255) && good;
    uint32_t bad=DGAcquireSurface(key+1,1,W+1,H);int rejected=DGAttachSurfaceDrawable(bad);GLenum error=glGetError();
    good=check("dimension mismatch does not replace render storage",!rejected && error==GL_INVALID_OPERATION) && good;
    good=pixel("old render storage survives failed attach",0,255,0,255) && good;DGReleaseSurface(bad);
    DGMakeCurrent(consumer,shadows[1],GL_UNSIGNED_BYTE,W,H);uint32_t alias=DGAcquireSurface(key,1,W,H);
    GLuint imported=0;glGenTextures(1,&imported);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,imported);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    good=DGBindSurface(alias,GL_RGBA8) && sample(imported,0,255,0,255,"independent context sees drawing") && good;
    fill(cpu,255,0,255,255);good=DGUploadSurface(alias,cpu,W,H,W*4,3) && good;
    DGMakeCurrent(producer,shadows[0],GL_UNSIGNED_BYTE,W,H);
    good=pixel("newer CPU epoch updates the actual render target",255,0,255,255) && good;
    glDeleteTextures(1,&binding);good=DGReleaseSurface(lease) && good;
    DGMakeCurrent(consumer,shadows[1],GL_UNSIGNED_BYTE,W,H);glDeleteTextures(1,&imported);good=DGReleaseSurface(alias) && good;
    DGMakeCurrent(producer,shadows[0],GL_UNSIGNED_BYTE,W,H);lease=DGAcquireSurface(key,1,W,H);uint64_t epoch=0;
    good=check("drawable retains pool identity when all context leases are released",lease && DGSurfaceEpoch(lease,&epoch) && epoch==3) && good;
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();
    DGMakeCurrent(consumer,shadows[1],GL_UNSIGNED_BYTE,W,H);alias=DGAcquireSurface(key,1,W,H);
    glGenTextures(1,&imported);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,imported);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    good=DGBindSurface(alias,GL_RGBA8) && sample(imported,0,255,0,255,"reacquired consumer sees writes without a copy publication") && good;
    good=DGRetireSurface(alias) && good;
    DGMakeCurrent(producer,shadows[0],GL_UNSIGNED_BYTE,W,H);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);error=glGetError();
    good=check("late default-buffer clear cannot resurrect a retired surface",error==GL_INVALID_OPERATION) && good;
    good=pixel("retired render target stays transparent",0,0,0,0) && good;
    GLuint fbo=0,image=0;glGenTextures(1,&image);glBindTexture(GL_TEXTURE_2D,image);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,image,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glClear(GL_COLOR_BUFFER_BIT);
    good=pixel("unrelated user FBO remains writable after source retirement",0,0,255,255) && good;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&image);
    uint32_t replacement=DGAcquireSurface(key,2,W,H);fill(cpu,0,0,255,255);
    good=DGUploadSurface(replacement,cpu,W,H,W*4,1) && DGAttachSurfaceDrawable(replacement) &&
        pixel("new backing incarnation replaces the retired render target",255,0,0,255) && good;
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);good=pixel("replacement render target accepts drawing",0,255,0,255) && good;
    DGReleaseSurface(lease);DGReleaseSurface(replacement);DGDestroyContext(producer);
    DGMakeCurrent(consumer,shadows[1],GL_UNSIGNED_BYTE,W,H);
    good=sample(imported,0,0,0,0,"old-incarnation consumer remains retired") && good;
    glDeleteTextures(1,&imported);DGReleaseSurface(alias);DGDestroyContext(consumer);return good?0:1;
}
int main(int argc,char **argv)
{
    unsigned cycles=argc==2?(unsigned)strtoul(argv[1],NULL,10):1;if(!cycles || cycles>300)return 2;
    for(unsigned i=0;i<cycles;++i) { int result=run();if(result)return result; }
    printf("PASS %u shared-drawable lifecycle cycle(s)\n",cycles);return 0;
}
