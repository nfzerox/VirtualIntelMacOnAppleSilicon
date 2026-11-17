/* Real native GPU storage/operations oracle, not a dispatch/conformance pass.
 * Exercises the production accumulation engine without a VM or guest copies.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "../PassthroughOpenGL/private-textures-host.c"
#define glDeleteTextures DGPrivateDeleteTextures
#define glDeleteFramebuffersEXT DGPrivateDeleteFramebuffers
#define glFramebufferTexture2DEXT DGPrivateAttachTexture
#include "../PassthroughOpenGL/accumulation-buffer-host.h"
static int failed;
#define CHECK(test) do{if(!(test)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#test);failed=1;}}while(0)
static void pixel(int x,int y,float r,float g,float b,float a)
{
    float p[4]={0};glReadPixels(x,y,1,1,GL_RGBA,GL_FLOAT,p);
    const float expected[]={r,g,b,a};
    for(unsigned i=0;i<4;++i)if(!isfinite(p[i])||fabsf(p[i]-expected[i])>.006f){
        fprintf(stderr,"FAIL pixel(%d,%d) component%u expected %.4f got %.4f\n",x,y,i,expected[i],p[i]);failed=1;
    }
}
int main(void)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    CHECK(!CGLChoosePixelFormat(attributes,&format,&count));
    CHECK(format && !CGLCreateContext(format,NULL,&context));
    if(format)CGLDestroyPixelFormat(format);
    if(!context || CGLSetCurrentContext(context))return 2;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    struct DGPrivateTextures *registry=DGPrivateTexturesCreate();CHECK(registry);
    enum {W=17,H=13};GLuint textures[2]={0},fbo=0;
    glGenTextures(2,textures);
    for(unsigned i=0;i<2;++i){
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[i]);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,textures[0],0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    struct DGAccumulationEngine engine={0};struct DGAccumulationBuffer first={0},second={0};
    struct DGAccumulationTarget target={textures[0],{GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE}};
    glViewport(2,3,7,5);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_LOAD,.5f,NULL,NULL,textures[0],NULL,0,GL_FALSE));
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_ACCUM,.5f,NULL,NULL,textures[0],NULL,0,GL_FALSE));
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_RETURN,1,NULL,NULL,0,&target,1,GL_FALSE));
    pixel(2,3,.5f,0,.5f,1);pixel(W-1,H-1,.5f,0,.5f,1);
    GLint viewport[4]={0};glGetIntegerv(GL_VIEWPORT,viewport);
    CHECK(viewport[0]==2 && viewport[1]==3 && viewport[2]==7 && viewport[3]==5);
    puts("checked LOAD/ACCUM/RETURN with actual float GPU backing and independent caller viewport");
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_MULT,.5f,NULL,NULL,0,NULL,0,GL_FALSE));
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_ADD,-.125f,NULL,NULL,0,NULL,0,GL_FALSE));
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_RETURN,1,NULL,NULL,0,&target,1,GL_FALSE));
    pixel(0,0,.125f,0,.125f,.375f);
    const GLint clip[]={3,4,5,6};const GLfloat clear[]={-.25f,.25f,.75f,.5f};
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,DG_ACCUMULATION_CLEAR,0,clear,clip,0,NULL,0,GL_FALSE));
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_RETURN,1,NULL,NULL,0,&target,1,GL_FALSE));
    pixel(3,4,0,.25f,.75f,.5f);pixel(7,9,0,.25f,.75f,.5f);
    pixel(2,4,.125f,0,.125f,.375f);pixel(8,9,.125f,0,.125f,.375f);
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_ADD,.5f,NULL,clip,0,NULL,0,GL_FALSE));
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_RETURN,1,NULL,NULL,0,&target,1,GL_FALSE));
    pixel(3,4,.25f,.75f,1,1);pixel(2,4,.125f,0,.125f,.375f);
    puts("checked signed MULT/ADD, RETURN clamping and scissored clear/update preservation");
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_LOAD,-.5f,NULL,clip,textures[0],NULL,0,GL_FALSE));
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_ACCUM,.5f,NULL,clip,textures[0],NULL,0,GL_FALSE));
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_RETURN,1,NULL,NULL,0,&target,1,GL_FALSE));
    pixel(3,4,0,0,.5f,0);pixel(2,4,.125f,0,.125f,.375f);
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,DG_ACCUMULATION_CLEAR,0,clear,clip,0,NULL,0,GL_FALSE));
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_ADD,.5f,NULL,clip,0,NULL,0,GL_FALSE));
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_RETURN,1,NULL,NULL,0,&target,1,GL_FALSE));
    const GLfloat green[]={0,1,0,1};
    CHECK(!DGAccumulationExecute(&engine,&second,W,H,DG_ACCUMULATION_CLEAR,0,green,NULL,0,NULL,0,GL_FALSE));
    target.mask[0]=target.mask[2]=GL_FALSE;
    CHECK(!DGAccumulationExecute(&engine,&second,W,H,GL_RETURN,1,NULL,clip,0,&target,1,GL_FALSE));
    pixel(3,4,.25f,1,1,1);pixel(2,4,.125f,0,.125f,.375f);
    target.mask[0]=target.mask[2]=GL_TRUE;
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_RETURN,1,NULL,NULL,0,&target,1,GL_FALSE));
    pixel(3,4,.25f,.75f,1,1);
    GLuint old=first.textures[first.current];
    CHECK(!DGPrivateTextureClaim(registry,old));CHECK(first.textures[first.current]!=old);
    CHECK(!DGAccumulationExecute(&engine,&first,W,H,GL_RETURN,1,NULL,NULL,0,&target,1,GL_FALSE));
    pixel(3,4,.25f,.75f,1,1);
    struct DGAccumulationTarget two[]={target,{textures[1],{GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE}}};
    CHECK(!DGAccumulationExecute(&engine,&second,W,H,GL_RETURN,1,NULL,NULL,0,two,2,GL_FALSE));
    pixel(0,0,0,1,0,1);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,textures[1],0);
    pixel(W-1,H-1,0,1,0,1);
    CHECK(!DGAccumulationExecute(&engine,&second,W-1,H-1,DG_ACCUMULATION_CLEAR,0,green,NULL,0,NULL,0,GL_FALSE));
    CHECK(DGAccumulationExecute(&engine,&second,W,H,0xffffffffu,1,NULL,NULL,0,NULL,0,GL_FALSE)==GL_INVALID_ENUM);
    puts("checked independent drawable contents, color masks, both draw buffers, private-name relocation and resize retirement");
    CHECK(glGetError()==GL_NO_ERROR);
    DGAccumulationDeleteBuffer(&first);DGAccumulationDeleteBuffer(&second);
    DGAccumulationDeleteEngine(&engine);
    CHECK(registry->count==0);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(2,textures);
    DGPrivateTexturesRelease(registry);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    puts(failed?"FAIL GPU accumulation engine":"PASS GPU accumulation engine (not public GL dispatch qualification)");
    return failed?1:0;
}
