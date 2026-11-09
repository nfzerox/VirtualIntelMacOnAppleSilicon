/* Compare native and bridged Apple vertex-program evaluator state and draw. */
#ifdef VIM_APPLE_EVALUATOR_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static void inspect(GLuint index,GLenum base,unsigned width,unsigned u,unsigned v,
                    const GLdouble *coefficients,const GLdouble *domain)
{
    const GLdouble width_value[]={width},order[]={u,v};
    unsigned dims=base==GL_VERTEX_ATTRIB_MAP1_SIZE_APPLE?1:2;
    const GLdouble *values[]={width_value,coefficients,order,domain};
    unsigned counts[]={1,width*u*v,dims,dims*2};
    for(unsigned i=0;i<4;++i){
        GLdouble d[64];GLfloat f[64];GLint k[64];
        for(unsigned j=0;j<64;++j){d[j]=-999;f[j]=-999;k[j]=-999;}
        glGetVertexAttribdvARB(index,base+i,d);GLenum ed=glGetError();
        glGetVertexAttribfvARB(index,base+i,f);GLenum ef=glGetError();
        glGetVertexAttribivARB(index,base+i,k);GLenum ei=glGetError();
        CHECK(!ed && !ef && !ei);
        for(unsigned j=0;j<counts[i];++j)
            CHECK(d[j]==values[i][j] && f[j]==(GLfloat)values[i][j] && k[j]==(GLint)values[i][j]);
        CHECK(d[counts[i]]==-999 && f[counts[i]]==-999 && k[counts[i]]==-999);
        printf("map index=%u pname=%04x error=%04x/%04x/%04x double:",index,base+i,ed,ef,ei);
        for(unsigned j=0;j<64 && d[j]!=-999;++j)printf(" %g",d[j]);
        printf(" float:");
        for(unsigned j=0;j<64 && f[j]!=-999;++j)printf(" %g",(double)f[j]);
        printf(" int:");
        for(unsigned j=0;j<64 && k[j]!=-999;++j)printf(" %d",k[j]);
        putchar('\n');
    }
}
static void render(void)
{
    GLuint rb=0,fb=0,program=0;
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glGenProgramsARB(1,&program);glBindProgramARB(GL_VERTEX_PROGRAM_ARB,program);
    const char source[]="!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\n"
        "MOV result.color, vertex.attrib[3];\nEND\n";
    glProgramStringARB(GL_VERTEX_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(source)-1,source);
    CHECK(glGetError()==GL_NO_ERROR);
    glEnable(GL_VERTEX_PROGRAM_ARB);
    const GLfloat position[]={-0.5625f,-0.5625f,0,1, 0.5625f,-0.5625f,0,1};
    const GLfloat color[]={1,0,0,1, 1,0,0,1};
    glMapVertexAttrib1fAPPLE(0,4,0,1,4,2,position);
    glMapVertexAttrib1fAPPLE(3,4,0,1,4,2,color);
    glEnableVertexAttribAPPLE(0,GL_VERTEX_ATTRIB_MAP1_APPLE);
    glEnableVertexAttribAPPLE(3,GL_VERTEX_ATTRIB_MAP1_APPLE);
    glBegin(GL_POINTS);glEvalCoord1f(0);glEvalCoord1f(1);glEnd();
    GLubyte left[4]={0},right[4]={0};
    glReadPixels(3,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left);
    glReadPixels(12,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
    GLenum error=glGetError();
    printf("evaluator rendered red=%u,%u error=%04x\n",left[0],right[0],error);
    CHECK(!error && left[0]==255 && right[0]==255);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
#ifdef VIM_APPLE_EVALUATOR_WIRE
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
    printf("evaluator renderer=%s\n",glGetString(GL_RENDERER));
    if(!ext || !strstr(ext,"GL_APPLE_vertex_program_evaluators"))return 77;
    const GLdouble defaults[]={0,0,0,1},default_domain[]={0,1,0,1};
    inspect(3,GL_VERTEX_ATTRIB_MAP1_SIZE_APPLE,4,1,1,defaults,default_domain);
    inspect(3,GL_VERTEX_ATTRIB_MAP2_SIZE_APPLE,4,1,1,defaults,default_domain);
    GLdouble one[]={1.25,2.25,99,3.25,4.25,99,5.25,6.25};
    GLfloat two[]={1.25f,2.25f,99,3.25f,4.25f,99,5.25f,6.25f,99,7.25f,8.25f};
    glMapVertexAttrib1dAPPLE(3,2,-1,1,3,3,one);
    CHECK(glGetError()==GL_NO_ERROR);
    glMapVertexAttrib2fAPPLE(3,2,-2,2,6,2,-3,3,3,2,two);
    CHECK(glGetError()==GL_NO_ERROR);
    const GLdouble coeff[]={1.25,2.25,3.25,4.25,5.25,6.25,7.25,8.25};
    const GLdouble domain1[]={-1,1},domain2[]={-2,2,-3,3};
    inspect(3,GL_VERTEX_ATTRIB_MAP1_SIZE_APPLE,2,3,1,coeff,domain1);
    inspect(3,GL_VERTEX_ATTRIB_MAP2_SIZE_APPLE,2,2,2,coeff,domain2);
    GLuint list=glGenLists(1);
    glNewList(list,GL_COMPILE);
    glMapVertexAttrib2dAPPLE(3,1,-2,2,3,2,-3,3,1,2,one);
    glEndList();
    inspect(3,GL_VERTEX_ATTRIB_MAP2_SIZE_APPLE,2,2,2,coeff,domain2);
    glCallList(list);
    const GLdouble list_coeff[]={1.25,2.25,3.25,4.25};
    inspect(3,GL_VERTEX_ATTRIB_MAP2_SIZE_APPLE,1,2,2,list_coeff,domain2);
    glDeleteLists(list,1);
    glEnableVertexAttribAPPLE(3,GL_VERTEX_ATTRIB_MAP2_APPLE);
    GLboolean active=glIsVertexAttribEnabledAPPLE(3,GL_VERTEX_ATTRIB_MAP2_APPLE);
    CHECK(active && glGetError()==GL_NO_ERROR);
    glDisableVertexAttribAPPLE(3,GL_VERTEX_ATTRIB_MAP2_APPLE);
    active=glIsVertexAttribEnabledAPPLE(3,GL_VERTEX_ATTRIB_MAP2_APPLE);
    CHECK(!active && glGetError()==GL_NO_ERROR);
    glMapVertexAttrib1dAPPLE(3,5,0,1,5,1,one);
    CHECK(glGetError()==GL_INVALID_VALUE);
    glEnableVertexAttribAPPLE(3,GL_TEXTURE_2D);
    CHECK(glGetError()==GL_INVALID_ENUM);
    GLdouble sentinel=-999;
    glGetVertexAttribdvARB(~0u,GL_VERTEX_ATTRIB_MAP1_COEFF_APPLE,&sentinel);
    CHECK(glGetError()==GL_INVALID_VALUE && sentinel==-999);
    render();
    return failures!=0;
}
