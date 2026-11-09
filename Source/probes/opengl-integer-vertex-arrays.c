/* Compare actual Apple integer vertex array state before submitting a draw. */
#ifdef VIM_INTEGER_ARRAY_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glBindAttribLocation(p,i,n) glBindAttribLocationARB((GLhandleARB)(uintptr_t)(p),i,n)
#define glEnableVertexAttribArray glEnableVertexAttribArrayARB
#define glDisableVertexAttribArray glDisableVertexAttribArrayARB
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
extern void glGenVertexArrays(GLsizei,GLuint *);
extern void glBindVertexArray(GLuint);
#endif
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);GLint compiled=0;
    glShaderSource(s,1,&source,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&compiled);
    if(!compiled){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(compiled);return s;
}
static void capture_integer_vertices(int core,GLuint input,const GLuint *words)
{
    const char *vs=core?
        "#version 150\nin uvec2 payload;flat out uvec2 recorded;"
        "void main(){recorded=payload;gl_Position=vec4(0,0,0,1);}":
        "#version 120\n#extension GL_EXT_gpu_shader4 : require\nattribute uvec2 payload;flat varying uvec2 recorded;"
        "void main(){recorded=payload;gl_Position=vec4(0,0,0,1);}";
    const char *fs=core?"#version 150\nout vec4 c;void main(){c=vec4(1);}":
                           "#version 120\nvoid main(){gl_FragColor=vec4(1);}";
    GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,f);glBindAttribLocation(p,3,"payload");
    const char *output="recorded";
    glTransformFeedbackVaryingsEXT(p,1,&output,GL_INTERLEAVED_ATTRIBS_EXT);glLinkProgram(p);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);CHECK(linked);
    GLuint tex=0,fbo=0,target=0;GLuint actual[4]={0};
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glGenBuffers(1,&target);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,target);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,sizeof(actual),NULL,GL_STREAM_READ);
    glBindBufferBaseEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,target);
    const GLfloat positions[]={0,0,1,0};
    if(!core){glBindBuffer(GL_ARRAY_BUFFER,0);glVertexPointer(2,GL_FLOAT,0,positions);glEnableClientState(GL_VERTEX_ARRAY);}
    glUseProgram(p);glEnable(GL_RASTERIZER_DISCARD_EXT);glEnableVertexAttribArray(3);
    const GLuint expected[]={words[0],words[1],words[3],words[4]};
    // Physical Iris Pro's compatibility pipeline produces float encodings
    // here, unlike its core profile and the M4 compatibility renderer.
    const GLfloat legacyIntel[]={words[0],words[1],words[3],words[4]};
    for(int source=0;source<(core?1:2);++source){
        glBindBuffer(GL_ARRAY_BUFFER,source?0:input);
        glVertexAttribIPointerEXT(3,2,GL_UNSIGNED_INT,12,source?words:NULL);
        glBeginTransformFeedbackEXT(GL_POINTS);glDrawArrays(GL_POINTS,0,2);glEndTransformFeedbackEXT();
        memset(actual,0,sizeof(actual));
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER_EXT,0,sizeof(actual),actual);
        GLenum error=glGetError();
        int exact=!memcmp(actual,expected,sizeof(actual));
        int accepted=exact||(!core&&!memcmp(actual,legacyIntel,sizeof(actual)));
        if(error||!accepted)fprintf(stderr,
            "integer capture core=%d cpu=%d error=%x values=%x,%x,%x,%x\n",
            core,source,error,actual[0],actual[1],actual[2],actual[3]);
        CHECK(error==GL_NO_ERROR && accepted);
        printf("integer capture core=%d cpu=%d encoding=%s\n",core,source,exact?"uint":"legacy-float");
    }
    glDisableVertexAttribArray(3);glDisable(GL_RASTERIZER_DISCARD_EXT);glUseProgram(0);
    if(!core)glDisableClientState(GL_VERTEX_ARRAY);
    glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);glDeleteBuffers(1,&target);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&tex);
}
static void integer_arrays(int core)
{
    const GLuint index=3;
    const GLuint words[]={0xfedcba98u,0x01000001u,0,0xffffffffu,0x80000001u,0};
    GLuint vao=0,vbo=0;
    if(core){glGenVertexArrays(1,&vao);glBindVertexArray(vao);}
    GLint initial=-1;
    glGetVertexAttribIivEXT(index,GL_VERTEX_ATTRIB_ARRAY_INTEGER_EXT,&initial);
    CHECK(glGetError()==GL_NO_ERROR && initial==0);
    glVertexAttribIPointerEXT(index,2,GL_UNSIGNED_INT,0,words);
    GLenum cpu_error=glGetError();
    GLint stride=-1,integer=-1;
    glGetVertexAttribIivEXT(index,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&stride);
    glGetVertexAttribIivEXT(index,GL_VERTEX_ATTRIB_ARRAY_INTEGER_EXT,&integer);
    CHECK(glGetError()==GL_NO_ERROR && (core?(cpu_error==GL_INVALID_OPERATION && stride==0 && integer==0):
          (cpu_error==GL_NO_ERROR && stride==8 && integer==1)));
    glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(words),words,GL_STATIC_DRAW);
    glVertexAttribIPointerEXT(index,2,GL_UNSIGNED_INT,12,(void *)4);
    GLenum valid=glGetError();
    GLint s[3]={-1,-1,-1};GLuint u[3]={~0u,~0u,~0u};
    glGetVertexAttribIivEXT(index,GL_VERTEX_ATTRIB_ARRAY_STRIDE,s);
    glGetVertexAttribIivEXT(index,GL_VERTEX_ATTRIB_ARRAY_INTEGER_EXT,s+1);
    glGetVertexAttribIuivEXT(index,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,u);
    glGetVertexAttribIuivEXT(index,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,u+1);
    CHECK(valid==GL_NO_ERROR && glGetError()==GL_NO_ERROR && s[0]==12 && s[1]==1 && s[2]==-1 &&
          u[0]==vbo && u[1]==0 && u[2]==~0u);
    glVertexAttribIPointerEXT(index,2,GL_FLOAT,12,(void *)4);
    GLenum invalid=glGetError();
    glGetVertexAttribIivEXT(index,GL_VERTEX_ATTRIB_ARRAY_INTEGER_EXT,s);
    CHECK(invalid==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR && s[0]==1);
    glVertexAttribI4uiEXT(index,words[0],words[1],words[3],words[4]);
    GLuint current[5]={0,0,0,0,42};
    glGetVertexAttribIuivEXT(index,GL_CURRENT_VERTEX_ATTRIB,current);
    const GLuint expected[]={words[0],words[1],words[3],words[4],42};
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(current,expected,sizeof(expected)));
    capture_integer_vertices(core,vbo,words);
    glDeleteBuffers(1,&vbo);
}

int main(void)
{
    for(int core=0;core<2;++core){
#ifdef VIM_INTEGER_ARRAY_WIRE
        unsigned char screen[16*16*4]={0};
        const int attrs[]={0x56494d02,core?0x3200:0x1000,0};
        struct DGContext *c=DGCreateContext(attrs,NULL);
        if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
        CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
            kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
            (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),0};
        CGLPixelFormatObj pf=NULL;GLint n=0;CGLContextObj c=NULL;
        if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
        CGLDestroyPixelFormat(pf);
#endif
        printf("integer vertex renderer=%s core=%d\n",glGetString(GL_RENDERER),core);
        integer_arrays(core);
#ifdef VIM_INTEGER_ARRAY_WIRE
        CHECK(!c->connection->dead);DGDestroyContext(c);
#else
        CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    }
    printf("integer vertex failures=%d\n",failures);return failures?1:0;
}
