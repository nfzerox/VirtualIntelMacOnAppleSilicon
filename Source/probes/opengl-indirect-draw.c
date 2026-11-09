/* A transform-feedback producer writes the array draw command on the GPU. */
#ifdef VIM_INDIRECT_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glTransformFeedbackVaryings glTransformFeedbackVaryingsEXT
#define glBindBufferRange glBindBufferRangeEXT
#define glBeginTransformFeedback glBeginTransformFeedbackEXT
#define glEndTransformFeedback glEndTransformFeedbackEXT
#define glVertexAttribPointer glVertexAttribPointerARB
#define glEnableVertexAttribArray glEnableVertexAttribArrayARB
#define glGetUniformLocation(p,n) glGetUniformLocationARB((GLhandleARB)(uintptr_t)(p),n)
#define glUniform4f glUniform4fARB
#define GL_INTERLEAVED_ATTRIBS GL_INTERLEAVED_ATTRIBS_EXT
#define GL_TRANSFORM_FEEDBACK_BUFFER GL_TRANSFORM_FEEDBACK_BUFFER_EXT
#define GL_RASTERIZER_DISCARD GL_RASTERIZER_DISCARD_EXT
#define GL_DRAW_INDIRECT_BUFFER 0x8f3f
#define GL_DRAW_INDIRECT_BUFFER_BINDING 0x8f43
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static GLuint shader(GLenum stage,const char *source)
{
    GLuint s=glCreateShader(stage);GLint good=0;
    glShaderSource(s,1,&source,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&good);
    if(!good){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(good);return s;
}
static int link_program(GLuint p)
{
    GLint linked=0;glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&linked);
    if(!linked){char log[1024]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(linked);return linked;
}
static void pixel(unsigned char red,unsigned char green)
{
    GLubyte data[4]={0};
    glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,data);
    printf("indirect pixel=%u,%u,%u,%u\n",data[0],data[1],data[2],data[3]);
    CHECK(glGetError()==GL_NO_ERROR && data[0]==red && data[1]==green && data[2]==0 && data[3]==255);
}
static void verify(void)
{
    GLuint vao=0,fbo=0,image=0,buffers[3]={0};
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&image);glBindTexture(GL_TEXTURE_2D,image);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,image,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,4,4);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glGenBuffers(3,buffers);
    const GLuint commands[]={0,0,0,0, 0,0,0,0, 3,1,1,(GLuint)-3,0};
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER,buffers[0]);
    glBufferData(GL_DRAW_INDIRECT_BUFFER,sizeof(commands),commands,GL_DYNAMIC_DRAW);
    GLuint frag=shader(GL_FRAGMENT_SHADER,"#version 410\nuniform vec4 tint;out vec4 color;void main(){color=tint;}");
    GLuint producer=glCreateProgram();
    glAttachShader(producer,shader(GL_VERTEX_SHADER,"#version 410\nflat out uvec4 command;"
        "void main(){command=uvec4(3,1,3,0);gl_Position=vec4(0,0,0,1);}"));
    glAttachShader(producer,frag);
    const char *capture="command";
    glTransformFeedbackVaryings(producer,1,&capture,GL_INTERLEAVED_ATTRIBS);
    if(!link_program(producer))return;
    glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffers[0],16,16);
    glUseProgram(producer);glEnable(GL_RASTERIZER_DISCARD);
    glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,1);glEndTransformFeedback();
    glDisable(GL_RASTERIZER_DISCARD);
    CHECK(glGetError()==GL_NO_ERROR);

    GLuint draw=glCreateProgram();
    glAttachShader(draw,shader(GL_VERTEX_SHADER,"#version 410\nlayout(location=0) in vec2 position;"
        "void main(){gl_Position=vec4(position,0,1);}"));
    glAttachShader(draw,frag);
    if(!link_program(draw))return;
    glUseProgram(draw);
    GLint tint=glGetUniformLocation(draw,"tint");CHECK(tint>=0);
    const GLfloat vertices[]={0,0,0,0,0,0,-1,-1,3,-1,-1,3};
    const GLushort indices[]={0,6,7,8};
    glBindBuffer(GL_ARRAY_BUFFER,buffers[1]);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
    glUniform4f(tint,1,0,0,1);
    glDrawArraysIndirect(GL_TRIANGLES,(const void *)16);pixel(255,0);
    glUniform4f(tint,0,1,0,1);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[2]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glDrawElementsIndirect(GL_TRIANGLES,GL_UNSIGNED_SHORT,(const void *)32);pixel(0,255);
    GLint binding=0;glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING,&binding);
    CHECK(glGetError()==GL_NO_ERROR && (GLuint)binding==buffers[0]);
    glDrawElementsIndirect(GL_TRIANGLES,GL_FLOAT,(const void *)32);
    CHECK(glGetError()==GL_INVALID_ENUM);
    glDrawArraysIndirect(GL_TRIANGLES,(const void *)64);
    CHECK(glGetError()==GL_INVALID_OPERATION);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER,0);
    glDrawArraysIndirect(GL_TRIANGLES,NULL);
    CHECK(glGetError()==GL_INVALID_OPERATION);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER,buffers[0]);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    glDrawElementsIndirect(GL_TRIANGLES,GL_UNSIGNED_SHORT,(const void *)32);
    CHECK(glGetError()==GL_INVALID_OPERATION);
    pixel(0,255);
}
int main(void)
{
#ifdef VIM_INDIRECT_WIRE
    const int attrs[]={0x56494d02,DG_CGL_PROFILE_GL4,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_DRAW_INDIRECT);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("indirect draws renderer=%s\n",glGetString(GL_RENDERER));verify();
#ifdef VIM_INDIRECT_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("indirect draw failures=%d\n",failures);return failures?1:0;
}
