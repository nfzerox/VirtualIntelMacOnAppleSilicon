/* Native and wire ARB_vertex_attrib_64bit: values that collapse in float. */
#ifdef VIM_FP64_VERTEX_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGetVertexAttribiv glGetVertexAttribivARB
#define glEnableVertexAttribArray glEnableVertexAttribArrayARB
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define GL_CURRENT_VERTEX_ATTRIB 0x8626
#define GL_VERTEX_ATTRIB_ARRAY_TYPE 0x8625
#define GL_VERTEX_ATTRIB_ARRAY_SIZE 0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE 0x8624
#define GL_VERTEX_ATTRIB_ARRAY_INTEGER 0x88fd
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static GLuint shader(GLenum stage,const char *source)
{
    GLuint s=glCreateShader(stage);GLint good=0;
    glShaderSource(s,1,&source,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&good);
    if(!good){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(good);return s;
}
static void verify(void)
{
    const GLdouble value[]={0x1.0000000001p+0,0x1.23456789abcdp+4,-0x1.abcde0123456p-7,0x1.000000001p-2};
    typedef void (*Vector)(GLuint,const GLdouble *);
    const Vector vector[]={glVertexAttribL1dv,glVertexAttribL2dv,glVertexAttribL3dv,glVertexAttribL4dv};
    size_t page=(size_t)getpagesize();
    char *guard=mmap(NULL,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(guard!=MAP_FAILED);
    if(guard==MAP_FAILED)return;
    if(mprotect(guard+page,page,PROT_NONE)){++failures;munmap(guard,2*page);return;}
    for(unsigned n=1;n<=4;++n){
        GLdouble scalar[5]={-1,-1,-1,-1,37},from_vector[5]={-1,-1,-1,-1,37};
        if(n==1)glVertexAttribL1d(1,value[0]);
        if(n==2)glVertexAttribL2d(1,value[0],value[1]);
        if(n==3)glVertexAttribL3d(1,value[0],value[1],value[2]);
        if(n==4)glVertexAttribL4d(1,value[0],value[1],value[2],value[3]);
        glGetVertexAttribLdv(1,GL_CURRENT_VERTEX_ATTRIB,scalar);
        GLdouble *bounded=(GLdouble *)(guard+page-n*sizeof(GLdouble));
        memcpy(bounded,value,n*sizeof(GLdouble));vector[n-1](1,bounded);
        glGetVertexAttribLdv(1,GL_CURRENT_VERTEX_ATTRIB,from_vector);
        CHECK(glGetError()==GL_NO_ERROR && !memcmp(scalar,from_vector,n*sizeof(GLdouble)) &&
              scalar[4]==37 && from_vector[4]==37);
        CHECK(!memcmp(scalar,value,n*sizeof(GLdouble)));
        printf("FP64 current %u=%a,%a,%a,%a\n",n,scalar[0],scalar[1],scalar[2],scalar[3]);
    }
    munmap(guard,2*page);
    GLuint vao=0,fb=0,tex=0,buf=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    GLuint p=glCreateProgram();
    glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 410\nlayout(location=1) in dvec4 preciseValue;"
        "out float shade;void main(){shade=preciseValue.x>double(1)?1:0;"
        "gl_Position=vec4(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1,0,1);}"));
    glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 410\nin float shade;out vec4 color;"
        "void main(){color=vec4(shade,1-shade,0,1);}"));
    glLinkProgram(p);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);CHECK(linked);
    if(!linked)return;
    glUseProgram(p);glViewport(0,0,2,2);
    glDrawArrays(GL_TRIANGLES,0,3);
    GLubyte rgba[4]={0},red[]={255,0,0,255},green[]={0,255,0,255};
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    printf("FP64 current pixel=%u,%u,%u,%u\n",rgba[0],rgba[1],rgba[2],rgba[3]);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(rgba,red,sizeof(rgba)));
    GLdouble stream[15]={0};
    for(unsigned i=0;i<3;++i)stream[5*i+1]=0x1.fffffffffep-1;
    glGenBuffers(1,&buf);glBindBuffer(GL_ARRAY_BUFFER,buf);
    glBufferData(GL_ARRAY_BUFFER,sizeof(stream),stream,GL_STATIC_DRAW);
    glVertexAttribLPointer(1,4,GL_DOUBLE,5*sizeof(GLdouble),(const void *)sizeof(GLdouble));
    GLint kind=0;glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_TYPE,&kind);
    CHECK(glGetError()==GL_NO_ERROR && kind==GL_DOUBLE);
    const struct { GLenum key; GLdouble expected; } queries[]={
        {GL_VERTEX_ATTRIB_ARRAY_TYPE,GL_DOUBLE},{GL_VERTEX_ATTRIB_ARRAY_SIZE,4},
        {GL_VERTEX_ATTRIB_ARRAY_STRIDE,5*sizeof(GLdouble)},{GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,buf},
        {GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,GL_FALSE},{GL_VERTEX_ATTRIB_ARRAY_INTEGER,GL_FALSE},
    };
    for(unsigned i=0;i<sizeof(queries)/sizeof(queries[0]);++i){
        GLdouble result[]={-1,37};
        glGetVertexAttribLdv(1,queries[i].key,result);
        GLenum error=glGetError();
        printf("FP64 query %04x = %g (%04x)\n",queries[i].key,result[0],error);
        CHECK(error==GL_NO_ERROR && result[0]==queries[i].expected && result[1]==37);
    }
    glVertexAttribLPointer(1,4,GL_FLOAT,5*sizeof(GLdouble),(const void *)sizeof(GLdouble));
    CHECK(glGetError()==GL_INVALID_ENUM);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES,0,3);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    printf("FP64 buffer pixel=%u,%u,%u,%u\n",rgba[0],rgba[1],rgba[2],rgba[3]);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(rgba,green,sizeof(rgba)));
}
int main(void)
{
#ifdef VIM_FP64_VERTEX_WIRE
    const int attrs[]={0x56494d02,DG_CGL_PROFILE_GL4,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_FP64_VERTEX);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("FP64 vertex renderer=%s\n",glGetString(GL_RENDERER));verify();
#ifdef VIM_FP64_VERTEX_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("FP64 vertex failures=%d\n",failures);return failures?1:0;
}
