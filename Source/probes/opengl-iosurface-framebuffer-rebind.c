/* Real native IOSurface/FBO coherency, including unrelated dirty images,
 * attachment replacement, rejected mutations and object deletion. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <stdio.h>
static unsigned failures;
static void check(int good,const char *label)
{printf("%s %s\n",good?"PASS":"FAIL",label);failures+=!good;fflush(stdout);}
static IOSurfaceRef surface(void)
{
    CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfacePixelFormat};
    const int values[]={8,8,4,0x42475241};
    for(unsigned i=0;i<4;i++){CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,values+i);CFDictionarySetValue(d,keys[i],n);CFRelease(n);}
    IOSurfaceRef s=IOSurfaceCreate(d);CFRelease(d);return s;
}
static void fill(IOSurfaceRef s,unsigned char r,unsigned char g,unsigned char b)
{
    if(IOSurfaceLock(s,0,NULL)){failures++;return;}
    unsigned char *base=IOSurfaceGetBaseAddress(s);size_t stride=IOSurfaceGetBytesPerRow(s);
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
        unsigned char *p=base+y*stride+x*4;p[0]=b;p[1]=g;p[2]=r;p[3]=255;
    }
    if(IOSurfaceUnlock(s,0,NULL))failures++;
}
static void pixel(unsigned char r,unsigned char g,unsigned char b,const char *label)
{
    unsigned char p[4]={0};glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum error=glGetError();check(!error&&p[0]==r&&p[1]==g&&p[2]==b&&p[3]==255,label);
}
static void run(CGLOpenGLProfile profile)
{
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    CGLError e=CGLChoosePixelFormat(a,&pf,&n);if(!e&&pf)e=CGLCreateContext(pf,NULL,&c);
    if(pf)CGLDestroyPixelFormat(pf);check(!e&&c,"native accelerated context");if(!c)return;
    CGLSetCurrentContext(c);printf("profile=%x renderer=%s\n",profile,glGetString(GL_RENDERER));
    IOSurfaceRef s[2]={surface(),surface()};GLuint t[2],f[2];
    if(!s[0]||!s[1]){failures++;return;}
    fill(s[0],255,0,0);fill(s[1],0,255,0);glGenTextures(2,t);glGenFramebuffersEXT(2,f);
    for(unsigned i=0;i<2;i++){
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,t[i]);
        e=CGLTexImageIOSurface2D(c,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,8,8,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,s[i],0);
        check(!e&&!glGetError(),"native IOSurface import");
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f[i]);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,t[i],0);
        check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"complete framebuffer");
    }
    fill(s[1],0,0,255);
    for(unsigned i=0;i<12;i++)glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f[0]);
    pixel(255,0,0,"unrelated dirty surface does not change attachment");
    fill(s[0],0,255,0);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f[0]);
    pixel(0,255,0,"cached attachment observes actual CPU producer update");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,t[1],0);
    fill(s[1],255,0,255);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f[0]);
    pixel(255,0,255,"attachment replacement invalidates cache");
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,t[0],-1);
    check(glGetError()==GL_INVALID_VALUE,"rejected attachment change keeps native error");
    fill(s[1],255,255,0);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f[0]);
    pixel(255,255,0,"failed mutation preserves actual attachment");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f[1]);glDeleteTextures(1,t+1);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f[0]);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,t[0],0);
    fill(s[0],0,255,255);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f[0]);
    pixel(0,255,255,"deleted texture and replacement retain coherent framebuffer");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(2,f);glDeleteTextures(1,t);
    CFRelease(s[0]);CFRelease(s[1]);check(!glGetError(),"clean teardown");
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
}
int main(void){run(kCGLOGLPVersion_Legacy);run(kCGLOGLPVersion_3_2_Core);return failures?1:0;}
