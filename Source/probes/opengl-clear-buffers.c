/* Match actual framebuffer contents after all four typed clear calls. */
#ifdef VIM_CLEAR_BUFFER_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
extern void glClearBufferiv(GLenum,GLint,const GLint *);
extern void glClearBufferuiv(GLenum,GLint,const GLuint *);
extern void glClearBufferfv(GLenum,GLint,const GLfloat *);
extern void glClearBufferfi(GLenum,GLint,GLfloat,GLint);
#endif
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
#define DEPTH_STENCIL_ATTACHMENT 0x821a

int main(void)
{
#ifdef VIM_CLEAR_BUFFER_WIRE
    unsigned char screen[16*16*4]={0};
    const int attrs[]={0x56494d02,0x3200,0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("clear buffer renderer=%s\n",renderer);
    GLuint fb=0,t[4]={0};
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glGenTextures(4,t);
    const GLenum format[]={GL_RGBA_INTEGER_EXT,GL_RGBA_INTEGER_EXT,GL_RGBA,GL_DEPTH_STENCIL};
    const GLenum type[]={GL_INT,GL_UNSIGNED_INT,GL_FLOAT,GL_UNSIGNED_INT_24_8};
    const GLint internal[]={GL_RGBA32I_EXT,GL_RGBA32UI_EXT,GL_RGBA32F_ARB,GL_DEPTH24_STENCIL8};
    const GLint signed_value[]={-7,12345,-10000003,19};
    const GLuint unsigned_value[]={0xfedcba98u,1,0x80000001u,0xffffffffu};
    const GLfloat float_value[]={0.125f,0.25f,0.5f,1.0f};
    const void *values[]={signed_value,unsigned_value,float_value};
    for(unsigned i=0;i<4;i++){
        glBindTexture(GL_TEXTURE_2D,t[i]);
        glTexImage2D(GL_TEXTURE_2D,0,internal[i],1,1,0,format[i],type[i],NULL);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
            i==3?DEPTH_STENCIL_ATTACHMENT:GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,t[i],0);
        CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
        CHECK(glGetError()==GL_NO_ERROR);
        if(i==0)glClearBufferiv(GL_COLOR,0,signed_value);
        if(i==1)glClearBufferuiv(GL_COLOR,0,unsigned_value);
        if(i==2)glClearBufferfv(GL_COLOR,0,float_value);
        if(i==3)glClearBufferfi(GL_DEPTH_STENCIL,0,.5f,0xa5);
        GLuint actual[4]={0};
        glGetTexImage(GL_TEXTURE_2D,0,format[i],type[i],actual);
        CHECK(glGetError()==GL_NO_ERROR);
        if(i<3)CHECK(!memcmp(actual,values[i],sizeof(actual)));
        else {
            printf("packed depth-stencil combined=%08x\n",actual[0]);
            CHECK((actual[0]&255)==0xa5 && (actual[0]>>8)>=0x7fffff && (actual[0]>>8)<=0x800000);
            GLint stencil=0x4a;GLfloat depth=.25f;
            glClearBufferiv(GL_STENCIL,0,&stencil);
            glGetTexImage(GL_TEXTURE_2D,0,format[i],type[i],actual);
            CHECK(glGetError()==GL_NO_ERROR);
            printf("packed depth-stencil after stencil=%08x\n",actual[0]);
            glClearBufferfv(GL_DEPTH,0,&depth);
            glGetTexImage(GL_TEXTURE_2D,0,format[i],type[i],actual);
            CHECK(glGetError()==GL_NO_ERROR);
            printf("packed depth-stencil separate=%08x\n",actual[0]);
            // The physical High Sierra Iris Pro reports 0 after this
            // particular separate clear sequence on a packed attachment.
            // Report the exact word; don't synthesize Intel output on M4.
            unsigned low=actual[0]&255;
            CHECK((low==0x4a || (low==0 && strstr(renderer,"Intel Iris Pro OpenGL Engine"))) &&
                  (actual[0]>>8)>=0x3fffff && (actual[0]>>8)<=0x400000);
        }
    }
    glClearBufferuiv(GL_TEXTURE_2D,0,unsigned_value);CHECK(glGetError()==GL_INVALID_ENUM);
    glClearBufferfv(GL_COLOR,-1,float_value);CHECK(glGetError()==GL_INVALID_VALUE);
    glClearBufferfi(GL_DEPTH_STENCIL,1,.5f,0);CHECK(glGetError()==GL_INVALID_VALUE);
    glDeleteFramebuffersEXT(1,&fb);glDeleteTextures(4,t);
#ifdef VIM_CLEAR_BUFFER_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("clear buffer failures=%d\n",failures);return failures?1:0;
}
