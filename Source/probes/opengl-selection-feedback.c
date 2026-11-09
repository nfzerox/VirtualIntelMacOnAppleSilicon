/* Native and bridge retained output and overflow in legacy render modes. */
#ifdef VIM_SELECTION_FEEDBACK_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static void point(void)
{
    glBegin(GL_POINTS);glColor4f(1,0,0,1);glVertex3f(0,0,0);glEnd();
}
static void select_points(GLsizei capacity)
{
    GLuint result[20];for(unsigned i=0;i<20;++i)result[i]=0xfaceb00c;
    glSelectBuffer(capacity,result);
    void *pointer=NULL;glGetPointerv(GL_SELECTION_BUFFER_POINTER,&pointer);CHECK(pointer==result);
    CHECK(glRenderMode(GL_SELECT)==0);
    glInitNames();glPushName(41);point();glLoadName(77);point();
    int interim=result[0]!=0xfaceb00c; /* Visibility before RenderMode is unspecified. */
    GLint hits=glRenderMode(GL_RENDER);
    CHECK(glGetError()==GL_NO_ERROR && hits==(capacity>=8?2:-1) && result[capacity]==0xfaceb00c);
    CHECK(result[0]==1);
    if(capacity>=4)CHECK(result[3]==41);
    if(capacity>=5)CHECK(result[4]==1);
    if(capacity>=8)CHECK(result[0]==1 && result[3]==41 && result[4]==1 && result[7]==77);
    for(int i=8;i<capacity;++i)CHECK(result[i]==0xfaceb00c);
    printf("selection cap=%d hits=%d interim=%d:",capacity,hits,interim);
    for(int i=0;i<capacity;++i)printf(" %08x",result[i]);
    putchar('\n');
}
static void feedback_point(GLsizei capacity)
{
    GLfloat result[20];for(unsigned i=0;i<20;++i)result[i]=-137.0f;
    glFeedbackBuffer(capacity,GL_3D_COLOR,result);
    void *pointer=NULL;glGetPointerv(GL_FEEDBACK_BUFFER_POINTER,&pointer);CHECK(pointer==result);
    CHECK(glRenderMode(GL_FEEDBACK)==0);
    glPassThrough(2.5f);point();
    int interim=result[0]!=-137.0f;
    GLint values=glRenderMode(GL_RENDER);
    CHECK(glGetError()==GL_NO_ERROR && values==(capacity>=10?10:-1) && result[capacity]==-137.0f);
    const GLfloat expected[]={GL_PASS_THROUGH_TOKEN,2.5f,GL_POINT_TOKEN,8,8,.5f,1,0,0,1};
    for(int i=0;i<capacity;++i)CHECK(result[i]==(i<10?expected[i]:-137.0f));
    printf("feedback cap=%d count=%d interim=%d:",capacity,values,interim);
    for(int i=0;i<capacity;++i)printf(" %g",result[i]);
    putchar('\n');
}
static void transition_state(void)
{
    GLuint selected[12]={0},alternate[12]={0};
    GLfloat feedback[12]={0};
    glSelectBuffer(12,selected);
    CHECK(glRenderMode(GL_SELECT)==0);
    glSelectBuffer(12,alternate);CHECK(glGetError()==GL_INVALID_OPERATION);
    void *pointer=NULL;glGetPointerv(GL_SELECTION_BUFFER_POINTER,&pointer);
    CHECK(pointer==selected);
    glInitNames();glPushName(9);point();
    GLint invalid=glRenderMode(0);
    GLenum invalid_error=glGetError();
    GLint mode=0;glGetIntegerv(GL_RENDER_MODE,&mode);
    printf("invalid transition result=%d error=%04x mode=%04x\n",invalid,invalid_error,mode);
    CHECK(invalid_error==GL_INVALID_ENUM && mode==GL_SELECT);
    CHECK(glRenderMode(GL_RENDER)==1 && selected[0]==1 && selected[3]==9);
    CHECK(glRenderMode(GL_SELECT)==0);
    glInitNames();glPushName(17);point();
    GLint missing=glRenderMode(GL_FEEDBACK);
    GLenum missing_error=glGetError();glGetIntegerv(GL_RENDER_MODE,&mode);
    printf("missing buffer transition result=%d error=%04x mode=%04x\n",missing,missing_error,mode);
    CHECK(missing==1 && missing_error==GL_INVALID_OPERATION && mode==GL_SELECT && selected[3]==17);
    if(mode!=GL_RENDER)glRenderMode(GL_RENDER);
    CHECK(glGetError()==GL_NO_ERROR);
    glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glSelectBuffer(12,alternate);glFeedbackBuffer(12,GL_3D_COLOR,feedback);
    glPopClientAttrib();glPopAttrib();
    glGetPointerv(GL_SELECTION_BUFFER_POINTER,&pointer);
    printf("client stack selection retained new=%d\n",pointer==alternate);
    CHECK(glGetError()==GL_NO_ERROR && pointer==alternate);
}
static void chunked_feedback(void)
{
    enum { tokens=600, count=tokens*2 };
    GLfloat output[count+4];
    for(unsigned i=0;i<count+4;++i)output[i]=-137.0f;
    glFeedbackBuffer(count+3,GL_2D,output);
    GLint capacity=0,type=0;glGetIntegerv(GL_FEEDBACK_BUFFER_SIZE,&capacity);
    glGetIntegerv(GL_FEEDBACK_BUFFER_TYPE,&type);
    CHECK(capacity==count+3 && type==GL_2D);
    CHECK(glRenderMode(GL_FEEDBACK)==0);
    for(unsigned i=0;i<tokens;++i)glPassThrough((GLfloat)i+.25f);
    CHECK(glRenderMode(GL_FEEDBACK)==count); /* Same-mode transition resets the cursor. */
    for(unsigned i=0;i<tokens;++i)
        CHECK(output[2*i]==GL_PASS_THROUGH_TOKEN && output[2*i+1]==(GLfloat)i+.25f);
    for(unsigned i=count;i<count+4;++i)CHECK(output[i]==-137.0f);
    glPassThrough(9.5f);
    CHECK(glRenderMode(GL_RENDER)==2 && output[0]==GL_PASS_THROUGH_TOKEN &&
          output[1]==9.5f && output[3]==1.25f && glGetError()==GL_NO_ERROR);
    printf("feedback chunked output words=%d with tail preserved\n",count);
}
int main(void)
{
#ifdef VIM_SELECTION_FEEDBACK_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_RETAINED_RENDER);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("selection/feedback renderer=%s\n",glGetString(GL_RENDERER));
    GLuint rb=0,fb=0;glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);
    transition_state();
    select_points(12);select_points(6);select_points(1);
    feedback_point(12);feedback_point(6);feedback_point(1);
    chunked_feedback();
    glDeleteFramebuffersEXT(1,&fb);glDeleteRenderbuffersEXT(1,&rb);
#ifdef VIM_SELECTION_FEEDBACK_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("selection/feedback failures=%d\n",failures);return failures?1:0;
}
