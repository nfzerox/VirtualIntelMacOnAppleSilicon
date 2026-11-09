/* Native ARB_internalformat_query capacity and result-size baseline. */
#ifdef VIM_INTERNAL_FORMAT_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define GL_NUM_SAMPLE_COUNTS 0x9380
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9102
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#endif
#include <stdio.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static void report(GLenum target,GLenum format,int required)
{
    GLint count[2]={-1,37};
    glGetInternalformativ(target,format,GL_NUM_SAMPLE_COUNTS,1,count);
    GLenum status=glGetError();
    printf("internal format target=%x format=%x count=%d error=%x\n",target,format,count[0],status);
    CHECK(count[1]==37);
    if(!required && status)return;
    CHECK(status==GL_NO_ERROR && count[0]>=0 && count[0]<=32);
    if(status || count[0]<0 || count[0]>32)return;
    GLint values[33];
    for(unsigned i=0;i<33;++i)values[i]=37;
    glGetInternalformativ(target,format,GL_SAMPLES,count[0],values);
    CHECK(glGetError()==GL_NO_ERROR && values[count[0]]==37);
    if(count[0]>0){
        GLint prefix[2]={-1,37};
        glGetInternalformativ(target,format,GL_SAMPLES,1,prefix);
        CHECK(glGetError()==GL_NO_ERROR && prefix[0]==values[0] && prefix[1]==37);
    }
    for(GLint i=0;i<count[0];++i){
        printf("sample %d=%d\n",i,values[i]);
        CHECK(values[i]>0 && (i==0 || values[i]<values[i-1]));
    }
    GLint ignored=37;
    glGetInternalformativ(target,format,GL_SAMPLES,0,&ignored);
    CHECK(glGetError()==GL_NO_ERROR && ignored==37);
    glGetInternalformativ(target,format,GL_SAMPLES,-1,&ignored);
    CHECK(glGetError()==GL_INVALID_VALUE && ignored==37);
}
int main(void)
{
#ifdef VIM_INTERNAL_FORMAT_WIRE
    const int attrs[]={0x56494d02,DG_CGL_PROFILE_GL4,0};unsigned char screen[16*16*4]={0};
    struct DGContext *context=DGCreateContext(attrs,NULL);
    if(!context || !DGMakeCurrent(context,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(context->connection->features2&DG_FEATURE2_INTERNAL_FORMAT);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint formats=0;
    if(CGLChoosePixelFormat(attrs,&format,&formats)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
#endif
    printf("internal format renderer=%s\n",glGetString(GL_RENDERER));
    report(GL_RENDERBUFFER,GL_RGBA8,1);
    report(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,1);
    report(GL_TEXTURE_2D_MULTISAMPLE,GL_RGBA8,0);
    report(GL_TEXTURE_2D_MULTISAMPLE_ARRAY,GL_RGBA8,0);
#ifdef VIM_INTERNAL_FORMAT_WIRE
    CHECK(!context->connection->dead);DGDestroyContext(context);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
#endif
    printf("internal format query failures=%d\n",failures);return failures?1:0;
}
