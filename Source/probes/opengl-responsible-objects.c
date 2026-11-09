/* Survey which object namespaces the private GLI accounting slot accepts. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

typedef void (*Generate)(GLsizei,GLuint *);
typedef void (*Bind)(GLuint);
typedef void (*BindTarget)(GLenum,GLuint);
typedef GLboolean (*Exists)(GLuint);
typedef void (*Delete)(GLsizei,const GLuint *);

static GLenum label(CGLContextObj ctx,GLenum type,GLuint name)
{
    ctx->disp.label_object_with_responsible_process_APPLE(ctx->rend,type,name,getpid());
    return glGetError();
}

static void object(CGLContextObj ctx,const char *description,GLenum type,
                   const char *gen,const char *bind,const char *exists,const char *del,GLenum target)
{
    Generate create=(Generate)dlsym(RTLD_DEFAULT,gen);
    Exists is=(Exists)dlsym(RTLD_DEFAULT,exists);
    Delete destroy=(Delete)dlsym(RTLD_DEFAULT,del);
    void *make=bind?dlsym(RTLD_DEFAULT,bind):NULL;
    if(!create || !is || !destroy || (bind && !make)){
        printf("%s exports unavailable\n",description);return;
    }
    GLuint name=0;create(1,&name);
    GLenum generated=glGetError();
    if(make){
        if(target)((BindTarget)make)(target,name);
        else ((Bind)make)(name);
    }
    GLboolean live=is(name);
    GLenum bound=glGetError();
    GLenum valid=label(ctx,type,name),missing=label(ctx,type,0xffffffffu);
    if(make){
        if(target)((BindTarget)make)(target,0);
        else ((Bind)make)(0);
    }
    destroy(1,&name);
    GLenum deleted=glGetError();
    GLenum after=label(ctx,type,name);
    printf("%s type=%04x native=%04x/%04x/%04x live=%d labels=%04x/%04x/%04x\n",
        description,type,generated,bound,deleted,live,valid,missing,after);
}

int main(void)
{
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    CGLDestroyPixelFormat(pf);
    if(!ctx->disp.label_object_with_responsible_process_APPLE)return 3;
    printf("accounting renderer=%s\n",glGetString(GL_RENDERER));
    object(ctx,"sampler",0x82e6,"glGenSamplers",NULL,"glIsSampler","glDeleteSamplers",0);
    object(ctx,"pipeline",0x82e4,"glGenProgramPipelines","glBindProgramPipeline",
        "glIsProgramPipeline","glDeleteProgramPipelines",0);
    object(ctx,"feedback",0x8e22,"glGenTransformFeedbacks","glBindTransformFeedback",
        "glIsTransformFeedback","glDeleteTransformFeedbacks",0x8e22);
    printf("sync token error=%04x\n",label(ctx,0x9116,0));
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);
    return 0;
}
