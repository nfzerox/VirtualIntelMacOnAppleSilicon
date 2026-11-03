/* Exact context-free kernel wire service -> GPU worker -> IOSurface capability.
 * No VM mutation. CPU image reads below are consumer assertions only, never
 * used by the implementation to publish, copy or make a frame complete.
 */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
static struct DGReply exchange(int fd,struct DGMessage *message)
{
    struct DGReply reply={0};assert(!message->bytes);
    assert(!DGTransfer(fd,message,sizeof(*message),1));
    assert(!DGTransfer(fd,&reply,sizeof(reply),0) && !reply.bytes);return reply;
}
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
static void seal(struct DGHostContext *c,uint32_t lease,uint64_t epoch)
{
    struct DGMessage m={.op=DG_SURFACE_SEAL_VERSION};struct DGReply reply={0};
    m.args[0]=lease;m.args[1]=epoch;
    assert(!DGSurfaceCommand(c,&m,&reply) && !reply.error);
}
static void run(unsigned profile)
{
    struct DGHostContext c={0};c.surface_namespace[0]=getpid();c.surface_namespace[1]=profile;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl));c.profile=profile;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("profile=%x producer=%s\n",profile,renderer);fflush(stdout);
    assert(renderer && strstr(renderer,"Intel"));
    uint32_t lease=DGSurfaceAcquire(&c,71,3,640,480);assert(lease);
    struct DGSurfaceLease *p=DGSurfaceFind(&c,lease);assert(p);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,p->framebuffer);glDisable(GL_SCISSOR_TEST);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);p->resource->epoch=7;seal(&c,lease,7);
    mach_port_t receive=0;assert(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&receive));
    assert(!mach_port_insert_right(mach_task_self(),receive,receive,MACH_MSG_TYPE_MAKE_SEND));
    char name[128],path[256];snprintf(name,sizeof(name),"com.virtualintelmac.test.display.service.%d.%x",getpid(),profile);
    snprintf(path,sizeof(path),"/tmp/vim-display-service-%d-%x.plist",getpid(),profile);
    assert(!bootstrap_register(bootstrap_port,name,receive));endpoint(path,name);
    assert(!setenv("VIM_OPENGL_DISPLAY_ENDPOINT",path,1));
    int fds[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fds));
    pthread_t server;assert(!pthread_create(&server,NULL,DGServe,(void *)(intptr_t)fds[1]));
    struct DGMessage m={.op=VIM_GL_SERVICE_HELLO,.flags=DG_REPLY};
    m.args[0]=VIM_GL_SERVICE_MAGIC;m.args[1]=VIM_GL_SERVICE_VERSION;
    memcpy(&m.args[3],c.surface_namespace,16);
    struct DGReply reply=exchange(fds[0],&m);
    assert(!reply.error && (reply.result&VIM_GL_SERVICE_CAP_PRESENT));
    memset(&m,0,sizeof(m));m.op=DG_SURFACE_HOLD;m.flags=DG_REPLY;
    m.args[0]=71;m.args[1]=3;m.args[2]=640;m.args[3]=480;m.args[4]=7;
    reply=exchange(fds[0],&m);assert(!reply.error && reply.result && reply.result<=UINT32_MAX);
    uint32_t hold=(uint32_t)reply.result;
    // Replace the current publication before submitting the earlier exact
    // version. A silent "latest" substitution would turn red into blue.
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);p->resource->epoch=8;seal(&c,lease,8);
    // Keep this producer inside a real glBegin while the service submits and
    // the worker completes its already sealed version. Borrowing this live
    // context would violate the API or block completion until glEnd.
    if(profile==kCGLOGLPVersion_Legacy)glBegin(GL_POINTS);
    memset(&m,0,sizeof(m));m.flags=DG_REPLY;m.op=DG_SURFACE_PRESENT_READY;
    reply=exchange(fds[0],&m);assert(!reply.error && reply.result==1);
    m.op=DG_SURFACE_HOLD_PRESENT;m.args[0]=hold;m.args[1]=8;
    reply=exchange(fds[0],&m);assert(reply.error && !reply.result);
    m.args[0]=hold+1;m.args[1]=7;reply=exchange(fds[0],&m);assert(reply.error && !reply.result);
    m.args[0]=hold;
    uint64_t tickets[3];
    for(unsigned i=0;i<3;++i){
        reply=exchange(fds[0],&m);assert(!reply.error && reply.result);
        tickets[i]=reply.result;assert(!i || tickets[i]>tickets[i-1]);
    }
    reply=exchange(fds[0],&m);assert(reply.error && !reply.result);
    memset(&m,0,sizeof(m));m.flags=DG_REPLY;m.op=DG_SURFACE_PRESENT_READY;
    reply=exchange(fds[0],&m);assert(!reply.error && !reply.result);
    struct VIMSurfaceFrameMessage frames[3];IOSurfaceRef surfaces[3]={0};
    assert(!VIMSurfaceFrameReceive(receive,geteuid(),getpid(),&frames[0],&surfaces[0],5000));
    assert(CGLGetCurrentContext()==c.cgl);
    if(profile==kCGLOGLPVersion_Legacy)glEnd();
    assert(!glGetError());
    // Destroy the source's public context and all its leases. The kernel
    // hold, completion queue and in-flight presentation refs must suffice.
    CGLSetCurrentContext(c.cgl);DGDestroySurfaceLeases(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);c.cgl=NULL;
    for(unsigned i=0;i<3;++i){
        if(i)assert(!VIMSurfaceFrameReceive(receive,geteuid(),getpid(),&frames[i],&surfaces[i],5000));
        assert(frames[i].ticket==tickets[i]);
        assert(!IOSurfaceLock(surfaces[i],kIOSurfaceLockReadOnly,NULL));
        unsigned char *pixels=IOSurfaceGetBaseAddress(surfaces[i]);size_t row=IOSurfaceGetBytesPerRow(surfaces[i]);
        for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x){
            unsigned char *b=pixels+y*row+x*4;assert(b[0]==0 && b[1]==0 && b[2]==255 && b[3]==255);
        }
        assert(!IOSurfaceUnlock(surfaces[i],kIOSurfaceLockReadOnly,NULL));
    }
    memset(&m,0,sizeof(m));m.flags=DG_REPLY;m.op=DG_SURFACE_UNHOLD;m.args[0]=hold;
    reply=exchange(fds[0],&m);assert(!reply.error);
    assert(!VIMSurfaceFrameRelease(&frames[1],VIMSurfaceGPUCompleted,1000));
    assert(!VIMSurfaceFrameRelease(&frames[0],VIMSurfaceSkipped,1000));
    mach_msg_destroy(&frames[2].header);
    shutdown(fds[0],SHUT_RDWR);close(fds[0]);pthread_join(server,NULL);
    for(unsigned i=0;i<3;++i)CFRelease(surfaces[i]);
    mach_port_destroy(mach_task_self(),receive);assert(!unlink(path));DGSnapshotCleanupDrain();
    assert(!dg_display_sender && !dg_snapshot_bytes && !dg_surface_bytes && !dg_surface_leases);
    printf("PASS profile=%x real service wire: context-free exact-version presentation, readiness/backpressure, stale rejection, producer destruction and consumer release\n",profile);
}
int main(void){run(kCGLOGLPVersion_Legacy);run(kCGLOGLPVersion_3_2_Core);return 0;}
