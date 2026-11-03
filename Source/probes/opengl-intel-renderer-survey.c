/* Read-only physical reference selection. Never fall back to an offline GPU
 * or silently qualify NVIDIA/AMD/software instead of the main Intel display. */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <AvailabilityMacros.h>
#include <stdio.h>
#include <string.h>
int main(void){
    GLuint mask=CGDisplayIDToOpenGLDisplayMask(CGMainDisplayID());
    CGLRendererInfoObj info=NULL;GLint count=0;
    CGLError error=CGLQueryRendererInfo(mask,&info,&count);
    printf("main-display-mask=%x query=%d renderers=%d\n",mask,error,count);
    if(error||!mask||!info)return 1;
    GLint chosen=0;
    for(GLint i=0;i<count;i++){
        GLint renderer=0,online=0,accelerated=0,display=0;
        CGLDescribeRenderer(info,i,kCGLRPRendererID,&renderer);
        CGLDescribeRenderer(info,i,kCGLRPOnline,&online);
        CGLDescribeRenderer(info,i,kCGLRPAccelerated,&accelerated);
        CGLDescribeRenderer(info,i,kCGLRPDisplayMask,&display);
        printf("renderer=%x online=%d accelerated=%d display-mask=%x\n",renderer,online,accelerated,display);
        // QueryRendererInfo already selected the main-display renderer set.
        // Select its unique online hardware ID, not a guessed Intel model ID.
        if(online&&accelerated){if(chosen&&chosen!=renderer){CGLDestroyRendererInfo(info);return 77;}chosen=renderer;}
    }
    CGLDestroyRendererInfo(info);if(!chosen)return 77;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
        kCGLPFASupportsAutomaticGraphicsSwitching,
#endif
        kCGLPFARendererID,(CGLPixelFormatAttribute)chosen,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint formats=0;
    if(CGLChoosePixelFormat(attrs,&format,&formats)||!format)return 2;
    error=CGLCreateContext(format,NULL,&context);CGLDestroyPixelFormat(format);
    if(error||!context||CGLSetCurrentContext(context))return 3;
    const char *vendor=(const char *)glGetString(GL_VENDOR),*renderer=(const char *)glGetString(GL_RENDERER);
    GLint actual=0;CGLGetParameter(context,kCGLCPCurrentRendererID,&actual);
    printf("vendor=%s renderer=%s id=%x version=%s\n",vendor?vendor:"unknown",renderer?renderer:"unknown",actual,glGetString(GL_VERSION));
    GLint dispatchBytes=0;
    CGLError dispatchError=CGLGetParameter(context,kCGLCPDispatchTableSize,&dispatchBytes);
    printf("native-dispatch-error=%d bytes=%d pointer-bytes=%zu\n",dispatchError,dispatchBytes,sizeof(void *));
    GLint accelerated=0,recovery=0;
    CGLDescribePixelFormat(CGLGetPixelFormat(context),0,kCGLPFAAccelerated,&accelerated);
    CGLDescribePixelFormat(CGLGetPixelFormat(context),0,kCGLPFANoRecovery,&recovery);
    printf("selected-format accelerated=%d no-recovery=%d\n",accelerated,recovery);
    const GLenum limits[]={GL_MAX_TEXTURE_SIZE,GL_MAX_3D_TEXTURE_SIZE,GL_MAX_CUBE_MAP_TEXTURE_SIZE,
        GL_MAX_TEXTURE_IMAGE_UNITS,GL_MAX_VERTEX_ATTRIBS,GL_MAX_DRAW_BUFFERS,GL_MAX_SAMPLES_EXT};
    for(unsigned i=0;i<sizeof(limits)/sizeof(limits[0]);++i){
        GLint value=-1;glGetIntegerv(limits[i],&value);
        printf("limit=%x value=%d error=%x\n",limits[i],value,glGetError());
    }
    printf("extensions=%s\n",glGetString(GL_EXTENSIONS));
    int ok=vendor&&strstr(vendor,"Intel")&&renderer&&strstr(renderer,"Intel")&&((actual&0xffffff00)==(chosen&0xffffff00));
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);return ok?0:77;
}
