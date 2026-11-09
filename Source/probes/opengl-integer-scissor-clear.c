/* Defined counterpart to CTS viewport_array.scissor_clear. Floating-point
 * glClearColor/glClear on an integer attachment has undefined results; here
 * typed clears must honor scissor, ignore viewport and preserve exact bits.
 * Run unchanged through public CGL on native Intel/M4 and the guest.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static unsigned failures;
static void check(int condition,const char *name)
{printf("%s %s\n",condition?"PASS":"FAIL",name);failures+=!condition;}
static void exercise(int is_unsigned)
{
    GLuint texture=0,framebuffer=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,is_unsigned?GL_R32UI:GL_R32I,32,32,0,
        GL_RED_INTEGER,is_unsigned?GL_UNSIGNED_INT:GL_INT,NULL);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"integer clear FBO complete");
    const GLint rects[][4]={{4,5,7,11},{-3,-2,6,5},{31,31,8,8},{6,7,0,8},{6,7,8,0}};
    const uint32_t first=is_unsigned?UINT32_C(0xf1234567):UINT32_C(0xfedcba99);
    const uint32_t second=is_unsigned?UINT32_C(0x81234567):UINT32_C(0xffedcba9);
    GLuint u0[4]={first,0,0,0},u1[4]={second,0,0,0};
    GLint i0[4]={(GLint)first,0,0,0},i1[4]={(GLint)second,0,0,0};
    for(unsigned test=0;test<sizeof(rects)/sizeof(*rects);++test){
        glDisable(GL_SCISSOR_TEST);
        if(is_unsigned)glClearBufferuiv(GL_COLOR,0,u0);else glClearBufferiv(GL_COLOR,0,i0);
        glViewport(12,12,2,2); /* Clears do not use the viewport rectangle. */
        const GLint *r=rects[test];glScissor(r[0],r[1],r[2],r[3]);glEnable(GL_SCISSOR_TEST);
        if(is_unsigned)glClearBufferuiv(GL_COLOR,0,u1);else glClearBufferiv(GL_COLOR,0,i1);
        uint32_t pixels[32*32];memset(pixels,0xa5,sizeof(pixels));
        glReadPixels(0,0,32,32,GL_RED_INTEGER,is_unsigned?GL_UNSIGNED_INT:GL_INT,pixels);
        GLenum error=glGetError();int good=!error;
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){
            int inside=x>=r[0]&&x<r[0]+r[2]&&y>=r[1]&&y<r[1]+r[3];
            if(pixels[y*32+x]!=(inside?second:first))good=0;
        }
        printf("unsigned=%d rect=%d,%d,%d,%d error=%x\n",is_unsigned,r[0],r[1],r[2],r[3],error);
        check(good,"typed clear obeys exact clipped/empty scissor and ignores viewport");
    }
    glDisable(GL_SCISSOR_TEST);glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&texture);
}
int main(void)
{
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    exercise(0);exercise(1);CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    return failures?1:0;
}
