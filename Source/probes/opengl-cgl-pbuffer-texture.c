/* Public CGL rectangle/2D-pbuffer render -> shared-context texture.
 * Run unchanged on the physical Intel oracle and actual accelerated guest.
 * No private layout offsets, injection, system preferences or synthetic UI.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pixel(unsigned char *p,unsigned r,unsigned g,unsigned b,unsigned a)
{return abs((int)p[0]-(int)r)<=1 && abs((int)p[1]-(int)g)<=1 &&
        abs((int)p[2]-(int)b)<=1 && abs((int)p[3]-(int)a)<=1;}
static int check_pixel(int x,int y,unsigned r,unsigned g,unsigned b,unsigned a,const char *phase)
{
    unsigned char p[4]={0};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum error=glGetError();
    printf("%s pixel(%d,%d)=%u,%u,%u,%u error=%x\n",phase,x,y,p[0],p[1],p[2],p[3],error);
    return !error && pixel(p,r,g,b,a);
}
int main(int argc,char **argv)
{
    int two_d=0,require_pbuffer=0,rgba16=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--2d")&&!two_d)two_d=1;
        else if(!strcmp(argv[i],"--require-pbuffer-format")&&!require_pbuffer)require_pbuffer=1;
        else if(!strcmp(argv[i],"--rgba16")&&!rgba16)rgba16=1;
        else {fputs("usage: opengl-cgl-pbuffer-texture [--2d] [--require-pbuffer-format] [--rgba16]\n",stderr);return 2;}
    }
    const GLenum target=two_d?GL_TEXTURE_2D:GL_TEXTURE_RECTANGLE_ARB;
    const int width=64,height=two_d?64:48;
    setvbuf(stdout,NULL,_IOLBF,0);
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAColorSize,32,kCGLPFAAlphaSize,8,0,0,0,0,0};
    if(rgba16){attrs[3]=64;attrs[5]=16;}
    unsigned used=6;
    if(require_pbuffer)attrs[used++]=kCGLPFAPBuffer;
    const char *rid=getenv("VIM_PASSTHROUGH_OPENGL_RENDERER_ID");
    if(rid){attrs[used++]=kCGLPFASupportsAutomaticGraphicsSwitching;
        attrs[used++]=kCGLPFARendererID;attrs[used++]=(CGLPixelFormatAttribute)strtoul(rid,NULL,0);}
    attrs[used]=0;
    CGLPixelFormatObj pf=NULL;CGLContextObj producer=NULL,consumer=NULL;
    CGLPBufferObj pb=NULL;GLint count=0;int good=0;GLuint texture=0,color=0,fbo=0;
    CGLError error=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!error)error=CGLCreateContext(pf,NULL,&producer);
    if(!error)error=CGLCreateContext(pf,producer,&consumer);
    printf("choose/create=%d count=%d\n",error,count);if(error)goto done;
    CGLSetCurrentContext(producer);
    GLint renderer=0,accelerated=0,colorBits=0,alphaBits=0;
    CGLDescribePixelFormat(pf,0,kCGLPFARendererID,&renderer);
    CGLDescribePixelFormat(pf,0,kCGLPFAAccelerated,&accelerated);
    CGLDescribePixelFormat(pf,0,kCGLPFAColorSize,&colorBits);
    CGLDescribePixelFormat(pf,0,kCGLPFAAlphaSize,&alphaBits);
    printf("renderer_id=%x accelerated=%d renderer=%s\n",renderer,accelerated,glGetString(GL_RENDERER));
    printf("actual format color=%d alpha=%d\n",colorBits,alphaBits);
    if(!accelerated || !strstr((const char*)glGetString(GL_RENDERER),"Intel"))goto done;
    printf("pbuffer_target=%x size=%d,%d\n",target,width,height);
    error=CGLCreatePBuffer(width,height,target,rgba16?GL_RGBA16:GL_RGBA8,0,&pb);
    printf("create_pbuffer=%d\n",error);if(error)goto done;
    error=CGLSetPBuffer(producer,pb,0,0,0);
    printf("attach_pbuffer=%d\n",error);if(error)goto done;
    GLint draw=0,read=0,db=0;
    glGetIntegerv(GL_DRAW_BUFFER,&draw);glGetIntegerv(GL_READ_BUFFER,&read);glGetIntegerv(GL_DOUBLEBUFFER,&db);
    printf("producer draw=%x read=%x double=%d\n",draw,read,db);
    glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);
    if(rgba16){
        const GLushort expected[]={0x1234,0x5678,0x9abc,0xffff};GLushort observed[4]={0};
        glClearColor(expected[0]/65535.f,expected[1]/65535.f,expected[2]/65535.f,1);
        glClear(GL_COLOR_BUFFER_BIT);glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_SHORT,observed);
        printf("RGBA16 native pixel=%x,%x,%x,%x error=%x\n",observed[0],observed[1],observed[2],observed[3],glGetError());
        for(unsigned i=0;i<4;++i)if(abs((int)expected[i]-(int)observed[i])>1)goto done;
    }
    glClearColor(.25f,.5f,.75f,.5f);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,width,12);
    glClearColor(1,0,0,.25f);glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);glFinish();
    if(!check_pixel(32,30,64,128,191,128,"producer") ||
       !check_pixel(32,4,255,0,0,64,"producer"))goto done;
    error=CGLFlushDrawable(producer);printf("flush_drawable=%d\n",error);if(error)goto done;
    CGLSetCurrentContext(consumer);glGenTextures(1,&texture);
    glBindTexture(target,texture);
    error=CGLTexImagePBuffer(consumer,pb,GL_FRONT);printf("texture_import=%d\n",error);if(error)goto done;
    glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLint w=0,h=0;glGetTexLevelParameteriv(target,0,GL_TEXTURE_WIDTH,&w);
    glGetTexLevelParameteriv(target,0,GL_TEXTURE_HEIGHT,&h);
    printf("texture_size=%d,%d\n",w,h);if(w!=width || h!=height)goto done;
    // The CGL pbuffer import is a TEXTURE SOURCE, not a request to make that
    // private imported storage a new render target. Exercise real sampling.
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,color,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)goto done;
    glViewport(0,0,width,height);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,width,0,height,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBindTexture(target,texture);glEnable(target);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    GLfloat s=target==GL_TEXTURE_2D?1:width,t=target==GL_TEXTURE_2D?1:height;
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(s,0);glVertex2f(width,0);
    glTexCoord2f(s,t);glVertex2f(width,height);glTexCoord2f(0,t);glVertex2f(0,height);
    glEnd();glFinish();
    good=check_pixel(32,30,64,128,191,128,"consumer") && check_pixel(32,4,255,0,0,64,"consumer");
done:
    if(consumer){CGLSetCurrentContext(consumer);if(fbo)glDeleteFramebuffersEXT(1,&fbo);
        if(texture)glDeleteTextures(1,&texture);if(color)glDeleteTextures(1,&color);}
    CGLSetCurrentContext(NULL);
    if(consumer)CGLDestroyContext(consumer);if(producer)CGLDestroyContext(producer);
    if(pb)CGLDestroyPBuffer(pb);if(pf)CGLDestroyPixelFormat(pf);
    puts(good?"PASS pbuffer pixels, alpha, row orientation and shared texture":"FAIL pbuffer contract");
    return good?0:1;
}
