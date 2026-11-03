/* Bounded blocking byte streams for the SAME native GL interpreter inside
 * QEMU. Preserve partial I/O and lifetime/cancellation, without socket syscalls,
 * process hops, file backing, a spin loop or unbounded queued pixel replies.
 * No caller pointer is retained after an I/O callback returns. */
#include "passthrough-opengl-memory-stream.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#define DG_MEMORY_RING_BYTES (64u*1024u)
#define DG_MEMORY_STREAM_LIMIT 128u /* Same bound as the research device. */
struct DGMemoryRing {
    size_t first,used;
    unsigned char bytes[DG_MEMORY_RING_BYTES];
};
struct DGMemoryStream {
    pthread_mutex_t lock;
    pthread_cond_t readable[2],writable[2];
    struct DGMemoryRing ring[2];
    unsigned refs,closed,cancelled,client_closed,worker_finished;
    DGMemoryWorker worker;
};
static pthread_mutex_t dg_memory_count_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned dg_memory_count;
unsigned DGMemoryStreamLiveCount(void)
{
    pthread_mutex_lock(&dg_memory_count_lock);unsigned n=dg_memory_count;
    pthread_mutex_unlock(&dg_memory_count_lock);return n;
}
static void DGMemoryDestroy(struct DGMemoryStream *s)
{
    for(unsigned i=0;i<2;++i){pthread_cond_destroy(&s->readable[i]);pthread_cond_destroy(&s->writable[i]);}
    pthread_mutex_destroy(&s->lock);free(s);
    pthread_mutex_lock(&dg_memory_count_lock);--dg_memory_count;
    pthread_mutex_unlock(&dg_memory_count_lock);
}
static void DGMemoryWake(struct DGMemoryStream *s)
{
    for(unsigned i=0;i<2;++i){pthread_cond_broadcast(&s->readable[i]);pthread_cond_broadcast(&s->writable[i]);}
}
static void *DGMemoryRun(void *opaque)
{
    struct DGMemoryStream *s=opaque;s->worker(s);
    pthread_mutex_lock(&s->lock);s->closed=s->worker_finished=1;DGMemoryWake(s);
    unsigned destroy=!--s->refs;pthread_mutex_unlock(&s->lock);
    if(destroy)DGMemoryDestroy(s);return NULL;
}
struct DGMemoryStream *DGMemoryStreamOpen(DGMemoryWorker worker)
{
    if(!worker){errno=EINVAL;return NULL;}
    pthread_mutex_lock(&dg_memory_count_lock);
    if(dg_memory_count==DG_MEMORY_STREAM_LIMIT){pthread_mutex_unlock(&dg_memory_count_lock);errno=EMFILE;return NULL;}
    ++dg_memory_count;pthread_mutex_unlock(&dg_memory_count_lock);
    struct DGMemoryStream *s=calloc(1,sizeof(*s));int error=ENOMEM;
    unsigned mutex_ready=0,conditions=0;pthread_attr_t attr;int attr_ready=0;
    if(!s)goto fail;
    error=pthread_mutex_init(&s->lock,NULL);if(error)goto fail;mutex_ready=1;
    for(unsigned i=0;i<2;++i){
        error=pthread_cond_init(&s->readable[i],NULL);if(error)goto fail;++conditions;
        error=pthread_cond_init(&s->writable[i],NULL);if(error)goto fail;++conditions;
    }
    s->refs=2;s->worker=worker;
    error=pthread_attr_init(&attr);if(error)goto fail;attr_ready=1;
    error=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);if(error)goto fail;
    pthread_t thread;error=pthread_create(&thread,&attr,DGMemoryRun,s);
    pthread_attr_destroy(&attr);attr_ready=0;
    if(!error)return s;
fail:
    if(attr_ready)pthread_attr_destroy(&attr);
    if(s){
        while(conditions){--conditions;unsigned i=conditions/2;
            pthread_cond_destroy((conditions&1)?&s->writable[i]:&s->readable[i]);}
        if(mutex_ready)pthread_mutex_destroy(&s->lock);free(s);
    }
    pthread_mutex_lock(&dg_memory_count_lock);--dg_memory_count;
    pthread_mutex_unlock(&dg_memory_count_lock);errno=error;return NULL;
}
static ssize_t DGMemoryVectorIO(struct DGMemoryStream *s,int server,int writing,const struct iovec *v,int count)
{
    if(!s || count<0 || count>1024 || (count && !v)){errno=EINVAL;return -1;}
    size_t total=0;
    for(int i=0;i<count;++i){
        if(v[i].iov_len>(size_t)SSIZE_MAX-total || (v[i].iov_len && !v[i].iov_base)){errno=EINVAL;return -1;}
        total+=v[i].iov_len;
    }
    if(!total)return 0;
    unsigned direction=writing?(server?1:0):(server?0:1);
    pthread_mutex_lock(&s->lock);struct DGMemoryRing *r=&s->ring[direction];
    while(!s->closed && (writing?r->used==DG_MEMORY_RING_BYTES:!r->used)){
        int error=pthread_cond_wait(writing?&s->writable[direction]:&s->readable[direction],&s->lock);
        if(error){pthread_mutex_unlock(&s->lock);errno=error;return -1;}
    }
    if((writing && s->closed) || s->cancelled || (!writing && !r->used)){
        pthread_mutex_unlock(&s->lock);if(writing){errno=EPIPE;return -1;}return 0;
    }
    size_t available=writing?DG_MEMORY_RING_BYTES-r->used:r->used;
    size_t wanted=total<available?total:available,done=0;
    for(int i=0;i<count && done<wanted;++i){
        unsigned char *bytes=v[i].iov_base;size_t left=v[i].iov_len;
        if(left>wanted-done)left=wanted-done;
        while(left){
            size_t at=writing?(r->first+r->used)%DG_MEMORY_RING_BYTES:r->first;
            size_t n=DG_MEMORY_RING_BYTES-at;if(n>left)n=left;
            if(writing){memcpy(r->bytes+at,bytes,n);r->used+=n;}
            else {memcpy(bytes,r->bytes+at,n);r->first=(at+n)%DG_MEMORY_RING_BYTES;r->used-=n;}
            bytes+=n;left-=n;done+=n;
        }
    }
    pthread_cond_signal(writing?&s->readable[direction]:&s->writable[direction]);
    pthread_mutex_unlock(&s->lock);return (ssize_t)done;
}
ssize_t DGMemoryClientReadv(struct DGMemoryStream *s,const struct iovec *v,int n)
{return DGMemoryVectorIO(s,0,0,v,n);}
ssize_t DGMemoryClientWritev(struct DGMemoryStream *s,const struct iovec *v,int n)
{return DGMemoryVectorIO(s,0,1,v,n);}
ssize_t DGMemoryClientRead(struct DGMemoryStream *s,void *p,size_t n)
{struct iovec v={p,n};return DGMemoryVectorIO(s,0,0,&v,1);}
ssize_t DGMemoryClientWrite(struct DGMemoryStream *s,const void *p,size_t n)
{struct iovec v={(void *)p,n};return DGMemoryVectorIO(s,0,1,&v,1);}
ssize_t DGMemoryServerRead(struct DGMemoryStream *s,void *p,size_t n)
{struct iovec v={p,n};return DGMemoryVectorIO(s,1,0,&v,1);}
ssize_t DGMemoryServerWrite(struct DGMemoryStream *s,const void *p,size_t n)
{struct iovec v={(void *)p,n};return DGMemoryVectorIO(s,1,1,&v,1);}
ssize_t DGMemoryServerWritev(struct DGMemoryStream *s,const struct iovec *v,int n)
{return DGMemoryVectorIO(s,1,1,v,n);}
int DGMemoryStreamShutdown(struct DGMemoryStream *s)
{
    if(!s){errno=EINVAL;return -1;}
    pthread_mutex_lock(&s->lock);s->closed=s->cancelled=1;DGMemoryWake(s);
    pthread_mutex_unlock(&s->lock);return 0;
}
static int DGMemoryClose(struct DGMemoryStream *s,int wait)
{
    if(!s){errno=EINVAL;return -1;}
    pthread_mutex_lock(&s->lock);
    if(s->client_closed){pthread_mutex_unlock(&s->lock);errno=EBADF;return -1;}
    // Graceful EOF: the interpreter must still see already accepted void
    // commands (e.g. a final publication before producer destruction).
    // A reset uses Shutdown first and deliberately aborts that old generation.
    s->client_closed=s->closed=1;DGMemoryWake(s);
    // Keep the client's reference while asleep. Shutdown/EOF can wake a
    // reader before the worker has retired its GPU resources; only callback
    // return qualifies this wait. Closed output unblocks an unread reply.
    while(wait && !s->worker_finished)pthread_cond_wait(&s->readable[0],&s->lock);
    unsigned destroy=!--s->refs;pthread_mutex_unlock(&s->lock);
    if(destroy)DGMemoryDestroy(s);return 0;
}
int DGMemoryStreamClose(struct DGMemoryStream *s)
{return DGMemoryClose(s,0);}
int DGMemoryStreamCloseAndWait(struct DGMemoryStream *s)
{return DGMemoryClose(s,1);}
