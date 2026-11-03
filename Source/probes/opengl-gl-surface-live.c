/* CGLIOSurface.h's documented live binding, checked against real Mavericks.
 * CPU changes require Lock/Unlock and texture or FBO rebind, not reimport. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <IOSurface/IOSurface.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>
static int failures;
static unsigned storedAlpha=255,expectedAlpha=255;
static void number(CFMutableDictionaryRef d,CFStringRef key,int value)
{
    CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&value);
    CFDictionarySetValue(d,key,n);CFRelease(n);
}
static void fill(IOSurfaceRef surface,unsigned r,unsigned g,unsigned b)
{
    IOSurfaceLock(surface,0,NULL);
    unsigned char *pixels=IOSurfaceGetBaseAddress(surface);
    size_t pitch=IOSurfaceGetBytesPerRow(surface);
    for(int y=0;y<64;y++)for(int x=0;x<64;x++) {
        unsigned char *p=pixels+y*pitch+x*4;p[0]=b;p[1]=g;p[2]=r;p[3]=storedAlpha;
    }
    IOSurfaceUnlock(surface,0,NULL);
}
static void pixel(const char *test,unsigned r,unsigned g,unsigned b)
{
    unsigned char p[4]={0};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum e=glGetError();int pass=!e && p[0]==r && p[1]==g && p[2]==b && p[3]==expectedAlpha;
    printf("%s %s RGBA=%u,%u,%u,%u error=%x\n",pass?"PASS":"FAIL",test,p[0],p[1],p[2],p[3],e);
    if(!pass)++failures;
}
static void quad(void)
{
    glClear(GL_COLOR_BUFFER_BIT);glEnable(GL_TEXTURE_RECTANGLE_ARB);glColor4f(1,1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(64,0);glVertex2f(1,-1);
    glTexCoord2f(64,64);glVertex2f(1,1);glTexCoord2f(0,64);glVertex2f(-1,1);
    glEnd();glFinish();
}
int main(int argc,char **argv)
{
    GLenum internal=GL_RGBA8;int requireIntel=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--require-intel"))requireIntel=1;
        else if(!strcmp(argv[i],"--rgb8")){internal=GL_RGB8;storedAlpha=37;expectedAlpha=255;}
        else if(!strcmp(argv[i],"--rgb")){internal=GL_RGB;storedAlpha=37;expectedAlpha=255;}
        else if(!strcmp(argv[i],"--rgba")){internal=GL_RGBA;storedAlpha=expectedAlpha=37;}
        else if(!strcmp(argv[i],"--rgba8")){internal=GL_RGBA8;storedAlpha=expectedAlpha=37;}
        else {fprintf(stderr,"Usage: %s [--require-intel] [--rgb8|--rgb|--rgba8|--rgba]\n",argv[0]);return 2;}
    }
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!e && pf)e=CGLCreateContext(pf,NULL,&c);
    if(e || !c)return 2;
    CGLSetCurrentContext(c);printf("renderer=%s internal=%x\n",glGetString(GL_RENDERER),internal);
    if(requireIntel && !strstr((const char *)glGetString(GL_RENDERER),"Intel"))return 2;
    CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    number(d,kIOSurfaceWidth,64);number(d,kIOSurfaceHeight,64);number(d,kIOSurfaceBytesPerElement,4);
    number(d,kIOSurfaceBytesPerRow,256);number(d,kIOSurfacePixelFormat,0x42475241);
    IOSurfaceRef surface=IOSurfaceCreate(d);CFRelease(d);if(!surface)return 2;
    fill(surface,0,0,255);
    GLuint source=0,output=0,fbo=0,alias_fbo=0;
    glGenTextures(1,&source);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    e=CGLTexImageIOSurface2D(c,GL_TEXTURE_RECTANGLE_ARB,internal,64,64,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0);
    if(e) {printf("import=%d\n",e);return 2;}
    GLint actualInternal=0,alphaSize=0;
    glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_INTERNAL_FORMAT,&actualInternal);
    glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_ALPHA_SIZE,&alphaSize);
    GLenum metadataError=glGetError();int rgb=internal==GL_RGB || internal==GL_RGB8;
    int metadataPass=!metadataError && actualInternal==(rgb?GL_RGB8:GL_RGBA8) && alphaSize==(rgb?0:8);
    printf("%s texture internal=%x alpha-bits=%d error=%x\n",metadataPass?"PASS":"FAIL",actualInternal,alphaSize,metadataError);
    if(!metadataPass)++failures;
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    glViewport(0,0,64,64);quad();pixel("initial IOSurface sample",0,0,255);
    fill(surface,255,0,0);
    c->disp.bind_texture(c->rend,GL_TEXTURE_RECTANGLE_ARB,source);
    quad();pixel("CPU change after texture rebind",255,0,0);
    glGenFramebuffersEXT(1,&alias_fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,alias_fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,source,0);
    fill(surface,0,255,0);
    c->disp.bind_framebuffer_EXT(c->rend,GL_FRAMEBUFFER_EXT,alias_fbo);
    // Native Intel keeps the IOSurface's alpha channel for attachment reads,
    // even when RGB sampling supplies one. Do not silently weaken this check.
    expectedAlpha=storedAlpha;
    pixel("CPU change after framebuffer rebind",0,255,0);
    if(!rgb) {
        // Native flush/finish must publish GPU writes into the imported
        // guest IOSurface while an offscreen FBO, not the window, is bound.
        for(unsigned finish=0;finish<2;++finish){
            glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glClearColor(finish?1:0,0,finish?0:1,1);glClear(GL_COLOR_BUFFER_BIT);
            if(finish)c->disp.finish(c->rend);else c->disp.flush(c->rend);
            GLint bound=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&bound);
            uint32_t seed=0;int locked=!IOSurfaceLock(surface,kIOSurfaceLockReadOnly,&seed);
            const unsigned char *p=locked?(const unsigned char *)IOSurfaceGetBaseAddress(surface)+
                32*IOSurfaceGetBytesPerRow(surface)+32*4:NULL;
            int good=p && p[0]==(finish?0:255) && !p[1] && p[2]==(finish?255:0) && p[3]==255 &&
                bound==(GLint)alias_fbo && !glGetError();
            if(locked)IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
            printf("%s GPU IOSurface publication after %s, unchanged offscreen FBO\n",good?"PASS":"FAIL",finish?"Finish":"Flush");
            if(!good)++failures;
        }
        /* A binding change does not discard writes to the previous FBO.
         * CA/WebKit can restore framebuffer zero before their flush. The
         * native IOSurface CPU lock still observes those submitted writes. */
        for(unsigned finish=0;finish<2;++finish){
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,alias_fbo);
            glClearColor(finish?0:1,1,finish?1:0,1);glClear(GL_COLOR_BUFFER_BIT);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
            if(finish)c->disp.finish(c->rend);else c->disp.flush(c->rend);
            GLint bound=-1;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&bound);
            int locked=!IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
            const unsigned char *p=locked?(const unsigned char *)IOSurfaceGetBaseAddress(surface)+
                32*IOSurfaceGetBytesPerRow(surface)+32*4:NULL;
            unsigned char sample[4]={0};if(p)memcpy(sample,p,4);
            int good=p && p[0]==(finish?255:0) && p[1]==255 && p[2]==(finish?0:255) && p[3]==255 &&
                bound==0 && !glGetError();
            if(locked)IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
            printf("%s GPU IOSurface publication after FBO unbind and %s BGRA=%u,%u,%u,%u\n",
                good?"PASS":"FAIL",finish?"Finish":"Flush",sample[0],sample[1],sample[2],sample[3]);
            if(!good)++failures;
        }
        uint32_t unchanged_seed=IOSurfaceGetSeed(surface);
        for(unsigned i=0;i<3;++i)c->disp.flush(c->rend);
        int unchanged=unchanged_seed==IOSurfaceGetSeed(surface)&&!glGetError();
        printf("%s unchanged flushes do not rewrite CPU IOSurface\n",unchanged?"PASS":"FAIL");if(!unchanged)++failures;
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,alias_fbo);
        glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
        /* Native CGL requires submitting the GPU producer before CPU writes.
         * Writing CPU memory BEFORE this Flush instead races unsubmitted GL;
         * physical Intel overwrites that early CPU write on its later Flush. */
        c->disp.flush(c->rend);
        fill(surface,0,255,0);c->disp.flush(c->rend);
        int locked=!IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
        const unsigned char *fresh=locked?(const unsigned char *)IOSurfaceGetBaseAddress(surface)+32*IOSurfaceGetBytesPerRow(surface)+32*4:NULL;
        int latest=fresh&&!fresh[0]&&fresh[1]==255&&!fresh[2]&&fresh[3]==storedAlpha;
        if(locked)IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
        printf("%s newer CPU write survives repeated completed GPU publication\n",latest?"PASS":"FAIL");if(!latest)++failures;
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,alias_fbo);expectedAlpha=storedAlpha;
        pixel("newer CPU version reaches GPU on framebuffer rebind",0,255,0);
        /* Flushing a different shared context must not acknowledge a writer's
         * pending version before that writer submits it. Only inspect CPU
         * bytes after the real producer's required Flush: no premature CPU
         * lock whose contents would be undefined by CGLIOSurface.h. */
        CGLContextObj consumer=NULL;
        e=CGLCreateContext(pf,c,&consumer);
        if(e||!consumer){printf("FAIL create shared IOSurface consumer error=%d\n",e);++failures;}
        else {
            CGLSetCurrentContext(c);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,alias_fbo);
            glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
            CGLSetCurrentContext(consumer);consumer->disp.flush(consumer->rend);
            CGLSetCurrentContext(c);c->disp.flush(c->rend);
            locked=!IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
            const unsigned char *shared=locked?(const unsigned char *)IOSurfaceGetBaseAddress(surface)+
                32*IOSurfaceGetBytesPerRow(surface)+32*4:NULL;
            int shared_good=shared&&!shared[0]&&!shared[1]&&shared[2]==255&&shared[3]==255&&!glGetError();
            if(locked)IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
            printf("%s producer publication after unrelated shared-context Flush\n",shared_good?"PASS":"FAIL");
            if(!shared_good)++failures;
            fill(surface,0,255,0);uint32_t shared_seed=IOSurfaceGetSeed(surface);
            CGLSetCurrentContext(consumer);consumer->disp.flush(consumer->rend);
            CGLSetCurrentContext(c);c->disp.flush(c->rend);
            int shared_unchanged=shared_seed==IOSurfaceGetSeed(surface)&&!glGetError();
            printf("%s shared-context completed publication preserves newer CPU version\n",shared_unchanged?"PASS":"FAIL");
            if(!shared_unchanged)++failures;
            CGLDestroyContext(consumer);
        }
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,64,64,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,NULL);
        int retained=glGetError()==GL_INVALID_OPERATION;
        printf("%s invalid imported-texture redefinition rejected\n",retained?"PASS":"FAIL");if(!retained)++failures;
        fill(surface,0,0,255);c->disp.bind_framebuffer_EXT(c->rend,GL_FRAMEBUFFER_EXT,alias_fbo);
        pixel("failed redefinition retains live import",0,0,255);
        glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&alias_fbo);alias_fbo=0;
        glDeleteTextures(1,&source);source=0;c->disp.flush(c->rend);
        locked=!IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
        const unsigned char *retired=locked?(const unsigned char *)IOSurfaceGetBaseAddress(surface)+32*IOSurfaceGetBytesPerRow(surface)+32*4:NULL;
        int retired_good=retired&&retired[0]==255&&!retired[1]&&retired[2]==255&&retired[3]==255;
        if(locked)IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
        printf("%s texture/FBO retirement preserves prior GPU writes\n",retired_good?"PASS":"FAIL");if(!retired_good)++failures;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteFramebuffersEXT(1,&alias_fbo);
    glDeleteTextures(1,&source);glDeleteTextures(1,&output);CFRelease(surface);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(pf);return failures?1:0;
}
