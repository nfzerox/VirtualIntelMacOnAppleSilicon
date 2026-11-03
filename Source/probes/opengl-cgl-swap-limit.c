/* Finite public CGL state/pixel comparison on the physical Intel renderer and
 * PassthroughOpenGL. No WindowServer attachment or private object offsets. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
static int observed(CGLContextObj context,int expected)
{
    GLint value=-1;CGLError error=CGLIsEnabled(context,kCGLCESwapLimit,&value);
    fprintf(stderr,"swap-limit expected=%d value=%d result=%d\n",expected,value,error);
    return error || (expected>=0 && value!=expected);
}
int main(void)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
    if(!error)error=CGLCreateContext(format,NULL,&context);
    if(format)CGLDestroyPixelFormat(format);
    if(error || !context || CGLSetCurrentContext(context))return 2;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    fprintf(stderr,"renderer=%s\n",renderer?renderer:"none");
    int failed=observed(context,-1);
    for(int enabled=0;enabled<4;++enabled){
        int state=enabled&1;
        error=state?CGLEnable(context,kCGLCESwapLimit):CGLDisable(context,kCGLCESwapLimit);
        fprintf(stderr,"set swap-limit=%d result=%d\n",state,error);
        failed|=error || observed(context,state);
    }
    GLuint texture=0,framebuffer=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    unsigned char pixel[4]={0};
    glClearColor(0.25f,0.5f,0.75f,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    fprintf(stderr,"pixel=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
    failed|=pixel[0]<62 || pixel[0]>66 || pixel[1]<126 || pixel[1]>130 ||
        pixel[2]<189 || pixel[2]>193;
    GLenum glError=glGetError();fprintf(stderr,"GL error=%x\n",glError);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&framebuffer);
    glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    return failed || glError?1:0;
}
