/* Run the same bounded integer texture and shader cases natively and through
 * the guest client. Pass a physical Intel renderer ID to require that GPU. */
#ifdef VIM_INTEGER_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
#define GUARD 0xa5a5a5a5u

static void texture(void)
{
    GLuint tex=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    static const GLuint uborder[]={0,1,0xf0000013u,0xffffffffu};
    static const GLint iborder[]={-23,123,-1,0x43000000};
    GLuint uout[]={GUARD,GUARD,GUARD,GUARD,GUARD};
    GLint iout[]={0,0,0,0,99};
    glTexParameterIuivEXT(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,(GLuint *)uborder);
    glGetTexParameterIuivEXT(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,uout);
    CHECK(!glGetError() && !memcmp(uout,uborder,sizeof(uborder)) && uout[4]==GUARD);
    glTexParameterIivEXT(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,(GLint *)iborder);
    glGetTexParameterIivEXT(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,iout);
    CHECK(!glGetError() && !memcmp(iout,iborder,sizeof(iborder)) && iout[4]==99);
    static const GLint filter=GL_NEAREST;
    glTexParameterIivEXT(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,(GLint *)&filter);
    glGetTexParameterIivEXT(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,iout);
    CHECK(!glGetError() && iout[0]==GL_NEAREST && iout[1]==iborder[1]);
    iout[0]=99;
    glGetTexParameterIivEXT(GL_TEXTURE_2D,0xffffffffu,iout);
    CHECK(glGetError()==GL_INVALID_ENUM && iout[0]==99);
    glDeleteTextures(1,&tex);
}

static GLuint shader(void)
{
    GLuint s=glCreateShader(GL_VERTEX_SHADER);
    const char *source="#version 150\n"
        "uniform uint one;uniform uvec2 two[2];uniform uvec3 three;uniform uvec4 four;"
        "void main(){uint v=one+two[0].x+two[1].y+three.z+four.w;"
        "gl_Position=vec4(float(v));}";
    glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){
        char log[2048]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);
        fprintf(stderr,"compile: %s\n",log);return 0;
    }
    GLuint f=glCreateShader(GL_FRAGMENT_SHADER);
    const char *fragment="#version 150\nout vec4 color;void main(){color=vec4(1);}";
    glShaderSource(f,1,&fragment,NULL);glCompileShader(f);
    GLuint p=glCreateProgram();glAttachShader(p,s);glAttachShader(p,f);glLinkProgram(p);
    glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){
        char log[2048]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);
        fprintf(stderr,"link: %s\n",log);return 0;
    }
    glDeleteShader(s);glDeleteShader(f);
    return p;
}

static GLint location(GLuint program,const char *name)
{return glGetUniformLocationARB((GLhandleARB)(uintptr_t)program,name);}

static void uniform(GLuint program,const char *name,const GLuint *value,unsigned width)
{
    GLint index=location(program,name);
    GLuint out[]={GUARD,GUARD,GUARD,GUARD,GUARD};
    CHECK(index>=0);
    glGetUniformuivEXT(program,index,out);
    CHECK(!glGetError() && !memcmp(value,out,width*sizeof(*out)) && out[width]==GUARD);
}

static void shaders(void)
{
    GLuint p=shader();CHECK(p!=0);if(!p)return;
    glUseProgram(p);
    GLint a=location(p,"one"),b=location(p,"two[0]"),
          c=location(p,"three"),d=location(p,"four");
    const GLuint value[]={0xeffff001u,0xffffffffu,0x80000000u,0x10000001u};
    glUniform1uivEXT(a,1,value);
    glUniform2uivEXT(b,2,value);
    glUniform3uivEXT(c,1,value);
    glUniform4uivEXT(d,1,value);
    CHECK(!glGetError());
    uniform(p,"one",value,1);uniform(p,"two[0]",value,2);
    uniform(p,"two[1]",value+2,2);uniform(p,"three",value,3);uniform(p,"four",value,4);
    glUniform4uivEXT(-1,1,NULL);
    glUniform4uivEXT(d,0,NULL);
    CHECK(!glGetError());
    glUniform4uivEXT(d,-1,NULL);
    CHECK(glGetError()==GL_INVALID_VALUE);
    GLuint out=GUARD;glGetUniformuivEXT(p,-1,&out);
    CHECK(glGetError()==GL_INVALID_OPERATION && out==GUARD);
    glUseProgram(0);glDeleteProgram(p);
}

int main(int argc,char **argv)
{
    for(int core=0;core<2;++core){
#ifdef VIM_INTEGER_WIRE
    (void)argc;(void)argv;
    unsigned char pixels[16*16*4]={0};
    const int attrs[]={0x56494d02,core?0x3200:0x1000,0};
    struct DGContext *context=DGCreateContext(attrs,NULL);
    if(!context || !DGMakeCurrent(context,pixels,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(context->connection->features&DG_FEATURE_INTEGER_PARAMETERS);
#else
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),0,0,0};
    if(argc==2){
        attrs[5]=kCGLPFARendererID;
        attrs[6]=(CGLPixelFormatAttribute)strtoul(argv[1],NULL,16);
    }
    CGLPixelFormatObj format=NULL;GLint n=0;CGLContextObj context=NULL;
    if(CGLChoosePixelFormat(attrs,&format,&n)||!format||CGLCreateContext(format,NULL,&context)||
        CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    const char *vendor=(const char *)glGetString(GL_VENDOR);
    if(argc==2 && (!vendor || !strstr(vendor,"Intel")))return 77;
#endif
    printf("integer parameters renderer=%s core=%d\n",glGetString(GL_RENDERER),core);
    if(core)shaders();else texture();
#ifdef VIM_INTEGER_WIRE
    context->connection->features&=~DG_FEATURE_INTEGER_PARAMETERS;
    GLuint out=GUARD;glGetUniformuivEXT(0,0,&out);
    CHECK(glGetError()==GL_INVALID_OPERATION && out==GUARD && !context->connection->dead);
    DGDestroyContext(context);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
#endif
    }
    printf("integer parameter failures=%d\n",failures);
    return failures?1:0;
}
