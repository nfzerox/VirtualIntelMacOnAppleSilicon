/* Public CGL surface parameters on the physical Intel reference and guest.
 * Own finite context only; no window/system preferences or driver overrides. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLRenderers.h>
#include "opengl-reference-renderer.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    if((argc!=2&&argc!=3)||strcmp(argv[1],"--require-intel")||
       (argc==3&&strcmp(argv[2],"--intel-hd5000")))return 2;
    GLint intel=argc==3?VIMReferenceIntelHD5000():0;if(argc==3&&!intel)return 2;
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAAllowOfflineRenderers,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAAlphaSize,8,
        argc==3?kCGLPFARendererID:0,(CGLPixelFormatAttribute)intel,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format||
       CGLCreateContext(format,NULL,&context)||!context)return 2;
    CGLSetCurrentContext(context);
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s\n",renderer?renderer:"none");
    if(!renderer||!strstr(renderer,"Intel")){
        CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);return 2;
    }
    int failures=0;
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)++failures;
    for(GLint value=0;value<2;++value){
        GLint actual=-1;
        CGLError set=CGLSetParameter(context,kCGLCPSurfaceOpacity,&value);
        CGLError get=CGLGetParameter(context,kCGLCPSurfaceOpacity,&actual);
        glClearColor(1,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        unsigned char pixel[4]={0};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        GLenum error=glGetError();
        /* No window surface is attached: native CGL reports BadDrawable and
         * leaves output untouched. Window round trips are tested separately. */
        int good=!set&&get==kCGLBadDrawable&&actual==-1&&!error&&pixel[0]==255&&
            !pixel[1]&&!pixel[2]&&!pixel[3];
        printf("%s opacity=%d set=%d get=%d actual=%d FBO_RGBA=%u,%u,%u,%u error=%x\n",
            good?"PASS":"FAIL",value,set,get,actual,pixel[0],pixel[1],pixel[2],pixel[3],error);
        if(!good)++failures;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
    return failures?1:0;
}
