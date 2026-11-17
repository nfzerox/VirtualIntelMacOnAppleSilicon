/* Public accumulation-buffer oracle, not a conformance pass/fail test.
 * Compare this unmodified on native Intel and Apple-silicon hosts. Real
 * default/pbuffer accumulation and user-FBO errors are distinct contracts. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
int main(void) {
    for(int policy=0;policy<3;++policy)for(int bits=0;bits<=64;bits+=32) {
        CGLPixelFormatAttribute attributes[]={kCGLPFADoubleBuffer,kCGLPFAAccelerated,kCGLPFANoRecovery,
            kCGLPFASupportsAutomaticGraphicsSwitching,
            kCGLPFADepthSize,32,kCGLPFAAccumSize,(CGLPixelFormatAttribute)bits,
            policy==1?kCGLPFAClosestPolicy:policy==2?kCGLPFAMinimumPolicy:0,0};
        CGLPixelFormatObj format=NULL;GLint count=0,actual=-1,depth=-1;
        CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
        if(!error&&format) {
            CGLDescribePixelFormat(format,0,kCGLPFAAccumSize,&actual);
            CGLDescribePixelFormat(format,0,kCGLPFADepthSize,&depth);
            if(policy==0&&bits==32) {
                CGLContextObj context=NULL;
                CGLError context_error=CGLCreateContext(format,NULL,&context);
                if(!context_error)context_error=CGLSetCurrentContext(context);
                if(!context_error) {
                    printf("native renderer=%s\n",glGetString(GL_RENDERER));
                    CGLPBufferObj pbuffer=NULL;
                    CGLError created=CGLCreatePBuffer(16,16,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,0,&pbuffer);
                    CGLError attached=created?created:CGLSetPBuffer(context,pbuffer,0,0,0);
                    if(!attached) {
                        glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
                        glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glAccum(GL_LOAD,.5f);
                        GLenum load=glGetError();
                        glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glAccum(GL_ACCUM,.5f);
                        GLenum accum=glGetError();glAccum(GL_RETURN,1);GLenum returned=glGetError();
                        unsigned char pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                        printf("native pbuffer accum load=%x accum=%x return=%x pixel=%u,%u,%u,%u\n",
                            load,accum,returned,pixel[0],pixel[1],pixel[2],pixel[3]);
                    } else printf("native pbuffer creation=%d attach=%d\n",created,attached);
                    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
                    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
                    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
                    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
                    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glAccum(GL_LOAD,.5f);
                    GLenum load=glGetError();
                    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glAccum(GL_ACCUM,.5f);
                    GLenum accum=glGetError();glAccum(GL_RETURN,1);GLenum returned=glGetError();
                    unsigned char pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                    printf("native FBO accum renderer=%s load=%x accum=%x return=%x pixel=%u,%u,%u,%u\n",
                        glGetString(GL_RENDERER),load,accum,returned,pixel[0],pixel[1],pixel[2],pixel[3]);
                    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
                    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
                    if(pbuffer)CGLDestroyPBuffer(pbuffer);
                } else {
                    printf("native context error=%d\n",context_error);
                    if(context)CGLDestroyContext(context);
                }
            }
            CGLDestroyPixelFormat(format);
        }
        printf("policy=%s request=%d error=%d formats=%d actual-accum=%d depth=%d\n",
            policy==1?"closest":policy==2?"minimum":"default",bits,error,count,actual,depth);
    }
    return 0;
}
