/* Anonymous Mach memory-entry transport for i386 -> x64 inside one guest.
 * A connection owns two bounded SPSC rings, serialized by the existing GL
 * client/interpreter. Mach messages carry rights and wakeups, never pixels.
 * No polling thread, file backing, TCP stack or per-command allocation.
 * No-senders notifications unblock a waiter when its peer crashes. */
#include "passthrough-opengl-mach-stream.h"
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/notify.h>
#include <servers/bootstrap.h>
#include <bsm/libbsm.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

#define DG_MACH_SERVICE "com.mac.virtualintelmac.opengl32"
#define DG_MACH_BYTES (256u*1024u)
#define DG_MACH_OPEN 0x56474d31
#define DG_MACH_READY 0x56474d32
#define DG_MACH_WAKE 0x56474d33
#define DG_MACH_LIMIT 128u
struct DGMachRing {
    uint32_t produced,consumed,read_wait,write_wait;
    unsigned char bytes[DG_MACH_BYTES];
};
struct DGMachShared {
    uint32_t version,bytes,aborted,eof,done,reserved[3];
    struct DGMachRing ring[2];
};
struct DGMachStream {
    struct DGMachShared *shared;
    mach_port_t receive,peer;
    int server;
    DGMachWorker worker;
};
struct DGMachOpenMessage {
    mach_msg_header_t header;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t memory;
    uint32_t version,bytes;
};
struct DGMachReadyMessage {
    mach_msg_header_t header;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t event;
};
static unsigned dg_mach_live;
unsigned DGMachLiveCount(void){return __atomic_load_n(&dg_mach_live,__ATOMIC_RELAXED);}
static uint32_t DGMachLoad(uint32_t *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void DGMachStore(uint32_t *p,uint32_t value){__atomic_store_n(p,value,__ATOMIC_RELEASE);}
static mach_msg_return_t DGMachWakePort(mach_port_t port)
{
    mach_msg_header_t message={0};
    message.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0);
    message.msgh_size=sizeof(message);message.msgh_remote_port=port;message.msgh_id=DG_MACH_WAKE;
    return mach_msg(&message,MACH_SEND_MSG|MACH_SEND_TIMEOUT,
        sizeof(message),0,MACH_PORT_NULL,0,MACH_PORT_NULL);
}
static int DGMachWake(struct DGMachStream *s)
{
    mach_msg_return_t r=DGMachWakePort(s->peer);
    // A full queue already contains a wakeup. Never block the GL producer.
    if(r==MACH_MSG_SUCCESS || r==MACH_SEND_TIMED_OUT)return 0;
    DGMachStore(&s->shared->aborted,1);return -1;
}
static int DGMachWait(struct DGMachStream *s)
{
    union {mach_msg_header_t header;unsigned char bytes[512];} message;
    mach_msg_return_t r=mach_msg(&message.header,MACH_RCV_MSG,0,sizeof(message),
        s->receive,MACH_MSG_TIMEOUT_NONE,MACH_PORT_NULL);
    if(r!=MACH_MSG_SUCCESS || message.header.msgh_id==MACH_NOTIFY_NO_SENDERS){
        DGMachStore(&s->shared->aborted,1);return -1;
    }
    if(message.header.msgh_bits&MACH_MSGH_BITS_COMPLEX)mach_msg_destroy(&message.header);
    return 0;
}
static int DGMachEvent(mach_port_t *port)
{
    if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,port))return -1;
    mach_port_t old=MACH_PORT_NULL;
    // MAKE_SEND during the handshake increments the count to1. No local
    // send right is retained, so peer death really produces no-senders.
    if(mach_port_request_notification(mach_task_self(),*port,MACH_NOTIFY_NO_SENDERS,
        1,*port,MACH_MSG_TYPE_MAKE_SEND_ONCE,&old)){
        mach_port_destroy(mach_task_self(),*port);*port=MACH_PORT_NULL;return -1;
    }
    if(old)mach_port_deallocate(mach_task_self(),old);return 0;
}
static void DGMachDispose(struct DGMachStream *s)
{
    if(s->peer)mach_port_deallocate(mach_task_self(),s->peer);
    if(s->receive)mach_port_destroy(mach_task_self(),s->receive);
    if(s->shared)mach_vm_deallocate(mach_task_self(),(mach_vm_address_t)(uintptr_t)s->shared,sizeof(*s->shared));
    free(s);__sync_sub_and_fetch(&dg_mach_live,1);
}
static struct DGMachStream *DGMachAllocate(void)
{
    if(__sync_add_and_fetch(&dg_mach_live,1)>DG_MACH_LIMIT){
        __sync_sub_and_fetch(&dg_mach_live,1);errno=EMFILE;return NULL;}
    struct DGMachStream *s=calloc(1,sizeof(*s));
    if(!s){__sync_sub_and_fetch(&dg_mach_live,1);return NULL;}
    if(DGMachEvent(&s->receive)){DGMachDispose(s);return NULL;}return s;
}
struct DGMachStream *DGMachOpen(void)
{
    mach_port_t service=MACH_PORT_NULL,memory=MACH_PORT_NULL;
    if(bootstrap_look_up(bootstrap_port,DG_MACH_SERVICE,&service))return NULL;
    struct DGMachStream *s=DGMachAllocate();if(!s)goto fail;
    mach_vm_address_t address=0;
    if(mach_vm_allocate(mach_task_self(),&address,sizeof(*s->shared),VM_FLAGS_ANYWHERE))goto fail;
    s->shared=(void *)(uintptr_t)address;
    s->shared->version=1;s->shared->bytes=sizeof(*s->shared);
    memory_object_size_t bytes=sizeof(*s->shared);
    if(mach_make_memory_entry_64(mach_task_self(),&bytes,address,VM_PROT_READ|VM_PROT_WRITE,&memory,MACH_PORT_NULL))goto fail;
    struct DGMachOpenMessage request={0};
    request.header.msgh_bits=MACH_MSGH_BITS_COMPLEX|MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,MACH_MSG_TYPE_MAKE_SEND);
    request.header.msgh_size=sizeof(request);request.header.msgh_remote_port=service;
    request.header.msgh_local_port=s->receive;request.header.msgh_id=DG_MACH_OPEN;
    request.body.msgh_descriptor_count=1;request.memory.name=memory;
    request.memory.disposition=MACH_MSG_TYPE_COPY_SEND;request.memory.type=MACH_MSG_PORT_DESCRIPTOR;
    request.version=1;request.bytes=sizeof(*s->shared);
    if(mach_msg(&request.header,MACH_SEND_MSG,sizeof(request),0,MACH_PORT_NULL,0,MACH_PORT_NULL))goto fail;
    mach_port_deallocate(mach_task_self(),memory);memory=MACH_PORT_NULL;
    mach_port_deallocate(mach_task_self(),service);service=MACH_PORT_NULL;
    union {struct DGMachReadyMessage ready;unsigned char bytes[512];} reply;
    if(mach_msg(&reply.ready.header,MACH_RCV_MSG,0,sizeof(reply),s->receive,0,MACH_PORT_NULL))goto fail;
    if(reply.ready.header.msgh_id!=DG_MACH_READY || reply.ready.header.msgh_size!=sizeof(reply.ready) ||
       !(reply.ready.header.msgh_bits&MACH_MSGH_BITS_COMPLEX) || reply.ready.body.msgh_descriptor_count!=1 ||
       reply.ready.event.type!=MACH_MSG_PORT_DESCRIPTOR || reply.ready.event.disposition!=MACH_MSG_TYPE_PORT_SEND){
        mach_msg_destroy(&reply.ready.header);goto fail;
    }
    s->peer=reply.ready.event.name;return s;
fail:
    if(memory)mach_port_deallocate(mach_task_self(),memory);
    if(service)mach_port_deallocate(mach_task_self(),service);
    if(s)DGMachDispose(s);errno=ECONNREFUSED;return NULL;
}
static ssize_t DGMachVectorIO(struct DGMachStream *s,int writing,const struct iovec *vectors,int count)
{
    if(!s || count<0 || count>1024 || (count&&!vectors)){errno=EINVAL;return -1;}
    size_t total=0;
    for(int i=0;i<count;++i){
        if(vectors[i].iov_len>(size_t)SSIZE_MAX-total || (vectors[i].iov_len&&!vectors[i].iov_base)){errno=EINVAL;return -1;}
        total+=vectors[i].iov_len;
    }
    if(!total)return 0;
    struct DGMachRing *ring=&s->shared->ring[writing?s->server:!s->server];
    uint32_t *wait=writing?&ring->write_wait:&ring->read_wait;
    uint32_t produced,consumed,available;
    for(;;){
        produced=DGMachLoad(&ring->produced);consumed=DGMachLoad(&ring->consumed);
        if(produced-consumed>DG_MACH_BYTES){DGMachShutdown(s);errno=EPROTO;return -1;}
        available=writing?DG_MACH_BYTES-(produced-consumed):produced-consumed;
        if(DGMachLoad(&s->shared->aborted) || (writing&&DGMachLoad(&s->shared->eof))){errno=EPIPE;return -1;}
        if(available)break;
        if(!writing && (s->server?DGMachLoad(&s->shared->eof):DGMachLoad(&s->shared->done)))return 0;
        DGMachStore(wait,1);
        // Recheck after publishing the waiter, closing the lost-wakeup race.
        if(produced!=DGMachLoad(&ring->produced) || consumed!=DGMachLoad(&ring->consumed) ||
           DGMachLoad(&s->shared->aborted) || DGMachLoad(&s->shared->eof) || DGMachLoad(&s->shared->done)){
            DGMachStore(wait,0);continue;
        }
        int r=DGMachWait(s);DGMachStore(wait,0);if(r){errno=EPIPE;return -1;}
    }
    size_t wanted=total<available?total:available,done=0;
    uint32_t position=writing?produced:consumed;
    for(int i=0;i<count&&done<wanted;++i){
        unsigned char *p=vectors[i].iov_base;size_t left=vectors[i].iov_len;
        if(left>wanted-done)left=wanted-done;
        while(left){
            size_t at=position&(DG_MACH_BYTES-1),n=DG_MACH_BYTES-at;if(n>left)n=left;
            if(writing)memcpy(ring->bytes+at,p,n);else memcpy(p,ring->bytes+at,n);
            p+=n;position+=(uint32_t)n;done+=n;left-=n;
        }
    }
    DGMachStore(writing?&ring->produced:&ring->consumed,position);
    if(__atomic_exchange_n(writing?&ring->read_wait:&ring->write_wait,0,__ATOMIC_ACQ_REL))DGMachWake(s);
    return (ssize_t)done;
}
ssize_t DGMachReadv(struct DGMachStream *s,const struct iovec *v,int n){return DGMachVectorIO(s,0,v,n);}
ssize_t DGMachWritev(struct DGMachStream *s,const struct iovec *v,int n){return DGMachVectorIO(s,1,v,n);}
ssize_t DGMachRead(struct DGMachStream *s,void *p,size_t n){struct iovec v={p,n};return DGMachReadv(s,&v,1);}
ssize_t DGMachWrite(struct DGMachStream *s,const void *p,size_t n){struct iovec v={(void *)p,n};return DGMachWritev(s,&v,1);}
int DGMachShutdown(struct DGMachStream *s)
{
    if(!s){errno=EINVAL;return -1;}DGMachStore(&s->shared->aborted,1);DGMachWake(s);
    // Wake an I/O thread in our own process as well as the remote peer.
    if(!mach_port_insert_right(mach_task_self(),s->receive,s->receive,MACH_MSG_TYPE_MAKE_SEND)){
        DGMachWakePort(s->receive);
        mach_port_deallocate(mach_task_self(),s->receive);
    }
    return 0;
}
int DGMachClose(struct DGMachStream *s)
{
    if(!s || s->server){errno=EINVAL;return -1;}
    DGMachStore(&s->shared->eof,1);DGMachWake(s);
    while(!DGMachLoad(&s->shared->done))if(DGMachWait(s))break;
    int completed=DGMachLoad(&s->shared->done);DGMachDispose(s);
    if(!completed){errno=EPIPE;return -1;}return 0;
}
static void *DGMachRun(void *opaque)
{
    struct DGMachStream *s=opaque;s->worker(s);
    // Worker return means its interpreter/GPU resources have actually drained.
    DGMachStore(&s->shared->done,1);DGMachWake(s);DGMachDispose(s);return NULL;
}
int DGMachServe(DGMachWorker worker)
{
    mach_port_t listener=MACH_PORT_NULL;
    if(!worker || mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&listener) ||
       mach_port_insert_right(mach_task_self(),listener,listener,MACH_MSG_TYPE_MAKE_SEND))return 1;
    if(bootstrap_register(bootstrap_port,DG_MACH_SERVICE,listener)){
        fprintf(stderr,"PassthroughOpenGL guest service already registered or unavailable\n");
        mach_port_destroy(mach_task_self(),listener);return 1;
    }
    for(;;){
        union {struct DGMachOpenMessage open;unsigned char bytes[1024];} message;
        mach_msg_return_t result=mach_msg(&message.open.header,MACH_RCV_MSG|
            MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0)|MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),
            0,sizeof(message),listener,0,MACH_PORT_NULL);
        if(result)break;
        struct DGMachOpenMessage *request=&message.open;
        mach_msg_audit_trailer_t *audit=(void *)(message.bytes+round_msg(request->header.msgh_size));
        if(request->header.msgh_id!=DG_MACH_OPEN || request->header.msgh_size!=sizeof(*request) ||
           !(request->header.msgh_bits&MACH_MSGH_BITS_COMPLEX) || request->body.msgh_descriptor_count!=1 ||
           request->memory.type!=MACH_MSG_PORT_DESCRIPTOR || request->memory.disposition!=MACH_MSG_TYPE_PORT_SEND ||
           request->version!=1 || request->bytes!=sizeof(struct DGMachShared) ||
           audit->msgh_trailer_size<sizeof(*audit) || audit_token_to_euid(audit->msgh_audit)!=geteuid()){
            mach_msg_destroy(&request->header);continue;
        }
        struct DGMachStream *s=DGMachAllocate();mach_vm_address_t address=0;
        if(!s){mach_msg_destroy(&request->header);continue;}
        s->server=1;s->worker=worker;
        result=mach_vm_map(mach_task_self(),&address,sizeof(struct DGMachShared),0,VM_FLAGS_ANYWHERE,
            request->memory.name,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE);
        if(!result)s->shared=(void *)(uintptr_t)address;
        if(result || DGMachLoad(&s->shared->version)!=1 || DGMachLoad(&s->shared->bytes)!=sizeof(*s->shared)){
            DGMachDispose(s);mach_msg_destroy(&request->header);continue;}
        s->peer=request->header.msgh_remote_port;request->header.msgh_remote_port=MACH_PORT_NULL;
        mach_msg_destroy(&request->header);
        struct DGMachReadyMessage reply={0};
        reply.header.msgh_bits=MACH_MSGH_BITS_COMPLEX|MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0);
        reply.header.msgh_size=sizeof(reply);reply.header.msgh_remote_port=s->peer;reply.header.msgh_id=DG_MACH_READY;
        reply.body.msgh_descriptor_count=1;reply.event.name=s->receive;
        reply.event.type=MACH_MSG_PORT_DESCRIPTOR;reply.event.disposition=MACH_MSG_TYPE_MAKE_SEND;
        if(mach_msg(&reply.header,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(reply),0,MACH_PORT_NULL,0,MACH_PORT_NULL)){
            DGMachDispose(s);continue;}
        pthread_t thread;
        if(pthread_create(&thread,NULL,DGMachRun,s)){
            DGMachStore(&s->shared->aborted,1);DGMachWake(s);DGMachDispose(s);
        }else pthread_detach(thread);
    }
    mach_port_destroy(mach_task_self(),listener);return 1;
}
#ifdef VIM_GL_MACH_CLIENT_LIBRARY
#include "../vendor/qemu/include/hw/display/vim-passthrough-opengl-inprocess.h"
__attribute__((visibility("default")))
const struct VIMGLInprocessAPI *VIMPassthroughOpenGLInprocessGetAPI(uint32_t version,uint32_t bytes)
{
    static const struct VIMGLInprocessAPI api={VIM_GL_INPROCESS_ABI,sizeof(api),
        (void *(*)(void))DGMachOpen,(ssize_t (*)(void *,void *,size_t))DGMachRead,
        (ssize_t (*)(void *,const void *,size_t))DGMachWrite,
        (ssize_t (*)(void *,const struct iovec *,int))DGMachReadv,
        (ssize_t (*)(void *,const struct iovec *,int))DGMachWritev,
        (int (*)(void *))DGMachShutdown,(int (*)(void *))DGMachClose,DGMachLiveCount};
    return version==api.version && bytes==sizeof(api)?&api:NULL;
}
#endif
