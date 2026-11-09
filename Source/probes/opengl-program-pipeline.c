/* Exercise native separable GLSL, sized replies and typed direct uniforms. */
#ifdef VIM_PIPELINE_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glProgramParameteri glProgramParameteriEXT
#define glGetUniformLocation(p,n) glGetUniformLocationARB((GLhandleARB)(uintptr_t)(p),n)
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>

enum { vertex_bit=1,fragment_bit=2,program_separable=0x8258,active_program=0x8259,pipeline_binding=0x825a };
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static void link_check(GLuint p)
{
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char info[1024]={0};glGetProgramInfoLog(p,sizeof(info),NULL,info);fprintf(stderr,"%s\n",info);}
    CHECK(p && ok);
}
static void check_pixel(const char *label,GLubyte r,GLubyte g,GLubyte b)
{
    GLubyte p[4]={0};glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    printf("%s=%u,%u,%u,%u\n",label,p[0],p[1],p[2],p[3]);
    CHECK(glGetError()==GL_NO_ERROR && p[0]==r && p[1]==g && p[2]==b && p[3]==255);
}
static void verify(int fp64)
{
    const char *vertex[]={"#version 410\n","out gl_PerVertex { vec4 gl_Position; };\n",
        "void main(){gl_Position=vec4(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1,0,1);}"};
    const char *fragment[]={"#version 410\n","uniform vec4 tint;\n",
        fp64?"uniform dmat2x3 weight;uniform double gain;\n":"uniform mat2x3 weight;uniform float gain;\n",
        "uniform uint tag;uniform ivec2 gates[2];out vec4 color;\n",
        ("void main(){color=tag==0xf1234567u && gates[1].y==-100003 && gain==1.25"
         "?vec4(tint.r,float(weight[1].y),tint.b,1):vec4(0,0,0,1);}")};
    GLuint v=glCreateShaderProgramv(GL_VERTEX_SHADER,3,vertex);link_check(v);
    /* Exercise the existing GLI ProgramParameteri slot with the core pname,
       as well as multi-string construction for the vertex program. */
    GLuint shader=glCreateShader(GL_FRAGMENT_SHADER),f=glCreateProgram();
    glShaderSource(shader,5,fragment,NULL);glCompileShader(shader);glAttachShader(f,shader);
    glProgramParameteri(f,program_separable,GL_TRUE);glLinkProgram(f);link_check(f);
    glDeleteShader(shader);
    if(failures)return;

    GLuint pipes[2]={0,0x13579bdf},vao=0,fb=0,texture=0;
    glGenProgramPipelines(0,NULL);glDeleteProgramPipelines(0,NULL);
    glGenProgramPipelines(1,pipes);
    CHECK(glGetError()==GL_NO_ERROR && pipes[0] && pipes[1]==0x13579bdf);
    GLuint pipe=pipes[0];
    glBindProgramPipeline(pipe);CHECK(glIsProgramPipeline(pipe));
    glUseProgramStages(pipe,vertex_bit,v);glUseProgramStages(pipe,fragment_bit,f);
    glActiveShaderProgram(pipe,f);
    GLint state[2]={0,0x13579bdf};
    glGetProgramPipelineiv(pipe,active_program,state);
    CHECK(glGetError()==GL_NO_ERROR && state[0]==(GLint)f && state[1]==0x13579bdf);
    glGetProgramPipelineiv(pipe,GL_VERTEX_SHADER,state);CHECK(state[0]==(GLint)v);
    glGetProgramPipelineiv(pipe,GL_FRAGMENT_SHADER,state);CHECK(state[0]==(GLint)f);
    glGetIntegerv(pipeline_binding,state);CHECK(state[0]==(GLint)pipe);
    GLuint framebuffer_status;
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    framebuffer_status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    CHECK(framebuffer_status==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,4,4);
    glValidateProgramPipeline(pipe);
    glGetProgramPipelineiv(pipe,GL_VALIDATE_STATUS,state);
    if(!state[0]){char info[1024]={0};glGetProgramPipelineInfoLog(pipe,sizeof(info),NULL,info);fprintf(stderr,"Pipeline validation: %s\n",info);}
    CHECK(state[0]==GL_TRUE);
    GLsizei length=-1;char log[9]={'a','b','c','d','e','f','g','h','!'};
    glGetProgramPipelineInfoLog(pipe,8,&length,log);
    CHECK(glGetError()==GL_NO_ERROR && length>=0 && length<8 && log[length]=='\0' && log[8]=='!');
    glGetProgramPipelineInfoLog(pipe,0,&length,NULL);
    CHECK(glGetError()==GL_NO_ERROR && length==0);
    glGetProgramPipelineiv(pipe,~0u,state);
    CHECK(glGetError()==GL_INVALID_ENUM && state[0]==GL_TRUE && state[1]==0x13579bdf);

    GLint tint=glGetUniformLocation(f,"tint"),weight=glGetUniformLocation(f,"weight");
    GLint tag=glGetUniformLocation(f,"tag"),gain=glGetUniformLocation(f,"gain");
    GLint gates=glGetUniformLocation(f,"gates[0]");
    CHECK(tint>=0 && weight>=0 && tag>=0 && gain>=0 && gates>=0);
    const GLfloat matrix[]={0,0,0,0,1,0},blue[]={0,0,1,1};
    const GLdouble double_matrix[]={0,0,0,0,1,0};
    const GLint ints[]={-13,17,29,-100003};
    glProgramUniform4f(f,tint,1,0,0,1);
    if(fp64){
        glProgramUniformMatrix2x3dv(f,weight,1,GL_FALSE,double_matrix);
        glProgramUniform1d(f,gain,1.25);
    }else{
        glProgramUniformMatrix2x3fv(f,weight,1,GL_FALSE,matrix);
        glProgramUniform1f(f,gain,1.25);
    }
    glProgramUniform1ui(f,tag,0xf1234567u);
    glProgramUniform2iv(f,gates,2,ints);
    glProgramUniform4fv(f,-1,1,NULL); /* ignored location must not read pointer */
    glGetIntegerv(GL_CURRENT_PROGRAM,state);
    CHECK(glGetError()==GL_NO_ERROR && state[0]==0);
    glDrawArrays(GL_TRIANGLES,0,3);check_pixel("pipeline first pixel",255,255,0);
    glProgramUniform4fv(f,tint,1,blue);
    glDrawArrays(GL_TRIANGLES,0,3);check_pixel("pipeline updated pixel",0,255,255);

    glDeleteProgramPipelines(1,pipes);CHECK(!glIsProgramPipeline(pipe));
    glGetIntegerv(pipeline_binding,state);CHECK(glGetError()==GL_NO_ERROR && state[0]==0);
    glDeleteProgram(f);glDeleteProgram(v);
}
int main(int argc,char **argv)
{
#ifdef VIM_PIPELINE_WIRE
    const int attrs[]={0x56494d02,0x3200,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features&DG_FEATURE_PROGRAM_PIPELINE);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("pipeline renderer=%s\n",glGetString(GL_RENDERER));
    verify(argc>1 && argv[1][0]=='d');
#ifdef VIM_PIPELINE_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("program pipeline failures=%d\n",failures);return failures?1:0;
}
