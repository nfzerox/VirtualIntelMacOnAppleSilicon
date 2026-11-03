/* Host backend qualification for the native Mavericks live-surface contract.
 * Independent connections and nonsharing CGL contexts must see the SAME
 * retained resource after producer flush + ordinary consumer texture bind.
 * This tests the new primitive, not yet the GLI/kernel integration. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-surfaces.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
extern const char *DGTransportName(void);
enum { W=64,H=32 };
static int sample(GLuint source,int green,const char *label)
{
    glViewport(0,0,W,H);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glColor4f(1,1,1,1);glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(W,0);glVertex2f(1,-1);
    glTexCoord2f(W,H);glVertex2f(1,1);glTexCoord2f(0,H);glVertex2f(-1,1);glEnd();
    unsigned char bottom[4]={0},top[4]={0};
    glReadPixels(W/2,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bottom);
    glReadPixels(W/2,H-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,top);
    GLenum error=glGetError();
    int good=!error && bottom[0]==(green==2?255:0) && bottom[1]==(green==1?255:0) &&
        bottom[2]==(green==0||green==2?255:0) && bottom[3]==(green==3?0:255) &&
        top[0]==(green==0||green==2?255:0) && top[1]==(green==1?255:0) &&
        top[2]==(green==2?255:0) && top[3]==(green==3?0:255);
    printf("%s %s bottom=%u,%u,%u,%u top=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",label,
        bottom[0],bottom[1],bottom[2],bottom[3],top[0],top[1],top[2],top[3],error);
    return good;
}
static int run(void)
{
    unsigned char producer_shadow[W*H*4]={0},consumer_shadow[W*H*4]={0};
    int attrs[]={0x56494d01,1,0};uint64_t resource=0;arc4random_buf(&resource,sizeof(resource));
    if(!resource) resource=1;
    void *producer=DGCreateContext(attrs,NULL),*consumer=DGCreateContext(NULL,NULL);
    if(!producer || !consumer) return 2;
    if(!DGMakeCurrent(producer,producer_shadow,GL_UNSIGNED_BYTE,W,H)) return 2;
    printf("renderer=%s transport=%s; independent producer/consumer connections\n",glGetString(GL_RENDERER),DGTransportName());
    uint32_t publication=DGAcquireSurface(resource,1,W,H);
    if(!publication) { fprintf(stderr,"producer acquire error=%x\n",glGetError());return 2; }
    glDrawBuffer(GL_BACK);glDisable(GL_SCISSOR_TEST);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,H/2,W,H/2);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    /* Publish must ignore but preserve the caller's clipping/color-space
     * state. Preserve a nondefault READ FBO and bound rectangle texture too. */
    GLuint other=0,rect=0;glGenFramebuffersEXT(1,&other);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,other);
    glGenTextures(1,&rect);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,rect);
    glScissor(3,4,0,0);glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    glEnable(0xffffffffu); // An earlier caller error must survive publication.
    int good=DGPublishSurface(publication,1,1);
    GLenum prior=glGetError();good=prior==GL_INVALID_ENUM && good;
    printf("%s publication retains earlier caller GL error=%x\n",prior==GL_INVALID_ENUM?"PASS":"FAIL",prior);
    GLint read=0,draw=0,binding=0,box[4]={0},mode=0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&binding);glGetIntegerv(GL_SCISSOR_BOX,box);
    glGetIntegerv(GL_DRAW_BUFFER,&mode);
    int state=read==(GLint)other && !draw && binding==(GLint)rect && mode==GL_BACK &&
        glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT) &&
        box[0]==3 && box[1]==4 && !box[2] && !box[3] && glGetError()==GL_NO_ERROR;
    printf("%s publication preserves independent framebuffer, buffer, texture, scissor and sRGB state\n",state?"PASS":"FAIL");
    good=state && good;
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&other);glDeleteTextures(1,&rect);
    if(!DGMakeCurrent(consumer,consumer_shadow,GL_UNSIGNED_BYTE,W,H)) return 2;
    uint32_t alias=DGAcquireSurface(resource,1,W,H);
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    good=alias && DGBindSurface(alias,GL_RGBA8) && good;
    good=sample(texture,0,"initial live alias, native CGS row order") && good;
    GLuint view=0;
    if(getenv("VIM_GL_IMPORT_CROP")) {
        glGenTextures(1,&view);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,view);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        int bound=DGBindSurfaceView(alias,GL_RGBA8,W-2,H);GLint width=0,height=0;
        glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_HEIGHT,&height);
        GLenum error=glGetError();int sized=bound && !error && width==W-2 && height==H;
        printf("%s cropped live texture dimensions=%d,%d error=%x\n",sized?"PASS":"FAIL",width,height,error);
        good=sized && sample(view,0,"cropped view retains resource row order") && good;
        int oversized=DGBindSurfaceView(alias,GL_RGBA8,W+1,H);error=glGetError();
        good=!oversized && error==GL_INVALID_VALUE && good;
        printf("%s oversized view rejected without changing association error=%x\n",!oversized&&error==GL_INVALID_VALUE?"PASS":"FAIL",error);
    }
    DGMakeCurrent(producer,producer_shadow,GL_UNSIGNED_BYTE,W,H);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    good=DGPublishSurface(publication,1,1) && good;
    DGMakeCurrent(consumer,consumer_shadow,GL_UNSIGNED_BYTE,W,H);
    // Crucial: NO DGBindSurface reimport, no readback/upload between contexts.
    good=sample(texture,1,"same texture after producer redraw, ordinary rebind only") && good;
    if(view) {
        good=sample(view,1,"cropped existing view sees subsequent GPU publication") && good;
        glDeleteTextures(1,&view);
    }
    unsigned char cpu[W*H*4];
    for(unsigned i=0;i<sizeof(cpu);i+=4) { cpu[i]=255;cpu[i+1]=0;cpu[i+2]=255;cpu[i+3]=255; }
    good=DGUploadSurface(alias,cpu,W,H,W*4,11) && good;
    good=sample(texture,2,"CPU epoch updates the same live storage") && good;
    memset(cpu,0,sizeof(cpu));good=DGUploadSurface(alias,cpu,W,H,W*4,10) && good;
    uint64_t epoch=0;good=DGSurfaceEpoch(alias,&epoch) && epoch==11 && good;
    good=sample(texture,2,"older CPU epoch cannot overwrite current content") && good;
    DGMakeCurrent(producer,producer_shadow,GL_UNSIGNED_BYTE,W,H);
    good=DGPublishSurfaceEpoch(publication,1,1,10) && good;
    DGMakeCurrent(consumer,consumer_shadow,GL_UNSIGNED_BYTE,W,H);
    good=sample(texture,2,"older GPU publication cannot overwrite current content") && good;
    DGMakeCurrent(producer,producer_shadow,GL_UNSIGNED_BYTE,W,H);
    good=DGPublishSurfaceEpoch(publication,1,1,12) && good;
    void *hold=DGCreateSurfaceHold(producer,resource,1,W,H);good=hold && good;
    DGMakeCurrent(consumer,consumer_shadow,GL_UNSIGNED_BYTE,W,H);
    good=sample(texture,1,"newer GPU epoch supersedes CPU storage") && good;
    DGDestroyContext(producer);
    good=sample(texture,1,"consumer retains live resource after producer destruction") && good;
    uint32_t conflict=DGAcquireSurface(resource,1,W+1,H);
    GLenum conflict_error=glGetError();
    int rejected=!conflict && conflict_error==GL_INVALID_OPERATION;
    printf("%s conflicting dimensions reject instead of aliasing/replacing live storage error=%x\n",rejected?"PASS":"FAIL",conflict_error);
    good=rejected && good;
    unsigned touched=0;
    for(unsigned i=0;i<sizeof(producer_shadow);++i) touched|=producer_shadow[i]|consumer_shadow[i];
    printf("%s guest CPU shadows unchanged=%u\n",touched?"FAIL":"PASS",touched);
    good=!touched && good;
    /* A backing incarnation never aliases another allocation, even while
     * the previous incarnation is still sampled by this consumer. */
    uint32_t fresh=DGAcquireSurface(resource,2,W,H);
    GLuint empty=0;glGenTextures(1,&empty);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,empty);
    good=fresh && fresh!=alias && DGBindSurface(fresh,GL_RGBA8) && good;
    unsigned char pixel[4]={1,1,1,1};
    /* Sample through the consumer drawable, not a full-image CPU download. */
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glViewport(0,0,W,H);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,empty);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(W,0);glVertex2f(1,-1);
    glTexCoord2f(W,H);glVertex2f(1,1);glTexCoord2f(0,H);glVertex2f(-1,1);glEnd();
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error=glGetError();int isolated=!pixel[0] && !pixel[1] && !pixel[2] && !pixel[3] && !error;
    printf("%s new backing incarnation initialized and isolated error=%x\n",isolated?"PASS":"FAIL",error);
    good=isolated && good;
    glDeleteTextures(1,&empty);glDeleteTextures(1,&texture);
    good=DGReleaseSurface(alias) && DGReleaseSurface(fresh) && good;
    DGDestroyContext(consumer);
    // No original native CGL context remains. The explicit connection-level
    // hold must preserve the resource, not rely on an incidental GL alias.
    consumer=DGCreateContext(NULL,NULL);
    if(!consumer || !DGMakeCurrent(consumer,consumer_shadow,GL_UNSIGNED_BYTE,W,H)) return 2;
    alias=DGAcquireSurface(resource,1,W,H);texture=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    good=alias && DGBindSurface(alias,GL_RGBA8) && good;
    good=sample(texture,1,"connection hold preserves GPU storage after all original contexts die") && good;
    DGReleaseSurfaceHold(hold);
    good=sample(texture,1,"new consumer remains valid after the old connection hold is released") && good;
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glEnable(GL_SCISSOR_TEST);glScissor(1,2,0,0);
    glClearColor(.25,.5,.75,1);good=DGRetireSurface(alias) && good;
    GLboolean mask[4]={1,1,1,1};GLfloat clear[4]={0};
    glGetBooleanv(GL_COLOR_WRITEMASK,mask);glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);glGetIntegerv(GL_SCISSOR_BOX,box);
    int scoped=!mask[0]&&!mask[1]&&!mask[2]&&!mask[3]&&glIsEnabled(GL_SCISSOR_TEST)&&
        box[0]==1&&box[1]==2&&!box[2]&&!box[3]&&clear[0]==.25f&&clear[1]==.5f&&clear[2]==.75f&&clear[3]==1;
    printf("%s retirement preserves caller clear, color-mask and scissor state\n",scoped?"PASS":"FAIL");
    good=scoped && good;glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_SCISSOR_TEST);
    good=sample(texture,3,"retired resource samples transparent like native Mavericks") && good;
    int uploaded=DGUploadSurface(alias,cpu,W,H,W*4,100);GLenum retirement_error=glGetError();
    good=!uploaded && retirement_error==GL_INVALID_OPERATION && good;
    printf("%s a late CPU writer cannot resurrect a retired resource error=%x\n",!uploaded&&retirement_error==GL_INVALID_OPERATION?"PASS":"FAIL",retirement_error);
    good=DGRetireSurface(alias) && sample(texture,3,"retirement is idempotent") && good;
    glDeleteTextures(1,&texture);good=DGReleaseSurface(alias) && good;
    DGDestroyContext(consumer);return !good;
}
int main(int argc,char **argv)
{
    unsigned cycles=argc==2 ? (unsigned)strtoul(argv[1],NULL,10) : 1;
    if(!cycles || cycles>300) return 2;
    for(unsigned i=0;i<cycles;++i) { int result=run();if(result) return result; }
    printf("PASS %u retained-resource lifecycle cycle(s)\n",cycles);return 0;
}
