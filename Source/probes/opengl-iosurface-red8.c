/* Native CGL/IOSurface contract used by Sierra's compositor alpha masks.
 * Same executable on the physical Intel reference, host and accelerated guest.
 * No compositor/private structures, injected code or desktop permission. */
#include <IOSurface/IOSurface.h>
#include <CoreFoundation/CoreFoundation.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count)&&pf&&!CGLCreateContext(pf,NULL,&context));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(context));
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    CFMutableDictionaryRef props=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow,kIOSurfacePixelFormat};
    int values[]={27,31,1,64,0x4c303038}; /* native one-component8 L008 storage */
    for(unsigned i=0;i<5;++i){CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,values+i);CFDictionarySetValue(props,keys[i],n);CFRelease(n);}
    IOSurfaceRef surface=IOSurfaceCreate(props);CFRelease(props);assert(surface);
    GLuint source=0,output=0,fbo=0;glGenTextures(1,&source);glGenTextures(1,&output);glGenFramebuffersEXT(1,&fbo);
    glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,27,31,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    for(unsigned round=0;round<3;++round){
        unsigned char value=(unsigned char)(47+round*61);glFlush();assert(!IOSurfaceLock(surface,0,NULL));
        size_t stride=IOSurfaceGetBytesPerRow(surface);unsigned char *base=IOSurfaceGetBaseAddress(surface);assert(base&&stride>=27);
        for(unsigned y=0;y<31;++y){memset(base+y*stride,0xa5,stride);memset(base+y*stride,value,27);}
        assert(!IOSurfaceUnlock(surface,0,NULL));
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
        if(!round){CGLError error=CGLTexImageIOSurface2D(context,GL_TEXTURE_RECTANGLE_ARB,GL_R8,27,31,GL_RED,GL_UNSIGNED_BYTE,surface,0);
            printf("red8_import=%d\n",error);assert(!error);}
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glViewport(0,0,27,31);glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glBegin(GL_QUADS);
        glTexCoord2i(0,0);glVertex2f(-1,-1);glTexCoord2i(27,0);glVertex2f(1,-1);
        glTexCoord2i(27,31);glVertex2f(1,1);glTexCoord2i(0,31);glVertex2f(-1,1);glEnd();
        unsigned char pixel[4]={0};glReadPixels(13,17,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        printf("round=%u pixel=%u,%u,%u,%u error=%u\n",round,pixel[0],pixel[1],pixel[2],pixel[3],glGetError());
        assert(pixel[0]==value&&!pixel[1]&&!pixel[2]&&pixel[3]==255);
    }
    glDeleteTextures(1,&source);glDeleteTextures(1,&output);glDeleteFramebuffersEXT(1,&fbo);CFRelease(surface);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    puts("PASS native red-only sampling, padded backing and CPU seed refresh");return 0;
}
