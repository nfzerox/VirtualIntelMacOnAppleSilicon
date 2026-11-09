/* Isolate indexed scissor zero and layered clears from viewport CTS.
 * Only the normalized and explicitly typed integer clears are normative. */
#ifdef VIM_LAYERED_CLEAR_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture glFramebufferTextureEXT
#define glFramebufferTextureLayer glFramebufferTextureLayerEXT
#define glEnablei glEnableIndexedEXT
#define GL_TEXTURE_2D_ARRAY 0x8c1a
#define GL_R32I 0x8235
#define GL_RED_INTEGER 0x8d94
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#endif
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static void verify(int scissor,int separate_layers)
{
    GLuint fbo=0,tex[2]={0};glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenTextures(2,tex);
    if(scissor==1){glScissor(0,0,1,1);glEnable(GL_SCISSOR_TEST);}
    if(scissor==2){
        glScissorIndexed(0,0,0,1,1);glScissorIndexed(1,1,1,1,1);
        glEnablei(GL_SCISSOR_TEST,0);glEnablei(GL_SCISSOR_TEST,1);
    }
    const GLenum target=GL_TEXTURE_2D_ARRAY;
    GLint integers[]={-1,-1,-1,-1,-1,-1,-1,-1};
    glBindTexture(target,tex[0]);glTexStorage3D(target,1,GL_R32I,2,2,2);
    glTexSubImage3D(target,0,0,0,0,2,2,2,GL_RED_INTEGER,GL_INT,integers);
    glFramebufferTexture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,tex[0],0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE && glGetError()==GL_NO_ERROR);
    GLint actual[9]={0,0,0,0,0,0,0,0,37};
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    GLenum error=glGetError();
    glGetTexImage(target,0,GL_RED_INTEGER,GL_INT,actual);
    printf("untyped integer clear: first pixel of each layer=%d,%d clear_error=%x read_error=%x\n",
           actual[0],actual[4],error,glGetError());
    glTexSubImage3D(target,0,0,0,0,2,2,2,GL_RED_INTEGER,GL_INT,integers);
    const GLint value[]={-7,0,0,0};
    if(separate_layers){
        for(GLint layer=0;layer<2;++layer){
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,tex[0],0,layer);
            glClearBufferiv(GL_COLOR,0,value);
        }
        glFramebufferTexture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,tex[0],0);
    }else glClearBufferiv(GL_COLOR,0,value);
    glGetTexImage(target,0,GL_RED_INTEGER,GL_INT,actual);
    CHECK(glGetError()==GL_NO_ERROR && actual[8]==37);
    printf("typed integer clear: %d %d %d %d / %d %d %d %d\n",
           actual[0],actual[1],actual[2],actual[3],actual[4],actual[5],actual[6],actual[7]);
    for(unsigned i=0;i<8;++i)CHECK(actual[i]==(!scissor || i%4==0?-7:-1));

    GLubyte zero[32]={0},colors[32]={0};
    glBindTexture(target,tex[1]);glTexStorage3D(target,1,GL_RGBA8,2,2,2);
    glTexSubImage3D(target,0,0,0,0,2,2,2,GL_RGBA,GL_UNSIGNED_BYTE,zero);
    glFramebufferTexture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,tex[1],0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE && glGetError()==GL_NO_ERROR);
    glClearColor(1,0,1,1);
    if(separate_layers){
        for(GLint layer=0;layer<2;++layer){
            glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,tex[1],0,layer);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glFramebufferTexture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,tex[1],0);
    }else glClear(GL_COLOR_BUFFER_BIT);
    glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,colors);
    CHECK(glGetError()==GL_NO_ERROR);
    printf("normalized clear: first red byte of each layer=%u,%u\n",colors[0],colors[16]);
    for(unsigned i=0;i<32;++i)CHECK(colors[i]==((!scissor || i%16<4) && i%4!=1?255:0));
}

int main(int argc,char **argv)
{
#ifdef VIM_LAYERED_CLEAR_WIRE
    const int attrs[]={0x56494d02,DG_CGL_PROFILE_GL4,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    int scissor=2,separate_layers=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--unscissored"))scissor=0;
        if(!strcmp(argv[i],"--single-scissor"))scissor=1;
        if(!strcmp(argv[i],"--per-layer"))separate_layers=1;
    }
    printf("layered clear renderer=%s scissor=%d per-layer=%d\n",glGetString(GL_RENDERER),scissor,separate_layers);
    verify(scissor,separate_layers);
#ifdef VIM_LAYERED_CLEAR_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("layered clear failures=%d\n",failures);return failures?1:0;
}
