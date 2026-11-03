/* Native-host diagnostic control: does glGetTexImage describe every row of
 * an IOSurface-backed small rectangle, with both APPLE pack-pitch choices?
 * Own pixels/context only; no VM, desktop capture or production mutation. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <IOSurface/IOSurface.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format||CGLCreateContext(format,NULL,&context))return 2;
    CGLDestroyPixelFormat(format);CGLSetCurrentContext(context);
    const int w=1154,h=22;unsigned failures=0;GLuint texture=0,fbo=0;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    CFMutableDictionaryRef props=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int values[]={1280,128,4,5120,5120*128,0x42475241};
    for(unsigned i=0;i<6;++i){CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,values+i);CFDictionarySetValue(props,keys[i],n);CFRelease(n);}
    IOSurfaceRef surface=IOSurfaceCreate(props);CFRelease(props);if(!surface)return 2;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    if(CGLTexImageIOSurface2D(context,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,w,h,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0))return 2;
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    glViewport(0,0,w,h);glEnable(GL_SCISSOR_TEST);
    for(int y=0;y<h;++y){glScissor(0,y,w,1);glClearColor(y/255.f,1,0,1);glClear(GL_COLOR_BUFFER_BIT);}
    glDisable(GL_SCISSOR_TEST);
    unsigned char *image=calloc((size_t)w*h,4);if(!image)return 2;
    for(unsigned mode=0;mode<3;++mode){
        memset(image,0,(size_t)w*h*4);
        glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_ALIGNMENT,4);
        glPixelStorei(GL_PACK_ROW_BYTES_APPLE,mode==1?w*4:0);
        if(mode==2)glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,image);
        else glGetTexImage(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
        unsigned bad=0;for(int y=0;y<h;++y){unsigned char *p=image+(size_t)y*w*4;bad+=p[0]!=y||p[1]!=255||p[2]||p[3]!=255;}
        /* Mode zero is the native diagnostic observation, not an expected
         * conformance failure on every host driver. Require the explicit
         * pitch and independent FBO read to be exact on this host. */
        GLenum error=glGetError();failures+=error||(mode&&bad);
        printf("%s mode=%u bad_rows=%u error=%x first=%u,%u,%u,%u last=%u,%u,%u,%u\n",!mode?"OBSERVED":bad||error?"FAIL":"PASS",mode,bad,error,
            image[0],image[1],image[2],image[3],image[(h-1)*w*4],image[(h-1)*w*4+1],image[(h-1)*w*4+2],image[(h-1)*w*4+3]);
    }
    free(image);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);CFRelease(surface);CGLDestroyContext(context);
    return failures?1:0;
}
