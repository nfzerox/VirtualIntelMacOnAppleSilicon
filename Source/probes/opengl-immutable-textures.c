/* Native core-profile immutable allocation used by El Capitan VectorKit.
 * Run unchanged against the period Intel driver and the installed GLI. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#include <stdio.h>
#include <string.h>
static int failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;fflush(stdout);}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
        kCGLPFAColorSize,32,kCGLPFADepthSize,32,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0,depth=-1;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&n);
    if(!e&&pf)e=CGLDescribePixelFormat(pf,0,kCGLPFADepthSize,&depth);
    if(!e&&pf)e=CGLCreateContext(pf,NULL,&c);
    if(pf)CGLDestroyPixelFormat(pf);
    check(!e&&c&&depth==24,"core context with native closest DepthSize32 selection");
    if(!c)return 1;CGLSetCurrentContext(c);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLuint texture[4]={0};glGenTextures(4,texture);
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_CUBE_MAP};
    for(unsigned i=0;i<4;i++){
        GLenum target=targets[i];glBindTexture(target,texture[i]);
        if(i==0)glTexStorage1D(target,3,GL_RGBA8,8);
        else if(i==2)glTexStorage3D(target,3,GL_RGBA8,8,8,8);
        else glTexStorage2D(target,3,GL_RGBA8,8,8);
        check(!glGetError(),"native immutable storage allocation");
        GLint immutable=0;glGetTexParameteriv(target,GL_TEXTURE_IMMUTABLE_FORMAT,&immutable);
        check(immutable==GL_TRUE&&!glGetError(),"immutable-format query");
        GLenum face=i==3?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target;
        for(GLint level=0;level<3;level++){
            GLint width=0;glGetTexLevelParameteriv(face,level,GL_TEXTURE_WIDTH,&width);
            check(width==(8>>level)&&!glGetError(),"complete mip-chain dimensions");
        }
    }
    glBindTexture(GL_TEXTURE_2D,texture[1]);
    unsigned char pixels[8*8*4],readback[sizeof(pixels)];
    for(unsigned i=0;i<sizeof(pixels);i++)pixels[i]=(unsigned char)(i*37+11);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLuint fbo=0;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture[1],0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"immutable texture FBO");
    glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,readback);
    check(!glGetError()&&!memcmp(pixels,readback,sizeof(pixels)),"immutable texture upload/readback");
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(3,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,readback);
    check(!glGetError()&&readback[0]==255&&!readback[1]&&readback[2]==255&&readback[3]==255,"actual GPU clear/readback");
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    check(glGetError()==GL_INVALID_OPERATION,"immutable storage cannot be redefined");
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(4,texture);
    check(!glGetError(),"clean resource teardown");
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);return failures?1:0;
}
