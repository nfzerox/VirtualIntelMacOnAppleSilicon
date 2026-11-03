/* Host-only typed backing qualification for the missing Snow CGLS formats.
 * Isolated public CGL/IOSurface allocations, no host display/driver changes.
 * A rejected import is evidence, never permission to advertise that format. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <IOSurface/IOSurface.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <stdio.h>
#include <math.h>
static int test(CGLContextObj context,const char *label,OSType pixelFormat,
                unsigned element,GLenum internal,GLenum type)
{
    CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int64_t values[]={16,16,element,16*element,16*16*element,pixelFormat};
    for(unsigned i=0;i<6;++i){CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,&values[i]);
        CFDictionarySetValue(properties,keys[i],n);CFRelease(n);}
    IOSurfaceRef surface=IOSurfaceCreate(properties);CFRelease(properties);
    if(!surface){fprintf(stderr,"%s: IOSurface allocation rejected\n",label);return 1;}
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    CGLError error=CGLTexImageIOSurface2D(context,GL_TEXTURE_RECTANGLE_ARB,internal,16,16,
        GL_RGBA,type,surface,0);
    GLenum before=glGetError();fprintf(stderr,"%s: import=%d GL=%x\n",label,error,before);
    int failed=error || before;
    if(!failed){
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
        GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
        failed=status!=GL_FRAMEBUFFER_COMPLETE_EXT;
        fprintf(stderr,"%s: FBO=%x\n",label,status);
        if(!failed){
            const GLfloat expected[]={0x1234/65535.f,0x5678/65535.f,0x9abc/65535.f,1.f};
            GLfloat observed[4]={0};glClearColor(expected[0],expected[1],expected[2],expected[3]);
            glClear(GL_COLOR_BUFFER_BIT);glFinish();glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,observed);
            GLenum pixels=glGetError();failed=pixels!=GL_NO_ERROR;
            float tolerance=internal==GL_RGBA16F_ARB?0.0005f:0.00002f;
            for(unsigned i=0;i<4;++i)failed|=!isfinite(observed[i]) || fabsf(observed[i]-expected[i])>tolerance;
            fprintf(stderr,"%s: pixel=%.8f,%.8f,%.8f,%.8f GL=%x %s\n",label,
                observed[0],observed[1],observed[2],observed[3],pixels,failed?"FAIL":"PASS");
        }
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);if(fbo)glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&texture);CFRelease(surface);return failed;
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attrs,&format,&count);
    if(!error)error=CGLCreateContext(format,NULL,&context);
    if(format)CGLDestroyPixelFormat(format);
    if(error || !context || CGLSetCurrentContext(context))return 2;
    fprintf(stderr,"renderer=%s\n",glGetString(GL_RENDERER));
    int failed=test(context,"RGBA16",kCVPixelFormatType_64RGBALE,8,GL_RGBA16,GL_UNSIGNED_SHORT);
    failed+=test(context,"RGBA16F",kCVPixelFormatType_64RGBAHalf,8,GL_RGBA16F_ARB,GL_HALF_FLOAT_ARB);
    failed+=test(context,"RGBA32F",kCVPixelFormatType_128RGBAFloat,16,GL_RGBA32F_ARB,GL_FLOAT);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);return failed?1:0;
}
