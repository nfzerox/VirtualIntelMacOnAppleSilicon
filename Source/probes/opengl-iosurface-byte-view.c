/* CGLIOSurface.h permits texture views with different bytes per element.
 * Test the native contract used by video shaders: two 16-bit storage pixels
 * become one raw BGRA texel, without conversion or a special video renderer.
 * Run unchanged on the native host/reference and PassthroughOpenGL guest.
 */
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||CGLCreateContext(pf,NULL,&ctx)||!ctx)return 1;
    CGLDestroyPixelFormat(pf);if(CGLSetCurrentContext(ctx))return 1;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int64_t values[]={8,2,2,64,128,0x32767579};
    CFMutableDictionaryRef props=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    for(unsigned i=0;i<6;++i){
        CFNumberRef number=CFNumberCreate(NULL,kCFNumberSInt64Type,values+i);
        CFDictionarySetValue(props,keys[i],number);CFRelease(number);
    }
    IOSurfaceRef surface=IOSurfaceCreate(props);CFRelease(props);if(!surface)return 2;
    const unsigned char bytes[]={17,34,51,68,81,98,115,132,145,162,179,196,209,226,243,255};
    if(IOSurfaceLock(surface,0,NULL))return 2;
    unsigned char *base=IOSurfaceGetBaseAddress(surface);size_t stride=IOSurfaceGetBytesPerRow(surface);
    if(!base||stride<sizeof(bytes))return 2;
    for(unsigned y=0;y<2;++y)memcpy(base+y*stride,bytes,sizeof(bytes));
    IOSurfaceUnlock(surface,0,NULL);
    GLuint source=0,output=0,fbo=0;
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 3;
    glGenTextures(1,&source);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
    CGLError error=CGLTexImageIOSurface2D(ctx,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,4,2,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0);
    printf("storage=%zux%zu bpe=%zu row=%zu view=4x2 BGRA4 bind=%d\n",
        IOSurfaceGetWidth(surface),IOSurfaceGetHeight(surface),IOSurfaceGetBytesPerElement(surface),stride,error);
    if(error)return 4;
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glViewport(0,0,4,2);glDisable(GL_BLEND);glDisable(GL_DITHER);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);glTexCoord2i(0,0);glVertex2f(-1,-1);glTexCoord2i(4,0);glVertex2f(1,-1);
    glTexCoord2i(4,2);glVertex2f(1,1);glTexCoord2i(0,2);glVertex2f(-1,1);glEnd();
    unsigned char pixels[32]={0};glReadPixels(0,0,4,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    int failed=glGetError()!=GL_NO_ERROR;
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x){
        const unsigned char *p=pixels+(y*4+x)*4,*b=bytes+x*4;
        if(p[0]!=b[2]||p[1]!=b[1]||p[2]!=b[0]||p[3]!=b[3])failed=1;
    }
    printf("%s raw byte-view sampling\n",failed?"FAIL":"PASS");
    glDeleteTextures(1,&source);glDeleteTextures(1,&output);glDeleteFramebuffersEXT(1,&fbo);
    CFRelease(surface);CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);
    return failed?5:0;
}
