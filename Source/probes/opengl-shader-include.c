/* Compare native and bridged ARB shader compilation and named strings. */
#ifdef VIM_SHADER_INCLUDE_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
int main(void)
{
#ifdef VIM_SHADER_INCLUDE_WIRE
    const int attrs[]={0x56494d02,0x4100,0};unsigned char screen[16*16*4]={0};
    struct DGContext *ctx=DGCreateContext(attrs,NULL);
    if(!ctx || !DGMakeCurrent(ctx,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    const GLubyte *(*indexed)(GLenum,GLuint)=glGetStringi;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)0x4100,
        kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    const GLubyte *(*indexed)(GLenum,GLuint)=dlsym(RTLD_DEFAULT,"glGetStringi");
#endif
    if(!indexed)return 77;
    GLint count=0,advertised=0;glGetIntegerv(0x821d,&count);
    for(GLint i=0;i<count;++i){
        const char *ext=(const char *)indexed(GL_EXTENSIONS,(GLuint)i);
        if(ext && !strcmp(ext,"GL_ARB_shading_language_include"))advertised=1;
    }
    printf("include renderer=%s advertised=%d\n",glGetString(GL_RENDERER),advertised);
    if(!advertised)return 77;
#ifdef VIM_SHADER_INCLUDE_WIRE
#define LOAD(name) name
#else
#define LOAD(name) dlsym(RTLD_DEFAULT,#name)
#endif
    void (*put)(GLenum,GLint,const GLchar *,GLint,const GLchar *)=LOAD(glNamedStringARB);
    void (*remove)(GLint,const GLchar *)=LOAD(glDeleteNamedStringARB);
    GLboolean (*exists)(GLint,const GLchar *)=LOAD(glIsNamedStringARB);
    void (*get)(GLint,const GLchar *,GLsizei,GLint *,GLchar *)=LOAD(glGetNamedStringARB);
    void (*property)(GLint,const GLchar *,GLenum,GLint *)=LOAD(glGetNamedStringivARB);
    void (*compile)(GLuint,GLsizei,const GLchar *const *,const GLint *)=LOAD(glCompileShaderIncludeARB);
#undef LOAD
    if(!put || !remove || !exists || !get || !property || !compile)return 2;
    const char name[]="/vim/colour.glsl",data[]="vec4 colour(){return vec4(1.0);}\n";
    put(0x8dae,sizeof(name)-1,name,sizeof(data)-1,data); /* GL_SHADER_INCLUDE_ARB */
    CHECK(glGetError()==GL_NO_ERROR && exists(-1,name) && !exists(-1,"/absent") &&
        !exists(-1,NULL) && glGetError()==GL_NO_ERROR);
#ifdef VIM_SHADER_INCLUDE_WIRE
    unsigned char second_screen[16*16*4]={0},third_screen[16*16*4]={0};
    struct DGContext *shared=DGCreateContext(attrs,ctx),*isolated=DGCreateContext(attrs,NULL);
    if(!shared || !isolated)return 2;
    CHECK(DGMakeCurrent(shared,second_screen,GL_UNSIGNED_BYTE,16,16) && exists(-1,name));
    CHECK(DGMakeCurrent(isolated,third_screen,GL_UNSIGNED_BYTE,16,16) && !exists(-1,name));
    CHECK(DGMakeCurrent(ctx,screen,GL_UNSIGNED_BYTE,16,16));
    DGDestroyContext(shared);DGDestroyContext(isolated);
#else
    CGLContextObj shared=NULL,isolated=NULL;
    if(CGLCreateContext(pf,ctx,&shared) || CGLCreateContext(pf,NULL,&isolated))return 2;
    CGLDestroyPixelFormat(pf);
    CHECK(!CGLSetCurrentContext(shared) && exists(-1,name));
    CHECK(!CGLSetCurrentContext(isolated) && !exists(-1,name));
    CHECK(!CGLSetCurrentContext(ctx));
    CGLDestroyContext(shared);CGLDestroyContext(isolated);
#endif
    GLint kind=-1,size=-1,length=-1;
    property(-1,name,0x8dea,&kind); /* GL_NAMED_STRING_TYPE_ARB */
    property(-1,name,0x8de9,&size); /* GL_NAMED_STRING_LENGTH_ARB */
    printf("include kind=%x size=%d\n",kind,size);
    CHECK(glGetError()==GL_NO_ERROR && kind==0x8dae && size==sizeof(data));
    char output[sizeof(data)+3];memset(output,'!',sizeof(output));
    get(-1,name,sizeof(output),&length,output);
    GLenum error=glGetError();
    printf("include full length=%d error=%04x tail=%02x,%02x,%02x\n",length,error,
        (unsigned char)output[sizeof(data)-1],(unsigned char)output[sizeof(data)],(unsigned char)output[sizeof(data)+1]);
    CHECK(error==GL_NO_ERROR && length==sizeof(data) && !memcmp(output,data,sizeof(data))
        && output[sizeof(data)]==0 && output[sizeof(data)+1]=='!');
    for(GLsizei capacity=sizeof(data)-1;capacity<=(GLsizei)sizeof(data)+1;++capacity){
        memset(output,'!',sizeof(output));length=-1;
        get(-1,name,capacity,&length,output);
        error=glGetError();
        printf("include boundary capacity=%d length=%d error=%04x tail=%02x,%02x,%02x\n",
            capacity,length,error,(unsigned char)output[capacity-1],
            (unsigned char)output[capacity],(unsigned char)output[capacity+1]);
        CHECK(error==GL_NO_ERROR && length==capacity-1 && output[length]==0 &&
            output[capacity]=='!' && output[capacity+1]=='!');
    }
    memset(output,'!',sizeof(output));length=-1;
    get(-1,name,4,&length,output);
    printf("include truncated length=%d bytes=%02x,%02x,%02x,%02x,%02x\n",length,
        (unsigned char)output[0],(unsigned char)output[1],(unsigned char)output[2],(unsigned char)output[3],(unsigned char)output[4]);
    CHECK(glGetError()==GL_NO_ERROR && length==3 && !memcmp(output,"vec\0!",5));
    length=-1;memset(output,'!',sizeof(output));
    get(-1,name,0,&length,output+1);
    error=glGetError();
    printf("include zero capacity length=%d error=%04x bytes=%02x,%02x\n",
        length,error,(unsigned char)output[0],(unsigned char)output[1]);
    CHECK(error==GL_NO_ERROR && length==-1 && output[0]=='!' && output[1]=='!');
    const GLchar *src="#version 410\n#extension GL_ARB_shading_language_include : require\n"
        "#include <colour.glsl>\nout vec4 color;void main(){color=colour();}\n";
    GLuint shader=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shader,1,&src,NULL);
    const GLchar exact_path[]={'/','v','i','m'};
    const GLchar *paths[]={"/missing",exact_path};const GLint lengths[]={-1,sizeof(exact_path)};
    compile(shader,2,paths,lengths);
    error=glGetError();
    GLint compiled=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
    char log[1024]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);
    printf("include compile=%d error=%04x log=%s\n",compiled,error,log);
    CHECK(glGetError()==GL_NO_ERROR && error==GL_NO_ERROR && compiled==GL_TRUE);
    remove(-1,name);
    CHECK(glGetError()==GL_NO_ERROR && !exists(-1,name));
    memset(output,'!',sizeof(output));length=-1;
    get(-1,name,sizeof(output),&length,output);
    CHECK(glGetError()==GL_INVALID_OPERATION && length==-1 && output[0]=='!');
    glDeleteShader(shader);
    shader=glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar *absent="#version 410\n#extension GL_ARB_shading_language_include : require\n"
        "#include <never-present.glsl>\nout vec4 color;void main(){color=colour();}\n";
    glShaderSource(shader,1,&absent,NULL);
    compile(shader,2,paths,lengths);
    glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
    error=glGetError();
    printf("include missing compile=%d error=%04x\n",compiled,error);
    CHECK(error==GL_NO_ERROR && compiled==GL_FALSE);
    glDeleteShader(shader);
    return failures!=0;
}
