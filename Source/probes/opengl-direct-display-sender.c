/* Exercise the actual command -> CGL worker -> IOSurface capability path.
 * CPU pixel access is confined to the consumer's verification below. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
static void endpoint(const char *path,const char *name)
{
    CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    int version=1,pid=getpid();
    CFNumberRef v=CFNumberCreate(NULL,kCFNumberIntType,&version),p=CFNumberCreate(NULL,kCFNumberIntType,&pid);
    CFStringRef n=CFStringCreateWithCString(NULL,name,kCFStringEncodingUTF8);
    CFDictionarySetValue(d,CFSTR("Version"),v);CFDictionarySetValue(d,CFSTR("RendererPID"),p);CFDictionarySetValue(d,CFSTR("Service"),n);
    CFDataRef data=CFPropertyListCreateData(NULL,d,kCFPropertyListBinaryFormat_v1_0,0,NULL);assert(data);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);
    assert(write(fd,CFDataGetBytePtr(data),CFDataGetLength(data))==CFDataGetLength(data));assert(!close(fd));
    CFRelease(data);CFRelease(n);CFRelease(p);CFRelease(v);CFRelease(d);
}
static uint64_t present(struct DGHostContext *c,uint32_t lease,uint64_t epoch)
{
    struct DGMessage m={.op=DG_SURFACE_PRESENT_VERSION};struct DGReply r={0};
    m.args[0]=lease;m.args[1]=epoch;assert(!DGSurfaceCommand(c,&m,&r));return r.error?0:r.result;
}
static void run(unsigned profile)
{
    struct DGHostContext c={0};c.surface_namespace[0]=getpid();c.surface_namespace[1]=profile;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count));assert(!CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl));c.profile=profile;
    uint32_t lease=DGSurfaceAcquire(&c,1,1,640,480);assert(lease);
    struct DGSurfaceLease *p=DGSurfaceFind(&c,lease);assert(p);
    mach_port_t receive=0;assert(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&receive));
    assert(!mach_port_insert_right(mach_task_self(),receive,receive,MACH_MSG_TYPE_MAKE_SEND));
    char name[128],path[256];snprintf(name,sizeof(name),"com.virtualintelmac.test.sender.%d.%x",getpid(),profile);
    snprintf(path,sizeof(path),"/tmp/vim-display-sender-%d-%x.plist",getpid(),profile);
    assert(!bootstrap_register(bootstrap_port,name,receive));endpoint(path,name);
    assert(!setenv("VIM_OPENGL_DISPLAY_ENDPOINT",path,1));
    uint64_t tickets[3];struct VIMSurfaceFrameMessage frames[3];IOSurfaceRef surfaces[3]={0};
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,p->framebuffer);glDisable(GL_SCISSOR_TEST);
    for(unsigned i=0;i<3;++i){
        glClearColor(i==0,i==1,i==2,1);glClear(GL_COLOR_BUFFER_BIT);
        p->resource->epoch=i+1;
        struct DGMessage seal={.op=DG_SURFACE_SEAL_VERSION};struct DGReply r={0};
        seal.args[0]=lease;seal.args[1]=i+1;assert(!DGSurfaceCommand(&c,&seal,&r) && !r.error);
        tickets[i]=present(&c,lease,i+1);assert(tickets[i] && (i==0 || tickets[i]>tickets[i-1]));
    }
    assert(!present(&c,lease,3)); /* no fourth source can be retained */
    assert(!present(&c,lease,99)); /* no implicit presentation of newer data */
    for(unsigned i=0;i<3;++i){
        assert(!VIMSurfaceFrameReceive(receive,geteuid(),getpid(),&frames[i],&surfaces[i],5000));
        assert(frames[i].ticket==tickets[i]);
        assert(!IOSurfaceLock(surfaces[i],kIOSurfaceLockReadOnly,NULL));
        unsigned char *pixels=IOSurfaceGetBaseAddress(surfaces[i]);
        size_t row=IOSurfaceGetBytesPerRow(surfaces[i]);
        for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x){
            unsigned char *b=pixels+y*row+x*4;
            assert(b[0]==(i==2?255:0) && b[1]==(i==1?255:0) && b[2]==(i==0?255:0) && b[3]==255);
        }
        assert(!IOSurfaceUnlock(surfaces[i],kIOSurfaceLockReadOnly,NULL));
    }
    /* Completion can arrive out of order. Closing a consumer also releases
     * its send-once right; neither case should strand renderer shutdown. */
    assert(!VIMSurfaceFrameRelease(&frames[1],VIMSurfaceGPUCompleted,1000));
    assert(!VIMSurfaceFrameRelease(&frames[0],VIMSurfaceSkipped,1000));
    mach_msg_destroy(&frames[2].header);
    DGDisplaySenderStop(&c);
    for(unsigned i=0;i<3;++i)CFRelease(surfaces[i]);
    mach_port_destroy(mach_task_self(),receive);assert(!unlink(path));
    DGDestroySurfaceLeases(&c);CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    DGSnapshotCleanupDrain();
    assert(!dg_display_sender && !dg_snapshot_bytes);
    printf("PASS profile=%x asynchronous GL fence, immutable GPU frames, ordered tickets, bounded retention and teardown\n",profile);
}
int main(void){run(kCGLOGLPVersion_Legacy);run(kCGLOGLPVersion_3_2_Core);return 0;}
