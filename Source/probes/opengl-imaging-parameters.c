/* Compare imaging parameter values, write counts and errors with native GL. */
#ifdef VIM_IMAGING_PARAMETERS_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

#define QUERY(stem,target,key,count) do { \
    GLint i[5]={-999,-999,-999,-999,-999}; \
    GLfloat f[5]={-999,-999,-999,-999,-999}; \
    glGet##stem##Parameteriv(target,key,i); GLenum ei=glGetError(); \
    glGet##stem##Parameterfv(target,key,f); GLenum ef=glGetError(); \
    CHECK(ei==GL_NO_ERROR && ef==GL_NO_ERROR); \
    CHECK(i[count]==-999 && f[count]==-999); \
    printf(#stem " " #key ":"); \
    for(unsigned j=0;j<count;++j){CHECK(i[j]!=-999 && f[j]!=-999);printf(" %d/%g",i[j],(double)f[j]);} \
    putchar('\n'); \
} while(0)

int main(void)
{
#ifdef VIM_IMAGING_PARAMETERS_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *ctx=DGCreateContext(attrs,NULL);
    if(!ctx || !DGMakeCurrent(ctx,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    CGLDestroyPixelFormat(pf);
#endif

    const GLfloat vector[]={0.25f,0.5f,0.75f,1};
    const GLint integers[]={1,0,1,0},mode=GL_REPLICATE_BORDER;
    glColorTableParameterfv(GL_COLOR_TABLE,GL_COLOR_TABLE_SCALE,vector);
    CHECK(glGetError()==GL_NO_ERROR);
    glColorTableParameteriv(GL_COLOR_TABLE,GL_COLOR_TABLE_BIAS,integers);
    CHECK(glGetError()==GL_NO_ERROR);
    QUERY(ColorTable,GL_COLOR_TABLE,GL_COLOR_TABLE_SCALE,4);
    QUERY(ColorTable,GL_COLOR_TABLE,GL_COLOR_TABLE_BIAS,4);
    QUERY(ColorTable,GL_PROXY_COLOR_TABLE,GL_COLOR_TABLE_WIDTH,1);
    glConvolutionParameterfv(GL_CONVOLUTION_2D,GL_CONVOLUTION_FILTER_SCALE,vector);
    CHECK(glGetError()==GL_NO_ERROR);
    glConvolutionParameteriv(GL_CONVOLUTION_2D,GL_CONVOLUTION_BORDER_COLOR,integers);
    CHECK(glGetError()==GL_NO_ERROR);
    glConvolutionParameteriv(GL_CONVOLUTION_2D,GL_CONVOLUTION_BORDER_MODE,&mode);
    CHECK(glGetError()==GL_NO_ERROR);
    QUERY(Convolution,GL_CONVOLUTION_2D,GL_CONVOLUTION_FILTER_SCALE,4);
    QUERY(Convolution,GL_CONVOLUTION_2D,GL_CONVOLUTION_BORDER_COLOR,4);
    QUERY(Convolution,GL_CONVOLUTION_2D,GL_CONVOLUTION_BORDER_MODE,1);
    QUERY(Convolution,GL_SEPARABLE_2D,GL_CONVOLUTION_HEIGHT,1);
    glHistogram(GL_HISTOGRAM,4,GL_RGBA,GL_TRUE);
    CHECK(glGetError()==GL_NO_ERROR);
    QUERY(Histogram,GL_HISTOGRAM,GL_HISTOGRAM_WIDTH,1);
    QUERY(Histogram,GL_HISTOGRAM,GL_HISTOGRAM_SINK,1);
    glMinmax(GL_MINMAX,GL_RGB,GL_TRUE);
    CHECK(glGetError()==GL_NO_ERROR);
    QUERY(Minmax,GL_MINMAX,GL_MINMAX_FORMAT,1);
    QUERY(Minmax,GL_MINMAX,GL_MINMAX_SINK,1);

    GLint untouched[]={-999,-999};
    glGetHistogramParameteriv(GL_HISTOGRAM,GL_MINMAX_FORMAT,untouched);
    CHECK(glGetError()==GL_INVALID_ENUM && untouched[0]==-999 && untouched[1]==-999);
    glGetColorTableParameteriv(GL_HISTOGRAM,GL_COLOR_TABLE_WIDTH,untouched);
    CHECK(glGetError()==GL_INVALID_ENUM && untouched[0]==-999 && untouched[1]==-999);
    GLuint fb=0,rb=0;
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glBegin(GL_POINTS);
    glGetMinmaxParameteriv(GL_MINMAX,GL_MINMAX_FORMAT,untouched);
    glEnd();
    CHECK(glGetError()==GL_INVALID_OPERATION && untouched[0]==-999 && untouched[1]==-999);
#ifdef VIM_IMAGING_PARAMETERS_WIRE
    DGDestroyContext(ctx);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);
#endif
    return failures!=0;
}
