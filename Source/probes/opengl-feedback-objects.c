/* Capture with pauses, then replay the GPU-owned count through a different VAO. */
#ifdef VIM_FEEDBACK_OBJECTS_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glTransformFeedbackVaryings glTransformFeedbackVaryingsEXT
#define glBindBufferBase glBindBufferBaseEXT
#define glBeginTransformFeedback glBeginTransformFeedbackEXT
#define glEndTransformFeedback glEndTransformFeedbackEXT
#define glVertexAttribPointer glVertexAttribPointerARB
#define glEnableVertexAttribArray glEnableVertexAttribArrayARB
#define GL_INTERLEAVED_ATTRIBS GL_INTERLEAVED_ATTRIBS_EXT
#define GL_TRANSFORM_FEEDBACK_BUFFER GL_TRANSFORM_FEEDBACK_BUFFER_EXT
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN_EXT
#define GL_RASTERIZER_DISCARD GL_RASTERIZER_DISCARD_EXT
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <string.h>

enum { feedback=0x8e22, paused=0x8e23, active=0x8e24, binding=0x8e25 };
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static GLuint shader(GLenum stage,const char *text)
{
    GLuint s=glCreateShader(stage);
    glShaderSource(s,1,&text,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(ok);return s;
}
static int linked(GLuint p)
{
    glLinkProgram(p);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[1024]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(ok);return ok;
}
static void pixel(int x,int green)
{
    GLubyte rgba[4]={0};
    glReadPixels(x,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    GLenum error=glGetError();
    printf("feedback replay x=%d rgba=%u,%u,%u,%u error=%x\n",x,rgba[0],rgba[1],rgba[2],rgba[3],error);
    CHECK(error==GL_NO_ERROR && rgba[0]==0 && rgba[1]==green && rgba[2]==0 && rgba[3]==255);
}
static void verify(int resume_draw)
{
    GLuint ids[2]={0,37};
    glGenTransformFeedbacks(-1,ids);
    CHECK(glGetError()==GL_INVALID_VALUE && ids[0]==0 && ids[1]==37);
    glGenTransformFeedbacks(1,ids);
    CHECK(glGetError()==GL_NO_ERROR && ids[0] && ids[1]==37 && !glIsTransformFeedback(ids[0]));
    glBindTransformFeedback(feedback,ids[0]);
    GLint current=0;glGetIntegerv(binding,&current);
    CHECK(glGetError()==GL_NO_ERROR && (GLuint)current==ids[0] && glIsTransformFeedback(ids[0]));

    GLuint vao[2]={0},buffer=0,query=0,image=0,fb=0;
    glGenVertexArrays(2,vao);glBindVertexArray(vao[0]);
    glGenTextures(1,&image);glBindTexture(GL_TEXTURE_2D,image);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,image,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,4,4);
    GLuint fragment=shader(GL_FRAGMENT_SHADER,"#version 410\nout vec4 color;void main(){color=vec4(0,1,0,1);}");
    GLuint capture=glCreateProgram();
    glAttachShader(capture,shader(GL_VERTEX_SHADER,"#version 410\nout float recorded;"
        "void main(){recorded=gl_VertexID==2?.75:float(gl_VertexID)-.75;gl_Position=vec4(0,0,0,1);}"));
    glAttachShader(capture,fragment);
    const char *varying="recorded";
    glTransformFeedbackVaryings(capture,1,&varying,GL_INTERLEAVED_ATTRIBS);
    if(!linked(capture))return;
    glUseProgram(capture);
    glGenBuffers(1,&buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,4*sizeof(GLfloat),NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);
    glGenQueries(1,&query);
    glBeginQueryIndexed(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,0,query);
    GLint query_state[2]={0,37};
    glGetQueryIndexediv(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,0,GL_CURRENT_QUERY,query_state);
    CHECK(glGetError()==GL_NO_ERROR && (GLuint)query_state[0]==query && query_state[1]==37);
    glEnable(GL_RASTERIZER_DISCARD);
    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS,0,resume_draw?1:2);
    glPauseTransformFeedback();
    GLboolean states[3]={0,0,37};
    glGetBooleanv(active,states);glGetBooleanv(paused,states+1);
    CHECK(glGetError()==GL_NO_ERROR && states[0] && states[1] && states[2]==37);
    glDrawArrays(GL_POINTS,2,1);
    glResumeTransformFeedback();
    glGetBooleanv(paused,states+1);
    CHECK(glGetError()==GL_NO_ERROR && !states[1]);
    if(resume_draw)glDrawArrays(GL_POINTS,1,1);
    glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);
    glEndQueryIndexed(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,0);
    GLuint count=0;glGetQueryObjectuiv(query,GL_QUERY_RESULT,&count);
    GLenum query_error=glGetError();
    printf("feedback captured primitives=%u error=%x\n",count,query_error);
    CHECK(query_error==GL_NO_ERROR && count==2);
    glBindTransformFeedback(feedback,0);
    glBindVertexArray(vao[1]);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glVertexAttribPointer(0,1,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
    GLuint replay=glCreateProgram();
    glAttachShader(replay,shader(GL_VERTEX_SHADER,"#version 410\nlayout(location=0) in float x;"
        "void main(){gl_Position=vec4(x,.25,0,1);}"));
    glAttachShader(replay,fragment);
    if(!linked(replay))return;
    glUseProgram(replay);
    for(int stream=0;stream<2;++stream){
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        if(stream)glDrawTransformFeedbackStream(GL_POINTS,ids[0],0);
        else glDrawTransformFeedback(GL_POINTS,ids[0]);
        CHECK(glGetError()==GL_NO_ERROR);
        pixel(0,255);pixel(1,0);pixel(2,255);pixel(3,0);
    }
    glDeleteTransformFeedbacks(1,ids);
    CHECK(glGetError()==GL_NO_ERROR && !glIsTransformFeedback(ids[0]));
}

int main(int argc,char **argv)
{
    int core32=argc>1 && !strcmp(argv[1],"--gl3");
#ifdef VIM_FEEDBACK_OBJECTS_WIRE
    const int attrs[]={0x56494d02,core32?0x3200:DG_CGL_PROFILE_GL4,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_FEEDBACK_OBJECTS);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)(core32?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_GL4_Core),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("feedback objects renderer=%s\n",glGetString(GL_RENDERER));
    verify(argc>1 && !strcmp(argv[1],"--draw-after-resume"));
#ifdef VIM_FEEDBACK_OBJECTS_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("feedback objects failures=%d\n",failures);return failures?1:0;
}
