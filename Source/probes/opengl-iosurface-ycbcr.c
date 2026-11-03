/* Finite native CGL video-IOSurface contract probe. Same source can run in a
 * period guest or on the host; no application/driver override or capture. */
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||CGLCreateContext(pf,NULL,&ctx)||!ctx)return 1;
    CGLDestroyPixelFormat(pf);if(CGLSetCurrentContext(ctx))return 1;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    CFDictionaryRef empty=CFDictionaryCreate(NULL,NULL,NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const void *key=kCVPixelBufferIOSurfacePropertiesKey,*value=empty;
    CFDictionaryRef attrsCV=CFDictionaryCreate(NULL,&key,&value,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CVPixelBufferRef buffer=NULL;CVReturn cv=CVPixelBufferCreate(NULL,4,2,kCVPixelFormatType_422YpCbCr8,attrsCV,&buffer);
    CFRelease(attrsCV);CFRelease(empty);
    if(cv||!buffer)return 2;
    IOSurfaceRef surface=CVPixelBufferGetIOSurface(buffer);if(!surface)return 2;
    if(IOSurfaceLock(surface,0,NULL))return 2;
    unsigned char *base=IOSurfaceGetBaseAddress(surface);size_t row=IOSurfaceGetBytesPerRow(surface);
    const unsigned char uyvy[]={128,16,128,235,128,81,128,145};
    for(unsigned y=0;y<2;y++)memcpy(base+y*row,uyvy,sizeof(uyvy));
    IOSurfaceUnlock(surface,0,NULL);
    GLuint source=0,output=0,fbo=0;
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 3;
    glGenTextures(1,&source);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    CGLError e=CGLTexImageIOSurface2D(ctx,GL_TEXTURE_RECTANGLE_ARB,GL_RGB,4,2,GL_YCBCR_422_APPLE,GL_UNSIGNED_SHORT_8_8_APPLE,surface,0);
    GLenum bindError=glGetError();
    printf("fourcc=%08x surface-size=%zu,%zu bpe=%zu row=%zu bind=%d GLerror=%x\n",(unsigned)IOSurfaceGetPixelFormat(surface),
        IOSurfaceGetWidth(surface),IOSurfaceGetHeight(surface),IOSurfaceGetBytesPerElement(surface),row,e,bindError);
    int result=e||bindError?4:0;
    if(!result){
        glViewport(0,0,4,2);glDisable(GL_BLEND);glDisable(GL_DITHER);
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glBegin(GL_QUADS);glTexCoord2i(0,0);glVertex2f(-1,-1);glTexCoord2i(4,0);glVertex2f(1,-1);
        glTexCoord2i(4,2);glVertex2f(1,1);glTexCoord2i(0,2);glVertex2f(-1,1);glEnd();
        unsigned char pixels[16]={0};glReadPixels(0,0,4,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        GLenum sampleError=glGetError();const int expected[]={0,255,76,150};
        printf("sample-error=%x pixels=",sampleError);if(sampleError)result=5;
        for(unsigned i=0;i<4;i++){
            printf(" %u,%u,%u,%u",pixels[i*4],pixels[i*4+1],pixels[i*4+2],pixels[i*4+3]);
            for(unsigned c=0;c<3;c++)if(abs((int)pixels[i*4+c]-expected[i])>5)result=5;
            if(pixels[i*4+3]!=255)result=5;
        }
        puts("");
    }
    glDeleteTextures(1,&source);glDeleteTextures(1,&output);glDeleteFramebuffersEXT(1,&fbo);
    CVPixelBufferRelease(buffer);CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);return result;
}
