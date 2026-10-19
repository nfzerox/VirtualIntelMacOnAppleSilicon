// Reproduce Aperture's GL_BGRA / packed-ARGB framebuffer without its library.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdint.h>
#include <stdio.h>
int main(int argc, char **argv) {
    (void)argv;
    setbuf(stdout, NULL);
    CGLPixelFormatAttribute attributes[]={kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,kCGLPFAAccelerated,0};
    CGLPixelFormatObj format=NULL; CGLContextObj context=NULL; GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format||
       CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    GLuint texture,framebuffer;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    // Packed word is B,G,R,A from high to low bits; intentionally distinct.
    uint32_t input[4]={0x332211ff,0x332211ff,0x332211ff,0x332211ff};
    glTexImage2D(GL_TEXTURE_2D,0,argc>1?GL_SRGB8_ALPHA8:GL_RGBA8,2,2,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8,input);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    printf("checking framebuffer\n");
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 3;
    unsigned char pixels[16];glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(int i=0;i<4;i++)if(pixels[4*i]!=0x11||pixels[4*i+1]!=0x22||pixels[4*i+2]!=0x33||pixels[4*i+3]!=255)return 4;
    glClearColor(0.2f,0.4f,0.6f,1);glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(int i=0;i<4;i++)if(pixels[4*i]!=51||pixels[4*i+1]!=102||pixels[4*i+2]!=153||pixels[4*i+3]!=255)return 5;
    uint32_t packed[4]={0};
    glReadPixels(0,0,2,2,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8,packed);
    for(int i=0;i<4;i++)if(packed[i]!=0x996633ff){printf("packed ARGB readback[%d]=%08x\n",i,packed[i]);return 7;}
    glReadPixels(0,0,2,2,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,packed);
    for(int i=0;i<4;i++)if(packed[i]!=0xff336699){printf("packed BGRA readback[%d]=%08x\n",i,packed[i]);return 8;}
    if(glGetError()!=GL_NO_ERROR)return 6;
    puts("PASS: ARGB upload, framebuffer, and rendered readback channels");
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);return 0;
}
