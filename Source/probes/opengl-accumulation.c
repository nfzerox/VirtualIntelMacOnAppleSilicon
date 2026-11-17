/* Public immediate-mode accumulation contract. Run unchanged against the
 * period guest and native Intel reference. The optional direct build also
 * qualifies the actual executor before installing any guest libraries.
 */
#ifdef VIM_ACCUMULATION_DIRECT
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>
#include <math.h>
static int failed;
#define CHECK(test) do{if(!(test)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#test);failed=1;}}while(0)
static void pixel(int x,int y,float r,float g,float b,float a)
{
    GLfloat p[4]={0},expected[]={r,g,b,a};glReadPixels(x,y,1,1,GL_RGBA,GL_FLOAT,p);
    for(unsigned i=0;i<4;++i)if(!isfinite(p[i])||fabsf(p[i]-expected[i])>.008f){
        fprintf(stderr,"FAIL pixel %d,%d component%u expected %.4f actual %.4f\n",x,y,i,expected[i],p[i]);failed=1;
    }
}
int main(void)
{
    enum{W=17,H=13};
#ifdef VIM_ACCUMULATION_DIRECT
    int attrs[]={0x56494d03,128,0};unsigned char backing[W*H*4]={0};
    struct DGContext *context=DGCreateContext(attrs,NULL);CHECK(context);if(!context)return 2;
    CHECK(DGMakeCurrent(context,backing,GL_UNSIGNED_BYTE,W,H));
#else
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFADoubleBuffer,kCGLPFADepthSize,32,kCGLPFAAccumSize,32,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;CGLPBufferObj buffer=NULL;GLint count=0,bits=0;
    CHECK(!CGLChoosePixelFormat(attributes,&format,&count));if(!format)return 2;
    CHECK(!CGLDescribePixelFormat(format,0,kCGLPFAAccumSize,&bits));CHECK(bits>=32);
    CHECK(!CGLCreateContext(format,NULL,&context));CGLDestroyPixelFormat(format);if(!context)return 2;
    CHECK(!CGLSetCurrentContext(context));
    CHECK(!CGLCreatePBuffer(W,H,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA,0,&buffer));
    if(!buffer)return 2;
    CHECK(!CGLSetPBuffer(context,buffer,0,0,0));
#endif
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    glDrawBuffer(GL_FRONT);glReadBuffer(GL_FRONT);
    const GLenum keys[]={GL_ACCUM_RED_BITS,GL_ACCUM_GREEN_BITS,GL_ACCUM_BLUE_BITS,GL_ACCUM_ALPHA_BITS};
    for(unsigned i=0;i<4;++i){GLint bits=0;glGetIntegerv(keys[i],&bits);CHECK(bits>=8);}
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glAccum(GL_LOAD,.5f);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glAccum(GL_ACCUM,.5f);glAccum(GL_RETURN,1);
    pixel(0,0,.5f,0,.5f,1);pixel(W-1,H-1,.5f,0,.5f,1);CHECK(glGetError()==GL_NO_ERROR);
    glAccum(GL_MULT,.5f);glAccum(GL_ADD,-.125f);glAccum(GL_RETURN,1);
    pixel(0,0,.125f,0,.125f,.375f);CHECK(glGetError()==GL_NO_ERROR);
    glClearAccum(-.25f,.25f,.75f,.5f);glPushAttrib(GL_ACCUM_BUFFER_BIT);
    glClearAccum(1,0,0,1);glPopAttrib();glEnable(GL_SCISSOR_TEST);glScissor(3,4,5,6);
    glClear(GL_ACCUM_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);glAccum(GL_RETURN,1);
    pixel(3,4,0,.25f,.75f,.5f);pixel(2,4,.125f,0,.125f,.375f);
    glEnable(GL_SCISSOR_TEST);glAccum(GL_ADD,.25f);glDisable(GL_SCISSOR_TEST);glAccum(GL_RETURN,1);
    pixel(3,4,0,.5f,1,.75f);pixel(2,4,.125f,0,.125f,.375f);CHECK(glGetError()==GL_NO_ERROR);
    glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);glAccum(GL_RETURN,.5f);
    pixel(3,4,0,.25f,1,.375f);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    CHECK(glGetError()==GL_NO_ERROR);
    glAccum(0xffffffffu,1);CHECK(glGetError()==GL_INVALID_ENUM);
    glAccum(0,1);CHECK(glGetError()==GL_INVALID_ENUM);
    glBegin(GL_POINTS);glAccum(GL_LOAD,1);glEnd();CHECK(glGetError()==GL_INVALID_OPERATION);
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    glAccum(GL_LOAD,1);CHECK(glGetError()==GL_INVALID_OPERATION);
    GLint fbo_bits=99;glGetIntegerv(GL_ACCUM_RED_BITS,&fbo_bits);CHECK(!fbo_bits);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,0);
    const GLenum operations[]={GL_LOAD,GL_ACCUM,GL_MULT,GL_ADD,GL_RETURN};
    for(unsigned i=0;i<5;++i){glAccum(operations[i],1);CHECK(glGetError()==GL_INVALID_OPERATION);}
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    CHECK(glGetError()==GL_NO_ERROR);
#ifdef VIM_ACCUMULATION_DIRECT
    DGDestroyContext(context);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPBuffer(buffer);
#endif
    puts(failed?"FAIL immediate accumulation dispatch":"PASS immediate accumulation pixels, queries, masks, scissor, attribute stack and errors");
    return failed?1:0;
}
