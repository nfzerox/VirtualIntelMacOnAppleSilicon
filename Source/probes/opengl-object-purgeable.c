/* Native and wire storage hints, retained data, and GL error semantics. */
#ifdef VIM_PURGEABLE_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int exercise(GLenum type,GLuint object)
{
    int failures=0;
    GLint state=-1;glGetObjectParameterivAPPLE(type,object,GL_PURGEABLE_APPLE,&state);
    GLenum error=glGetError();
    printf("purgeable type=%04x initial=%d error=%04x\n",type,state,error);
    if(error || state!=0)return 1;
    GLenum returned=glObjectPurgeableAPPLE(type,object,GL_VOLATILE_APPLE);error=glGetError();
    glGetObjectParameterivAPPLE(type,object,GL_PURGEABLE_APPLE,&state);
    GLenum query_error=glGetError();
    printf("purgeable volatile return=%04x state=%d error=%04x/%04x\n",returned,state,error,query_error);
    failures+=error || query_error || state!=1;
    GLenum duplicate=glObjectPurgeableAPPLE(type,object,GL_VOLATILE_APPLE);
    GLenum duplicate_error=glGetError();
    printf("purgeable duplicate return=%04x error=%04x\n",duplicate,duplicate_error);
    failures+=duplicate_error!=GL_INVALID_OPERATION &&
        !(type==GL_BUFFER_OBJECT_APPLE && duplicate_error==GL_NO_ERROR);
    returned=glObjectUnpurgeableAPPLE(type,object,GL_RETAINED_APPLE);error=glGetError();
    glGetObjectParameterivAPPLE(type,object,GL_PURGEABLE_APPLE,&state);query_error=glGetError();
    printf("purgeable retained return=%04x state=%d error=%04x/%04x\n",returned,state,error,query_error);
    failures+=error || query_error || state!=0 ||
        (returned!=GL_RETAINED_APPLE && returned!=GL_UNDEFINED_APPLE && returned!=GL_RELEASED_APPLE);
    if(type==GL_BUFFER_OBJECT_APPLE && returned==GL_RETAINED_APPLE){
        const GLuint expected[]={1,2,3,4};GLuint observed[4]={0};
        glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(observed),observed);
        failures+=glGetError()!=GL_NO_ERROR || memcmp(expected,observed,sizeof(observed))!=0;
    }
    state=-1;glGetObjectParameterivAPPLE(type,0,GL_PURGEABLE_APPLE,&state);
    error=glGetError();
    printf("purgeable invalid name state=%d error=%04x\n",state,error);
    failures+=error!=GL_INVALID_VALUE || state!=-1;
    return failures;
}
int main(int argc,char **argv)
{
    int profile=0x1000,expect_unavailable=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"gl3"))profile=0x3200;
        else if(!strcmp(argv[i],"core"))profile=0x4100;
        else if(!strcmp(argv[i],"--expect-unavailable"))expect_unavailable=1;
        else return 2;
    }
    int core=profile!=0x1000;
#ifdef VIM_PURGEABLE_WIRE
    const int attrs[]={0x56494d02,profile,0};unsigned char screen[16*16*4]={0};
    struct DGContext *ctx=DGCreateContext(attrs,NULL);
    if(!ctx || !DGMakeCurrent(ctx,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)profile,kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    GLuint buffer=0,texture=0,rb=0;
    GLuint data[]={1,2,3,4};
    glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(data),data,GL_STATIC_DRAW);
    if(glGetError())return 2;
    /* First discovery may need an isolated native probe. It must not
     * consume the application's pending error or change its binding. */
    glEnable(0xffffffff);
    const char *ext=core?NULL:(const char *)glGetString(GL_EXTENSIONS);
    int advertised=ext && strstr(ext,"GL_APPLE_object_purgeable");
    if(core){
#ifdef VIM_PURGEABLE_WIRE
        const GLubyte *(*indexed)(GLenum,GLuint)=glGetStringi;
#else
        const GLubyte *(*indexed)(GLenum,GLuint)=dlsym(RTLD_DEFAULT,"glGetStringi");
#endif
        if(!indexed)return 77;
        GLint count=0;glGetIntegerv(0x821d,&count);
        for(GLint i=0;i<count;++i){
            const char *name=(const char *)indexed(GL_EXTENSIONS,(GLuint)i);
            if(name && !strcmp(name,"GL_APPLE_object_purgeable"))advertised=1;
        }
    }
    printf("purgeable renderer=%s core=%d advertised=%d\n",glGetString(GL_RENDERER),core,advertised);
    GLint binding=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);
    if(glGetError()!=GL_INVALID_ENUM || binding!=(GLint)buffer)return 3;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,4,4);
    if(glGetError())return 2;
#ifdef VIM_PURGEABLE_WIRE
    if(!advertised)expect_unavailable=1;
#endif
    if(expect_unavailable){
        GLint state=-1;
        if(advertised)return 4;
        glGetObjectParameterivAPPLE(GL_BUFFER_OBJECT_APPLE,buffer,GL_PURGEABLE_APPLE,&state);
        if(glGetError()!=GL_INVALID_OPERATION || state!=-1)return 4;
        if(glObjectPurgeableAPPLE(GL_BUFFER_OBJECT_APPLE,buffer,GL_VOLATILE_APPLE) ||
           glGetError()!=GL_INVALID_OPERATION)return 4;
        if(glObjectUnpurgeableAPPLE(GL_BUFFER_OBJECT_APPLE,buffer,GL_RETAINED_APPLE) ||
           glGetError()!=GL_INVALID_OPERATION)return 4;
        puts("purgeability safely unavailable on this renderer");
        return 0;
    }
    int failures=exercise(GL_BUFFER_OBJECT_APPLE,buffer);
    failures+=exercise(GL_TEXTURE,texture);
    failures+=exercise(GL_RENDERBUFFER_EXT,rb);
    return failures!=0;
}
