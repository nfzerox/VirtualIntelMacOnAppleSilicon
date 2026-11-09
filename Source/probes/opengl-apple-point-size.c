/* Native and bridged Apple point-size arrays. */
#ifdef VIM_POINT_SIZE_WIRE
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
static void pixels(const char *step,unsigned expected_left,unsigned expected_right)
{
    GLubyte image[16*16*4]={0};
    glReadPixels(0,0,16,16,GL_RGBA,GL_UNSIGNED_BYTE,image);
    unsigned left=0,right=0;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){
        if(image[(y*16+x)*4]>128){if(x<8)++left;else ++right;}
    }
    GLenum error=glGetError();
    printf("point-size %s coverage=%u,%u error=%04x\n",step,left,right,error);
    CHECK(!error && left==expected_left && right==expected_right);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
#ifdef VIM_POINT_SIZE_WIRE
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
    printf("point-size renderer=%s\n",glGetString(GL_RENDERER));
    if(!ext || !strstr(ext,"GL_APPLE_vertex_point_size"))return 77;
    const GLenum states[]={GL_VERTEX_POINT_SIZE_APPLE,GL_CURRENT_POINT_SIZE_APPLE,GL_POINT_SIZE_ARRAY_APPLE,
        GL_POINT_SIZE_ARRAY_TYPE_APPLE,GL_POINT_SIZE_ARRAY_STRIDE_APPLE,GL_POINT_SIZE_ARRAY_BUFFER_BINDING_APPLE};
    const GLfloat defaults[]={0,1,0,GL_FLOAT,0,0};
    for(unsigned i=0;i<sizeof(states)/sizeof(*states);++i){
        GLfloat value=-1;glGetFloatv(states[i],&value);
        CHECK(glGetError()==GL_NO_ERROR && value==defaults[i]);
    }
    GLuint rb=0,fb=0;glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    glViewport(0,0,16,16);glClearColor(0,0,0,1);glColor4ub(255,0,0,255);
    const GLfloat positions[]={-0.5625f,0.0625f,0.4375f,0.0625f};
    GLfloat size[]={1,5};
    glVertexPointer(2,GL_FLOAT,0,positions);glEnableClientState(GL_VERTEX_ARRAY);
    glPointSizePointerAPPLE(GL_FLOAT,0,size);glEnableClientState(GL_POINT_SIZE_ARRAY_APPLE);
    GLint stride=-1;void *pointer=NULL;
    glGetIntegerv(GL_POINT_SIZE_ARRAY_STRIDE_APPLE,&stride);
    glGetPointerv(GL_POINT_SIZE_ARRAY_POINTER_APPLE,&pointer);
    CHECK(pointer==size && stride==4 && glIsEnabled(GL_POINT_SIZE_ARRAY_APPLE) && glGetError()==GL_NO_ERROR);
    glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_POINTS,0,2);pixels("array disabled server state",1,1);
    glEnable(GL_VERTEX_POINT_SIZE_APPLE);
    glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_POINTS,0,2);pixels("array enabled server state",1,25);
    glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_POINTS);
    glVertexPointSizefAPPLE(1);glVertex2fv(positions);
    glVertexPointSizefAPPLE(5);glVertex2fv(positions+2);
    glEnd();pixels("immediate",1,25);
    glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_POINTS);glArrayElement(0);glArrayElement(1);glEnd();
    pixels("array element",1,25);
    GLuint list=glGenLists(1);glNewList(list,GL_COMPILE);glDrawArrays(GL_POINTS,0,2);glEndList();
    size[1]=3;glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_POINTS,0,2);pixels("changed CPU data",1,9);
    glClear(GL_COLOR_BUFFER_BIT);glCallList(list);pixels("captured display list",1,25);
    glDeleteLists(list,1);size[1]=5;
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glDisableClientState(GL_POINT_SIZE_ARRAY_APPLE);
    glPointSizePointerAPPLE(GL_FLOAT,0,size+1);
    glPopClientAttrib();glGetPointerv(GL_POINT_SIZE_ARRAY_POINTER_APPLE,&pointer);
    CHECK(pointer==size && glIsEnabled(GL_POINT_SIZE_ARRAY_APPLE) && glGetError()==GL_NO_ERROR);
    GLuint vbo=0;glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(size),size,GL_STATIC_DRAW);
    glPointSizePointerAPPLE(GL_FLOAT,0,NULL);glBindBuffer(GL_ARRAY_BUFFER,0);
    GLint binding=0;glGetIntegerv(GL_POINT_SIZE_ARRAY_BUFFER_BINDING_APPLE,&binding);
    glGetPointerv(GL_POINT_SIZE_ARRAY_POINTER_APPLE,&pointer);
    CHECK(binding==(GLint)vbo && pointer==NULL && glGetError()==GL_NO_ERROR);
    glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_POINTS,0,2);pixels("VBO",1,25);
    glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_POINTS);glArrayElement(0);glArrayElement(1);glEnd();
    pixels("VBO array element",1,25);
    if(strstr(ext,"GL_ARB_vertex_blend")){
        const GLfloat weights[]={1,1};
        glWeightPointerARB(1,GL_FLOAT,0,weights);glEnableClientState(GL_WEIGHT_ARRAY_ARB);
        glVertexBlendARB(1);glEnable(GL_VERTEX_BLEND_ARB);
        glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_POINTS,0,2);pixels("point and weight arrays",1,25);
        glDisable(GL_VERTEX_BLEND_ARB);glDisableClientState(GL_WEIGHT_ARRAY_ARB);
    }
    const GLenum types[]={GL_FLOAT,GL_DOUBLE,GL_SHORT,GL_HALF_FLOAT_ARB,0x140c /* FIXED */};
    for(unsigned i=0;i<sizeof(types)/sizeof(*types);++i){
        glPointSizePointerAPPLE(types[i],0,size);
        CHECK(glGetError()==(i?GL_INVALID_ENUM:GL_NO_ERROR));
    }
    return failures!=0;
}
