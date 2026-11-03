/* Compare the private 10.9 compositor mipmap contract against native GL. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count) || CGLCreateContext(pf,NULL,&c)) return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c);
    GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,0x8a4b,1);
    GLenum flag_error=glGetError();
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_TRUE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_STORAGE_HINT_APPLE,GL_STORAGE_SHARED_APPLE);
    unsigned char pixels[4*4*4];
    for(unsigned i=0;i<sizeof(pixels);i+=4) { pixels[i]=255;pixels[i+1]=0;pixels[i+2]=0;pixels[i+3]=255; }
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum upload_error=glGetError();
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_FALSE);
    GLint width=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,1,GL_TEXTURE_WIDTH,&width);
    /* Native Apple's private mode keeps generated levels driver-internal:
     * level-one width can remain zero. Compare actual minified sampling,
     * not the public visibility of hidden driver mip levels. */
    GLuint destination=0,fbo=0;
    glGenTextures(1,&destination);glBindTexture(GL_TEXTURE_2D,destination);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,destination,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT) return 3;
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    GLenum fbo_error=glGetError();
    glViewport(0,0,1,1);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
    glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
    unsigned char level[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,level);
    GLenum image_error=glGetError();
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,1);
    GLenum exclusive_error=glGetError();
    printf("renderer=%s flag=%x upload=%x fbo=%x width1=%d pixel=%u,%u,%u,%u image=%x exclusive=%x\n",
        glGetString(GL_RENDERER),flag_error,upload_error,fbo_error,width,level[0],level[1],level[2],level[3],image_error,exclusive_error);
    int okay=!flag_error && !upload_error && level[0]==255 && !level[1] && !level[2] && level[3]==255 && !image_error && exclusive_error==GL_INVALID_OPERATION;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&destination);glDeleteTextures(1,&texture);CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    puts(okay?"PASS private automatic mipmaps":"FAIL private automatic mipmaps");return okay?0:1;
}
