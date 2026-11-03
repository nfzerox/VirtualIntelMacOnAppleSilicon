/* Real public CGL/CoreVideo contract used by era thumbnail/video clients.
 * Run the same dual-architecture executable on a guest or native Intel Mac.
 * No injection, capture permission, application policy, or timing assumption. */
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count)&&pf&&!CGLCreateContext(pf,NULL,&context));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(context));
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    CFDictionaryRef empty=CFDictionaryCreate(NULL,NULL,NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const void *keys[]={kCVPixelBufferIOSurfacePropertiesKey};const void *values[]={empty};
    CFDictionaryRef options=CFDictionaryCreate(NULL,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CVPixelBufferRef buffer=NULL;
    assert(!CVPixelBufferCreate(NULL,186,240,kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,options,&buffer));
    CFRelease(options);CFRelease(empty);
    IOSurfaceRef surface=CVPixelBufferGetIOSurface(buffer);assert(surface&&IOSurfaceGetPlaneCount(surface)==2);
    GLuint textures[2],output=0,fbo=0;glGenTextures(2,textures);glGenTextures(1,&output);glGenFramebuffersEXT(1,&fbo);
    glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,184,240,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    for(unsigned round=0;round<3;++round){
        unsigned char luma=(unsigned char)(37+round*61),chroma=(unsigned char)(83+round*37),alpha=(unsigned char)(193-round*41);
        glFlush();assert(!IOSurfaceLock(surface,0,NULL));
        for(unsigned plane=0;plane<2;++plane){
            unsigned char *base=IOSurfaceGetBaseAddressOfPlane(surface,plane);
            size_t stride=IOSurfaceGetBytesPerRowOfPlane(surface,plane),height=IOSurfaceGetHeightOfPlane(surface,plane);
            unsigned width=plane?92:184,bytes=plane?2:1;assert(base&&stride>=width*bytes&&height>=(plane?120:240));
            for(unsigned y=0;y<height;++y){
                memset(base+y*stride,0xa5,stride);
                for(unsigned x=0;x<width;++x){base[y*stride+x*bytes]=plane?chroma:luma;if(plane)base[y*stride+x*bytes+1]=alpha;}
            }
        }
        assert(!IOSurfaceUnlock(surface,0,NULL));
        for(unsigned plane=0;plane<2;++plane){
            unsigned width=plane?92:184,height=plane?120:240;
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[plane]);
            if(!round)assert(!CGLTexImageIOSurface2D(context,GL_TEXTURE_RECTANGLE_ARB,
                plane?GL_LUMINANCE8_ALPHA8:GL_LUMINANCE8,width,height,
                plane?GL_LUMINANCE_ALPHA:GL_LUMINANCE,GL_UNSIGNED_BYTE,surface,plane));
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glViewport(0,0,width,height);glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
            glBegin(GL_QUADS);
            glTexCoord2i(0,0);glVertex2f(-1,-1);glTexCoord2i(width,0);glVertex2f(1,-1);
            glTexCoord2i(width,height);glVertex2f(1,1);glTexCoord2i(0,height);glVertex2f(-1,1);glEnd();
            unsigned char pixel[4]={0};glReadPixels(17,23,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            unsigned char expected=plane?chroma:luma;
            assert(!glGetError()&&pixel[0]==expected&&pixel[1]==expected&&pixel[2]==expected&&pixel[3]==(plane?alpha:255));
        }
    }
    assert(CGLTexImageIOSurface2D(context,GL_TEXTURE_RECTANGLE_ARB,GL_LUMINANCE8,184,240,GL_LUMINANCE,GL_UNSIGNED_BYTE,surface,2)==kCGLBadValue);
    glDeleteTextures(2,textures);glDeleteTextures(1,&output);glDeleteFramebuffersEXT(1,&fbo);
    CVPixelBufferRelease(buffer);CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    puts("PASS actual NV12 planes, cropped views, luminance/chroma sampling, CPU seed refresh and invalid-plane rejection");
    return 0;
}
