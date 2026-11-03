/* A modest browser-sized set of live images. Native Intel permits more than
 * 256; verify both import lifetime and GPU-to-CPU publication after Flush. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/CGLRenderers.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <IOSurface/IOSurface.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include "opengl-reference-renderer.h"
int main(int argc,char **argv)
{
    (void)argv;enum {N=300};GLint renderer=argc>1?VIMReferenceIntelHD5000():0;
    if(argc>1&&!renderer)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFARendererID,(CGLPixelFormatAttribute)renderer,0};
    if(argc==1)attrs[3]=0;
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint formats=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&formats);
    if(!e&&pf)e=CGLCreateContext(pf,NULL,&c);if(pf)CGLDestroyPixelFormat(pf);
    if(e||!c){fprintf(stderr,"context=%d\n",e);return 2;}CGLSetCurrentContext(c);
    printf("renderer=%s\n",glGetString(GL_RENDERER));fflush(stdout);
    IOSurfaceRef surfaces[N]={0};GLuint textures[N]={0},fbo=0;unsigned imported=0,visible=0,failed=0;
    glGenTextures(N,textures);glGenFramebuffersEXT(1,&fbo);
    for(unsigned i=0;i<N;++i){
        const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfacePixelFormat};
        int raw[]={8,8,4,0x42475241};CFNumberRef values[4];
        for(unsigned j=0;j<4;++j)values[j]=CFNumberCreate(NULL,kCFNumberIntType,raw+j);
        CFDictionaryRef properties=CFDictionaryCreate(NULL,keys,(const void **)values,4,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        surfaces[i]=IOSurfaceCreate(properties);CFRelease(properties);
        for(unsigned j=0;j<4;++j)CFRelease(values[j]);
        if(!surfaces[i]){failed=1;break;}
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[i]);
        e=CGLTexImageIOSurface2D(c,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,8,8,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surfaces[i],0);
        if(e){fprintf(stderr,"import=%u CGL=%d GL=0x%x\n",i,e,glGetError());failed=1;break;}
        ++imported;glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,textures[i],0);
        glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glFlush();GLenum error=glGetError();
    for(unsigned i=0;i<imported;++i){
        if(IOSurfaceLock(surfaces[i],kIOSurfaceLockReadOnly,NULL)){failed=1;continue;}
        const unsigned char *p=IOSurfaceGetBaseAddress(surfaces[i]);
        visible+=p&&p[0]==0&&p[1]==255&&p[2]==0&&p[3]==255;
        IOSurfaceUnlock(surfaces[i],kIOSurfaceLockReadOnly,NULL);
    }
    printf("%s imported=%u visible=%u requested=%u error=0x%x\n",
        !failed&&!error&&imported==N&&visible==N?"PASS":"FAIL",imported,visible,N,error);fflush(stdout);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(N,textures);
    for(unsigned i=0;i<N;++i)if(surfaces[i])CFRelease(surfaces[i]);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    return failed||error||imported!=N||visible!=N?1:0;
}
