/* Owned native accelerated context: qualify private GLI commands without
 * modifying WindowServer or relying on the software renderer. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef CGLError (*SetInteger)(GLIContext, unsigned, const GLint *);
__attribute__((noinline)) static CGLError VIMPrivateIntegerInvoke(SetInteger setter,GLIContext context)
{ GLint value=0;return setter(context,300,&value); }
int main(void)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,
        kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
    if(!error)error=CGLCreateContext(format,NULL,&context);
    if(format)CGLDestroyPixelFormat(format);
    if(error || !context){fprintf(stderr,"native context error=%d\n",error);return 2;}
    CGLSetCurrentContext(context);
    const char *resources=getenv("GL_RESOURCES");char path[4096];
    snprintf(path,sizeof(path),"%s/GLEngine.bundle/GLEngine",resources?resources:
        "/System/Library/Frameworks/OpenGL.framework/Resources");
    void *engine=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    SetInteger setter=engine?(SetInteger)dlsym(engine,"gliSetInteger"):NULL;
    if(!setter){fprintf(stderr,"missing native gliSetInteger\n");return 2;}
    fprintf(stderr,"native renderer=%s\n",glGetString(GL_RENDERER));
    error=VIMPrivateIntegerInvoke(setter,context->rend);
    GLenum glError=glGetError();
    fprintf(stderr,"private integer 300 value=0 result=%d GL-error=%x\n",error,glError);
    glTextureBarrierNV();GLenum publicError=glGetError();
    fprintf(stderr,"public texture barrier GL-error=%x\n",publicError);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);dlclose(engine);
    return error || glError || publicError?1:0;
}
