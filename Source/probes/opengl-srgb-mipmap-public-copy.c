/* Test an ordinary GL/GPU-copy correction, without native renderer hooks. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#define DECODE_PARAMETER 0x8a48
#define SKIP_DECODE 0x8a4a
int main(void)
{
    const CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("public-copy renderer=%s\n",glGetString(GL_RENDERER));int failures=0;
    for(int encoding=0;encoding<2;++encoding)for(int pattern=0;pattern<3;++pattern){
        GLuint textures[2],fbo=0;glGenTextures(2,textures);glGenFramebuffers(1,&fbo);
        unsigned char input[16],output[4];unsigned expected=128;
        const unsigned char samples[3][4]={{0,255,255,0},{64,192,192,64},{128,128,128,128}};
        for(int pixel=0;pixel<4;++pixel)for(int channel=0;channel<4;++channel)
            input[pixel*4+channel]=channel==3?255:samples[pattern][pixel];
        GLenum internal=encoding?GL_SRGB8_ALPHA8:GL_SRGB8;
        glBindTexture(GL_TEXTURE_2D,textures[0]);
        glTexImage2D(GL_TEXTURE_2D,0,internal,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,input);
        glTexParameteri(GL_TEXTURE_2D,DECODE_PARAMETER,SKIP_DECODE);
        glGenerateMipmap(GL_TEXTURE_2D);glGetTexImage(GL_TEXTURE_2D,1,GL_RGBA,GL_UNSIGNED_BYTE,output);
        printf("internal=%x pattern=%d native=%u\n",internal,pattern,output[0]);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glDisable(GL_FRAMEBUFFER_SRGB);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){++failures;goto next;}
        glBindTexture(GL_TEXTURE_2D,textures[1]);
        glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,0,0,2,2,0);
        glGenerateMipmap(GL_TEXTURE_2D);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],1);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){++failures;goto next;}
        glBindTexture(GL_TEXTURE_2D,textures[0]);
        glCopyTexSubImage2D(GL_TEXTURE_2D,1,0,0,0,0,1,1);
        glGetTexImage(GL_TEXTURE_2D,1,GL_RGBA,GL_UNSIGNED_BYTE,output);
        GLenum error=glGetError();int good=!error&&abs((int)output[0]-(int)expected)<=1&&
            output[1]==output[0]&&output[2]==output[0]&&output[3]==255;
        printf("%s public-copy internal=%x pattern=%d rgb=%u,%u,%u alpha=%u expected=%u error=%x\n",
            good?"PASS":"FAIL",internal,pattern,output[0],output[1],output[2],output[3],expected,error);
        failures+=!good;
next:
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(2,textures);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("public-copy failures=%d\n",failures);return failures?1:0;
}
