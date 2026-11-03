/* Explicit asynchronous-backing prototype. The ticket retains a private
 * completion context/fence independently of its rendering endpoint. This does
 * NOT retain a guest pin or permit CPU access: the kernel must own both until
 * actual Wait succeeds. No implicit retirement on EOF or cancellation.
 * Bounded outstanding tickets, sleeping native waits only for a real job;
 * no periodic worker, timer-based completion, pixels or process pointers. */
#ifndef VIM_VRAM_RENDER_TARGET_EXPERIMENT
#error Asynchronous backing requires explicit scoped-render ownership
#endif
#if defined(VIM_BACKING_CANONICAL_EXPERIMENT) && !defined(VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT)
#error Qualify asynchronous and canonical backing separately
#endif
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
#if !defined(VIM_BACKING_CANONICAL_EXPERIMENT) || !defined(VIM_BACKING_FUTURE_EXPERIMENT) || !defined(VIM_BACKING_WRITER_EXPERIMENT)
#error Combined canonical ownership requires canonical storage, sealed future versions and retained GPU writers
#endif
/* Explicit combined qualification, never inferred from either earlier
 * experiment: canonical publication avoids a copy; future/writer ownership
 * must independently retain immutable readers and CPU exclusion. */
#endif
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
struct DGSharedSurface;
struct DGSurfaceSnapshot;
static void DGSurfacePut(struct DGSharedSurface *);
static void DGSurfaceSnapshotDestroy(struct DGSurfaceSnapshot *);
#endif
#include "../vendor/qemu/include/hw/display/vim-passthrough-opengl-backing-notify.h"
struct DGBackingTicket {
    unsigned refs,completed,subscribed;
    uint64_t name,namespace_id[2];
    struct DGCompletionQueue *queue;
    GLsync fence;
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
    unsigned publish_future;
    struct DGSharedSurface *future_source;
    struct DGSurfaceSnapshot *future_version;
#endif
#ifdef VIM_BACKING_WRITER_EXPERIMENT
    uint64_t writer_backing[9]; // offset,length,width,height,pitch,object,incarnation,GPU epoch,kind
#endif
    struct DGBackingTicket *next;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    struct DGScatterPlan *scatter_plan;
    struct DGBackingTicket *cpu_predecessor;
    pthread_mutex_t scatter_lock;
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    uint64_t cpu_successor; // Registry-locked NAME, not a reference cycle.
    unsigned scatter_failed;
#endif
#endif
};
static pthread_mutex_t dg_backing_ticket_lock=PTHREAD_MUTEX_INITIALIZER;
static struct DGBackingTicket *dg_backing_tickets;
static uint64_t dg_backing_ticket_serial;
static unsigned dg_backing_ticket_count;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
static int DGBackingTicketScatter(struct DGBackingTicket *);
static int DGBackingTicketSealCPU(struct DGHostContext *,struct DGBackingTicket *);
#endif
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
static volatile int dg_backing_cpu_coalescing=1; // Same-process oracle comparison only.
static uint64_t dg_cpu_coalescing_stats[4]; // groups, skipped CPU copies, avoided bytes, forwarded waits
static int DGBackingTicketCanCoalesce(struct DGBackingTicket *,struct DGBackingTicket *);
static int DGBackingTicketWaitCoalesced(struct DGBackingTicket *);
#endif
static uint64_t dg_backing_deferred_submissions,dg_backing_sync_waits;
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
static int DGBackingTicketSealFuture(struct DGHostContext *,struct DGBackingTicket *,
    uint64_t,uint64_t,uint64_t,int,int);
#endif
static void DGBackingTicketPut(struct DGBackingTicket *p)
{
    if(!p || __sync_sub_and_fetch(&p->refs,1))return;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    DGScatterPlanPut(p->scatter_plan);DGBackingTicketPut(p->cpu_predecessor);
    pthread_mutex_destroy(&p->scatter_lock);
#endif
    if(p->fence)DGCompletionQueueDelete(p->queue,p->fence);
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
    DGSurfaceSnapshotDestroy(p->future_version);
    if(p->future_source)DGSurfacePut(p->future_source);
#endif
    DGCompletionQueuePut(p->queue);free(p);
}
static struct DGBackingTicket *DGBackingTicketPrepare(struct DGHostContext *c)
{
    if(!(c->surface_namespace[0]|c->surface_namespace[1]))return NULL;
    struct DGBackingTicket *p=calloc(1,sizeof(*p));if(!p)return NULL;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    if(pthread_mutex_init(&p->scatter_lock,NULL)){free(p);return NULL;}
#endif
    p->refs=1;p->queue=DGCompletionQueueGet(c);
    if(!p->queue){
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
        pthread_mutex_destroy(&p->scatter_lock);
#endif
        free(p);return NULL;
    }
    memcpy(p->namespace_id,c->surface_namespace,sizeof(p->namespace_id));
    pthread_mutex_lock(&dg_backing_ticket_lock);
    int allowed=dg_backing_ticket_count<256 && dg_backing_ticket_serial<UINT64_MAX;
    if(allowed){p->name=++dg_backing_ticket_serial;++dg_backing_ticket_count;}
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    if(!allowed){DGBackingTicketPut(p);return NULL;}return p;
}
static void DGBackingTicketCancel(struct DGBackingTicket *p)
{
    /* Only before publication; caller drains any submitted GPU work first. */
    pthread_mutex_lock(&dg_backing_ticket_lock);--dg_backing_ticket_count;
    pthread_mutex_unlock(&dg_backing_ticket_lock);DGBackingTicketPut(p);
}
static int DGBackingTicketPublish(struct DGBackingTicket *p)
{
    p->fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    glFlush();if(!p->fence || glGetError())return 0;
    pthread_mutex_lock(&dg_backing_ticket_lock);
    p->next=dg_backing_tickets;dg_backing_tickets=p;
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    // Only a SEALED, submitted successor may cover an older CPU write.
    // An active/unpublished writer must never become a wait dependency.
    struct DGBackingTicket *older=p->cpu_predecessor;
    if(dg_backing_cpu_coalescing && older && !older->cpu_successor &&
       !__atomic_load_n(&older->completed,__ATOMIC_ACQUIRE) && DGBackingTicketCanCoalesce(p,older))
        older->cpu_successor=p->name;
#endif
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    __atomic_fetch_add(&dg_backing_deferred_submissions,1,__ATOMIC_RELAXED);return 1;
}
static int DGBackingTicketMatches(const struct DGBackingTicket *p,const uint64_t namespace_id[2],uint64_t name)
{return p->name==name && !memcmp(p->namespace_id,namespace_id,sizeof(p->namespace_id));}
static int DGBackingTicketWaitOrdered(struct DGBackingTicket *p)
{
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    if(p->scatter_plan){
        // Names increase monotonically. A writer waits only on an OLDER
        // CPU materialization, before acquiring its own native queue lock.
        // A per-ticket lock prevents duplicate Wait from replaying an old
        // CPU copy after a later writer has already materialized new bytes.
        pthread_mutex_lock(&p->scatter_lock);
        if(!__atomic_load_n(&p->completed,__ATOMIC_ACQUIRE)){
            int good=!p->cpu_predecessor || (p->cpu_predecessor->name<p->name &&
                !DGBackingTicketWaitOrdered(p->cpu_predecessor));
            GLenum status=GL_WAIT_FAILED;
            if(good)do{status=DGCompletionQueueWait(p->queue,p->fence);}while(status==GL_TIMEOUT_EXPIRED);
            if((status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED) && DGBackingTicketScatter(p))
                __atomic_store_n(&p->completed,1,__ATOMIC_RELEASE);
        }
        int error=__atomic_load_n(&p->completed,__ATOMIC_ACQUIRE)?0:EIO;
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
        if(error)__atomic_store_n(&p->scatter_failed,1,__ATOMIC_RELEASE);
#endif
        pthread_mutex_unlock(&p->scatter_lock);return error;
    }
#endif
    GLenum status=GL_ALREADY_SIGNALED;
    if(!__atomic_load_n(&p->completed,__ATOMIC_ACQUIRE)){
        do {status=DGCompletionQueueWait(p->queue,p->fence);}while(status==GL_TIMEOUT_EXPIRED);
        if(status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED)
            __atomic_store_n(&p->completed,1,__ATOMIC_RELEASE);
    }
    return __atomic_load_n(&p->completed,__ATOMIC_ACQUIRE)?0:EIO;
}
static int DGBackingTicketWait(struct DGBackingTicket *p)
{
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    if(dg_backing_cpu_coalescing && p->scatter_plan && p->publish_future && p->future_source &&
       !__atomic_load_n(&p->completed,__ATOMIC_ACQUIRE))return DGBackingTicketWaitCoalesced(p);
#endif
    return DGBackingTicketWaitOrdered(p);
}
struct DGBackingNotification {
    struct DGBackingTicket *ticket;
    VIMGLBackingNotifyCallback callback;
    void *opaque;
};
static void DGBackingTicketNotify(void *opaque)
{
    struct DGBackingNotification *job=opaque;
    int status=DGBackingTicketWait(job->ticket);
    /* Never call a consumer while holding the registry or auxiliary CGL
     * mutex. It may schedule a device BH or release its completed ticket. */
    job->callback(job->opaque,status);
    DGBackingTicketPut(job->ticket);free(job);
}
static int DGBackingTicketSubscribe(const uint64_t namespace_id[2],uint64_t name,
    VIMGLBackingNotifyCallback callback,void *opaque)
{
    if(!namespace_id || !(namespace_id[0]|namespace_id[1]) || !name || !callback)return EINVAL;
    struct DGBackingNotification *job=calloc(1,sizeof(*job));if(!job)return ENOMEM;
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    struct DGCommandProfileStamp profile=DGCommandProfileBegin();
#endif
    pthread_mutex_lock(&dg_backing_ticket_lock);
    struct DGBackingTicket *p=dg_backing_tickets;
    while(p && !DGBackingTicketMatches(p,namespace_id,name))p=p->next;
    int error=!p?EINVAL:p->subscribed?EALREADY:0;
    if(!error && !p->queue->backing_notifications){
        p->queue->backing_notifications=dispatch_queue_create("com.mac.virtualintelmac.gl-backing-completion",NULL);
        if(!p->queue->backing_notifications)error=ENOMEM;
    }
    if(!error){
        p->subscribed=1;__sync_add_and_fetch(&p->refs,1);
        job->ticket=p;job->callback=callback;job->opaque=opaque;
    }
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    if(error)free(job);
    /* Do not access job after dispatch: callback is allowed before return.
     * The retained ticket also retains the dispatch and native CGL queues. */
    else dispatch_async_f(p->queue->backing_notifications,job,DGBackingTicketNotify);
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    DGCommandProfileEnd(profile,DG_PROFILE_BACKING_SUBSCRIBE,0,0);
#endif
    return error;
}
static int DGBackingTicketReleaseCompleted(const uint64_t namespace_id[2],uint64_t name)
{
    if(!namespace_id || !(namespace_id[0]|namespace_id[1]) || !name)return EINVAL;
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    struct DGCommandProfileStamp profile=DGCommandProfileBegin();
#endif
    pthread_mutex_lock(&dg_backing_ticket_lock);
    struct DGBackingTicket **link=&dg_backing_tickets;
    while(*link && !DGBackingTicketMatches(*link,namespace_id,name))link=&(*link)->next;
    struct DGBackingTicket *p=*link;
    int error=!p?EINVAL:!__atomic_load_n(&p->completed,__ATOMIC_ACQUIRE)?EBUSY:0;
    if(!error){*link=p->next;--dg_backing_ticket_count;}
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    if(!error)DGBackingTicketPut(p);
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    DGCommandProfileEnd(profile,DG_PROFILE_BACKING_RETIRE,0,0);
#endif
    return error;
}
static int DGBackingTicketCommand(const uint64_t namespace_id[2],const struct DGMessage *m,struct DGReply *reply)
{
    if(m->context || m->bytes || m->flags!=DG_REPLY || !m->args[0] ||
       !(namespace_id[0]|namespace_id[1])){reply->error=GL_INVALID_VALUE;return 0;}
    for(unsigned i=1;i<16;++i)if(m->args[i]){reply->error=GL_INVALID_VALUE;return 0;}
    if(m->op==DG_BACKING_COMPLETION_RELEASE){
        int error=DGBackingTicketReleaseCompleted(namespace_id,m->args[0]);
        if(!error)reply->result=1;
        else reply->error=error==EBUSY?GL_INVALID_OPERATION:GL_INVALID_VALUE;
        return 0;
    }
    pthread_mutex_lock(&dg_backing_ticket_lock);
    struct DGBackingTicket **link=&dg_backing_tickets;
    while(*link && !DGBackingTicketMatches(*link,namespace_id,m->args[0]))link=&(*link)->next;
    struct DGBackingTicket *p=*link;
    if(!p){pthread_mutex_unlock(&dg_backing_ticket_lock);reply->error=GL_INVALID_VALUE;return 0;}
    if(m->op==DG_BACKING_COMPLETION_CHECK){
        pthread_mutex_unlock(&dg_backing_ticket_lock);reply->result=1;return 0;
    }
    __sync_add_and_fetch(&p->refs,1);pthread_mutex_unlock(&dg_backing_ticket_lock);
    if(!DGBackingTicketWait(p))reply->result=1;
    else reply->error=GL_INVALID_OPERATION; // retain failed ticket; never grant CPU ownership
    DGBackingTicketPut(p);return 0;
}
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS
__attribute__((visibility("default")))
const struct VIMGLBackingNotifyAPI *VIMPassthroughOpenGLBackingNotifyGetAPI(uint32_t version,uint32_t bytes)
{
    static const struct VIMGLBackingNotifyAPI api={VIM_GL_BACKING_NOTIFY_ABI,sizeof(api),
        DGBackingTicketSubscribe,DGBackingTicketReleaseCompleted};
    return version==api.version && bytes==sizeof(api)?&api:NULL;
}
#endif
