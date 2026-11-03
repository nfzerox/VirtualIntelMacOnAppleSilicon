/* Same bounded getter/canary/relink cases on native Intel and the real typed
 * client/interpreter. No cached values, adjacent-location assumptions or
 * artificial renderer/state results. */
#ifdef VIM_UNIFORM_DIRECT
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);if(!good)++failures;}
static int tail(const void *values,unsigned count)
{const unsigned char *p=values;for(unsigned i=count*4;i<64;++i)if(p[i]!=0xa5)return 0;return 1;}
static GLint location(GLhandleARB p,const char *name)
{GLint n=glGetUniformLocationARB(p,name);check(n>=0,name);return n;}
static void getf(GLhandleARB p,const char *name,unsigned count,const GLfloat *expected)
{
    struct {uint64_t before;GLfloat values[16];uint64_t after;} out;
    memset(&out,0xa5,sizeof(out));GLint loc=location(p,name);glGetUniformfvARB(p,loc,out.values);
    GLenum error=glGetError();if(error)printf("native error=%x query=%s\n",error,name);
    check(!error&&out.before==UINT64_C(0xa5a5a5a5a5a5a5a5)&&out.after==UINT64_C(0xa5a5a5a5a5a5a5a5)&&
        !memcmp(out.values,expected,count*4)&&tail(out.values,count),name);
}
static void geti(GLhandleARB p,const char *name,unsigned count,const GLint *expected)
{
    struct {uint64_t before;GLint values[16];uint64_t after;} out;
    memset(&out,0xa5,sizeof(out));GLint loc=location(p,name);glGetUniformivARB(p,loc,out.values);
    GLenum error=glGetError();if(error)printf("native error=%x query=%s\n",error,name);
    check(!error&&out.before==UINT64_C(0xa5a5a5a5a5a5a5a5)&&out.after==UINT64_C(0xa5a5a5a5a5a5a5a5)&&
        !memcmp(out.values,expected,count*4)&&tail(out.values,count),name);
}
static void unchanged(GLhandleARB p,GLint loc,GLenum expected,const char *name)
{
    GLfloat out[16];memset(out,0xa5,sizeof(out));glGetUniformfvARB(p,loc,out);GLenum error=glGetError();
    printf("query error=%x expected=%x\n",error,expected);check(error==expected&&tail(out,0),name);
}
static void compile(GLhandleARB shader,const char *source,int expected)
{glShaderSourceARB(shader,1,&source,NULL);glCompileShaderARB(shader);GLint status=0;
 glGetObjectParameterivARB(shader,GL_OBJECT_COMPILE_STATUS_ARB,&status);check(!!status==expected,"shader compile status");}
static void link_program(GLhandleARB p,int expected)
{glLinkProgramARB(p);GLint status=0;glGetObjectParameterivARB(p,GL_OBJECT_LINK_STATUS_ARB,&status);
 check(!!status==expected,"program link status");}
int main(int argc,char **argv)
{
    if(argc!=2||strcmp(argv[1],"--require-intel"))return 2;
#ifdef VIM_UNIFORM_DIRECT
    unsigned char shadow[8*8*4]={0};struct DGContext *context=DGCreateContext(NULL,NULL);
    if(!context||!DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,8,8))return 2;
#else
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint formats=0;
    if(CGLChoosePixelFormat(attrs,&format,&formats)||!format||CGLCreateContext(format,NULL,&context)||!context)return 2;
    CGLSetCurrentContext(context);
#endif
    const char *renderer=(const char *)glGetString(GL_RENDERER);printf("renderer=%s\n",renderer?renderer:"none");
    if(!renderer||!strstr(renderer,"Intel"))return 2;
    GLhandleARB vertex=glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB),fragment=glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
    const char *source="#version 120\nuniform float scalar;uniform vec3 vectors[3];uniform mat4 matrix;"
        "uniform mat2x3 rectangular;uniform ivec4 integers;uniform bvec2 flags;"
        "struct Item{vec2 coord;float gain;};uniform Item records[2];"
        "void main(){float s=scalar+dot(vectors[0]+vectors[1]+vectors[2]+rectangular*vec2(1),vec3(1))"
        "+dot(matrix*vec4(1),vec4(1))+dot(vec4(integers),vec4(1))+(flags.x?1.0:0.0)+(flags.y?2.0:0.0)"
        "+dot(records[0].coord+records[1].coord,vec2(1))+records[0].gain+records[1].gain;gl_Position=gl_Vertex+vec4(s*.000001);}";
    compile(vertex,source,1);compile(fragment,"#version 120\nuniform sampler2D image;void main(){gl_FragColor=texture2D(image,vec2(.5));}",1);
    GLhandleARB program=glCreateProgramObjectARB();glAttachObjectARB(program,vertex);glAttachObjectARB(program,fragment);link_program(program,1);
    glUseProgramObjectARB(program);
    GLfloat scalar=.25f,vectors[]={.25f,.5f,.75f,1,2,3,4,5,6},matrix[16],rectangular[]={1,2,3,4,5,6};
    for(unsigned i=0;i<16;++i)matrix[i]=(GLfloat)i;
    GLint integers[]={1,-2,3,-4},flags[]={-3,0},true_flags[]={1,0},sampler=3;
    GLfloat coords[]={5,6,7,8},gain0=3,gain1=4;
    glUniform1fARB(location(program,"scalar"),scalar);glUniform3fvARB(location(program,"vectors[0]"),3,vectors);
    glUniformMatrix4fvARB(location(program,"matrix"),1,GL_FALSE,matrix);
    glUniformMatrix2x3fv(location(program,"rectangular"),1,GL_FALSE,rectangular);
    glUniform4ivARB(location(program,"integers"),1,integers);glUniform2ivARB(location(program,"flags"),1,flags);
    glUniform1iARB(location(program,"image"),sampler);
    glUniform2fvARB(location(program,"records[0].coord"),1,coords);
    glUniform2fvARB(location(program,"records[1].coord"),1,coords+2);
    glUniform1fARB(location(program,"records[0].gain"),gain0);glUniform1fARB(location(program,"records[1].gain"),gain1);
    check(!glGetError(),"uniform setup");
    getf(program,"scalar",1,&scalar);getf(program,"vectors[0]",3,vectors);
    getf(program,"vectors[1]",3,vectors+3);getf(program,"vectors[2]",3,vectors+6);
    getf(program,"matrix",16,matrix);getf(program,"rectangular",6,rectangular);
    geti(program,"integers",4,integers);geti(program,"flags",2,true_flags);geti(program,"image",1,&sampler);
    GLint converted[]={1,2,3};geti(program,"vectors[1]",3,converted);
    GLfloat converted_flags[]={1,0};getf(program,"flags",2,converted_flags);
    getf(program,"records[0].coord",2,coords);getf(program,"records[1].coord",2,coords+2);
    getf(program,"records[0].gain",1,&gain0);getf(program,"records[1].gain",1,&gain1);
    unchanged(program,-1,GL_INVALID_OPERATION,"invalid location preserves output");
    unchanged((GLhandleARB)0,0,GL_INVALID_VALUE,"invalid program preserves output");
    unchanged(vertex,0,GL_INVALID_OPERATION,"shader is not a program");
    /* A headless native context has no default framebuffer: without an actual
     * attachment Begin itself fails (506), so it cannot test this restriction. */
    GLuint texture=0,framebuffer=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT&&!glGetError(),"Begin test framebuffer complete");
    glBegin(GL_POINTS);GLfloat inside[16];memset(inside,0xa5,sizeof(inside));
    glGetUniformfvARB(program,0,inside);glEnd();
    GLenum inside_error=glGetError();printf("inside Begin/End error=%x untouched=%d first=%g\n",inside_error,tail(inside,0),inside[0]);
    check(inside_error==GL_INVALID_OPERATION&&tail(inside,0),"GetUniform inside Begin/End preserves output");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&framebuffer);glDeleteTextures(1,&texture);
    /* Warm a vec4 shape, relink the SAME program in another shared context
     * to scalar, then query from the first context. No stale shape/value. */
    GLhandleARB changing_vertex=glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB),changing_fragment=glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
    compile(changing_vertex,"#version 120\nuniform vec4 tint;void main(){gl_Position=gl_Vertex+tint;}",1);
    compile(changing_fragment,"#version 120\nvoid main(){gl_FragColor=vec4(1);}",1);
    GLhandleARB changing=glCreateProgramObjectARB();glAttachObjectARB(changing,changing_vertex);glAttachObjectARB(changing,changing_fragment);link_program(changing,1);
    glUseProgramObjectARB(changing);GLfloat tint4[]={1,2,3,4};glUniform4fvARB(location(changing,"tint"),1,tint4);getf(changing,"tint",4,tint4);
    glUseProgramObjectARB(0);
#ifdef VIM_UNIFORM_DIRECT
    struct DGContext *shared=DGCreateContext(NULL,context);check(shared!=NULL,"shared context create");
    if(!shared||!DGMakeCurrent(shared,shadow,GL_UNSIGNED_BYTE,8,8))return 2;
#else
    CGLContextObj shared=NULL;check(!CGLCreateContext(format,context,&shared)&&shared,"shared context create");
    if(!shared||CGLSetCurrentContext(shared))return 2;
#endif
    compile(changing_vertex,"#version 120\nuniform float tint;void main(){gl_Position=gl_Vertex+vec4(tint);}",1);link_program(changing,1);
    glUseProgramObjectARB(changing);GLfloat tint1=.5f;glUniform1fARB(location(changing,"tint"),tint1);glUseProgramObjectARB(0);
#ifdef VIM_UNIFORM_DIRECT
    DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,8,8);
#else
    CGLSetCurrentContext(context);
#endif
    getf(changing,"tint",1,&tint1);
    compile(changing_vertex,"not a shader",0);link_program(changing,0);
    unchanged(changing,0,GL_INVALID_OPERATION,"failed relink preserves output");
    glUseProgramObjectARB(0);glDeleteObjectARB(changing);glDeleteObjectARB(changing_vertex);glDeleteObjectARB(changing_fragment);
    glDeleteObjectARB(program);glDeleteObjectARB(vertex);glDeleteObjectARB(fragment);check(!glGetError(),"cleanup");
#ifdef VIM_UNIFORM_DIRECT
    DGDestroyContext(shared);DGDestroyContext(context);
#else
    CGLDestroyContext(shared);CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
#endif
    printf("uniform query failures=%d\n",failures);return failures?1:0;
}
