/* Native host acceptance for renderer-private IOSurface name eviction.
 * The same helper is included by the executor after integration. */
#include "../PassthroughOpenGL/private-textures-host.c"
#include <stdio.h>
static int Pixel(GLuint framebuffer)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
    unsigned char p[4]={0};glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glReadPixels(3,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum error=glGetError();
    printf("pixel=%u,%u,%u,%u error=%x\n",p[0],p[1],p[2],p[3],error);
    return !error&&p[0]==64&&p[1]==128&&p[2]==191&&p[3]==255;
}
int main(void)
{
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c1=NULL,c2=NULL;GLint n=0;
    if(CGLChoosePixelFormat(a,&pf,&n)||CGLCreateContext(pf,NULL,&c1)||
       CGLCreateContext(pf,c1,&c2)||CGLSetCurrentContext(c1))return 1;
    CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfacePixelFormat};
    int values[]={17,13,4,0x42475241};
    for(unsigned i=0;i<4;++i){CFNumberRef v=CFNumberCreate(NULL,kCFNumberIntType,values+i);
        CFDictionarySetValue(properties,keys[i],v);CFRelease(v);}
    IOSurfaceRef storage=IOSurfaceCreate(properties);CFRelease(properties);if(!storage)return 1;
    struct DGPrivateTextures *registry=DGPrivateTexturesCreate();
    GLuint name=0,f1=0,f2=0;glGenTextures(1,&name);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,name);
    if(CGLTexImageIOSurface2D(c1,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,17,13,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,storage,0))return 1;
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    struct DGPrivateTexture *texture=DGPrivateTextureRegister(registry,name,storage,GL_RGBA8,17,13,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV);
    if(!texture)return 1;
    GLuint canonical=name,mirror=name;
    struct DGPrivateTextureWatch canonical_watch={0},mirror_watch={0};
    DGPrivateTextureWatchSet(&canonical_watch,texture,&canonical);
    DGPrivateTextureWatchSet(&mirror_watch,texture,&mirror);
    glGenFramebuffersEXT(1,&f1);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f1);
    if(!DGPrivateTextureAttach(texture,GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,0)||
       glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 1;
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,17,13);
    glClearColor(.25,.5,.75,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    /* Another context's pending application error must not be mistaken for
     * a failed import or consumed while moving private objects. */
    glEnable(0xffffffff);
    CGLSetCurrentContext(c2);glGenFramebuffersEXT(1,&f2);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f2);
    if(!DGPrivateTextureAttach(texture,GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,0))return 1;
    GLenum error=DGPrivateTextureClaim(registry,name);
    printf("claim=%x old=%u private=%u current_preserved=%d\n",error,name,DGPrivateTextureName(texture),CGLGetCurrentContext()==c2);
    if(error||DGPrivateTextureName(texture)==name||DGPrivateTextureContains(registry,name)||
       canonical!=DGPrivateTextureName(texture)||mirror!=canonical)return 1;
    glBindTexture(GL_TEXTURE_2D,name);error=glGetError();GLint bound=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
    printf("public bind=%x name=%d\n",error,bound);
    int good=!error&&bound==(GLint)name&&Pixel(f2);
    CGLSetCurrentContext(c1);good=glGetError()==GL_INVALID_ENUM&&good;good=Pixel(f1)&&good;
    DGPrivateTexturesForgetFramebuffer(registry,f1);glDeleteFramebuffersEXT(1,&f1);
    CGLSetCurrentContext(c2);DGPrivateTexturesForgetFramebuffer(registry,f2);glDeleteFramebuffersEXT(1,&f2);
    glDeleteTextures(1,&name);DGPrivateTextureDestroy(texture);DGPrivateTexturesRelease(registry);
    if(canonical||mirror||canonical_watch.texture||mirror_watch.texture)good=0;
    CFRelease(storage);CGLSetCurrentContext(NULL);CGLReleaseContext(c2);CGLReleaseContext(c1);CGLReleasePixelFormat(pf);
    return good?0:1;
}
