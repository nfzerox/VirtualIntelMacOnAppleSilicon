/* Exercise the research kernel ABI on an actual registered GL window, never
 * WindowServer's primary surface. No synthetic CGS number, kernel pointer,
 * or Float-derived layout is used. */
#import <Cocoa/Cocoa.h>
#import <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#import <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
#include "../vendor/source/VMsvga2/vminclude/vim_gl_surface_service.h"
#include <dlfcn.h>
#include <sys/sysctl.h>
#include <uuid/uuid.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
extern CGLError CGLGetSurface(CGLContextObj,uint32_t *,uint32_t *,uint32_t *);
static int outcome=2;
static IOReturn lockSurface(io_connect_t c,struct VIMSurfaceMapping *m,struct VIMSurfaceState *s)
{
    size_t n=sizeof(*m);IOReturn e=IOConnectCallStructMethod(c,kIOVM2DLockSurfaceBacking,NULL,0,m,&n);
    if(!e && n!=sizeof(*m)) return kIOReturnBadArgument;
    n=sizeof(*s);
    if(!e) e=IOConnectCallStructMethod(c,kIOVM2DGetSurfaceState,NULL,0,s,&n);
    if(!e && (n!=sizeof(*s) || s->version!=VIM_SURFACE_STATE_VERSION || !s->object ||
        !s->incarnation || !s->content_epoch || !(s->flags&VIM_SURFACE_STATE_CONTEXT_LOCKED))) return kIOReturnBadArgument;
    return e;
}
static IOReturn unlockSurface(io_connect_t c,int readonly,struct VIMSurfaceState *s)
{
    uint64_t flag=readonly?VIM_SURFACE_UNLOCK_READ_ONLY:0;size_t n=sizeof(*s);
    IOReturn e=IOConnectCallMethod(c,kIOVM2DUnlockSurfaceBacking,&flag,1,NULL,0,NULL,NULL,s,&n);
    if(!e && (n!=sizeof(*s) || s->version!=VIM_SURFACE_STATE_VERSION ||
        (s->flags&VIM_SURFACE_STATE_CONTEXT_LOCKED))) return kIOReturnBadArgument;
    return e;
}
static int StateCheck(const char *name,int good)
{ fprintf(stderr,"%s %s\n",good?"PASS":"FAIL",name);return good; }
struct VIMWaitProbe {
    pthread_mutex_t mutex;pthread_cond_t condition;
    io_connect_t client;int entered,finished;
    IOReturn error;struct VIMSurfaceMapping mapping;struct VIMSurfaceState state;
};
static void *VIMWaitBacking(void *opaque)
{
    struct VIMWaitProbe *p=opaque;
    pthread_mutex_lock(&p->mutex);p->entered=1;pthread_cond_signal(&p->condition);pthread_mutex_unlock(&p->mutex);
    size_t size=sizeof(p->mapping);
    p->error=IOConnectCallStructMethod(p->client,kIOVM2DWaitSurfaceBacking,NULL,0,&p->mapping,&size);
    if(!p->error){
        if(size!=sizeof(p->mapping))p->error=kIOReturnBadArgument;
        size=sizeof(p->state);
        if(!p->error)p->error=IOConnectCallStructMethod(p->client,kIOVM2DGetSurfaceState,NULL,0,&p->state,&size);
        if(!p->error && size!=sizeof(p->state))p->error=kIOReturnBadArgument;
    }
    pthread_mutex_lock(&p->mutex);p->finished=1;pthread_cond_signal(&p->condition);pthread_mutex_unlock(&p->mutex);
    return NULL;
}
static void *VIMHoldBacking(void *opaque)
{
    struct VIMWaitProbe *p=opaque;
    p->error=lockSurface(p->client,&p->mapping,&p->state);
    pthread_mutex_lock(&p->mutex);p->entered=1;pthread_cond_signal(&p->condition);pthread_mutex_unlock(&p->mutex);
    if(!p->error){
        /* Deliberate finite contention, not a rendering-path retry loop. */
        struct timespec hold={0,500000000};nanosleep(&hold,NULL);
        p->error=unlockSurface(p->client,1,&p->state);
    }
    return NULL;
}
static int VIMCheckContendedFlush(io_connect_t client)
{
    struct VIMWaitProbe p={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,.client=client};
    pthread_t thread;
    if(pthread_create(&thread,NULL,VIMHoldBacking,&p))return StateCheck("create competing surface owner",0);
    pthread_mutex_lock(&p.mutex);while(!p.entered)pthread_cond_wait(&p.condition,&p.mutex);pthread_mutex_unlock(&p.mutex);
    glFlush();GLenum error=glGetError();
    pthread_join(thread,NULL);
    unsigned char pixel[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    int good=StateCheck("real public GL flush survives a competing surface owner without losing pixels",
        !p.error && !error && !glGetError() && pixel[0]==51 && pixel[1]==102 && pixel[2]==204 && pixel[3]==255);
    fprintf(stderr,"contended GL flush error=%x owner=%x pixel=%u,%u,%u,%u\n",
        error,p.error,pixel[0],pixel[1],pixel[2],pixel[3]);
    pthread_cond_destroy(&p.condition);pthread_mutex_destroy(&p.mutex);return good;
}
static int VIMCheckWaitBacking(io_connect_t *owner,io_connect_t waiter,int close_owner)
{
    struct VIMSurfaceMapping mapping={0};struct VIMSurfaceState before={0},after={0};
    IOReturn acquired=lockSurface(*owner,&mapping,&before);
    if(acquired)return StateCheck("wait fixture acquires its owned surface",0);
    size_t size=sizeof(mapping);
    IOReturn recursive=IOConnectCallStructMethod(*owner,kIOVM2DWaitSurfaceBacking,NULL,0,&mapping,&size);
    size=sizeof(mapping);
    IOReturn same_thread=IOConnectCallStructMethod(waiter,kIOVM2DWaitSurfaceBacking,NULL,0,&mapping,&size);
    size=sizeof(mapping);
    IOReturn nonblocking=IOConnectCallStructMethod(waiter,kIOVM2DLockSurfaceBacking,NULL,0,&mapping,&size);
    int good=StateCheck("legacy try-lock and same-thread waits reject rather than deadlock",
        recursive==kIOReturnCannotLock && same_thread==kIOReturnCannotLock && nonblocking==kIOReturnCannotLock);
    struct VIMWaitProbe p={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,.client=waiter};
    pthread_t thread;
    if(pthread_create(&thread,NULL,VIMWaitBacking,&p)){
        unlockSurface(*owner,1,&after);return StateCheck("create independent waiting owner",0);
    }
    pthread_mutex_lock(&p.mutex);
    while(!p.entered)pthread_cond_wait(&p.condition,&p.mutex);
    struct timeval now;gettimeofday(&now,NULL);
    struct timespec until={now.tv_sec,now.tv_usec*1000+100000000};
    if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}
    int waited=0;
    while(!p.finished && !waited)waited=pthread_cond_timedwait(&p.condition,&p.mutex,&until);
    good=StateCheck("contended acquisition stays pending without stealing ownership",!p.finished)&&good;
    pthread_mutex_unlock(&p.mutex);
    IOReturn released=close_owner?IOServiceClose(*owner):unlockSurface(*owner,1,&after);
    if(close_owner)*owner=0;
    pthread_join(thread,NULL);
    good=StateCheck(close_owner?"closing owner wakes waiter with write invalidation":"read-only release wakes waiter with the same backing and content epoch",
        !released && !p.error && p.mapping.address && p.state.object==before.object &&
        p.state.incarnation==before.incarnation && p.state.content_epoch==before.content_epoch+!!close_owner &&
        (p.state.flags&VIM_SURFACE_STATE_CONTEXT_LOCKED))&&good;
    if(!p.error)good=StateCheck("awakened client owns and can release its mapping",!unlockSurface(waiter,1,&after))&&good;
    pthread_cond_destroy(&p.condition);pthread_mutex_destroy(&p.mutex);return good;
}
@interface VIMStateView : NSOpenGLView { BOOL _measured; }
@property BOOL measured;
@end
@implementation VIMStateView
@synthesize measured=_measured;
- (void)finish:(NSTimer *)timer { (void)timer;exit(outcome); }
- (void)drawRect:(NSRect)rect
{
    (void)rect;if(self.measured)return;self.measured=YES;
    [self.openGLContext makeCurrentContext];glClearColor(.2,.4,.8,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    uint32_t ids[3]={0};CGLError ce=CGLGetSurface(self.openGLContext.CGLContextObj,&ids[0],&ids[1],&ids[2]);
    fprintf(stderr,"renderer=%s CGS surface=%x get=%d GL-error=%x\n",glGetString(GL_RENDERER),ids[2],ce,glGetError());
    io_service_t accel=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t a=0,b=0;IOReturn e=accel ? IOServiceOpen(accel,mach_task_self(),2,&a) : kIOReturnNotFound;
    if(!e)e=IOServiceOpen(accel,mach_task_self(),2,&b);if(accel)IOObjectRelease(accel);
    uint64_t target[]={ids[2],0x800};uint32_t record[11];size_t n;
    for(unsigned i=0;!e && i<2;++i) {
        n=sizeof(record);e=IOConnectCallMethod(i?b:a,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&n);
    }
    struct VIMSurfaceMapping mapping={0};struct VIMSurfaceState first={0},after={0},next={0};
    int producer_deferred=1;
    if(!e && getenv("VIM_TEST_WAIT_ACQUIRE")){
        alarm(20);producer_deferred=VIMCheckContendedFlush(a);alarm(0);
    }
    if(!e && getenv("VIM_TEST_PRODUCER_DEFER")) {
        n=sizeof(mapping);IOReturn gpu=IOConnectCallStructMethod(a,kIOVM2DLockGPUSurface,NULL,0,&mapping,&n);
        producer_deferred=!gpu && !mapping.address && !mapping.length;
        if(!gpu) {
            n=sizeof(first);gpu=IOConnectCallStructMethod(a,kIOVM2DGetSurfaceState,NULL,0,&first,&n);
            producer_deferred=!gpu && (first.flags&VIM_SURFACE_STATE_GPU_NEWER) && producer_deferred;
            if(unlockSurface(a,1,&after))producer_deferred=0;
        }
        StateCheck("normal GL finish leaves GPU-current window pixels",producer_deferred);
        if(producer_deferred && getenv("VIM_TEST_GPU_REATTACH")) {
            CGLContextObj context=self.openGLContext.CGLContextObj;
            Dl_info active={0};dladdr((const void *)context->disp.flush,&active);
            void *engine=active.dli_fname?dlopen(active.dli_fname,RTLD_NOW|RTLD_LOCAL|RTLD_FIRST):NULL;
            CGLError (*attach)(GLIContext,unsigned,const void *,unsigned,unsigned)=engine?dlsym(engine,"gliAttachDrawableWithOptions"):NULL;
            Dl_info entry={0};if(attach)dladdr((const void *)attach,&entry);
            fprintf(stderr,"reattach context=%p renderer=%p entry=%s\n",context,context?context->rend:NULL,entry.dli_fname?:"unavailable");
            /* The compositor's unflipped attach contract, exercised on this
             * probe's own real CGS surface. Repeating an attachment must not
             * turn GPU-current contents into a CPU ownership transition. */
            for(unsigned repeat=0;repeat<2 && producer_deferred;++repeat) {
                CGLError ae=attach?attach(context->rend,0x5a,ids,0,0):kCGLBadState;
                unsigned char pixel[4]={0};if(!ae)glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                int pixels=!ae && !glGetError() && pixel[0]==51 && pixel[1]==102 && pixel[2]==204 && pixel[3]==255;
                n=sizeof(mapping);IOReturn ge=IOConnectCallStructMethod(a,kIOVM2DLockGPUSurface,NULL,0,&mapping,&n);
                struct VIMSurfaceState state={0};size_t sn=sizeof(state);
                if(!ge)ge=IOConnectCallStructMethod(a,kIOVM2DGetSurfaceState,NULL,0,&state,&sn);
                producer_deferred=pixels && !ge && n==sizeof(mapping) && sn==sizeof(state) &&
                    !mapping.address && !mapping.length && (state.flags&VIM_SURFACE_STATE_GPU_NEWER) &&
                    state.object==first.object && state.incarnation==first.incarnation && state.content_epoch==first.content_epoch;
                if(state.flags&VIM_SURFACE_STATE_CONTEXT_LOCKED)if(unlockSurface(a,1,&after))producer_deferred=0;
                fprintf(stderr,"reattach=%u error=%d gpu-lock=%x flags=%x epoch=%llu\n",repeat,ae,ge,state.flags,state.content_epoch);
            }
            StateCheck("repeated compositor attachments keep GPU ownership without CPU materialization",producer_deferred);
            if(engine)dlclose(engine);
        }
    }
    if(!e)e=lockSurface(a,&mapping,&first);
    fprintf(stderr,"state error=%x resource=%llx incarnation=%llu epoch=%llu flags=%x\n",e,
        first.object,first.incarnation,first.content_epoch,first.flags);
    int good=!ce && !e && producer_deferred;
    if(!e && getenv("VIM_TEST_KERNEL_HANDOFF")) {
        void *backend=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle/libVIMPassthroughOpenGL.dylib",RTLD_NOW|RTLD_LOCAL);
        uint32_t (*acquire)(uint64_t,uint64_t,int,int)=backend?dlsym(backend,"DGAcquireSurface"):NULL;
        int (*publish)(uint32_t,int,int,uint64_t)=backend?dlsym(backend,"DGPublishSurfaceEpoch"):NULL;
        int (*release)(uint32_t)=backend?dlsym(backend,"DGReleaseSurface"):NULL;
        uint32_t lease=acquire?acquire(first.object,first.incarnation,mapping.layout.width,mapping.layout.height):0;
        uint64_t published_epoch=first.content_epoch+10;
        int ready=lease && publish && release && publish(lease,0,0,published_epoch);
        struct VIMSurfaceMaterialize request={0};char boot[64]={0};size_t length=sizeof(boot);uuid_t uuid={0};
        ready=!sysctlbyname("kern.bootsessionuuid",boot,&length,NULL,0) && !uuid_parse(boot,uuid) && ready;
        memcpy(request.namespace_id,uuid,16);request.object=first.object;request.incarnation=first.incarnation;
        request.cpu_epoch=first.content_epoch;request.gpu_epoch=published_epoch;
        IOReturn wrong=kIOReturnError,stale=kIOReturnError;
        if(ready) {
            n=sizeof(after);wrong=IOConnectCallStructMethod(b,kIOVM2DMaterializeSurface,&request,sizeof(request),&after,&n);
            ++request.cpu_epoch;n=sizeof(after);stale=IOConnectCallStructMethod(a,kIOVM2DMaterializeSurface,&request,sizeof(request),&after,&n);
            --request.cpu_epoch;
            // Only our own window's locked memory. Its GPU drawable remains
            // blue; a no-op handoff cannot pass the pixel comparison.
            for(unsigned y=0;y<mapping.layout.height;++y)memset((void *)(uintptr_t)(mapping.address+mapping.layout.offset+(uint64_t)y*mapping.layout.row_bytes),0,mapping.layout.width*4);
            n=sizeof(after);e=IOConnectCallStructMethod(a,kIOVM2DMaterializeSurface,&request,sizeof(request),&after,&n);
            ready=!e && after.content_epoch>first.content_epoch && wrong==kIOReturnNotOpen && stale==kIOReturnNotReady;
            if(!e)for(unsigned y=0;y<mapping.layout.height;++y)for(unsigned x=0;x<mapping.layout.width;++x) {
                unsigned char *p=(void *)(uintptr_t)(mapping.address+mapping.layout.offset+(uint64_t)y*mapping.layout.row_bytes+x*4);
                ready=p[0]==204 && p[1]==102 && p[2]==51 && p[3]==255 && ready;
            }
            if(!e)first=after;
        }
        fprintf(stderr,"kernel handoff %ux%u status=%x wrong-owner=%x stale=%x\n",mapping.layout.width,mapping.layout.height,e,wrong,stale);
        good=StateCheck("kernel materialization restores real GL window pixels and rejects wrong owner/stale CPU epoch",ready) && good;
        if(ready && getenv("VIM_TEST_DEFERRED_HANDOFF")) {
            int (*seal)(uint32_t,uint64_t)=backend?dlsym(backend,"DGSealSurfaceVersion"):NULL;
            request.cpu_epoch=first.content_epoch;request.gpu_epoch=first.content_epoch+100;
            int deferred=seal && publish(lease,0,0,request.gpu_epoch) && seal(lease,request.gpu_epoch);
            if(deferred) {
                for(unsigned y=0;y<mapping.layout.height;++y)memset((void *)(uintptr_t)(mapping.address+mapping.layout.offset+(uint64_t)y*mapping.layout.row_bytes),0,mapping.layout.width*4);
                n=sizeof(after);e=IOConnectCallStructMethod(a,kIOVM2DDeferSurface,&request,sizeof(request),&after,&n);
                deferred=!e && (after.flags&VIM_SURFACE_STATE_GPU_NEWER) && after.content_epoch==request.gpu_epoch;
                // The old mapping is revoked. Never read its pointer after defer.
                if(!e)e=unlockSurface(a,1,&after);
                deferred=!e && (after.flags&VIM_SURFACE_STATE_GPU_NEWER) && deferred;
                if(!e && getenv("VIM_TEST_GPU_ACQUIRE")) {
                    n=sizeof(mapping);e=IOConnectCallStructMethod(a,kIOVM2DLockGPUSurface,NULL,0,&mapping,&n);
                    deferred=!e && !mapping.address && !mapping.length && deferred;
                    n=sizeof(after);if(!e)e=IOConnectCallStructMethod(a,kIOVM2DGetSurfaceState,NULL,0,&after,&n);
                    deferred=!e && (after.flags&VIM_SURFACE_STATE_GPU_NEWER) && after.content_epoch==request.gpu_epoch && deferred;
                    if(!e)e=unlockSurface(a,1,&after);
                    deferred=!e && (after.flags&VIM_SURFACE_STATE_GPU_NEWER) && deferred;
                }
                if(!e)e=lockSurface(a,&mapping,&after);
                deferred=!e && !(after.flags&VIM_SURFACE_STATE_GPU_NEWER) && after.content_epoch==request.gpu_epoch && deferred;
                if(!e)for(unsigned y=0;y<mapping.layout.height;++y)for(unsigned x=0;x<mapping.layout.width;++x) {
                    unsigned char *q=(void *)(uintptr_t)(mapping.address+mapping.layout.offset+(uint64_t)y*mapping.layout.row_bytes+x*4);
                    deferred=q[0]==204 && q[1]==102 && q[2]==51 && q[3]==255 && deferred;
                }
                if(!e)first=after;
            }
            good=StateCheck("deferred GPU ownership materializes on CPU acquire without changing epoch",deferred) && good;
        }
        if(lease && release)release(lease);if(backend)dlclose(backend);
    }
    if(!e) {
        IOReturn wrong=unlockSurface(b,1,&after);
        good=StateCheck("another 2D client cannot unlock this mapping",wrong==kIOReturnNotOpen) && good;
        n=sizeof(next);e=IOConnectCallStructMethod(a,kIOVM2DGetSurfaceState,NULL,0,&next,&n);
        good=StateCheck("read-only state queries preserve identity and epoch",!e && !memcmp(&first,&next,sizeof(first))) && good;
        e=unlockSurface(a,1,&after);
        good=StateCheck("read-only unlock does not dirty GPU storage",!e && after.object==first.object &&
            after.incarnation==first.incarnation && after.content_epoch==first.content_epoch) && good;
        if(!e)e=lockSurface(b,&mapping,&next);
        good=StateCheck("independent clients resolve the same kernel resource",!e && next.object==first.object &&
            next.incarnation==first.incarnation && next.content_epoch==first.content_epoch) && good;
        uint64_t option=0,flags=0;uint32_t count=1;
        if(!e)e=IOConnectCallScalarMethod(b,kIOVM2DUnlockMemory,&option,1,&flags,&count);
        if(!e)e=lockSurface(a,&mapping,&next);
        good=StateCheck("legacy write unlock invalidates content, not resource identity",!e && next.object==first.object &&
            next.incarnation==first.incarnation && next.content_epoch==first.content_epoch+1) && good;
        if(!e)e=unlockSurface(a,0,&after);
        good=StateCheck("private write unlock returns its exact committed epoch",!e && after.content_epoch==next.content_epoch+1 &&
            after.incarnation==next.incarnation && after.object==next.object) && good;
        if(!e)e=lockSurface(a,&mapping,&next);
        if(!e) { IOServiceClose(a);a=0;e=lockSurface(b,&mapping,&after); }
        good=StateCheck("closing a lock owner releases its mapping and invalidates possible writes",!e &&
            after.object==first.object && after.content_epoch==next.content_epoch+1) && good;
        struct VIMSurfaceState before=after;
        if(!e)e=unlockSurface(b,1,&after);
        uint64_t dimensions[]={2,mapping.layout.width+128,mapping.layout.height+128};
        if(!e)e=IOConnectCallScalarMethod(b,kIOVM2DScaleSurface,dimensions,3,NULL,NULL);
        if(!e)e=lockSurface(b,&mapping,&after);
        good=StateCheck("backing growth changes incarnation while preserving resource identity",!e &&
            after.object==before.object && after.incarnation>before.incarnation && after.content_epoch>before.content_epoch) && good;
        if(!e)e=unlockSurface(b,1,&after);
        n=sizeof(after);IOReturn unlocked=IOConnectCallStructMethod(b,kIOVM2DGetSurfaceState,NULL,0,&after,&n);
        good=StateCheck("metadata requires the calling client to own a mapping",unlocked==kIOReturnNotOpen) && good;
        if(!e)e=lockSurface(b,&mapping,&after);
        uint32_t seed=0xff3366cc;
        if(!e)memcpy((void *)(uintptr_t)(mapping.address+mapping.layout.offset+(uint64_t)3*mapping.layout.row_bytes+4*4),&seed,4);
        if(!e)e=unlockSurface(b,0,&after);
        struct VIMSurfaceState before_fill=after;
        IOBlitRectangle fill={3,2,5,7};uint64_t color=0x7f123456;
        if(!e)e=IOConnectCallMethod(b,kIOVM2DRectFill,&color,1,&fill,sizeof(fill),NULL,NULL,NULL,NULL);
        if(!e)e=lockSurface(b,&mapping,&after);
        uint32_t painted=0;
        if(!e && mapping.layout.row_bytes>=8*4 && mapping.length>=mapping.layout.offset+
            (uint64_t)3*mapping.layout.row_bytes+5*4) {
            memcpy(&painted,(void *)(uintptr_t)(mapping.address+mapping.layout.offset+
                (uint64_t)3*mapping.layout.row_bytes+4*4),4);
        }
        good=StateCheck("2D fill updates pixels and epoch without replacing the backing",!e &&
            painted==(seed&(uint32_t)color&0x00ffffffu) && after.object==before_fill.object &&
            after.incarnation==before_fill.incarnation && after.content_epoch>before_fill.content_epoch) && good;
        if(!e)e=unlockSurface(b,1,&after);
        before_fill=after;
        IOReturn malformed=IOConnectCallMethod(b,kIOVM2DRectFill,&color,1,&fill,sizeof(fill)-1,NULL,NULL,NULL,NULL);
        if(!e)e=lockSurface(b,&mapping,&after);
        good=StateCheck("truncated fill rejects without dirtying storage",!e && malformed==kIOReturnBadArgument &&
            after.content_epoch==before_fill.content_epoch && after.incarnation==before_fill.incarnation) && good;
        if(!e)e=unlockSurface(b,1,&after);
    }
    if(!e && getenv("VIM_TEST_WAIT_ACQUIRE")){
        if(!a){
            io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
            e=service?IOServiceOpen(service,mach_task_self(),2,&a):kIOReturnNotFound;
            if(service)IOObjectRelease(service);
            n=sizeof(record);
            if(!e)e=IOConnectCallMethod(a,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&n);
        }
        /* A test-only process deadline catches a broken wakeup instead of
         * wedging automation; the driver itself has no polling or timeout. */
        alarm(20);
        if(!e){good=VIMCheckWaitBacking(&a,b,0)&&good;good=VIMCheckWaitBacking(&a,b,1)&&good;}
        alarm(0);
    }
    if(a)IOServiceClose(a);if(b)IOServiceClose(b);
    if(e)fprintf(stderr,"surface state test error=%x\n",e);
    outcome=good && !e?0:1;
    [NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(finish:) userInfo:nil repeats:NO];
}
@end
int main(void)
{
    @autoreleasepool {
        NSApplication *app=[NSApplication sharedApplication];[app setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSOpenGLPixelFormatAttribute attrs[]={NSOpenGLPFAAccelerated,NSOpenGLPFANoRecovery,
            NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,0};
        NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];if(!format)return 2;
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,180,96,64)
            styleMask:NSTitledWindowMask|NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
        [window setTitle:@"Native resource identity and write epochs"];
        VIMStateView *view=[[VIMStateView alloc] initWithFrame:NSMakeRect(0,0,96,64) pixelFormat:format];
        [window setContentView:view];[window makeKeyAndOrderFront:nil];[app activateIgnoringOtherApps:YES];[app run];
    }
    return outcome;
}
