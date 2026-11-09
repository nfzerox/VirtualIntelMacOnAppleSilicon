/* Verify native query lifecycle and exact 64-bit result transport. */
#ifdef VIM_TIMER_QUERY_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
extern void glQueryCounter(GLuint,GLenum);
extern void glGetQueryObjecti64v(GLuint,GLenum,GLint64 *);
extern void glGetQueryObjectui64v(GLuint,GLenum,GLuint64 *);
#endif
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define TIMER_TIMESTAMP 0x8e28
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
typedef void (*GetSigned)(GLuint,GLenum,GLint64EXT *);
typedef void (*GetUnsigned)(GLuint,GLenum,GLuint64EXT *);

static void run(int core)
{
    GetSigned get_signed=glGetQueryObjecti64vEXT;
    GetUnsigned get_unsigned=glGetQueryObjectui64vEXT;
#ifndef VIM_TIMER_QUERY_WIRE
    if(core){get_signed=glGetQueryObjecti64v;get_unsigned=glGetQueryObjectui64v;}
#endif
    GLuint texture=0,fbo=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    GLint elapsed_bits=-1;
    glGetQueryiv(GL_TIME_ELAPSED_EXT,GL_QUERY_COUNTER_BITS,&elapsed_bits);
    CHECK(glGetError()==GL_NO_ERROR && (elapsed_bits==0 || (elapsed_bits>=30 && elapsed_bits<=64)));
    GLuint q[3]={0};
    glGenQueries(3,q);
    CHECK(glGetError()==GL_NO_ERROR && q[0] && q[1] && q[2]);
    glBeginQuery(GL_TIME_ELAPSED_EXT,q[0]);
    glClear(GL_COLOR_BUFFER_BIT);
    glEndQuery(GL_TIME_ELAPSED_EXT);
    CHECK(glGetError()==GL_NO_ERROR);

    struct { uint64_t pre;GLuint64EXT value;uint64_t post; } u={0x1234,UINT64_MAX,0xabcd};
    struct { uint64_t pre;GLint64EXT value;uint64_t post; } s={0x1234,-1,0xabcd};
    get_unsigned(q[0],GL_QUERY_RESULT_AVAILABLE,&u.value);
    CHECK(glGetError()==GL_NO_ERROR && u.value<=1);
    get_signed(q[0],GL_QUERY_RESULT,&s.value);
    get_unsigned(q[0],GL_QUERY_RESULT,&u.value);
    CHECK(glGetError()==GL_NO_ERROR && s.value>=0 && u.value==(uint64_t)s.value);
    if(!elapsed_bits)CHECK(u.value==0);
    printf("timer elapsed bits=%d\n",elapsed_bits);
    get_unsigned(q[0],GL_QUERY_RESULT_AVAILABLE,&u.value);
    CHECK(glGetError()==GL_NO_ERROR && u.value==1);

    if(core) {
        GLint bits=-1;glGetQueryiv(TIMER_TIMESTAMP,GL_QUERY_COUNTER_BITS,&bits);
        GLenum bits_error=glGetError();
        GLint64 now=-1;glGetInteger64v(TIMER_TIMESTAMP,&now);
        CHECK(glGetError()==GL_NO_ERROR && now>=0);
        glQueryCounter(q[1],TIMER_TIMESTAMP);
        glClear(GL_COLOR_BUFFER_BIT);
        glQueryCounter(q[2],TIMER_TIMESTAMP);
        GLenum counter_error=glGetError();
        GLuint64EXT start=UINT64_MAX,end=UINT64_MAX;GLint64EXT also=-1;
        get_unsigned(q[1],GL_QUERY_RESULT,&start);
        get_unsigned(q[2],GL_QUERY_RESULT,&end);
        get_signed(q[2],GL_QUERY_RESULT,&also);
        CHECK(glGetError()==GL_NO_ERROR && end>=start && also>=0 && end==(uint64_t)also);
        printf("timer counter bits=%d bits-error=%04x counter-error=%04x upper-bits=%s\n",
               bits,bits_error,counter_error,(end>>32)?"present":"zero");
        CHECK(bits_error==GL_NO_ERROR && (bits==0 || (bits>=30 && bits<=64)) &&
              counter_error==GL_NO_ERROR);
        if(!bits)CHECK(start==0 && end==0);
    }
    u.value=UINT64_C(0xfedcba9876543210);s.value=-1234567890123LL;
    get_unsigned(q[0],GL_TEXTURE_2D,&u.value);
    CHECK(glGetError()==GL_INVALID_ENUM && u.value==UINT64_C(0xfedcba9876543210));
    get_signed(0,GL_QUERY_RESULT,&s.value);
    CHECK(glGetError()==GL_INVALID_OPERATION && s.value==-1234567890123LL);
    CHECK(u.pre==0x1234 && u.post==0xabcd && s.pre==0x1234 && s.post==0xabcd);
    glDeleteQueries(3,q);CHECK(glGetError()==GL_NO_ERROR);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
}

int main(int argc,char **argv)
{
    int force_legacy=argc==2 && !strcmp(argv[1],"legacy-force");
    int core=argc!=2 || (strcmp(argv[1],"legacy") && !force_legacy);
#ifdef VIM_TIMER_QUERY_WIRE
    unsigned char screen[16*16*4]={0};
    const int attrs[]={0x56494d02,core?0x3200:0x1000,0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features&DG_FEATURE_TIMER_QUERY);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
    const char *extensions=core?NULL:(const char *)glGetString(GL_EXTENSIONS);
    if(!core && !force_legacy && (!extensions || !strstr(extensions,"GL_EXT_timer_query"))) {
        puts("timer query legacy not exposed");return 77;
    }
#endif
    printf("timer query renderer=%s core=%d version=%s\n",glGetString(GL_RENDERER),core,glGetString(GL_VERSION));
    run(core);
#ifdef VIM_TIMER_QUERY_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("timer query failures=%d\n",failures);return failures?1:0;
}
