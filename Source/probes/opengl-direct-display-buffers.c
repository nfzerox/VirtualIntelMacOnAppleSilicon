/* Real IOSurface/GPU integration of the display ownership state machine.
 * Readback is test-only; the implementation preserves pixels with GPU blits.
 */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include "../PassthroughOpenGL/direct-display-buffers-host.c"
#include <assert.h>
static int run(unsigned profile)
{
    struct DGHostContext c={0};struct DGDisplayBuffers d;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count) || CGLCreateContext(pf,NULL,&c.cgl))return 0;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);c.profile=profile;
    if(!DGDisplayBuffersCreate(&c,&d,32,24,1))return 0;
    int good=1;
    for(unsigned frame=0;frame<12;++frame){
        if(frame){
            glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
            good=DGDisplayBuffersCopyFront(&d) && glIsEnabled(GL_SCISSOR_TEST) && good;
        }
        uint32_t slot=d.state.draw;
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,d.fbos[slot]);
        glDisable(GL_SCISSOR_TEST);
        if(!frame){glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);}
        glEnable(GL_SCISSOR_TEST);glScissor(frame,0,1,24);
        glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        uint32_t submitted;uint64_t ticket=DGDisplayBuffersSubmit(&d,&submitted);
        good=ticket && slot==submitted && good;
        /* Test-only presenter: a real GPU wait, NOT glFlush/transport ACK.
         * Production completion must come from the app's display consumer. */
        glFinish();
        good=VIMDisplayBuffersPresented(&d.state,1,submitted,ticket) && good;
        unsigned char pixels[32*24*4];
        glReadPixels(0,0,32,24,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        for(unsigned y=0;y<24;++y)for(unsigned x=0;x<32;++x){
            unsigned char *p=pixels+4*(y*32+x);
            good=p[0]==(x<=frame?255:0) && p[1]==0 &&
                p[2]==(x<=frame?0:255) && p[3]==255 && good;
        }
        good=!glGetError() && good;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);DGDisplayBuffersDestroy(&d);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    printf("%s profile=%x triple IOSurface rotation and preserved partial updates\n",good?"PASS":"FAIL",profile);
    return good;
}
int main(void){int a=run(kCGLOGLPVersion_Legacy),b=run(kCGLOGLPVersion_3_2_Core);return !(a&&b);}
