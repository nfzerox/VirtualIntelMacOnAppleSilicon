/* Compare native and bridged ARB_vertex_blend. */
#ifdef VIM_VERTEX_BLEND_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static void pixels(const char *mode,int left_expected,int right_expected)
{
    GLubyte left[4]={0},right[4]={0};
    glReadPixels(3,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left);
    glReadPixels(11,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
    GLenum error=glGetError();
    printf("blend %s red=%u,%u error=%04x\n",mode,left[0],right[0],error);
    CHECK(!error && left[0]==left_expected && right[0]==right_expected);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
#ifdef VIM_VERTEX_BLEND_WIRE
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
    const char *ext=(const char *)glGetString(GL_EXTENSIONS);
    printf("blend renderer=%s\n",glGetString(GL_RENDERER));
    if(!ext || !strstr(ext,"GL_ARB_vertex_blend"))return 77;
    GLint units=0,active=0;
    glGetIntegerv(GL_MAX_VERTEX_UNITS_ARB,&units);glGetIntegerv(GL_ACTIVE_VERTEX_UNITS_ARB,&active);
    printf("blend max units=%d active=%d error=%04x\n",units,active,glGetError());
    if(units<2 || units>64)return 2;
    CHECK(active==1);
    const GLenum state[]={GL_WEIGHT_ARRAY_SIZE_ARB,GL_WEIGHT_ARRAY_TYPE_ARB,GL_WEIGHT_ARRAY_STRIDE_ARB,
        GL_WEIGHT_ARRAY_BUFFER_BINDING_ARB};
    const GLint expected[]={1,GL_FLOAT,0,0};
    for(unsigned i=0;i<sizeof(state)/sizeof(*state);++i){
        GLint value=-1;glGetIntegerv(state[i],&value);
        GLenum error=glGetError();
        printf("blend initial array %04x=%d error=%04x\n",state[i],value,error);
        CHECK(!error && value==expected[i]);
    }
    for(int i=0;i<2;++i){
        GLfloat weights[65];for(int j=0;j<65;++j)weights[j]=-999;
        if(i){const GLfloat input[]={0.25f,0.75f};glWeightfvARB(2,input);}
        glGetFloatv(GL_CURRENT_WEIGHT_ARB,weights);
        GLenum error=glGetError();
        CHECK(!error && weights[0]==(i?0.25f:1) && weights[1]==(i?0.75f:0) &&
            weights[units]==-999);
        printf("blend current weights error=%04x:",error);
        for(int j=0;j<65 && weights[j]!=-999;++j)printf(" %g",(double)weights[j]);
        putchar('\n');
    }
    const GLbyte wb[]={0,SCHAR_MAX};const GLshort ws[]={0,SHRT_MAX};const GLint wi[]={0,INT_MAX};
    const GLubyte wub[]={0,UCHAR_MAX};const GLushort wus[]={0,USHRT_MAX};const GLuint wui[]={0,UINT_MAX};
#define CHECK_VECTOR(suffix,data) do { GLfloat w[65]={0}; \
    glWeight##suffix##vARB(2,data);glGetFloatv(GL_CURRENT_WEIGHT_ARB,w); \
    CHECK(glGetError()==GL_NO_ERROR && w[1]==1.0f); } while(0)
    CHECK_VECTOR(b,wb);CHECK_VECTOR(s,ws);CHECK_VECTOR(i,wi);
    CHECK_VECTOR(ub,wub);CHECK_VECTOR(us,wus);CHECK_VECTOR(ui,wui);
#undef CHECK_VECTOR
    GLuint rb=0,fb=0;glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW0_ARB);glLoadIdentity();
    glMatrixMode(GL_MODELVIEW1_ARB);glLoadIdentity();glTranslatef(1,0,0);
    GLfloat second_matrix[17];for(unsigned i=0;i<17;++i)second_matrix[i]=-999;
    glGetFloatv(GL_MODELVIEW1_ARB,second_matrix);
    GLenum matrix_error=glGetError();
    printf("blend modelview1 translation=%g error=%04x\n",(double)second_matrix[12],matrix_error);
    CHECK(!matrix_error && second_matrix[12]==1 && second_matrix[16]==-999);
    glMatrixMode(GL_MODELVIEW0_ARB);
    glEnable(GL_VERTEX_BLEND_ARB);glVertexBlendARB(2);
    const GLfloat positions[]={-0.5625f,-0.5625f, -0.5625f,-0.5625f};
    GLfloat weights[]={1,0, 0,1};
    glVertexPointer(2,GL_FLOAT,0,positions);glEnableClientState(GL_VERTEX_ARRAY);
    glWeightPointerARB(2,GL_FLOAT,0,weights);glEnableClientState(GL_WEIGHT_ARRAY_ARB);
    GLint stride=-1;void *pointer=NULL;
    glGetIntegerv(GL_WEIGHT_ARRAY_STRIDE_ARB,&stride);glGetPointerv(GL_WEIGHT_ARRAY_POINTER_ARB,&pointer);
    CHECK(stride==8 && pointer==weights && glIsEnabled(GL_WEIGHT_ARRAY_ARB) && glGetError()==GL_NO_ERROR);
    glColor4ub(255,0,0,255);glDrawArrays(GL_POINTS,0,2);
    pixels("CPU array",255,255);
    glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_POINTS);glArrayElement(0);glArrayElement(1);glEnd();
    pixels("CPU ArrayElement",255,255);
    GLuint list=glGenLists(1);glNewList(list,GL_COMPILE);glDrawArrays(GL_POINTS,0,2);glEndList();
    weights[0]=0;weights[1]=1;
    glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_POINTS,0,2);pixels("changed CPU array",0,255);
    glClear(GL_COLOR_BUFFER_BIT);glCallList(list);pixels("captured display list",255,255);
    glDeleteLists(list,1);weights[0]=1;weights[1]=0;
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glDisableClientState(GL_WEIGHT_ARRAY_ARB);
    glWeightPointerARB(2,GL_FLOAT,0,weights+2);
    glPopClientAttrib();glGetPointerv(GL_WEIGHT_ARRAY_POINTER_ARB,&pointer);
    CHECK(pointer==weights && glIsEnabled(GL_WEIGHT_ARRAY_ARB) && glGetError()==GL_NO_ERROR);
    GLuint vbo=0;glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(weights),weights,GL_STATIC_DRAW);
    glWeightPointerARB(2,GL_FLOAT,0,NULL);glBindBuffer(GL_ARRAY_BUFFER,0);
    GLint binding=0;glGetIntegerv(GL_WEIGHT_ARRAY_BUFFER_BINDING_ARB,&binding);
    glGetPointerv(GL_WEIGHT_ARRAY_POINTER_ARB,&pointer);
    CHECK(binding==(GLint)vbo && !pointer && glGetError()==GL_NO_ERROR);
    glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_POINTS,0,2);pixels("VBO array",255,255);
    glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_POINTS);glArrayElement(0);glArrayElement(1);glEnd();
    pixels("VBO ArrayElement",255,255);
    glDisableClientState(GL_WEIGHT_ARRAY_ARB);
    const GLdouble a[]={1,0},b[]={0,1};
    glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_POINTS);
    glWeightdvARB(2,a);glVertex2fv(positions);
    glWeightdvARB(2,b);glVertex2fv(positions+2);
    glEnd();pixels("immediate double",255,255);
    return failures!=0;
}
