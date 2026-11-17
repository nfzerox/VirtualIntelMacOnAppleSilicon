/* Research only: the producer CPU readback/publication remains in BOTH arms.
 * Uniform frames check currency, not orientation or a guest protocol.
 *
 * Build with the generated host-wire headers from the matching checked-in
 * executor, OpenGL/IOSurface/CoreFoundation frameworks and libbsm. The normal
 * app and guest payloads do not compile or select this experiment. It compares
 * an additional immutable GPU snapshot against the existing consumer upload;
 * it does not model bypassing the PVG Metal reader's CPU-visible publication.
 * Twelve measured frames per phase, CPU/GPU/GPU/CPU, shared and foreign CGL
 * share groups. There is no guest, network transfer or production protocol. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
#include <mach/mach_time.h>
enum { W=1942,H=1404,N=12 };
static mach_timebase_info_data_t timebase;
static double milliseconds(uint64_t start)
{return (mach_absolute_time()-start)*(double)timebase.numer/timebase.denom/1e6;}
static void test(int shared)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj format=NULL;GLint count=0;struct DGHostContext producer={0},consumer={0};
    assert(!CGLChoosePixelFormat(attrs,&format,&count)&&format);
    assert(!CGLCreateContext(format,NULL,&producer.cgl));
    assert(!CGLCreateContext(format,shared?producer.cgl:NULL,&consumer.cgl));CGLDestroyPixelFormat(format);
    producer.profile=consumer.profile=kCGLOGLPVersion_Legacy;
    producer.draw_mode=producer.read_mode=consumer.draw_mode=consumer.read_mode=GL_FRONT;
    assert(!CGLSetCurrentContext(producer.cgl));
    printf("renderer=%s shared=%d size=%dx%d frames/arm=%d\n",glGetString(GL_RENDERER),shared,W,H,N);
    assert(!strstr((const char *)glGetString(GL_RENDERER),"Software"));
    assert(DGCreateAliases(&producer,NULL)&&DGSelectDrawable(&producer,1,W,H));
    assert(!CGLSetCurrentContext(consumer.cgl));assert(DGCreateAliases(&consumer,shared?&producer:NULL));
    GLuint texture=0,output=0,fbo=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,W,H);glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    size_t bytes=(size_t)W*H*4;
    struct DGMessage *upload=calloc(1,sizeof(*upload)+bytes);unsigned char *readback=malloc(bytes);
    assert(upload&&readback);upload->op=DG_IOSURFACE_TEXTURE;upload->flags=DG_REPLY|DG_INPUT_ONLY;upload->bytes=(uint32_t)bytes;
    upload->args[0]=GL_RGBA8;upload->args[1]=W;upload->args[2]=H;upload->args[3]=W*4;
    struct DGSharedSurface resource={.access=PTHREAD_MUTEX_INITIALIZER,.width=W,.height=H,.epoch=1};
    struct DGSurfaceLease lease={.resource=&resource,.framebuffer=producer.framebuffer};
    const int modes[]={0,1,1,0};unsigned serial=0;
    for(unsigned phase=0;phase<4;++phase) {
        int gpu=modes[phase];double publication=0,import=0,total=0;
        for(unsigned i=0;i<N+3;++i) {
            unsigned red=(++serial&1)?255:0;
            uint64_t start=mach_absolute_time();
            assert(!CGLSetCurrentContext(producer.cgl));glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,producer.framebuffer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
            glClearColor(red/255.f,0,1-red/255.f,1);glClear(GL_COLOR_BUFFER_BIT);
            glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,readback);
            for(unsigned y=0;y<H;++y)memcpy((unsigned char *)(upload+1)+(size_t)y*W*4,
                readback+(size_t)(H-1-y)*W*4,(size_t)W*4);
            double read=milliseconds(start);
            uint64_t copying=mach_absolute_time();struct DGSurfaceSnapshot *snapshot=NULL;
            if(gpu){resource.epoch=serial;snapshot=DGSurfaceSnapshotCreate(&producer,&lease,serial);assert(snapshot);}
            assert(!CGLSetCurrentContext(consumer.cgl));glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
            if(gpu)assert(!DGSurfaceSnapshotCopyTexture(&consumer,snapshot,GL_RGBA8,W,H));
            else {struct DGReply reply={0};assert(!DGIOSurfaceTexture(&consumer,upload,&reply)&&reply.result==1&&!reply.error);}
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
            glBegin(GL_QUADS);glTexCoord2i(0,0);glVertex2f(-1,-1);glTexCoord2i(W,0);glVertex2f(1,-1);
            glTexCoord2i(W,H);glVertex2f(1,1);glTexCoord2i(0,H);glVertex2f(-1,1);glEnd();glFinish();
            double copied=milliseconds(copying),elapsed=milliseconds(start);
            unsigned char pixel[4]={0};glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadPixels(W/2,H/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            if(pixel[0]!=red||pixel[1]!=0||pixel[2]!=255-red||pixel[3]!=255)
                fprintf(stderr,"stale frame phase=%u serial=%u pixel=%u,%u,%u,%u\n",phase,serial,pixel[0],pixel[1],pixel[2],pixel[3]);
            assert(pixel[0]==red&&pixel[1]==0&&pixel[2]==255-red&&pixel[3]==255&&!glGetError());
            if(snapshot){assert(!CGLSetCurrentContext(producer.cgl));DGSurfaceSnapshotDestroy(snapshot);}
            if(i>=3){publication+=read;import+=copied;total+=elapsed;}
        }
        printf("shared=%d phase=%u path=%s publication_ms=%.3f consumer_ms=%.3f total_ms=%.3f\n",
            shared,phase,gpu?"immutable-gpu-copy":"cpu-upload",publication/N,import/N,total/N);fflush(stdout);
    }
    free(upload);free(readback);DGSnapshotCleanupDrain();
    assert(!CGLSetCurrentContext(consumer.cgl));glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&output);glDeleteTextures(1,&texture);
    DGForgetAliases(&consumer,1,&texture);DGDestroySurfaceLeases(&consumer);DGDestroyDrawables(&consumer);DGDestroyAliases(&consumer);
    assert(!CGLSetCurrentContext(producer.cgl));DGDestroySurfaceLeases(&producer);DGDestroyDrawables(&producer);DGDestroyAliases(&producer);
    assert(!CGLSetCurrentContext(NULL));CGLDestroyContext(consumer.cgl);CGLDestroyContext(producer.cgl);
    DGSnapshotCleanupDrain();pthread_mutex_destroy(&resource.access);
    assert(!dg_snapshot_bytes&&!dg_snapshot_spare_bytes);
}
int main(void)
{mach_timebase_info(&timebase);test(1);test(0);puts("PASS finite native transfer-cost experiment; not a guest implementation or FPS qualification");return 0;}
