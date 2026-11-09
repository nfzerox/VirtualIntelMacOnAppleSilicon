/* Read-only physical reference selection. Never fall back to an offline GPU
 * or silently qualify NVIDIA/AMD/software instead of the main Intel display. */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <AvailabilityMacros.h>
#include <stdio.h>
#include <string.h>
static void evaluators(void)
{
    const GLfloat p[]={1,2,3,99,4,5,6,99,7,8,9,99,10,11,12};
    GLfloat result[12]={0};
    GLint order[2]={0};
    glMap2f(GL_MAP2_VERTEX_3,-3,4,4,2,-5,6,8,2,p);
    glGetMapiv(GL_MAP2_VERTEX_3,GL_ORDER,order);
    glGetMapfv(GL_MAP2_VERTEX_3,GL_COEFF,result);
    printf("eval map2 order=%d,%d coeff=",order[0],order[1]);
    for(unsigned i=0;i<12;++i)printf("%s%g",i?",":"",result[i]);
    printf(" error=%x\n",glGetError());
    glActiveTexture(GL_TEXTURE1);
    GLint unit=0;
    glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);
    GLenum unit_error=glGetError();
    glMap1f(GL_MAP1_VERTEX_3,0,1,3,1,p);
    GLenum map_error=glGetError();
    glMap1f(GL_MAP1_TEXTURE_COORD_2,0,1,2,1,p);
    GLenum texcoord_error=glGetError();
    glActiveTexture(GL_TEXTURE0);
    glGetMapfv(GL_MAP1_VERTEX_3,GL_COEFF,result);
    printf("eval other texture unit=%x unit-error=%x map-error=%x unit-zero-coeff=%g,%g,%g\n",
           unit,unit_error,map_error,result[0],result[1],result[2]);
    glGetMapfv(GL_MAP1_TEXTURE_COORD_2,GL_COEFF,result);
    printf("eval other texture unit texcoord-error=%x unit-zero-coeff=%g,%g\n",
           texcoord_error,result[0],result[1]);
    GLuint fbo=0,rbo=0;
    glGenFramebuffersEXT(1,&fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glGenRenderbuffersEXT(1,&rbo);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rbo);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rbo);
    printf("eval framebuffer status=%x error=%x\n",glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT),glGetError());
    glBegin(GL_POINTS);
    glMap1f(GL_MAP1_VERTEX_3,0,1,3,1,p);
    glEnd();
    printf("eval inside begin error=%x\n",glGetError());
    const GLdouble point[]={1,2,3};
    glMap1d(GL_MAP1_VERTEX_3,1.0,1.0+0x1p-30,3,1,point);
    printf("eval closely spaced double endpoints error=%x\n",glGetError());
    glDeleteFramebuffersEXT(1,&fbo);
    glDeleteRenderbuffersEXT(1,&rbo);
}
int main(int argc,char **argv){
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
    if(ok && argc==2 && !strcmp(argv[1],"--evaluators"))evaluators();
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);return ok?0:77;
}
