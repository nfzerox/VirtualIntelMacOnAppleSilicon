// Compile as i386 on Mojave. Verify stock software GL with PVG registered.
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLRenderers.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    int forced=argc>1&&!strcmp(argv[1],"--software");
    CGLPixelFormatAttribute software[]={kCGLPFARendererID,
        (CGLPixelFormatAttribute)kCGLRendererGenericFloatID,kCGLPFAColorSize,24,0};
    CGLPixelFormatAttribute automatic[]={kCGLPFAColorSize,24,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint n=0,accel=0;
    CGLError e=CGLChoosePixelFormat(forced?software:automatic,&format,&n);
    if(e||!format){printf("choose=%d count=%d\n",e,n);return 2;}
    CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&accel);
    if((e=CGLCreateContext(format,NULL,&context))||(e=CGLSetCurrentContext(context))){printf("context=%d\n",e);return 3;}
    printf("bits=%zu forced=%d accelerated=%d renderer=%s\n",sizeof(void*)*8,forced,accel,glGetString(GL_RENDERER));
    GLuint texture,buffer;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&buffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,buffer);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    if(status!=GL_FRAMEBUFFER_COMPLETE_EXT){printf("framebuffer=0x%x\n",status);return 4;}
    glDisable(GL_DITHER);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    unsigned char p[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum error=glGetError();printf("pixel=%u,%u,%u,%u error=0x%x\n",p[0],p[1],p[2],p[3],error);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
    return sizeof(void*)!=4||error||p[0]!=255||p[1]||p[2];
}
