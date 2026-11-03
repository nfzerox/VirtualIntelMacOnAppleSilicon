/* Verify actual sampled images, not only clears and constant-color shaders. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdint.h>
static int pixel(const char *name)
{
    unsigned char p[4]={0};glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum e=glGetError();int good=p[0]==255 && !p[1] && !p[2] && p[3]==255 && !e;
    printf("%s %s pixel=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",name,p[0],p[1],p[2],p[3],e);return good;
}
static void quad(void)
{
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(4,0);glVertex2f(1,-1);
    glTexCoord2f(4,4);glVertex2f(1,1);glTexCoord2f(0,4);glVertex2f(-1,1);glEnd();
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n) || CGLCreateContext(pf,NULL,&c) || CGLSetCurrentContext(c)) return 2;
    GLuint destination=0,fbo=0,source=0;
    glGenTextures(1,&destination);glBindTexture(GL_TEXTURE_2D,destination);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,destination,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT) return 3;
    unsigned char image[4*4*4];for(unsigned i=0;i<sizeof(image);i+=4) { image[i]=255;image[i+1]=image[i+2]=0;image[i+3]=255; }
    glGenTextures(1,&source);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    glViewport(0,0,64,64);glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    quad();int good=pixel("fixed-function rectangle texture");
    // Keep the fixed-function matrix built-in active as the native Quartz
    // immediate-mode shaders do. Diagnostic uniform inspection must not
    // query its nonexistent public location or inject an error into GL.
    const char *vs_text="#version 120\nvoid main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}";
    const char *fs_text="#version 120\n#extension GL_ARB_texture_rectangle:enable\nuniform sampler2DRect image;void main(){gl_FragColor=texture2DRect(image,gl_TexCoord[0].xy);}";
    GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER),program=glCreateProgram();
    glShaderSource(vs,1,&vs_text,NULL);glCompileShader(vs);glShaderSource(fs,1,&fs_text,NULL);glCompileShader(fs);
    glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked) { puts("FAIL texture shader link");return 4; }
    glUseProgram(program);glUniform1iARB(glGetUniformLocationARB((GLhandleARB)(uintptr_t)program,"image"),0);
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);quad();good=pixel("GLSL rectangle texture") && good;
    /* Mavericks Quartz uploads tiles directly from a much wider image with
     * APPLE_row_bytes, independent of ordinary row length. Exercise padding
     * and readback guards so truncating to width*height cannot pass. */
    unsigned char padded[8*64]={0};
    for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
        unsigned char *p=padded+y*64+x*4;p[0]=255;p[3]=255;
    }
    glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,64);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,padded);
    glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,0);
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);quad();good=pixel("APPLE padded texture rows") && good;
    unsigned char readback[96];for(unsigned i=0;i<sizeof(readback);++i) readback[i]=0x5a;
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,32);glReadPixels(16,16,2,2,GL_RGBA,GL_UNSIGNED_BYTE,readback);
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,0);
    GLenum packed_error=glGetError();int packed_good=!packed_error && readback[0]==255 && readback[1]==0 &&
        readback[32]==255 && readback[33]==0 && readback[8]==0x5a && readback[31]==0x5a && readback[40]==0x5a && readback[95]==0x5a;
    printf("%s APPLE padded readback rows error=%x\n",packed_good?"PASS":"FAIL",packed_error);good=packed_good && good;
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&source);glDeleteTextures(1,&destination);CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(pf);
    return !good;
}
