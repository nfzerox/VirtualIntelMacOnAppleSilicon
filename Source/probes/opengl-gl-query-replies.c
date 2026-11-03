/* Native Intel/public CGL and typed-executor output-preservation oracle.
 * Caller storage and tails are seeded so native errors/partial writes cannot
 * be mistaken for a zero-valued successful query. No private GL structures. */
#ifdef VIM_QUERY_REPLIES_DIRECT
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLRenderers.h>
#include "opengl-reference-renderer.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static int failures;
struct Output {uint64_t before;union {GLint integers[32];GLfloat floats[32];GLchar chars[128];} data;uint64_t after;};
static void seed(struct Output *out){memset(out,0xa5,sizeof(*out));}
static int intact(const void *bytes,size_t count)
{const unsigned char *p=bytes;for(size_t i=0;i<count;++i)if(p[i]!=0xa5)return 0;return 1;}
static int guards(const struct Output *out)
{return out->before==UINT64_C(0xa5a5a5a5a5a5a5a5)&&out->after==UINT64_C(0xa5a5a5a5a5a5a5a5);}
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);if(!good)++failures;}
static void unmodified(struct Output *out,GLenum expected,const char *name)
{GLenum error=glGetError();printf("query=%s error=%x expected=%x\n",name,error,expected);
 check(error==expected&&intact(out,sizeof(*out)),name);}
static GLuint shader(GLenum kind,const char *source)
{
    GLuint name=glCreateShader(kind);GLint compiled=0;
    glShaderSource(name,1,&source,NULL);glCompileShader(name);glGetShaderiv(name,GL_COMPILE_STATUS,&compiled);
    check(compiled&&!glGetError(),"compile actual shader");return name;
}
int main(int argc,char **argv)
{
    if(argc<2||argc>3||strcmp(argv[1],"--require-intel")||
       (argc==3&&strcmp(argv[2],"--intel-hd5000")&&strcmp(argv[2],"--old-peer")))return 2;
#ifdef VIM_QUERY_REPLIES_DIRECT
    if(argc==3&&strcmp(argv[2],"--old-peer"))return 2;
    unsigned char shadow[16*16*4]={0};struct DGContext *context=DGCreateContext(NULL,NULL);
    if(!context||!DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    if(argc==3&&!strcmp(argv[2],"--old-peer"))return 2;
    GLint intel=argc==3?VIMReferenceIntelHD5000():0;if(argc==3&&!intel)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAAllowOfflineRenderers,
        kCGLPFASupportsAutomaticGraphicsSwitching,argc==3?kCGLPFARendererID:0,(CGLPixelFormatAttribute)intel,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format||CGLCreateContext(format,NULL,&context)||!context)return 2;
    CGLSetCurrentContext(context);
#endif
    const char *renderer=(const char *)glGetString(GL_RENDERER);printf("renderer=%s\n",renderer?renderer:"none");
    if(!renderer||!strstr(renderer,"Intel"))return 2;
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"real framebuffer");
    glViewport(0,0,16,16);struct Output out;seed(&out);
    glGetTexParameteriv(0xdead,GL_TEXTURE_MIN_FILTER,out.data.integers);
    unmodified(&out,GL_INVALID_ENUM,"invalid texture target preserves scalar output");
    seed(&out);glGetMaterialfv(0xdead,GL_SHININESS,out.data.floats);
    unmodified(&out,GL_INVALID_ENUM,"invalid material face preserves float output");
    seed(&out);glBegin(GL_TRIANGLES);glGetIntegerv(GL_VIEWPORT,out.data.integers);glEnd();
    unmodified(&out,GL_INVALID_OPERATION,"Begin/End query preserves vector output");
    seed(&out);glEnable(0xdead);glGetIntegerv(GL_VIEWPORT,out.data.integers);
    const GLint viewport[]={0,0,16,16};GLenum error=glGetError();
    check(error==GL_INVALID_ENUM&&guards(&out)&&!memcmp(out.data.integers,viewport,sizeof(viewport))&&
        intact(out.data.integers+4,sizeof(out.data)-sizeof(viewport)),"prior error does not suppress valid query output");
#ifdef VIM_QUERY_REPLIES_DIRECT
    if(argc==3){
        check(!(context->connection->capabilities&DG_CAP_QUERY_REPLIES),"actual older executor lacks query-reply capability");
        seed(&out);glGenTextures(-1,(GLuint *)out.data.integers);
        unmodified(&out,GL_INVALID_OPERATION,"old peer rejects negative generator locally without writes");
        seed(&out);glGetShaderInfoLog(0,0,out.data.integers,NULL);
        unmodified(&out,GL_INVALID_OPERATION,"old peer rejects unsupported zero capacity without breaking transport");
        unsigned char pixel[4]={0};glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        check(!glGetError()&&pixel[0]==255&&!pixel[1]&&!pixel[2]&&pixel[3]==255&&
            !context->connection->dead,"old peer retains normal rendering and live transport");
        glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);DGDestroyContext(context);
        printf("old-peer query-reply failures=%d\n",failures);return failures?1:0;
    }
#endif
    const char *vs="#version 120\nvoid main(){gl_Position=gl_Vertex;}";
    GLuint vertex=shader(GL_VERTEX_SHADER,vs),fragment=shader(GL_FRAGMENT_SHADER,
        "#version 120\nuniform vec4 tint;void main(){gl_FragColor=tint;}");
    GLuint program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);check(linked&&!glGetError(),"link actual program");
    GLint object=0,subtype=0;GLfloat status=0;
    glGetObjectParameterivARB((GLhandleARB)(uintptr_t)vertex,GL_OBJECT_TYPE_ARB,&object);
    glGetObjectParameterivARB((GLhandleARB)(uintptr_t)vertex,GL_OBJECT_SUBTYPE_ARB,&subtype);
    glGetObjectParameterfvARB((GLhandleARB)(uintptr_t)vertex,GL_OBJECT_COMPILE_STATUS_ARB,&status);
    check(!glGetError()&&object==GL_SHADER_OBJECT_ARB&&subtype==GL_VERTEX_SHADER&&status==1,
        "native ARB shader queries accept core-created object names");
    object=0;status=0;
    glGetObjectParameterivARB((GLhandleARB)(uintptr_t)program,GL_OBJECT_TYPE_ARB,&object);
    glGetObjectParameterfvARB((GLhandleARB)(uintptr_t)program,GL_OBJECT_LINK_STATUS_ARB,&status);
    check(!glGetError()&&object==GL_PROGRAM_OBJECT_ARB&&status==1,
        "native ARB program queries accept core-created object names");
    GLsizei length=0x13572468;
    seed(&out);glGetShaderInfoLog(vertex,64,&length,out.data.chars);error=glGetError();
    check(!error&&length>=0&&length<64&&out.data.chars[length]==0&&guards(&out)&&
        intact(out.data.chars+length+1,sizeof(out.data)-length-1),"short shader log preserves unused capacity");
    seed(&out);length=0x13572468;
    glGetInfoLogARB((GLhandleARB)(uintptr_t)program,64,&length,out.data.chars);error=glGetError();
    check(!error&&length>=0&&length<64&&out.data.chars[length]==0&&guards(&out)&&
        intact(out.data.chars+length+1,sizeof(out.data)-length-1),"short ARB program log preserves unused capacity");
    seed(&out);length=0x13572468;
    glGetShaderSourceARB((GLhandleARB)(uintptr_t)vertex,96,&length,out.data.chars);error=glGetError();
    printf("source length=%d expected=%lu error=%x guards=%d tail=%d text='%.*s'\n",length,
        (unsigned long)strlen(vs),error,guards(&out),length>=0&&length<128?
        intact(out.data.chars+length+1,sizeof(out.data)-length-1):0,96,out.data.chars);
    // Both measured Intel implementations append a newline to this one
    // source string. Test their actual returned text, not an assumed copy.
    check(!error&&length==(GLsizei)strlen(vs)+1&&!memcmp(out.data.chars,vs,strlen(vs))&&
        out.data.chars[strlen(vs)]=='\n'&&!out.data.chars[length]&&guards(&out)&&
        intact(out.data.chars+length+1,sizeof(out.data)-length-1),"shader source preserves unused capacity");
    seed(&out);glGetObjectParameterfvARB((GLhandleARB)(uintptr_t)vertex,0xdead,out.data.floats);
    unmodified(&out,GL_INVALID_ENUM,"invalid ARB object pname preserves float output");
    seed(&out);glGetObjectParameterfvARB((GLhandleARB)(uintptr_t)0,GL_OBJECT_COMPILE_STATUS_ARB,out.data.floats);
    unmodified(&out,GL_INVALID_OPERATION,"invalid ARB object preserves float output");
    seed(&out);glBegin(GL_TRIANGLES);
    glGetObjectParameterfvARB((GLhandleARB)(uintptr_t)vertex,GL_OBJECT_COMPILE_STATUS_ARB,out.data.floats);glEnd();
    unmodified(&out,GL_INVALID_OPERATION,"ARB object query inside Begin preserves error priority and output");
    seed(&out);length=0x13572468;GLint size=0x13572468;GLenum type=0x13572468;
    glGetActiveUniformARB((GLhandleARB)(uintptr_t)program,0,64,&length,&size,&type,out.data.chars);error=glGetError();
    check(!error&&length==4&&size==1&&type==GL_FLOAT_VEC4&&!strcmp(out.data.chars,"tint")&&guards(&out)&&
        intact(out.data.chars+5,sizeof(out.data)-5),"active-uniform name preserves unused capacity");
    seed(&out);length=0x13572468;size=0x13572468;type=0x13572468;
    glGetActiveUniformARB((GLhandleARB)(uintptr_t)program,100,64,&length,&size,&type,out.data.chars);
    error=glGetError();printf("invalid uniform index error=%x length=%x size=%x type=%x\n",error,length,size,type);
    check(error==GL_INVALID_VALUE&&intact(&out,sizeof(out))&&length==0x13572468&&size==0x13572468&&type==0x13572468,
        "invalid uniform index preserves every output");
    long page=sysconf(_SC_PAGESIZE);if(page<4096)return 2;
    void *guard=mmap(NULL,(size_t)page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);if(guard==MAP_FAILED)return 2;
    length=0x13572468;glGetShaderInfoLog(vertex,0,&length,guard);error=glGetError();
    check(!error&&length==0,"zero-capacity shader log ignores protected name storage");
    length=0x13572468;glGetInfoLogARB((GLhandleARB)(uintptr_t)program,0,&length,NULL);error=glGetError();
    check(!error&&length==0,"zero-capacity ARB log permits null name storage");
    length=0x13572468;size=0x13572468;type=0x13572468;
    glGetActiveUniformARB((GLhandleARB)(uintptr_t)program,0,0,&length,&size,&type,NULL);error=glGetError();
    printf("zero uniform error=%x length=%d size=%d type=%x\n",error,length,size,type);
    check(!error&&length==0&&size==1&&type==GL_FLOAT_VEC4,"zero-capacity uniform query still returns its scalar metadata");
    length=0x13572468;glGetShaderInfoLog(vertex,-1,&length,guard);error=glGetError();
    check(error==GL_INVALID_VALUE&&length==0x13572468,"negative capacity rejects without touching outputs");
    GLuint names[2]={0xa5a5a5a5,0xa5a5a5a5};glGenTextures(0,names);error=glGetError();
    check(!error&&names[0]==0xa5a5a5a5&&names[1]==0xa5a5a5a5,"zero texture-name count leaves storage intact");
    glGenTextures(-1,names);error=glGetError();printf("negative generator error=%x\n",error);
    check(error==GL_INVALID_VALUE&&names[0]==0xa5a5a5a5&&names[1]==0xa5a5a5a5,"negative texture-name count rejects without writes");
    check(!glGetError(),"error consumed once");munmap(guard,(size_t)page);
    glDeleteProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);check(!glGetError(),"normal rendering teardown remains valid");
#ifdef VIM_QUERY_REPLIES_DIRECT
    check(!context->connection->dead,"query errors preserve live transport");DGDestroyContext(context);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
#endif
    printf("query-reply failures=%d\n",failures);return failures?1:0;
}
