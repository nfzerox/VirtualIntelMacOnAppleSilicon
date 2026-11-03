/* Own-process physical Tiger reference only. Optional flat-namespace launch;
 * never inject into WindowServer or install into a system directory. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLContext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
static unsigned events;
static int Trace(void){return __sync_fetch_and_add(&events,1)<2048;}
struct ContextTrace {GLIContext renderer;GLIFunctionDispatch original;};
static struct ContextTrace contexts[32];static unsigned count;
static struct ContextTrace *Find(GLIContext c)
{for(unsigned i=0;i<count;i++)if(contexts[i].renderer==c)return contexts+i;return NULL;}
static void Bind(GLIContext c,GLenum target,GLuint name)
{
    if(Trace())fprintf(stderr,"GLI bind ctx=%p target=%x name=%u caller=%p\n",c,(unsigned)target,(unsigned)name,__builtin_return_address(0));
    Find(c)->original.bind_texture(c,target,name);
}
static void Get(GLIContext c,GLenum key,GLint *value)
{
    Find(c)->original.get_integerv(c,key,value);
    if((key==GL_TEXTURE_BINDING_2D||key==GL_TEXTURE_BINDING_RECTANGLE_ARB||key==GL_ACTIVE_TEXTURE)&&Trace())
        fprintf(stderr,"GLI get ctx=%p key=%x value=%d caller=%p\n",c,(unsigned)key,(int)*value,__builtin_return_address(0));
}
static void Gen(GLIContext c,GLsizei n,GLuint *names)
{
    Find(c)->original.gen_textures(c,n,names);
    if(Trace())for(GLsizei i=0;i<n&&i<16;i++)fprintf(stderr,"GLI gen ctx=%p name=%u caller=%p\n",c,(unsigned)names[i],__builtin_return_address(0));
}
static void Delete(GLIContext c,GLsizei n,const GLuint *names)
{
    if(Trace())for(GLsizei i=0;i<n&&i<16;i++)fprintf(stderr,"GLI delete ctx=%p name=%u caller=%p\n",c,(unsigned)names[i],__builtin_return_address(0));
    Find(c)->original.delete_textures(c,n,names);
}
static void Install(CGLContextObj c)
{
    if(!c||!c->rend||Find(c->rend)||count==32)return;
    Dl_info info={0};if(!dladdr((void *)c->disp.bind_texture,&info)||!info.dli_fname||
        (!strstr(info.dli_fname,"GLDriver")&&!strstr(info.dli_fname,"OpenGL.framework")))return;
    struct ContextTrace *t=contexts+count++;t->renderer=c->rend;t->original=c->disp;
    fprintf(stderr,"GLI context cgl=%p renderer=%p image=%s\n",c,c->rend,info.dli_fname);
    c->disp.bind_texture=Bind;c->disp.get_integerv=Get;c->disp.gen_textures=Gen;c->disp.delete_textures=Delete;
}
void VIMTraceContext(CGLContextObj c){Install(c);}
void glBindTexture(GLenum target,GLuint name)
{
    static void (*next)(GLenum,GLuint);if(!next)next=dlsym(RTLD_NEXT,"glBindTexture");
    Install(CGLGetCurrentContext());
    if(Trace())fprintf(stderr,"TEX bind ctx=%p target=%x name=%u caller=%p\n",
        CGLGetCurrentContext(),(unsigned)target,(unsigned)name,__builtin_return_address(0));
    next(target,name);
}
void glGenTextures(GLsizei n,GLuint *names)
{
    static void (*next)(GLsizei,GLuint *);if(!next)next=dlsym(RTLD_NEXT,"glGenTextures");
    Install(CGLGetCurrentContext());
    next(n,names);if(Trace())for(GLsizei i=0;i<n&&i<16;i++)
        fprintf(stderr,"TEX gen ctx=%p name=%u caller=%p\n",CGLGetCurrentContext(),(unsigned)names[i],__builtin_return_address(0));
}
void glDeleteTextures(GLsizei n,const GLuint *names)
{
    static void (*next)(GLsizei,const GLuint *);if(!next)next=dlsym(RTLD_NEXT,"glDeleteTextures");
    if(Trace())for(GLsizei i=0;i<n&&i<16;i++)
        fprintf(stderr,"TEX delete ctx=%p name=%u caller=%p\n",CGLGetCurrentContext(),(unsigned)names[i],__builtin_return_address(0));
    next(n,names);
}
void glGetIntegerv(GLenum key,GLint *value)
{
    static void (*next)(GLenum,GLint *);if(!next)next=dlsym(RTLD_NEXT,"glGetIntegerv");
    next(key,value);
    if((key==GL_TEXTURE_BINDING_2D||key==GL_TEXTURE_BINDING_RECTANGLE_ARB||key==GL_ACTIVE_TEXTURE)&&Trace())
        fprintf(stderr,"TEX get ctx=%p key=%x value=%d caller=%p\n",CGLGetCurrentContext(),(unsigned)key,(int)*value,__builtin_return_address(0));
}
