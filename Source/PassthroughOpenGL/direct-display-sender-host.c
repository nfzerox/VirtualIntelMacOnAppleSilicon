/* Explicit display handoff. Sealing inserts a producer fence; the command or
 * context-free service thread only queues that immutable version. A worker
 * uses its retained private completion queue, never a live producer context;
 * Mach send-once releases retain each version through consumer
 * GPU completion. Three outstanding versions bound memory and backpressure.
 * One display-owner connection per executor is supported by this path. */
#include "display-surface-message.h"
#include <servers/bootstrap.h>
#include <mach/mach_time.h>
#include <mach/notify.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef VIM_DISPLAY_JOURNAL
#include "direct-display-journal-host.c"
#endif
struct DGDisplaySubmission {
    struct DGSurfaceSnapshot *version;
    mach_port_t reply;
    uint64_t ticket;
    int sent,attempted;
};
struct DGDisplaySender {
    const void *owner;
    pthread_t thread;
    pthread_mutex_t lock;
    mach_port_t destination,wake,events;
    uint64_t generation,next_ticket;
    int stopping;
    struct DGDisplaySubmission slots[3];
};
static pthread_mutex_t dg_display_sender_lock=PTHREAD_MUTEX_INITIALIZER;
static struct DGDisplaySender *dg_display_sender;
static void DGDisplaySenderWake(struct DGDisplaySender *s)
{
    mach_msg_header_t m={0};m.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0);
    m.msgh_size=sizeof(m);m.msgh_remote_port=s->wake;m.msgh_id=1;
    /* A full wake queue already guarantees the worker will inspect all jobs. */
    mach_msg(&m,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(m),0,0,0,0);
}
static mach_port_t DGDisplayDestination(void)
{
    const char *path=getenv("VIM_OPENGL_DISPLAY_ENDPOINT");if(!path || !*path)return 0;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
    struct stat st;unsigned char bytes[65536];ssize_t used=0;
    int good=!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==geteuid() &&
        !(st.st_mode&0022) && st.st_size>0 && st.st_size<(off_t)sizeof(bytes);
    while(good && used<st.st_size){
        ssize_t n=read(fd,bytes+used,(size_t)(st.st_size-used));
        if(n<0 && errno==EINTR)continue;
        if(n<=0){good=0;break;}used+=n;
    }
    close(fd);if(!good)return 0;
    CFDataRef data=CFDataCreate(NULL,bytes,used);
    CFPropertyListRef plist=data?CFPropertyListCreateWithData(NULL,data,kCFPropertyListImmutable,NULL,NULL):NULL;
    if(data)CFRelease(data);
    mach_port_t port=0;
    if(plist && CFGetTypeID(plist)==CFDictionaryGetTypeID()){
        CFTypeRef version=CFDictionaryGetValue(plist,CFSTR("Version"));
        CFTypeRef pid=CFDictionaryGetValue(plist,CFSTR("RendererPID"));
        CFTypeRef service=CFDictionaryGetValue(plist,CFSTR("Service"));
        int64_t v=0,p=0;char name[BOOTSTRAP_MAX_NAME_LEN];
        if(version && CFGetTypeID(version)==CFNumberGetTypeID() &&
           pid && CFGetTypeID(pid)==CFNumberGetTypeID() &&
           service && CFGetTypeID(service)==CFStringGetTypeID() &&
           CFNumberGetValue(version,kCFNumberSInt64Type,&v) && v==1 &&
           CFNumberGetValue(pid,kCFNumberSInt64Type,&p) && p==getpid() &&
           CFStringGetCString(service,name,sizeof(name),kCFStringEncodingUTF8))
            if(bootstrap_look_up(bootstrap_port,name,&port))port=0;
    }
    if(plist)CFRelease(plist);return port;
}
static void DGDisplaySubmissionRelease(struct DGDisplaySubmission *job,int safe)
{
    if(job->version && job->attempted && !safe)__atomic_store_n(&job->version->recyclable,0,__ATOMIC_RELEASE);
    if(job->reply)mach_port_destroy(mach_task_self(),job->reply);
    DGSurfaceSnapshotDestroy(job->version);memset(job,0,sizeof(*job));
}
static void *DGDisplaySenderWorker(void *opaque)
{
    struct DGDisplaySender *s=opaque;
    for(;;){
        pthread_mutex_lock(&s->lock);
        int stopping=s->stopping;unsigned next=3;uint64_t ticket=UINT64_MAX;
        for(unsigned i=0;i<3;++i)if(s->slots[i].version && !s->slots[i].sent && s->slots[i].ticket<ticket){next=i;ticket=s->slots[i].ticket;}
        pthread_mutex_unlock(&s->lock);if(stopping)break;
        if(next<3){
            struct DGDisplaySubmission *job=&s->slots[next];
            GLenum ready=DGCompletionQueueWait(job->version->completion_queue,job->version->completion);
            if(ready==GL_TIMEOUT_EXPIRED)continue;
            int good=ready==GL_ALREADY_SIGNALED || ready==GL_CONDITION_SATISFIED;
#ifdef VIM_DISPLAY_JOURNAL
            if(good)DGJournalSurfaceFrame(job->version,s->generation,job->ticket);
#endif
            if(good){job->attempted=1;good=!VIMSurfaceFrameSend(s->destination,job->reply,job->version->storage,
                s->generation,job->ticket,next,job->version->width,job->version->height,0);}
            pthread_mutex_lock(&s->lock);
            if(good)job->sent=1;
            else DGDisplaySubmissionRelease(job,0);
            pthread_mutex_unlock(&s->lock);
            continue;
        }
        struct {struct VIMSurfaceReleaseMessage m;mach_msg_max_trailer_t trailer;} packet;
        memset(&packet,0,sizeof(packet));
        /* Wake and release messages share one port set. Idle work is blocked
         * here, with no frame timer or receive polling. Stop wakes this port. */
        kern_return_t kr=mach_msg(&packet.m.header,MACH_RCV_MSG,0,sizeof(packet),s->events,0,0);
        if(kr!=KERN_SUCCESS)break;
        pthread_mutex_lock(&s->lock);
        for(unsigned i=0;i<3;++i){
            struct DGDisplaySubmission *job=&s->slots[i];
            if(!job->version || !job->sent || packet.m.header.msgh_local_port!=job->reply)continue;
            int released=packet.m.header.msgh_id==MACH_NOTIFY_SEND_ONCE &&
                packet.m.header.msgh_size==sizeof(mach_send_once_notification_t);
            int safe=0;
            if(packet.m.header.msgh_size==sizeof(packet.m) &&
               packet.m.header.msgh_id==VIM_SURFACE_FRAME_MESSAGE+1 &&
               !(packet.m.header.msgh_bits&MACH_MSGH_BITS_COMPLEX) &&
               packet.m.generation==s->generation && packet.m.ticket==job->ticket &&
               packet.m.slot==i && packet.m.status<=VIMSurfaceFailed){released=1;safe=packet.m.status!=VIMSurfaceFailed;}
            if(released)DGDisplaySubmissionRelease(job,safe);
        }
        pthread_mutex_unlock(&s->lock);mach_msg_destroy(&packet.m.header);
    }
    pthread_mutex_lock(&s->lock);s->stopping=1;
    for(unsigned i=0;i<3;++i)DGDisplaySubmissionRelease(&s->slots[i],0);
    pthread_mutex_unlock(&s->lock);
    return NULL;
}
static void DGDisplaySenderFree(struct DGDisplaySender *s)
{
    if(s->events)mach_port_destroy(mach_task_self(),s->events);
    if(s->wake)mach_port_destroy(mach_task_self(),s->wake);
    if(s->destination)mach_port_deallocate(mach_task_self(),s->destination);
    pthread_mutex_destroy(&s->lock);free(s);
}
static struct DGDisplaySender *DGDisplaySenderCreate(const void *owner)
{
    mach_port_t destination=DGDisplayDestination();if(!destination)return NULL;
    struct DGDisplaySender *s=calloc(1,sizeof(*s));
    if(!s){mach_port_deallocate(mach_task_self(),destination);return NULL;}
    s->destination=destination;s->owner=owner;s->generation=mach_absolute_time();s->next_ticket=1;
    pthread_mutex_init(&s->lock,NULL);
    if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_PORT_SET,&s->events) ||
       mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&s->wake) ||
       mach_port_insert_right(mach_task_self(),s->wake,s->wake,MACH_MSG_TYPE_MAKE_SEND) ||
       mach_port_move_member(mach_task_self(),s->wake,s->events) ||
       pthread_create(&s->thread,NULL,DGDisplaySenderWorker,s)){
        DGDisplaySenderFree(s);return NULL;
    }
    return s;
}
static void DGDisplaySenderStop(const void *owner)
{
    pthread_mutex_lock(&dg_display_sender_lock);struct DGDisplaySender *s=dg_display_sender;
    if(s && s->owner==owner){
        dg_display_sender=NULL;pthread_mutex_lock(&s->lock);s->stopping=1;pthread_mutex_unlock(&s->lock);
        DGDisplaySenderWake(s);pthread_join(s->thread,NULL);DGDisplaySenderFree(s);
    }
    pthread_mutex_unlock(&dg_display_sender_lock);
}
static GLenum DGDisplaySubmitSealed(const void *owner,struct DGSurfaceSnapshot *v,uint64_t *ticket)
{
    if(!owner || !v || !v->completion || !v->completion_queue || !ticket ||
       v->width<320 || v->height<200)return GL_INVALID_VALUE;
    pthread_mutex_lock(&dg_display_sender_lock);
    if(!dg_display_sender)dg_display_sender=DGDisplaySenderCreate(owner);
    struct DGDisplaySender *s=dg_display_sender;GLenum error=GL_INVALID_OPERATION;
    if(s && s->owner==owner){
        pthread_mutex_lock(&s->lock);
        for(unsigned i=0;i<3 && !s->stopping;++i)if(!s->slots[i].version && s->next_ticket!=UINT64_MAX){
            struct DGDisplaySubmission *job=&s->slots[i];
            if(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&job->reply) &&
               !mach_port_move_member(mach_task_self(),job->reply,s->events)){
                __sync_add_and_fetch(&v->refs,1);job->version=v;job->ticket=s->next_ticket++;
                *ticket=job->ticket;error=GL_NO_ERROR;
            }
            if(error && job->reply){mach_port_destroy(mach_task_self(),job->reply);job->reply=0;}
            break;
        }
        pthread_mutex_unlock(&s->lock);if(!error)DGDisplaySenderWake(s);
    }
    pthread_mutex_unlock(&dg_display_sender_lock);return error;
}
static GLenum DGDisplaySubmitVersion(struct DGHostContext *c,struct DGSurfaceSnapshot *v,uint64_t *ticket)
{
    if(!c || !v || v->producer!=c->cgl || CGLGetCurrentContext()!=c->cgl)return GL_INVALID_OPERATION;
    return DGDisplaySubmitSealed(c,v,ticket);
}
static int DGDisplaySenderReady(const void *owner)
{
    if(!owner)return 0;
    pthread_mutex_lock(&dg_display_sender_lock);
    if(!dg_display_sender)dg_display_sender=DGDisplaySenderCreate(owner);
    struct DGDisplaySender *s=dg_display_sender;int ready=0;
    if(s && s->owner==owner){
        pthread_mutex_lock(&s->lock);
        for(unsigned i=0;i<3 && !s->stopping;++i)
            if(!s->slots[i].version && s->next_ticket!=UINT64_MAX){ready=1;break;}
        pthread_mutex_unlock(&s->lock);
    }
    pthread_mutex_unlock(&dg_display_sender_lock);return ready;
}
