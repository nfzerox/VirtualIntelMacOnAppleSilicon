/* Owned drawing-view alias versus later CPU/GPU writes, with padded/odd
 * backing dimensions. No guest UI or system process is sampled or changed. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
extern int DGImportDrawable(const void *,int,int,size_t,int);
extern int DGReadDrawable(void *,int,int);
extern int DGBindDrawableTexture(unsigned,int,int);
static int check(const char *name,int w,int h,unsigned r,unsigned g,unsigned b)
{
    unsigned char pixel[4]={0};glReadPixels(w/2,h/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error=glGetError();int good=!error && pixel[0]==r && pixel[1]==g && pixel[2]==b && pixel[3]==255;
    printf("%s %s %dx%d RGBA=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",name,w,h,
        pixel[0],pixel[1],pixel[2],pixel[3],error);return good;
}
static int sample(GLuint fbo,const char *name,int w,int h,unsigned r,unsigned g,unsigned b)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(w,0);glVertex2f(1,-1);
    glTexCoord2f(w,h);glVertex2f(1,1);glTexCoord2f(0,h);glVertex2f(-1,1);glEnd();
    return check(name,w,h,r,g,b);
}
int main(int argc,char **argv)
{
    int w=argc==3?atoi(argv[1]):131,h=argc==3?atoi(argv[2]):37;
    if(w<1 || h<1 || w>2048 || h>2048)return 2;
    const char *setting=getenv("VIM_ALIAS_INTERNAL");
    unsigned internal=setting?(unsigned)strtoul(setting,NULL,0):GL_RGBA8;
    if(internal!=GL_RGBA && internal!=GL_RGBA8)return 2;
    unsigned char *cpu=calloc((size_t)w*h,4);void *context=DGCreateContext(NULL,NULL);
    if(!cpu || !context || !DGMakeCurrent(context,cpu,GL_UNSIGNED_BYTE,w,h))return 2;
    GLuint output=0,fbo=0,alias=0;
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glGenTextures(1,&alias);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias);
    int good=DGBindDrawableTexture(internal,w,h);
    printf("alias internal=%x\n",internal);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glViewport(0,0,w,h);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    good=sample(fbo,"initial alias",w,h,0,0,255) && good;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    for(int i=0;i<w*h;++i) { cpu[i*4]=0;cpu[i*4+1]=255;cpu[i*4+2]=0;cpu[i*4+3]=255; }
    good=DGImportDrawable(cpu,w,h,(size_t)w*4,0) && good;
    good=check("default after private CPU upload",w,h,0,255,0) && good;
    good=sample(fbo,"alias after private CPU upload without client rebind",w,h,0,255,0) && good;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    good=sample(fbo,"alias after later GPU clear without client rebind",w,h,255,0,0) && good;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glReadBuffer(GL_FRONT);glDrawBuffer(GL_FRONT);
    glPixelTransferf(GL_RED_BIAS,.25f);glPixelTransferf(GL_ALPHA_SCALE,0);glPixelTransferf(GL_ALPHA_BIAS,.25f);
    good=DGImportDrawable(cpu,w,h,(size_t)w*4,0) && good;
    glPixelTransferf(GL_RED_BIAS,0);glPixelTransferf(GL_ALPHA_SCALE,1);glPixelTransferf(GL_ALPHA_BIAS,0);
    good=check("private upload is canonical despite caller color-transfer state",w,h,0,255,0) && good;
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glPixelTransferf(GL_RED_BIAS,.25f);glPixelTransferf(GL_ALPHA_SCALE,0);glPixelTransferf(GL_ALPHA_BIAS,.25f);
    good=DGReadDrawable(cpu,w,h) && good;
    unsigned char *p=cpu+((size_t)(h/2)*w+w/2)*4;
    int canonical=!p[0] && p[1]==255 && !p[2] && p[3]==255;
    printf("%s private read ignores caller color-transfer state BGRA=%u,%u,%u,%u\n",
        canonical?"PASS":"FAIL",p[0],p[1],p[2],p[3]);good=canonical && good;
    unsigned char public_pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,public_pixel);
    GLenum transfer_error=glGetError();int preserved=!transfer_error && abs((int)public_pixel[0]-64)<=1 &&
        public_pixel[1]==255 && !public_pixel[2] && abs((int)public_pixel[3]-64)<=1;
    printf("%s public read retains caller color-transfer state RGBA=%u,%u,%u,%u error=%x\n",
        preserved?"PASS":"FAIL",public_pixel[0],public_pixel[1],public_pixel[2],public_pixel[3],transfer_error);good=preserved && good;
    glPixelTransferf(GL_RED_BIAS,0);glPixelTransferf(GL_ALPHA_SCALE,1);glPixelTransferf(GL_ALPHA_BIAS,0);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&alias);glDeleteTextures(1,&output);DGDestroyContext(context);free(cpu);return good?0:1;
}
