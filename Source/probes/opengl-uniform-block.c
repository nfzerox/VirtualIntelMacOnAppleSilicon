/* Shader reflection, indexed buffer state and a real uniform-buffer draw. */
#ifdef VIM_UNIFORM_BLOCK_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glBindBufferRange glBindBufferRangeEXT
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#define glGenFramebuffersEXT glGenFramebuffers
#define glBindFramebufferEXT glBindFramebuffer
#define glFramebufferTexture2DEXT glFramebufferTexture2D
#define glCheckFramebufferStatusEXT glCheckFramebufferStatus
#define glDeleteFramebuffersEXT glDeleteFramebuffers
#define GL_FRAMEBUFFER_EXT GL_FRAMEBUFFER
#define GL_FRAMEBUFFER_COMPLETE_EXT GL_FRAMEBUFFER_COMPLETE
#define GL_COLOR_ATTACHMENT0_EXT GL_COLOR_ATTACHMENT0
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8a11
#define GL_UNIFORM_BUFFER_BINDING 0x8a28
#define GL_UNIFORM_BUFFER_START 0x8a29
#define GL_UNIFORM_BUFFER_SIZE 0x8a2a
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 0x8a34
#define GL_ACTIVE_UNIFORM_BLOCKS 0x8a36
#define GL_UNIFORM_OFFSET 0x8a3b
#define GL_UNIFORM_BLOCK_BINDING 0x8a3f
#define GL_UNIFORM_BLOCK_DATA_SIZE 0x8a40
#define GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS 0x8a42
#define GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES 0x8a43
#endif
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static GLuint shader(GLenum type,const char *src)
{
    GLuint s=glCreateShader(type);GLint ready=0;
    glShaderSource(s,1,&src,NULL);glCompileShader(s);
    glGetShaderiv(s,GL_COMPILE_STATUS,&ready);
    if(!ready){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(ready);return s;
}

static void verify(void)
{
    const char *vert="#version 150\n"
        "void main(){vec2 p=vec2(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1);gl_Position=vec4(p,0,1);}";
    const char *frag="#version 150\n"
        "layout(std140) uniform Palette{vec4 colorA;vec4 colorB;};out vec4 outColor;"
        "void main(){outColor=colorA+colorB;}";
    GLuint v=shader(GL_VERTEX_SHADER,vert),f=shader(GL_FRAGMENT_SHADER,frag),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
    GLint linked=0,blocks=-1;glGetProgramiv(p,GL_LINK_STATUS,&linked);
    if(!linked){char log[1024]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(linked);if(!linked)return;
    glGetProgramiv(p,GL_ACTIVE_UNIFORM_BLOCKS,&blocks);
    CHECK(glGetError()==GL_NO_ERROR && blocks==1);
    GLuint block=glGetUniformBlockIndex(p,"Palette");
    CHECK(block!=~0u && glGetUniformBlockIndex(p,"Missing")==~0u && glGetError()==GL_NO_ERROR);
    const GLchar *names[]={"colorB","does_not_exist","colorA"};
    GLuint ids[]={~0u,0,~0u,0xfeedcafe};
    glGetUniformIndices(p,3,names,ids);
    CHECK(glGetError()==GL_NO_ERROR && ids[0]!=~0u && ids[1]==~0u && ids[2]!=~0u && ids[3]==0xfeedcafe);
    GLuint valid[]={ids[0],ids[2]};
    GLint offsets[]={-1,-1,0x12345678};
    glGetActiveUniformsiv(p,2,valid,GL_UNIFORM_OFFSET,offsets);
    CHECK(glGetError()==GL_NO_ERROR && offsets[0]==16 && offsets[1]==0 && offsets[2]==0x12345678);
    char name[16];memset(name,'?',sizeof(name));GLsizei n=-1;
    glGetActiveUniformName(p,ids[2],sizeof(name)-1,&n,name);
    CHECK(glGetError()==GL_NO_ERROR && n==6 && !strcmp(name,"colorA") && name[15]=='?');
    memset(name,'?',sizeof(name));n=-1;
    glGetActiveUniformBlockName(p,block,5,&n,name);
    CHECK(glGetError()==GL_NO_ERROR && n==4 && !strcmp(name,"Pale") && name[5]=='?');
    glGetActiveUniformBlockName(p,block,sizeof(name),NULL,name);
    CHECK(glGetError()==GL_NO_ERROR && !strcmp(name,"Palette"));
    glGetUniformIndices(p,0,NULL,NULL);
    glGetActiveUniformsiv(p,0,NULL,GL_UNIFORM_OFFSET,NULL);
    CHECK(glGetError()==GL_NO_ERROR);
    GLint count=-1,indices[]={-1,-1,0x12345678},size=-1;
    glGetActiveUniformBlockiv(p,block,GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS,&count);
    glGetActiveUniformBlockiv(p,block,GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES,indices);
    glGetActiveUniformBlockiv(p,block,GL_UNIFORM_BLOCK_DATA_SIZE,&size);
    CHECK(glGetError()==GL_NO_ERROR && count==2 && size==32 && indices[2]==0x12345678);
    CHECK(((GLuint)indices[0]==ids[0] && (GLuint)indices[1]==ids[2]) ||
          ((GLuint)indices[1]==ids[0] && (GLuint)indices[0]==ids[2]));
    GLint invalid=0x12345678;
    glGetActiveUniformBlockiv(p,block,GL_TEXTURE_2D,&invalid);
    CHECK(glGetError()==GL_INVALID_ENUM && invalid==0x12345678);
    glGetActiveUniformBlockiv(p,~0u,GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES,&invalid);
    CHECK(glGetError()==GL_INVALID_VALUE && invalid==0x12345678);

    GLint alignment=0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&alignment);
    CHECK(glGetError()==GL_NO_ERROR && alignment>0 && alignment<=65536);
    if(alignment<=0 || alignment>65536)return;
    GLuint buf=0,vao=0,tex=0,fb=0;
    const GLfloat colors[]={.25f,.125f,0,.25f,.25f,.125f,.125f,.75f};
    unsigned char *contents=calloc(1,(size_t)alignment+sizeof(colors));
    if(!contents){++failures;return;}
    memcpy(contents+alignment,colors,sizeof(colors));
    glGenBuffers(1,&buf);glBindBuffer(GL_UNIFORM_BUFFER,buf);
    glBufferData(GL_UNIFORM_BUFFER,(size_t)alignment+sizeof(colors),contents,GL_STATIC_DRAW);
    free(contents);
    glUniformBlockBinding(p,block,2);glBindBufferRange(GL_UNIFORM_BUFFER,2,buf,alignment,sizeof(colors));
    GLint binding=-1;glGetActiveUniformBlockiv(p,block,GL_UNIFORM_BLOCK_BINDING,&binding);
    struct {GLint64 a,b,c,guard;} indexed={-1,-1,-1,0x12345678};
    glGetInteger64i_v(GL_UNIFORM_BUFFER_START,2,&indexed.a);
    glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE,2,&indexed.b);
    glGetInteger64i_v(GL_UNIFORM_BUFFER_BINDING,2,&indexed.c);
    CHECK(glGetError()==GL_NO_ERROR && binding==2 && indexed.a==alignment &&
          indexed.b==sizeof(colors) && indexed.c==buf && indexed.guard==0x12345678);
    GLint64 wide=-42;glGetInteger64i_v(GL_UNIFORM_BUFFER_START,~0u,&wide);
    CHECK(glGetError()==GL_INVALID_VALUE && wide==-42);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,4,4);glUseProgram(p);glDrawArrays(GL_TRIANGLES,0,3);
    GLubyte pixel[4]={0};glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    CHECK(glGetError()==GL_NO_ERROR && pixel[0]>=127 && pixel[0]<=128 &&
          pixel[1]>=63 && pixel[1]<=64 && pixel[2]>=31 && pixel[2]<=32 && pixel[3]==255);
    printf("uniform block offsets=%d,%d binding=%d pixel=%u,%u,%u,%u\n",
           offsets[0],offsets[1],binding,pixel[0],pixel[1],pixel[2],pixel[3]);
    glDeleteFramebuffersEXT(1,&fb);glDeleteTextures(1,&tex);glDeleteBuffers(1,&buf);
    glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);
}

int main(void)
{
#ifdef VIM_UNIFORM_BLOCK_WIRE
    unsigned char screen[16*16*4]={0};
    const int attrs[]={0x56494d02,0x3200,0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK((c->connection->features&(DG_FEATURE_UNIFORM_BLOCKS|DG_FEATURE_INDEXED_INT64))==
          (DG_FEATURE_UNIFORM_BLOCKS|DG_FEATURE_INDEXED_INT64));
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("uniform block renderer=%s\n",glGetString(GL_RENDERER));
    verify();
#ifdef VIM_UNIFORM_BLOCK_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("uniform block failures=%d\n",failures);return failures?1:0;
}
