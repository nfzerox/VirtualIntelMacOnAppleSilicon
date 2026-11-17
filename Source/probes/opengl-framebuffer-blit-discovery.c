/* Finite native-framework FBO blit/discovery comparison for real apps such as
 * Valve ToGL. This does not bypass Apple's GL frontend or change extensions. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 1;
    CGLError error=CGLCreateContext(format,NULL,&context);CGLDestroyPixelFormat(format);
    if(error||!context||CGLSetCurrentContext(context))return 1;
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    int advertised=extensions&&strstr(extensions,"GL_EXT_framebuffer_blit")!=NULL;
    typedef void (*Blit)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
    void *framework=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_LAZY|RTLD_NOLOAD);
    Blit blit=framework?(Blit)dlsym(framework,"glBlitFramebufferEXT"):NULL;
    printf("renderer=%s version=%s framebuffer_blit=%d arb_fbo=%d symbol=%d\n",
        glGetString(GL_RENDERER),glGetString(GL_VERSION),advertised,
        extensions&&strstr(extensions,"GL_ARB_framebuffer_object")!=NULL,
        blit!=NULL);fflush(stdout);
    Dl_info owner;
    if(blit&&dladdr((void *)blit,&owner)&&owner.dli_fname)
        printf("blit_provider=%s\n",owner.dli_fname);
    if(!blit){
        printf("blit_pixels=not available through public framework discovery_consistent=%d\n",!advertised);
        if(framework)dlclose(framework);
        CGLSetCurrentContext(NULL);CGLDestroyContext(context);
        return advertised?1:0;
    }
    GLuint textures[2],buffers[2];glGenTextures(2,textures);glGenFramebuffersEXT(2,buffers);
    for(unsigned i=0;i<2;i++){
        glBindTexture(GL_TEXTURE_2D,textures[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,buffers[i]);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,textures[i],0);
        if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 1;
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glClearColor(i?0:1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,buffers[0]);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,buffers[1]);
    blit(0,0,8,8,0,0,8,8,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,buffers[1]);
    unsigned char pixels[8*8*4];glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum gl_error=glGetError();unsigned correct=0;
    for(unsigned i=0;i<64;i++)correct+=pixels[4*i]==255&&pixels[4*i+1]==0&&pixels[4*i+2]==0&&pixels[4*i+3]==255;
    printf("blit_pixels=%u/64 error=0x%x discovery_consistent=%d\n",correct,gl_error,advertised);fflush(stdout);
    glDeleteFramebuffersEXT(2,buffers);glDeleteTextures(2,textures);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    dlclose(framework);
    return gl_error||correct!=64?1:advertised?0:2;
}
