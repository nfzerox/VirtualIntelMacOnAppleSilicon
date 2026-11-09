/* Finite public-GL controls for our executor's compile adapter. No injection. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include "../PassthroughOpenGL/passthrough-opengl-shader-compile.h"
static int failures;
static int use_executor;
#define CHECK(test) do {if(!(test)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#test);++failures;}}while(0)
static GLenum compile(GLuint shader)
{if(use_executor){glCompileShader(shader);return GL_NO_ERROR;}return DGCompileAdaptedShader(shader,0);}
static GLuint create(GLenum type,GLsizei count,const char *const *strings,GLenum *error)
{if(use_executor){*error=GL_NO_ERROR;return glCreateShaderProgramv(type,count,strings);}
 return DGCreateAdaptedShaderProgram(glCreateShaderProgramv,type,count,strings,error);}
static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);
    CHECK(compile(s)==GL_NO_ERROR);GLint ok=0;
    glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    CHECK(ok);return s;
}
int main(int argc,char **argv)
{
    use_executor=argc==2&&!strcmp(argv[1],"--executor");
    const CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL,shared=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    printf("compile-api renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    const char *vertex="#version 400 core\nuniform double value;void main(){"
        "bool ok=roundEven(value)==-512.0&&roundEven(float(value))==-512.0;"
        "gl_Position=vec4(ok?0.0:4.0,0,0,1);}";
    const char *fragment="#version 400 core\nout vec4 color;void main(){color=vec4(1,0,0,1);}";
    GLuint fs=shader(GL_FRAGMENT_SHADER,fragment),vs=glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs,1,&vertex,NULL);char before[4096]={0},after[4096]={0};
    GLint before_length=0,after_length=0,ok=0;GLsizei n=0,m=0;
    glGetShaderSource(vs,sizeof(before),&n,before);glGetShaderiv(vs,GL_SHADER_SOURCE_LENGTH,&before_length);
    CHECK(compile(vs)==GL_NO_ERROR);
    glGetShaderSource(vs,sizeof(after),&m,after);glGetShaderiv(vs,GL_SHADER_SOURCE_LENGTH,&after_length);
    glGetShaderiv(vs,GL_COMPILE_STATUS,&ok);
    CHECK(ok&&n==m&&before_length==after_length&&!strcmp(before,after));
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);
    glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok);
    GLuint texture=0,fbo=0,vao=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,1,1);
    glUseProgram(program);glUniform1d(glGetUniformLocation(program,"value"),-511.5);
    glDrawArrays(GL_POINTS,0,1);unsigned char pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    CHECK(pixel[0]==255&&pixel[1]==0&&pixel[2]==0&&pixel[3]==255);
    /* Compile errors remain native errors/logs, rather than helper-source errors. */
    const char *bad="#version 400 core\nvoid main(){double x=roundEven(1.5);syntax_error;}";
    GLuint a=glCreateShader(GL_VERTEX_SHADER),b=glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(a,1,&bad,NULL);glShaderSource(b,1,&bad,NULL);glCompileShader(a);
    CHECK(compile(b)==GL_NO_ERROR);
    char log_a[4096]={0},log_b[4096]={0};glGetShaderInfoLog(a,sizeof(log_a),NULL,log_a);
    glGetShaderInfoLog(b,sizeof(log_b),NULL,log_b);CHECK(!strcmp(log_a,log_b));
    glGetShaderiv(b,GL_COMPILE_STATUS,&ok);CHECK(!ok);
    glUseProgram(0);
    /* The single-call separable API receives the same adaptation, including
     * tokens split across source strings. It has no shader source object. */
    const char *parts[]={"#version 410 core\nout gl_PerVertex {vec4 gl_Position;};uniform double value;void main(){gl_Position=vec4(rou",
        "ndEven(value)==-512.0?0.0:4.0,0,0,1);}"};GLenum error=GL_NO_ERROR;
    GLuint vp=create(GL_VERTEX_SHADER,2,parts,&error);
    CHECK(vp&&error==GL_NO_ERROR);glGetProgramiv(vp,GL_LINK_STATUS,&ok);CHECK(ok);
    if(!ok){char log[4096];glGetProgramInfoLog(vp,sizeof(log),NULL,log);fprintf(stderr,"separable: %s\n",log);}
    const char *separable_fragment="#version 410 core\nout vec4 color;void main(){color=vec4(1,0,0,1);}";
    GLuint fp=create(GL_FRAGMENT_SHADER,1,&separable_fragment,&error);
    GLuint pipeline=0;glGenProgramPipelines(1,&pipeline);glBindProgramPipeline(pipeline);
    glUseProgramStages(pipeline,GL_VERTEX_SHADER_BIT,vp);glUseProgramStages(pipeline,GL_FRAGMENT_SHADER_BIT,fp);
    glProgramUniform1d(vp,glGetUniformLocation(vp,"value"),-511.5);
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_POINTS,0,1);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);CHECK(pixel[0]==255&&pixel[3]==255);
    CHECK(glGetError()==GL_NO_ERROR);
    CHECK(CGLCreateContext(format,context,&shared)==kCGLNoError);
    CHECK(CGLSetCurrentContext(shared)==kCGLNoError);
    CHECK(glIsShader(fs)&&glIsProgram(program));
    glGetShaderSource(vs,sizeof(after),&m,after);CHECK(m==n&&!strcmp(before,after));
    CGLSetCurrentContext(context);CGLDestroyContext(shared);CGLDestroyPixelFormat(format);
    glDeleteProgramPipelines(1,&pipeline);glDeleteProgram(fp);glDeleteProgram(vp);
    glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);glDeleteShader(a);glDeleteShader(b);
    glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("compile-api failures=%d\n",failures);return failures?1:0;
}
