/* Exercise all sampler object entry points and the resulting native pixels. */
#ifdef VIM_SAMPLER_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
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
#include <string.h>
#ifndef GL_SAMPLER_BINDING
#define GL_SAMPLER_BINDING 0x8919
#endif
#ifndef GL_TEXTURE_BUFFER
#define GL_TEXTURE_BUFFER 0x8c2a
#endif
#ifndef GL_RGBA32F_ARB
#define GL_RGBA32F_ARB 0x8814
#endif
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static GLuint shader(GLenum type,const char *src)
{
    GLuint s=glCreateShader(type);GLint ready=0;
    glShaderSource(s,1,&src,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&ready);
    if(!ready){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(ready);return s;
}

static void verify(void)
{
    GLuint samplers[3]={0,0,0x12345678};
    glGenSamplers(0,NULL);glDeleteSamplers(0,NULL);
    glGenSamplers(2,samplers);
    CHECK(glGetError()==GL_NO_ERROR && samplers[0] && samplers[1] && samplers[0]!=samplers[1] &&
          samplers[2]==0x12345678 && glIsSampler(samplers[0]) && !glIsSampler(0));
    GLuint s=samplers[0],typed=samplers[1];
    const GLint border_wrap=GL_CLAMP_TO_BORDER;
    glSamplerParameteri(s,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glSamplerParameteri(s,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glSamplerParameteriv(s,GL_TEXTURE_WRAP_S,&border_wrap);
    glSamplerParameteriv(s,GL_TEXTURE_WRAP_T,&border_wrap);
    glSamplerParameterf(s,GL_TEXTURE_MIN_LOD,-.25f);
    const GLfloat border[]={0,1,0,1};
    glSamplerParameterfv(s,GL_TEXTURE_BORDER_COLOR,border);
    GLint wrap[2]={0,0x12345678};
    GLfloat f[5]={0,0,0,0,123};
    glGetSamplerParameteriv(s,GL_TEXTURE_WRAP_S,wrap);
    glGetSamplerParameterfv(s,GL_TEXTURE_MIN_LOD,f);
    CHECK(glGetError()==GL_NO_ERROR && wrap[0]==GL_CLAMP_TO_BORDER && wrap[1]==0x12345678 && f[0]==-.25f);
    glGetSamplerParameterfv(s,GL_TEXTURE_BORDER_COLOR,f);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(f,border,sizeof(border)) && f[4]==123);
    const GLint sb[]={-7,12345,-10000003,19};
    const GLuint ub[]={0xfedcba98u,1,0x80000001u,0xffffffffu};
    GLint got_s[5]={0,0,0,0,123};
    GLuint got_u[5]={0,0,0,0,123};
    glSamplerParameterIiv(typed,GL_TEXTURE_BORDER_COLOR,sb);
    glGetSamplerParameterIiv(typed,GL_TEXTURE_BORDER_COLOR,got_s);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(got_s,sb,sizeof(sb)) && got_s[4]==123);
    glSamplerParameterIuiv(typed,GL_TEXTURE_BORDER_COLOR,ub);
    glGetSamplerParameterIuiv(typed,GL_TEXTURE_BORDER_COLOR,got_u);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(got_u,ub,sizeof(ub)) && got_u[4]==123);
    GLint untouched=0x12345678;
    glGetSamplerParameteriv(s,GL_TEXTURE_2D,&untouched);
    CHECK(glGetError()==GL_INVALID_ENUM && untouched==0x12345678);
    glBindSampler(~0u,s);CHECK(glGetError()==GL_INVALID_VALUE);
    glGenSamplers(-1,samplers+2);CHECK(glGetError()==GL_INVALID_VALUE && samplers[2]==0x12345678);

    const char *vert="#version 150\n"
        "void main(){vec2 p=vec2(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1);gl_Position=vec4(p,0,1);}";
    const char *frag="#version 150\n"
        "uniform sampler2D picture;out vec4 outColor;void main(){outColor=texture(picture,vec2(-1,-1));}";
    GLuint v=shader(GL_VERTEX_SHADER,vert),z=shader(GL_FRAGMENT_SHADER,frag),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,z);glLinkProgram(p);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);CHECK(linked);
    if(!linked)return;
    const GLubyte red[]={255,0,0,255},green[]={0,255,0,255};
    GLuint textures[2]={0},fb=0,vao=0;
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(2,textures);glBindTexture(GL_TEXTURE_2D,textures[0]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,textures[0],0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glBindTexture(GL_TEXTURE_2D,textures[1]);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,red);
    // The linked sampler uniform starts at texture unit zero.
    glViewport(0,0,4,4);glUseProgram(p);
    glBindSampler(0,s);
    GLint binding=-1;glGetIntegerv(GL_SAMPLER_BINDING,&binding);
    CHECK(glGetError()==GL_NO_ERROR && binding==(GLint)s);
    glDrawArrays(GL_TRIANGLES,0,3);
    GLubyte pixel[4]={0};
    glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(pixel,green,4));
    printf("sampler bound pixel=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
    glDeleteSamplers(1,&s);
    glGetIntegerv(GL_SAMPLER_BINDING,&binding);
    CHECK(glGetError()==GL_NO_ERROR && binding==0 && !glIsSampler(s));
    glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(pixel,red,4));
    printf("sampler deleted pixel=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
    const char *buffer_frag="#version 150\n"
        "uniform samplerBuffer picture;out vec4 outColor;void main(){outColor=texelFetch(picture,1);}";
    GLuint buffer_shader=shader(GL_FRAGMENT_SHADER,buffer_frag),buffer_program=glCreateProgram();
    glAttachShader(buffer_program,v);glAttachShader(buffer_program,buffer_shader);glLinkProgram(buffer_program);
    glGetProgramiv(buffer_program,GL_LINK_STATUS,&linked);CHECK(linked);
    GLuint buffer=0,buffer_texture=0;
    const GLfloat texels[]={1,0,0,1,0,0,1,1};
    const GLfloat replacement[]={1,1,0,1};
    const GLubyte blue[]={0,0,255,255},yellow[]={255,255,0,255};
    glGenBuffers(1,&buffer);glBindBuffer(GL_TEXTURE_BUFFER,buffer);
    glBufferData(GL_TEXTURE_BUFFER,sizeof(texels),texels,GL_DYNAMIC_DRAW);
    glGenTextures(1,&buffer_texture);glBindTexture(GL_TEXTURE_BUFFER,buffer_texture);
    glTexBuffer(GL_TEXTURE_2D,GL_RGBA32F_ARB,buffer);CHECK(glGetError()==GL_INVALID_ENUM);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F_ARB,buffer);
    glUseProgram(buffer_program);glDrawArrays(GL_TRIANGLES,0,3);
    glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(pixel,blue,4));
    printf("texture buffer initial pixel=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
    glBufferSubData(GL_TEXTURE_BUFFER,sizeof(replacement),sizeof(replacement),replacement);
    glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(pixel,yellow,4));
    printf("texture buffer updated pixel=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F_ARB,0);CHECK(glGetError()==GL_NO_ERROR);
    glDeleteTextures(1,&buffer_texture);glDeleteBuffers(1,&buffer);
    glDeleteProgram(buffer_program);glDeleteShader(buffer_shader);
    glDeleteSamplers(1,&typed);
    glDeleteFramebuffersEXT(1,&fb);glDeleteTextures(2,textures);
    glDeleteProgram(p);glDeleteShader(v);glDeleteShader(z);
}

int main(void)
{
#ifdef VIM_SAMPLER_WIRE
    unsigned char screen[16*16*4]={0};
    const int attrs[]={0x56494d02,0x3200,0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK((c->connection->features&(DG_FEATURE_SAMPLERS|DG_FEATURE_TEXTURE_BUFFER))==
          (DG_FEATURE_SAMPLERS|DG_FEATURE_TEXTURE_BUFFER));
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("sampler objects renderer=%s\n",glGetString(GL_RENDERER));
    verify();
#ifdef VIM_SAMPLER_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("sampler object failures=%d\n",failures);return failures?1:0;
}
