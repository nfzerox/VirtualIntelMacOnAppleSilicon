/* Scalar and pointer forms of current packed vertex attributes. */
#ifdef VIM_PACKED_ATTRIBS_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGetVertexAttribfv glGetVertexAttribfvARB
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define GL_CURRENT_VERTEX_ATTRIB 0x8626
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#define GL_SAMPLE_SHADING 0x8c36
#define GL_MIN_SAMPLE_SHADING_VALUE 0x8c37
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static GLuint shader(GLenum stage,const char *text)
{
    GLuint s=glCreateShader(stage);GLint compiled=0;
    glShaderSource(s,1,&text,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&compiled);
    CHECK(compiled);return s;
}
static void verify(void)
{
    typedef void (*Scalar)(GLuint,GLenum,GLboolean,GLuint);
    typedef void (*Pointer)(GLuint,GLenum,GLboolean,const GLuint *);
    const Scalar scalar[]={glVertexAttribP1ui,glVertexAttribP2ui,glVertexAttribP3ui,glVertexAttribP4ui};
    const Pointer pointer[]={glVertexAttribP1uiv,glVertexAttribP2uiv,glVertexAttribP3uiv,glVertexAttribP4uiv};
    const GLenum packed=GL_UNSIGNED_INT_2_10_10_10_REV;
    size_t page=(size_t)getpagesize();
    char *memory=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(memory==MAP_FAILED){++failures;return;}
    if(mprotect(memory+page,page,PROT_NONE)){++failures;munmap(memory,page*2);return;}
    GLuint *one=(GLuint *)(memory+page-sizeof(GLuint));
    *one=(2u<<30)|(511u<<20)|(29u<<10)|1023u;
    for(unsigned n=0;n<4;++n){
        GLfloat a[5]={-1,-1,-1,-1,37},b[5]={-1,-1,-1,-1,37};
        scalar[n](1,packed,GL_TRUE,*one);
        glGetVertexAttribfv(1,GL_CURRENT_VERTEX_ATTRIB,a);
        pointer[n](2,packed,GL_TRUE,one);
        glGetVertexAttribfv(2,GL_CURRENT_VERTEX_ATTRIB,b);
        printf("packed %u = %g,%g,%g,%g\n",n+1,a[0],a[1],a[2],a[3]);
        CHECK(glGetError()==GL_NO_ERROR && !memcmp(a,b,sizeof(a)) && a[0]==1 && a[4]==37);
        CHECK(fabsf(a[1]-(n>=1?29.0f/1023:0))<1e-6f &&
              fabsf(a[2]-(n>=2?511.0f/1023:0))<1e-6f &&
              fabsf(a[3]-(n>=3?2.0f/3:1))<1e-6f);
    }
    munmap(memory,page*2);
    glVertexAttribP4ui(1,packed,GL_TRUE,(3u<<30)|1023u);
    GLuint p=glCreateProgram();
    glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 410\nlayout(location=1) in vec4 tint;out vec4 paint;"
        "void main(){paint=tint;gl_Position=vec4(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1,0,1);}"));
    glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 410\nin vec4 paint;out vec4 color;void main(){color=paint;}"));
    glLinkProgram(p);GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);CHECK(linked);
    if(!linked)return;
    GLuint vao=0,fb=0,tex=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glUseProgram(p);glViewport(0,0,2,2);glDrawArrays(GL_TRIANGLES,0,3);
    GLubyte rgba[4]={0},red[]={255,0,0,255};
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(rgba,red,sizeof(rgba)));

    GLfloat minimum[]={-1,37};GLboolean enabled[]={GL_FALSE,37};
    glMinSampleShading(1.25f);glEnable(GL_SAMPLE_SHADING);
    glGetFloatv(GL_MIN_SAMPLE_SHADING_VALUE,minimum);glGetBooleanv(GL_SAMPLE_SHADING,enabled);
    CHECK(glGetError()==GL_NO_ERROR && minimum[0]==1 && minimum[1]==37 && enabled[0] && enabled[1]==37);
    glMinSampleShading(-.125f);glDisable(GL_SAMPLE_SHADING);
    glGetFloatv(GL_MIN_SAMPLE_SHADING_VALUE,minimum);glGetBooleanv(GL_SAMPLE_SHADING,enabled);
    CHECK(glGetError()==GL_NO_ERROR && minimum[0]==0 && minimum[1]==37 && !enabled[0] && enabled[1]==37);
}
int main(void)
{
#ifdef VIM_PACKED_ATTRIBS_WIRE
    const int attrs[]={0x56494d02,DG_CGL_PROFILE_GL4,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK((c->connection->features2&(DG_FEATURE2_PACKED_ATTRIBS|DG_FEATURE2_SAMPLE_SHADING))==
          (DG_FEATURE2_PACKED_ATTRIBS|DG_FEATURE2_SAMPLE_SHADING));
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("packed attributes renderer=%s\n",glGetString(GL_RENDERER));verify();
#ifdef VIM_PACKED_ATTRIBS_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("packed attribute/sample shading failures=%d\n",failures);return failures?1:0;
}
