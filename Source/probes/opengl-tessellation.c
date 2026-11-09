/* Native and bridged tessellation with a real fragment result. Changing the
 * default outer level to zero must cull the patch, not just update a query. */
#ifdef VIM_TESSELLATION_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { patches=0x000e,patch_vertices=0x8e72,inner_level=0x8e73,
       outer_level=0x8e74,evaluation_shader=0x8e87 };
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);GLint ok=0;
    glShaderSource(s,1,&source,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(ok);return s;
}
static void draw(GLubyte green)
{
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(patches,0,3);
    GLubyte p[4]={0};glReadPixels(7,7,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    CHECK(glGetError()==GL_NO_ERROR && p[0]==0 && p[1]==green && p[2]==0 && p[3]==255);
}
static void verify(void)
{
    GLuint vao=0,fb=0,image=0,p=glCreateProgram();
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&image);glBindTexture(GL_TEXTURE_2D,image);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,image,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,16,16);
    glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 410\n"
        "void main(){vec2 a[3]=vec2[3](vec2(-1,-1),vec2(1,-1),vec2(0,1));"
        "gl_Position=vec4(a[gl_VertexID],0,1);}"));
    glAttachShader(p,shader(evaluation_shader,"#version 410\nlayout(triangles,equal_spacing,ccw) in;"
        "void main(){gl_Position=gl_TessCoord.x*gl_in[0].gl_Position+"
        "gl_TessCoord.y*gl_in[1].gl_Position+gl_TessCoord.z*gl_in[2].gl_Position;}"));
    glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 410\nout vec4 color;void main(){color=vec4(0,1,0,1);}"));
    glLinkProgram(p);GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);
    if(!linked){char log[2048]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(linked);if(!linked)return;
    glUseProgram(p);

    long page=sysconf(_SC_PAGESIZE);
    if(page<1){CHECK(0);return;}
    unsigned char *mem=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(mem==MAP_FAILED){CHECK(0);return;}
    if(mprotect(mem+page,(size_t)page,PROT_NONE)){CHECK(0);munmap(mem,(size_t)page*2);return;}
    GLfloat *inner=(GLfloat *)(mem+page)-2;
    inner[0]=1;inner[1]=2;
    GLfloat outer[4]={1,2,3,4},query[5]={-1,-1,-1,-1,37};
    glPatchParameteri(patch_vertices,3);
    glPatchParameterfv(inner_level,inner);
    glPatchParameterfv(outer_level,outer);
    GLint vertices=-1,maximum[2]={-1,37};glGetIntegerv(patch_vertices,&vertices);
    glGetIntegerv(0x8e7d,maximum); /* GL_MAX_PATCH_VERTICES */
    glGetFloatv(inner_level,query);
    CHECK(glGetError()==GL_NO_ERROR && vertices==3 && maximum[0]>=32 && maximum[1]==37 &&
          query[0]==1 && query[1]==2 && query[2]==-1);
    glGetFloatv(outer_level,query);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(query,outer,sizeof(outer)) && query[4]==37);
    glPatchParameteri(patch_vertices,0);CHECK(glGetError()==GL_INVALID_VALUE);
    glPatchParameterfv(patch_vertices,inner);CHECK(glGetError()==GL_INVALID_ENUM);
    draw(255);
    outer[0]=0;glPatchParameterfv(outer_level,outer);draw(0);
    outer[0]=1;glPatchParameterfv(outer_level,outer);draw(255);
    munmap(mem,(size_t)page*2);
}

int main(void)
{
#ifdef VIM_TESSELLATION_WIRE
    const int attrs[]={0x56494d02,DG_CGL_PROFILE_GL4,0};
    unsigned char screen[16*16*4]={0};struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    uint32_t features=c->connection->features2;
    CHECK(features&DG_FEATURE2_TESSELLATION);
    c->connection->features2=0;
    glPatchParameteri(patch_vertices,3);
    CHECK(glGetError()==GL_INVALID_OPERATION && !c->connection->dead);
    c->connection->features2=features;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("tessellation renderer=%s\n",glGetString(GL_RENDERER));verify();
#ifdef VIM_TESSELLATION_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("tessellation failures=%d\n",failures);return failures?1:0;
}
