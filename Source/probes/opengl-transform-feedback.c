/* The native driver, not the bridge, must link and capture these varyings. */
#ifdef VIM_FEEDBACK_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
extern void glGenVertexArrays(GLsizei,GLuint *);
extern void glBindVertexArray(GLuint);
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static GLuint compile(GLenum stage,const char *src)
{
    GLuint shader=glCreateShader(stage);
    glShaderSource(shader,1,&src,NULL);glCompileShader(shader);
    GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    CHECK(ok);return shader;
}
static void run_feedback(int core)
{
    const char *vertex=core?
        "#version 150\nout vec2 recorded;out float extra;void main(){float i=float(gl_VertexID);"
        "gl_Position=vec4(0,0,0,1);recorded=vec2(i,i+1);extra=i+2;}":
        "#version 120\nvarying vec2 recorded;varying float extra;void main(){float i=gl_Vertex.x;"
        "gl_Position=vec4(0,0,0,1);recorded=vec2(i,i+1);extra=i+2;}";
    const char *fragment=core?"#version 150\nout vec4 c;void main(){c=vec4(1);}":
                               "#version 120\nvoid main(){gl_FragColor=vec4(1);}";
    GLuint v=compile(GL_VERTEX_SHADER,vertex),f=compile(GL_FRAGMENT_SHADER,fragment);
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);
    glTransformFeedbackVaryingsEXT(p,0,NULL,GL_INTERLEAVED_ATTRIBS_EXT);
    CHECK(glGetError()==GL_NO_ERROR);
    const char *names[]={"extra","recorded"};
    glTransformFeedbackVaryingsEXT(p,2,names,GL_INTERLEAVED_ATTRIBS_EXT);
    glLinkProgram(p);
    GLint good=0;glGetProgramiv(p,GL_LINK_STATUS,&good);
    if(!good){char log[2048]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    CHECK(good);
    GLint count=-1,mode=-1,limit=-1;
    glGetProgramiv(p,GL_TRANSFORM_FEEDBACK_VARYINGS_EXT,&count);
    glGetProgramiv(p,GL_TRANSFORM_FEEDBACK_BUFFER_MODE_EXT,&mode);
    glGetProgramiv(p,GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH_EXT,&limit);
    CHECK(glGetError()==GL_NO_ERROR && count==2 && mode==GL_INTERLEAVED_ATTRIBS_EXT && limit==9);
    GLsizei len=-1,size=-1;GLenum type=0;char name[20];
    memset(name,0xa5,sizeof(name));
    glGetTransformFeedbackVaryingEXT(p,0,sizeof(name),&len,&size,&type,name);
    CHECK(glGetError()==GL_NO_ERROR && len==5 && size==1 && type==GL_FLOAT &&
        !memcmp(name,"extra",6) && (unsigned char)name[6]==0xa5);
    glGetTransformFeedbackVaryingEXT(p,1,4,NULL,&size,&type,name);
    CHECK(glGetError()==GL_NO_ERROR && size==1 && type==GL_FLOAT_VEC2 && !memcmp(name,"rec",4));
    len=-1;glGetTransformFeedbackVaryingEXT(p,1,0,&len,&size,&type,name);
    CHECK(glGetError()==GL_NO_ERROR && len==0 && !memcmp(name,"rec",4));
    len=-1;size=-1;type=0;
    glGetTransformFeedbackVaryingEXT(p,1,0,&len,&size,&type,NULL);
    CHECK(glGetError()==GL_NO_ERROR && len==0 && size==1 && type==GL_FLOAT_VEC2);
    len=-1;glGetTransformFeedbackVaryingEXT(p,1,-1,&len,&size,&type,NULL);
    CHECK(glGetError()==GL_INVALID_VALUE && len==-1);
    len=-1;glGetTransformFeedbackVaryingEXT(p,2,sizeof(name),&len,&size,&type,name);
    CHECK(glGetError()==GL_INVALID_VALUE && len==-1 && !memcmp(name,"rec",4));

    GLuint tex=0,fbo=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    if(core){GLuint vao=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);}
    const GLfloat vertices[]={0,0,1,0,2,0};
    if(!core){glVertexPointer(2,GL_FLOAT,0,vertices);glEnableClientState(GL_VERTEX_ARRAY);}
    GLuint buffer=0;GLfloat result[9]={0};
    glGenBuffers(1,&buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,sizeof(result),NULL,GL_STREAM_READ);
    glBindBufferOffsetEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,buffer,4);
    GLenum offsetBindError=glGetError();
    GLint offset=-1;
    glGetIntegerIndexedvEXT(GL_TRANSFORM_FEEDBACK_BUFFER_START_EXT,0,&offset);
    GLenum offsetGetError=glGetError();
    // Apple's EXT offset-only entry is not part of the core profile. It
    // leaves the binding intact there; the range and base calls still work.
    CHECK(offsetGetError==GL_NO_ERROR &&
        (core?(offsetBindError==GL_INVALID_OPERATION && offset==0):
              (offsetBindError==GL_NO_ERROR && offset==4)));
    glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,buffer);
    GLint global=-1;
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING_EXT,&global);
    CHECK(glGetError()==GL_NO_ERROR && (GLuint)global==buffer);
    glBindBufferRangeEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,buffer,0,sizeof(result));
    GLint state[5]={-1,-1,-1,-1,-1};
    glGetIntegerIndexedvEXT(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING_EXT,0,state);
    glGetIntegerIndexedvEXT(GL_TRANSFORM_FEEDBACK_BUFFER_START_EXT,0,state+1);
    glGetIntegerIndexedvEXT(GL_TRANSFORM_FEEDBACK_BUFFER_SIZE_EXT,0,state+2);
    CHECK(glGetError()==GL_NO_ERROR && (GLuint)state[0]==buffer && state[1]==0 &&
        state[2]==sizeof(result) && state[3]==-1 && state[4]==-1);
    glGetIntegerIndexedvEXT(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING_EXT,~0u,state+4);
    CHECK(glGetError()==GL_INVALID_VALUE && state[4]==-1);
    glColorMaskIndexedEXT(0,GL_TRUE,GL_FALSE,GL_TRUE,GL_FALSE);
    GLboolean mask[5]={2,2,2,2,2};
    glGetBooleanIndexedvEXT(GL_COLOR_WRITEMASK,0,mask);
    CHECK(glGetError()==GL_NO_ERROR && mask[0]==1 && mask[1]==0 && mask[2]==1 && mask[3]==0 && mask[4]==2);
    glColorMaskIndexedEXT(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glDisableIndexedEXT(GL_BLEND,0);glEnableIndexedEXT(GL_BLEND,1);
    CHECK(!glIsEnabledIndexedEXT(GL_BLEND,0) && glIsEnabledIndexedEXT(GL_BLEND,1));
    glDisableIndexedEXT(GL_BLEND,1);
    CHECK(!glIsEnabledIndexedEXT(GL_BLEND,1) && glGetError()==GL_NO_ERROR);
    GLuint query=0,written=0;
    glGenQueries(1,&query);
    glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN_EXT,query);
    glUseProgram(p);glEnable(GL_RASTERIZER_DISCARD_EXT);
    glBeginTransformFeedbackEXT(GL_POINTS);
    glDrawArrays(GL_POINTS,0,3);
    glEndTransformFeedbackEXT();glDisable(GL_RASTERIZER_DISCARD_EXT);
    glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN_EXT);
    glGetQueryObjectuiv(query,GL_QUERY_RESULT,&written);
    CHECK(glGetError()==GL_NO_ERROR && written==3);
    glDeleteQueries(1,&query);
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,sizeof(result),result);
    const GLfloat expected[]={2,0,1,3,1,2,4,2,3};
    GLenum error=glGetError();
    if(error || memcmp(result,expected,sizeof(result))){
        fprintf(stderr,"capture error=%x values:",error);
        for(unsigned i=0;i<9;++i)fprintf(stderr," %g",result[i]);
        fputc('\n',stderr);
    }
    CHECK(error==GL_NO_ERROR && !memcmp(result,expected,sizeof(result)));
    glUseProgram(0);
    glTransformFeedbackVaryingsEXT(p,2,names,GL_SEPARATE_ATTRIBS_EXT);
    glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&good);
    glGetProgramiv(p,GL_TRANSFORM_FEEDBACK_BUFFER_MODE_EXT,&mode);
    CHECK(glGetError()==GL_NO_ERROR && good && mode==GL_SEPARATE_ATTRIBS_EXT);
    GLuint separated=0;glGenBuffers(1,&separated);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,separated);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,6*sizeof(GLfloat),NULL,GL_STREAM_READ);
    glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,buffer);
    glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,1,separated);
    glUseProgram(p);glEnable(GL_RASTERIZER_DISCARD_EXT);
    GLboolean discard=GL_FALSE;glGetBooleanv(GL_RASTERIZER_DISCARD_EXT,&discard);
    CHECK(glGetError()==GL_NO_ERROR && discard==GL_TRUE);
    glBeginTransformFeedbackEXT(GL_POINTS);glDrawArrays(GL_POINTS,0,3);glEndTransformFeedbackEXT();
    glDisable(GL_RASTERIZER_DISCARD_EXT);
    const GLfloat expected0[]={2,3,4},expected1[]={0,1,1,2,2,3};
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,buffer);
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,sizeof(expected0),result);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(result,expected0,sizeof(expected0)));
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,separated);
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,sizeof(expected1),result);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(result,expected1,sizeof(expected1)));
    glDeleteBuffers(1,&separated);
    glUseProgram(0);glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);
    glDeleteBuffers(1,&buffer);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&tex);
}
int main(int argc,char **argv)
{
    for(int core=0;core<2;++core){
#ifdef VIM_FEEDBACK_WIRE
        (void)argc;(void)argv;
        unsigned char screen[16*16*4]={0};
        const int attrs[]={0x56494d02,core?0x3200:0x1000,0};
        struct DGContext *c=DGCreateContext(attrs,NULL);
        if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
        CHECK(c->connection->features&DG_FEATURE_TRANSFORM_FEEDBACK_NAMES);
        CHECK(c->connection->features&DG_FEATURE_INDEXED_STATE);
#else
        CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
            kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
            (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),0,0,0};
        if(argc==2){attrs[5]=kCGLPFARendererID;attrs[6]=(CGLPixelFormatAttribute)strtoul(argv[1],NULL,16);}
        CGLPixelFormatObj pf=NULL;GLint n=0;CGLContextObj c=NULL;
        if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
        CGLDestroyPixelFormat(pf);
        if(argc==2 && !strstr((const char *)glGetString(GL_VENDOR),"Intel"))return 77;
#endif
        printf("transform feedback renderer=%s core=%d\n",glGetString(GL_RENDERER),core);
        if(!core){
            const char *ext=(const char *)glGetString(GL_EXTENSIONS);
            unsigned flags=(ext && strstr(ext,"GL_EXT_transform_feedback")?1u:0u)|
                           (ext && strstr(ext,"GL_EXT_draw_buffers2")?2u:0u);
            printf("feedback extensions=%u\n",flags);
            const char *expected=getenv("VIM_EXPECT_FEEDBACK_EXTENSIONS");
            if(expected)CHECK(flags==strtoul(expected,NULL,10));
        }
        run_feedback(core);
#ifdef VIM_FEEDBACK_WIRE
        CHECK(!c->connection->dead);DGDestroyContext(c);
#else
        CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    }
    printf("transform feedback failures=%d\n",failures);return failures?1:0;
}
