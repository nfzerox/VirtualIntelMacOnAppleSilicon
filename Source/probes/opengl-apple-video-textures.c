/* Native Intel / PassthroughOpenGL comparison for Final Cut's real extension
 * gate and packed 2vuy/yuvs transfers. No application injection or GL override.
 * Khronos APPLE_ycbcr_422, APPLE_client_storage, APPLE_texture_range contracts.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include "opengl-reference-renderer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has(const char *all,const char *name)
{
    size_t n=strlen(name);const char *p=all;
    while(p && (p=strstr(p,name))){
        if((p==all||p[-1]==' ')&&(!p[n]||p[n]==' '))return 1;
        p+=n;
    }
    return 0;
}
static void pixels(const unsigned char *p)
{
    const int expected[]={0,255,76,150};
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x){
        const unsigned char *v=p+(y*4+x)*4;
        for(unsigned c=0;c<3;++c)assert(abs((int)v[c]-expected[x])<=5);
        assert(v[3]==255);
    }
}
int main(int argc,char **argv)
{
    int intel=0,indirect=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--intel-hd5000"))intel=1;
        else if(!strcmp(argv[i],"--expect-indirect"))indirect=1;
        else return 64;
    }
    GLint renderer=intel?VIMReferenceIntelHD5000():0;if(intel&&!renderer)return 77;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAAllowOfflineRenderers,
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1080
        kCGLPFASupportsAutomaticGraphicsSwitching,
#endif
        intel?kCGLPFARendererID:0,(CGLPixelFormatAttribute)renderer,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count)&&pf);
    assert(!CGLCreateContext(pf,NULL,&ctx)&&ctx);CGLDestroyPixelFormat(pf);
    assert(!CGLSetCurrentContext(ctx));
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    const char *ext=(const char *)glGetString(GL_EXTENSIONS);assert(ext);
    const char *required[]={"GL_EXT_texture_rectangle","GL_APPLE_ycbcr_422",
        "GL_APPLE_client_storage","GL_APPLE_texture_range"};
    int missing=0;
    for(unsigned i=0;i<4;++i){int found=has(ext,required[i]);printf("%s=%d\n",required[i],found);missing|=!found;}
    if(missing){CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);return 2;}
    GLuint output=0,fb=0,source=0;
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,4,2);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,4,0,2,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_BLEND);glDisable(GL_DITHER);
    glGenTextures(1,&source);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_STORAGE_HINT_APPLE,GL_STORAGE_CACHED_APPLE);
    glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    /* Nontrivial row length/skips: 8 pixels/row, one skipped row/two pixels.
     * Retain all client memory until texture redefinition/deletion completes. */
    unsigned char *storage=NULL;assert(!posix_memalign((void **)&storage,4096,4096));
    for(unsigned order=0;order<2;++order){
        memset(storage,0xa5,4096);
        const unsigned char luma[]={16,235,81,145};
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x){
            unsigned at=(1+y)*16+(2+x)*2;
            /* Packed ushort component significance, on the measured Intel
             * little-endian reference: non-REV has luminance in bits 15:8. */
            storage[at+!order]=luma[x];storage[at+order]=128;
        }
        glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_ARB,4096,storage);
        void *range=(void *)1;GLint length=-1;
        glGetTexParameterPointervAPPLE(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_RANGE_POINTER_APPLE,&range);
        glGetTexParameteriv(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_RANGE_LENGTH_APPLE,&length);
        assert(!glGetError());if(indirect)assert(!range&&!length);
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_TRUE);
        GLboolean client=GL_FALSE;glGetBooleanv(GL_UNPACK_CLIENT_STORAGE_APPLE,&client);assert(client);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,8);glPixelStorei(GL_UNPACK_SKIP_ROWS,1);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        GLenum type=order?GL_UNSIGNED_SHORT_8_8_REV_APPLE:GL_UNSIGNED_SHORT_8_8_APPLE;
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGB,4,2,0,GL_YCBCR_422_APPLE,type,storage);
        GLenum error=glGetError();printf("packed_order=%u upload_error=%x\n",order,error);assert(!error);
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_FALSE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glBegin(GL_QUADS);
        glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(4,0);glVertex2f(4,0);
        glTexCoord2f(4,2);glVertex2f(4,2);glTexCoord2f(0,2);glVertex2f(0,2);glEnd();
        unsigned char result[32];memset(result,0xa5,sizeof(result));
        glReadPixels(0,0,4,2,GL_RGBA,GL_UNSIGNED_BYTE,result);assert(!glGetError());
        for(unsigned x=0;x<4;++x)printf(" pixel%u=%u,%u,%u,%u",x,result[4*x],result[4*x+1],result[4*x+2],result[4*x+3]);
        putchar('\n');pixels(result);
        glFinish();assert(!glGetError());
    }
    glDeleteTextures(1,&source);glFinish();free(storage);
    glDeleteTextures(1,&output);glDeleteFramebuffersEXT(1,&fb);
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);
    puts("PASS video extension gate, both packed byte orders, strided/skipped upload, storage state and rendered pixels");
    return 0;
}
