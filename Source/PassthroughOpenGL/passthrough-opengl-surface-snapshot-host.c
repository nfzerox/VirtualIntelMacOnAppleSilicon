/* Private immutable pixel versions. No GL texture/FBO name escapes creation,
 * so later writes through producer aliases cannot change a retained version.
 * This first primitive uses a GPU copy, never a guest/CPU pixel round trip.
 * Wire ownership and GPU consumer binding are integrated separately. */
#include <OpenGL/CGLDevice.h>
/* A consumed version can be reused only AFTER the last actual source read in
 * each native consumer stream. Fences never cross native share groups. The
 * fixed bound prevents a retained version accumulating unbounded metadata;
 * overflow/foreign/error keeps the original non-recyclable behavior. */
#define DG_SNAPSHOT_READERS 8
struct DGSnapshotReader { CGLContextObj context;GLsync fence; };
struct DGSurfaceSnapshot {
    unsigned refs;
    IOSurfaceRef storage;
    CGLContextObj producer;
    struct DGCompletionQueue *completion_queue;
    GLsync completion;
    uint64_t epoch;
    size_t bytes;
    int width,height;
    GLuint native_texture;
    struct DGPrivateTextureWatch texture_watch;
    int recyclable;
    struct DGSnapshotReader readers[DG_SNAPSHOT_READERS];
    struct DGSnapshotPool *pool;
};
static pthread_mutex_t dg_snapshot_lock=PTHREAD_MUTEX_INITIALIZER;
static size_t dg_snapshot_bytes;
static size_t dg_snapshot_spare_bytes;
static volatile int dg_snapshot_storage_reuse=1; // Same-process diagnostic A/B only.
static volatile uint64_t dg_snapshot_storage_allocations,dg_snapshot_storage_reuses;
static volatile int dg_snapshot_shared_source=1; // Same-process native A/B only.
static uint64_t dg_snapshot_source_reuses,dg_snapshot_source_imports;
static volatile int dg_snapshot_framebuffer_reuse=1; // Same-process native A/B only.
static uint64_t dg_snapshot_framebuffer_allocations; // Existing report: consumer import binding allocations only.
static uint64_t dg_snapshot_seal_framebuffer_allocations,dg_snapshot_seal_framebuffer_reuses;
static volatile int dg_snapshot_consumer_reuse=1;
static uint64_t dg_snapshot_consumer_fences,dg_snapshot_consumer_waits;
static uint64_t dg_snapshot_consumer_foreign,dg_snapshot_consumer_refusals;
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
static void DGSnapshotRecyclePolicy(uint32_t flags,struct DGSnapshotRecyclePolicyReport *out)
{
    if(flags!=UINT32_MAX)__atomic_store_n(&dg_snapshot_consumer_reuse,!!flags,__ATOMIC_RELEASE);
    *out=(struct DGSnapshotRecyclePolicyReport){1,
        __atomic_load_n(&dg_snapshot_consumer_reuse,__ATOMIC_ACQUIRE),
        __atomic_load_n(&dg_snapshot_storage_allocations,__ATOMIC_RELAXED),
        __atomic_load_n(&dg_snapshot_storage_reuses,__ATOMIC_RELAXED),
        __atomic_load_n(&dg_snapshot_consumer_fences,__ATOMIC_RELAXED),
        __atomic_load_n(&dg_snapshot_consumer_waits,__ATOMIC_RELAXED),
        __atomic_load_n(&dg_snapshot_consumer_foreign,__ATOMIC_RELAXED),
        __atomic_load_n(&dg_snapshot_consumer_refusals,__ATOMIC_RELAXED)};
}
/* Present ONLY in an explicitly instrumented executor. A finite standalone
 * guest command can A/B this owned policy without host ptrace or VM restarts.
 * Flags do not alter fences, image allocation, ownership or completion.
 * Independent counters are cumulative attempts, not successful GPU work.
 */
static void DGSnapshotImportPolicy(uint32_t flags,struct DGSnapshotImportPolicyReport *out)
{
    if(flags!=UINT32_MAX){
        __atomic_store_n(&dg_snapshot_shared_source,!!(flags&1),__ATOMIC_RELEASE);
        __atomic_store_n(&dg_snapshot_framebuffer_reuse,!!(flags&2),__ATOMIC_RELEASE);
    }
    *out=(struct DGSnapshotImportPolicyReport){1,
        __atomic_load_n(&dg_snapshot_shared_source,__ATOMIC_ACQUIRE)|
        (__atomic_load_n(&dg_snapshot_framebuffer_reuse,__ATOMIC_ACQUIRE)<<1),
        __atomic_load_n(&dg_snapshot_source_reuses,__ATOMIC_RELAXED),
        __atomic_load_n(&dg_snapshot_source_imports,__ATOMIC_RELAXED),
        __atomic_load_n(&dg_snapshot_framebuffer_allocations,__ATOMIC_RELAXED)};
}
#endif
/* Called in the actual consumer context after its source blit. Nothing may
 * publish this fence before the consumer stream is flushed. Same-producer
 * reads need no extra command: future pool overwrite uses that same stream.
 * The caller owns a version reference through this operation. */
static void DGSnapshotRecordConsumer(struct DGHostContext *c,struct DGSurfaceSnapshot *v)
{
    if(!__atomic_load_n(&dg_snapshot_consumer_reuse,__ATOMIC_ACQUIRE)){
        __atomic_store_n(&v->recyclable,0,__ATOMIC_RELEASE);return;
    }
    CGLShareGroupObj group=CGLGetShareGroup(c->cgl);
    if(!group || !v->producer || group!=CGLGetShareGroup(v->producer)){
        __atomic_fetch_add(&dg_snapshot_consumer_foreign,1,__ATOMIC_RELAXED);
        __atomic_store_n(&v->recyclable,0,__ATOMIC_RELEASE);return;
    }
    if(c->cgl==v->producer || !__atomic_load_n(&v->recyclable,__ATOMIC_ACQUIRE))return;
    GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
    if(!fence || glGetError()){
        if(fence)glDeleteSync(fence);
        __atomic_fetch_add(&dg_snapshot_consumer_refusals,1,__ATOMIC_RELAXED);
        __atomic_store_n(&v->recyclable,0,__ATOMIC_RELEASE);return;
    }
    GLsync old=NULL;int accepted=0;
    pthread_mutex_lock(&dg_snapshot_lock);
    unsigned slot=DG_SNAPSHOT_READERS;
    for(unsigned i=0;i<DG_SNAPSHOT_READERS;++i){
        if(v->readers[i].context==c->cgl){slot=i;break;}
        if(!v->readers[i].context && slot==DG_SNAPSHOT_READERS)slot=i;
    }
    if(slot<DG_SNAPSHOT_READERS){
        struct DGSnapshotReader *r=&v->readers[slot];old=r->fence;
        if(!r->context){r->context=c->cgl;CGLRetainContext(r->context);}
        r->fence=fence;accepted=1;
    }
    pthread_mutex_unlock(&dg_snapshot_lock);
    // Later fences in ONE serialized native context cover its earlier reads.
    if(old)glDeleteSync(old);
    if(accepted)__atomic_fetch_add(&dg_snapshot_consumer_fences,1,__ATOMIC_RELAXED);
    else{
        glDeleteSync(fence);__atomic_store_n(&v->recyclable,0,__ATOMIC_RELEASE);
        __atomic_fetch_add(&dg_snapshot_consumer_refusals,1,__ATOMIC_RELAXED);
    }
}
/* Only last-reference destruction or a detached pool spare calls this. Native
 * group ownership is already established. WaitSync orders FUTURE producer
 * writes after the consumer's source reads; no client wait/Finish/readback.
 * DeleteSync after the queued wait is valid (ARB_sync deferred deletion).
 */
static int DGSnapshotReleaseReaders(struct DGSnapshotReader *readers,int wait)
{
    int found=0;
    for(unsigned i=0;i<DG_SNAPSHOT_READERS;++i){
        struct DGSnapshotReader *r=&readers[i];if(!r->context)continue;
        found=1;
        if(wait){glWaitSync(r->fence,0,GL_TIMEOUT_IGNORED);
            __atomic_fetch_add(&dg_snapshot_consumer_waits,1,__ATOMIC_RELAXED);}
        glDeleteSync(r->fence);CGLReleaseContext(r->context);*r=(struct DGSnapshotReader){0};
    }
    return !found || !glGetError();
}
/* Eviction may run on the existing context-free snapshot cleanup queue.
 * Use its owned auxiliary context; never call native GL with no context or
 * borrow another endpoint's live producer. No pool/global lock is held. */
static void DGSnapshotDiscardReaders(struct DGCompletionQueue *q,CGLContextObj producer,
    struct DGSnapshotReader *readers)
{
    int found=0;for(unsigned i=0;i<DG_SNAPSHOT_READERS;++i)if(readers[i].context)found=1;
    if(!found)return;
    if(CGLGetCurrentContext()==producer && producer){DGSnapshotReleaseReaders(readers,0);return;}
    struct DGCompletionCleanup cleanup;
    if(DGCompletionCleanupBegin(q,&cleanup)){
        DGSnapshotReleaseReaders(readers,0);DGCompletionCleanupEnd(&cleanup);
    }
}
#include "passthrough-opengl-snapshot-pool-host.c"
/* A private IOSurface gets a GPU-only copy, not a writable alias to the sealed
 * version. Preserve native imported-storage semantics across later rebinds. */
static GLenum DGSurfaceSnapshotCopyTexture(struct DGHostContext *c,
    struct DGSurfaceSnapshot *v,GLenum internal,int width,int height)
{
    if(!v || c->inside_begin || width<1 || height<1 || width>v->width || height>v->height ||
       (internal!=GL_RGB && internal!=GL_RGB8 && internal!=GL_RGBA && internal!=GL_RGBA8))return GL_INVALID_VALUE;
    GLint target=0,read=0,draw=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&target);
    if(!target)return GL_INVALID_OPERATION;
    size_t row=((size_t)width*4+63)&~(size_t)63;
    CFMutableDictionaryRef props=CFDictionaryCreateMutable(NULL,0,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!props)return GL_OUT_OF_MEMORY;
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int64_t values[]={width,height,4,(int64_t)row,(int64_t)(row*height),0x42475241};
    int allocated=1;
    for(unsigned i=0;i<6;++i) {
        CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,&values[i]);
        if(n) { CFDictionarySetValue(props,keys[i],n);CFRelease(n); } else allocated=0;
    }
    IOSurfaceRef consumer=allocated?IOSurfaceCreate(props):NULL;CFRelease(props);
    if(!consumer)return GL_OUT_OF_MEMORY;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    // One endpoint serializes this CGL context. Reuse empty PRIVATE binding
    // objects, not images. Never overwrite an already bound private FBO if a
    // diagnostic caller has explicitly borrowed one of its names.
    int reuse_framebuffers=__atomic_load_n(&dg_snapshot_framebuffer_reuse,__ATOMIC_ACQUIRE) &&
        (!c->snapshot_read_framebuffer || ((GLuint)read!=c->snapshot_read_framebuffer && (GLuint)draw!=c->snapshot_read_framebuffer)) &&
        (!c->snapshot_draw_framebuffer || ((GLuint)read!=c->snapshot_draw_framebuffer && (GLuint)draw!=c->snapshot_draw_framebuffer));
    /* The immutable version already owns this native texture. Reuse its
     * actual GL object only within the SAME non-null native share group;
     * a resource/namespace ID does not establish native object sharing.
     * The destination is still a NEW independent IOSurface on every import.
     * Source reuse requires the actual consumer-read dependencies recorded
     * below; foreign groups and failed tracking remain non-recyclable.
     */
    CGLShareGroupObj group=__atomic_load_n(&dg_snapshot_shared_source,__ATOMIC_ACQUIRE) && v->producer && v->completion && v->native_texture &&
        v->native_texture!=(GLuint)target?CGLGetShareGroup(c->cgl):NULL;
    int shared=group && group==CGLGetShareGroup(v->producer);
    GLuint texture=shared?v->native_texture:0,framebuffer=0;
    int good=shared;
    if(!shared){
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
        good=texture && !DGPrivateTextureImport(c->cgl,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,
            v->width,v->height,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,v->storage,0);
        __atomic_fetch_add(&dg_snapshot_source_imports,1,__ATOMIC_RELAXED);
    }else{
        /* Sharing a name is not producer completion. Insert the real
         * producer dependency in a DIFFERENT consumer stream, then rebind
         * the shared object after its producer flush. No CPU fence wait. */
        if(c->cgl!=v->producer)glWaitSync(v->completion,0,GL_TIMEOUT_IGNORED);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
        __atomic_fetch_add(&dg_snapshot_source_reuses,1,__ATOMIC_RELAXED);
    }
    if(reuse_framebuffers){
        if(!c->snapshot_read_framebuffer){
            glGenFramebuffersEXT(1,&c->snapshot_read_framebuffer);
            if(c->snapshot_read_framebuffer)__atomic_fetch_add(&dg_snapshot_framebuffer_allocations,1,__ATOMIC_RELAXED);
        }
        framebuffer=c->snapshot_read_framebuffer;
    }else{
        glGenFramebuffersEXT(1,&framebuffer);
        if(framebuffer)__atomic_fetch_add(&dg_snapshot_framebuffer_allocations,1,__ATOMIC_RELAXED);
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    good=framebuffer && glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT && good;
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)target);
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    if(good) {
        good=!CGLTexImageIOSurface2D(c->cgl,GL_TEXTURE_RECTANGLE_ARB,internal,
            width,height,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,consumer,0);
        GLuint destination=0;
        if(reuse_framebuffers){
            if(!c->snapshot_draw_framebuffer){
                glGenFramebuffersEXT(1,&c->snapshot_draw_framebuffer);
                if(c->snapshot_draw_framebuffer)__atomic_fetch_add(&dg_snapshot_framebuffer_allocations,1,__ATOMIC_RELAXED);
            }
            destination=c->snapshot_draw_framebuffer;
        }else{
            glGenFramebuffersEXT(1,&destination);
            if(destination)__atomic_fetch_add(&dg_snapshot_framebuffer_allocations,1,__ATOMIC_RELAXED);
        }
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,destination);
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB,(GLuint)target,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        good=good && destination && glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
        GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
        DGSetBlitScissor(c,GL_FALSE);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
        if(good)glBlitFramebufferEXT(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        if(scissor)DGSetBlitScissor(c,GL_TRUE);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB_EXT);
        // A cached binding must never retain a deleted/redefined consumer.
        if(destination)glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB,0,0);
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
        if(destination && !reuse_framebuffers)glDeleteFramebuffersEXT(1,&destination);
        good=glGetError()==GL_NO_ERROR && good;
        if(good)good=DGRegisterAlias(c,(GLuint)target,consumer);
    }
    DGRestoreColorTransfer(&transfer);
    if(framebuffer)glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB,0,0);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    if(framebuffer && !reuse_framebuffers)glDeleteFramebuffersEXT(1,&framebuffer);
    if(texture && !shared)glDeleteTextures(1,&texture);
    CFRelease(consumer);
    GLenum error=glGetError();
    if(!error && good)DGSnapshotRecordConsumer(c,v);
    else __atomic_store_n(&v->recyclable,0,__ATOMIC_RELEASE);
    return error?error:(good?GL_NO_ERROR:GL_INVALID_OPERATION);
}
#include <dispatch/dispatch.h>
static dispatch_queue_t dg_snapshot_cleanup;
static pthread_once_t dg_snapshot_cleanup_once=PTHREAD_ONCE_INIT;
static void DGSnapshotCleanupInitialize(void)
{ dg_snapshot_cleanup=dispatch_queue_create("com.virtualintelmac.research.gpu-version-release",DISPATCH_QUEUE_SERIAL); }
static void DGSurfaceSnapshotFree(void *opaque)
{
    struct DGSurfaceSnapshot *v=opaque;
    DGCompletionQueueDelete(v->completion_queue,v->completion);
    if(!DGSnapshotPoolReturn(v)){
        DGSnapshotDiscardReaders(v->completion_queue,v->producer,v->readers);
        DGSnapshotStorageFree(v->completion_queue,v->producer,v->storage,v->native_texture,v->bytes);
    }
    if(v->producer)CGLReleaseContext(v->producer);
    DGPrivateTextureWatchForget(&v->texture_watch);
    DGSnapshotPoolPut(v->pool);DGCompletionQueuePut(v->completion_queue);
    free(v);
}
static void DGSurfaceSnapshotDestroy(struct DGSurfaceSnapshot *v)
{
    if(!v || __sync_sub_and_fetch(&v->refs,1))return;
    if(!v->completion || CGLGetCurrentContext()==v->producer){
        if(v->completion){glDeleteSync(v->completion);v->completion=NULL;}
        DGSurfaceSnapshotFree(v);return;
    }
    // A context-free unhold/teardown must not block the kernel flush or
    // publisher on another version's GPU wait in the auxiliary context.
    // Reuse this allocation as its cleanup job. Its bytes remain charged
    // against the existing bounded snapshot budget until cleanup completes.
    pthread_once(&dg_snapshot_cleanup_once,DGSnapshotCleanupInitialize);
    if(dg_snapshot_cleanup)dispatch_async_f(dg_snapshot_cleanup,v,DGSurfaceSnapshotFree);
    else DGSurfaceSnapshotFree(v); // Allocation failure: retain safe synchronous cleanup.
}
static void DGSnapshotCleanupBarrier(void *unused){(void)unused;}
/* Diagnostic teardown only; never called by normal presentation/flush. */
static void DGSnapshotCleanupDrain(void)
{
    pthread_once(&dg_snapshot_cleanup_once,DGSnapshotCleanupInitialize);
    if(dg_snapshot_cleanup)dispatch_sync_f(dg_snapshot_cleanup,NULL,DGSnapshotCleanupBarrier);
}
/* Caller owns p->resource->access through command submission. This also lets
 * a trusted compound publication submit its publish + seal together without
 * exposing a new resource epoch to another context before glFlush. */
static struct DGSurfaceSnapshot *DGSurfaceSnapshotCreateLocked(struct DGHostContext *c,
    struct DGSurfaceLease *p,uint64_t epoch,int exported)
{
    if(!c || !p || !epoch || c->inside_begin || CGLGetCurrentContext()!=c->cgl)return NULL;
    struct DGSharedSurface *s=p->resource;
    if(s->retired || s->epoch!=epoch)return NULL;
    size_t row=((size_t)s->width*4+63)&~(size_t)63,bytes=row*(size_t)s->height;
    struct DGSurfaceSnapshot *v=calloc(1,sizeof(*v));
    if(!v)return NULL;
    v->refs=1;v->width=s->width;v->height=s->height;v->epoch=epoch;v->recyclable=1;
    v->producer=c->cgl;CGLRetainContext(v->producer);
    v->completion_queue=DGCompletionQueueGet(c);v->pool=DGSnapshotPoolGet(c);
    if(!v->completion_queue || !v->pool){DGSurfaceSnapshotDestroy(v);return NULL;}
    int reused=DGSnapshotPoolTake(v);
    pthread_mutex_lock(&dg_snapshot_lock);
    int allowed=reused || bytes<=DG_DISPLAY_BUFFER_BUDGET-dg_snapshot_bytes;
    if(allowed && !reused){dg_snapshot_bytes+=bytes;v->bytes=bytes;}
    pthread_mutex_unlock(&dg_snapshot_lock);
    if(!allowed){DGSurfaceSnapshotDestroy(v);return NULL;}
    CFMutableDictionaryRef props=reused?NULL:CFDictionaryCreateMutable(NULL,0,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int64_t values[]={s->width,s->height,4,(int64_t)row,(int64_t)bytes,0x42475241};
    int good=reused || props!=NULL;
    for(unsigned i=0;!reused && good && i<6;++i) {
        CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,&values[i]);
        if(n) { CFDictionarySetValue(props,keys[i],n);CFRelease(n); } else good=0;
    }
    if(good && !reused){v->storage=IOSurfaceCreate(props);__sync_fetch_and_add(&dg_snapshot_storage_allocations,1);}
    if(props)CFRelease(props);
    good=v->storage!=NULL;
    if(good && !reused) {
        size_t actual=IOSurfaceGetAllocSize(v->storage);
        pthread_mutex_lock(&dg_snapshot_lock);
        good=actual<=DG_DISPLAY_BUFFER_BUDGET-(dg_snapshot_bytes-v->bytes);
        if(good) { dg_snapshot_bytes=dg_snapshot_bytes-v->bytes+actual;v->bytes=actual; }
        pthread_mutex_unlock(&dg_snapshot_lock);
    }
    if(good) {
        GLint texture,read,draw,source_read;
        glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
        GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
        // Sealing is serialized in this producer context, just like importing
        // a version. Reuse ONLY the empty binding object, never its attachment
        // or pixels. A caller-bound scratch name must retain its own state;
        // fall back to a temporary in that case, including source aliases.
        int reuse_framebuffer=__atomic_load_n(&dg_snapshot_framebuffer_reuse,__ATOMIC_ACQUIRE) &&
            (!c->snapshot_draw_framebuffer ||
             ((GLuint)read!=c->snapshot_draw_framebuffer && (GLuint)draw!=c->snapshot_draw_framebuffer &&
              p->framebuffer!=c->snapshot_draw_framebuffer));
        GLuint f=reuse_framebuffer?c->snapshot_draw_framebuffer:0;
        if(f)__atomic_fetch_add(&dg_snapshot_seal_framebuffer_reuses,1,__ATOMIC_RELAXED);
        if(!v->native_texture){
            glGenTextures(1,&v->native_texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,v->native_texture);
            good=v->native_texture && !DGPrivateTextureImport(c->cgl,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,
                s->width,s->height,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,v->storage,0);
            DGPrivateTextureWatchName(&v->texture_watch,&v->native_texture);
        }
        if(!f){
            glGenFramebuffersEXT(1,&f);
            if(f){
                __atomic_fetch_add(&dg_snapshot_seal_framebuffer_allocations,1,__ATOMIC_RELAXED);
                if(reuse_framebuffer)c->snapshot_draw_framebuffer=f;
            }
        }
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,f);
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,v->native_texture,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        good=good && f && glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
        glGetIntegerv(GL_READ_BUFFER,&source_read);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        DGSetBlitScissor(c,GL_FALSE);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
        if(good)glBlitFramebufferEXT(0,0,s->width,s->height,0,0,s->width,s->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        // A cached binding must detach on success AND failure before restoring
        // the caller's draw binding. Temporary bindings retain their original
        // delete-based cleanup, so the diagnostic OFF path stays comparable.
        if(f && reuse_framebuffer)glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB,0,0);
        glReadBuffer((GLenum)source_read);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);
        if(scissor)DGSetBlitScissor(c,GL_TRUE);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB_EXT);
        if(good && exported){
            if(v->completion_queue)v->completion=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
            good=v->completion_queue && v->completion;
        }
        /* Exported immutable versions retain their fence and submission.
         * A transient copy source stays entirely in THIS producer's stream:
         * its caller submits snapshot+dependent blits as ONE operation before
         * publishing/unlocking. Same-context command ordering also protects
         * producer-local pool reuse after those reads. Never export this
         * fence-less object or hand it to another CGL context. Failed creation
         * still submits its possible prefix before retiring the storage. */
        if(exported || !good)glFlush();
        if(f && !reuse_framebuffer)glDeleteFramebuffersEXT(1,&f);
        good=glGetError()==GL_NO_ERROR && good;
    }
    if(!good) {
        if(!exported)glFlush(); // Includes errors discovered during GL cleanup.
        v->recyclable=0;DGSurfaceSnapshotDestroy(v);return NULL;
    }
    return v;
}
static struct DGSurfaceSnapshot *DGSurfaceSnapshotCreateCommands(struct DGHostContext *c,
    struct DGSurfaceLease *p,uint64_t epoch,int exported)
{
    if(!p)return NULL;
    pthread_mutex_lock(&p->resource->access);
    struct DGSurfaceSnapshot *v=DGSurfaceSnapshotCreateLocked(c,p,epoch,exported);
    pthread_mutex_unlock(&p->resource->access);return v;
}
static struct DGSurfaceSnapshot *DGSurfaceSnapshotCreate(struct DGHostContext *c,
    struct DGSurfaceLease *p,uint64_t epoch)
{ return DGSurfaceSnapshotCreateCommands(c,p,epoch,1); }
