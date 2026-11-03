/* Same native interpreter/wire protocol, executed by the already-unlocked
 * QEMU caller instead of a second thread. Per-endpoint serialization and
 * current-context restoration permit vCPU thread migration. No guest pointer
 * escapes an I/O call. Accepted input is snapshotted before returning; actual
 * GPU/CPU ownership fences are unchanged. This is opt-in during qualification.
 *
 * The existing API's lifetime rule still applies: retain the endpoint until
 * all caller I/O/shutdown calls return, then close once. Close drains native
 * GPU use before releasing backing registrations. No frame timer/spin loop.
 */
struct DGInlineOutput {
    struct DGInlineOutput *next;
    size_t bytes,at;
    unsigned char data[];
};
struct DGInlineStream {
    pthread_mutex_t lock;
    pthread_cond_t readable;
    struct DGInterpreter *interpreter;
    struct DGMessage header,*storage;
    size_t header_have,body_have,capacity,queued;
    struct DGInlineOutput *first,*last;
    unsigned prepared,dead,cancelled,queued_nodes;
};
static unsigned dg_inline_count;
static unsigned DGInlineLiveCount(void)
{return __atomic_load_n(&dg_inline_count,__ATOMIC_ACQUIRE);}
static void *DGInlineOpen(void)
{
    if(__atomic_fetch_add(&dg_inline_count,1,__ATOMIC_ACQ_REL)>=128){
        __atomic_fetch_sub(&dg_inline_count,1,__ATOMIC_ACQ_REL);errno=EMFILE;return NULL;
    }
    struct DGInlineStream *s=calloc(1,sizeof(*s));int error=ENOMEM;
    unsigned mutex_ready=0;
    if(!s)goto fail;
    s->interpreter=calloc(1,sizeof(*s->interpreter));if(!s->interpreter)goto fail;
    s->interpreter->next=1;
    error=pthread_mutex_init(&s->lock,NULL);if(error)goto fail;mutex_ready=1;
    error=pthread_cond_init(&s->readable,NULL);if(error)goto fail;
    return s;
fail:
    if(s){if(mutex_ready)pthread_mutex_destroy(&s->lock);free(s->interpreter);free(s);}
    __atomic_fetch_sub(&dg_inline_count,1,__ATOMIC_ACQ_REL);errno=error;return NULL;
}
static int DGInlineVectorSize(const struct iovec *v,int count,size_t *total)
{
    *total=0;if(count<0 || count>1024 || (count && !v))return 0;
    for(int i=0;i<count;++i){
        if((v[i].iov_len && !v[i].iov_base) || v[i].iov_len>(size_t)SSIZE_MAX-*total)return 0;
        *total+=v[i].iov_len;
    }
    return 1;
}
static ssize_t DGInlineHostWritev(const struct iovec *v,int count)
{
    struct DGInlineStream *s=dg_inline_host_stream;size_t bytes=0;
    if(!s || !DGInlineVectorSize(v,count,&bytes)){errno=EINVAL;return -1;}
    if(!bytes)return 0;
    const size_t budget=2u*(size_t)DG_MAX_BYTES+1024u*sizeof(struct DGReply);
    if(s->cancelled || s->queued_nodes>=1024 || bytes>budget || s->queued>budget-bytes){errno=EPIPE;return -1;}
    struct DGInlineOutput *out=malloc(sizeof(*out)+bytes);
    if(!out){errno=ENOMEM;return -1;}
    out->next=NULL;out->bytes=bytes;out->at=0;size_t offset=0;
    for(int i=0;i<count;++i){if(v[i].iov_len)memcpy(out->data+offset,v[i].iov_base,v[i].iov_len);offset+=v[i].iov_len;}
    if(s->last)s->last->next=out;else s->first=out;s->last=out;s->queued+=bytes;++s->queued_nodes;
    return (ssize_t)bytes;
}
static int DGInlineHostTransfer(void *p,size_t n,int writing)
{
    if(!writing){errno=EINVAL;return -1;}
    struct iovec v={p,n};return DGInlineHostWritev(&v,1)==(ssize_t)n?0:-1;
}
static int DGInlinePush(struct DGInlineStream *s,const unsigned char *p,size_t n)
{
    while(n){
        if(s->header_have<sizeof(s->header)){
            size_t take=sizeof(s->header)-s->header_have;if(take>n)take=n;
            memcpy((unsigned char *)&s->header+s->header_have,p,take);
            s->header_have+=take;p+=take;n-=take;
            if(s->header_have<sizeof(s->header))break;
        }
        if(!s->prepared){
            if(!DGHeaderValid(&s->header))return -1;
            int fast=DGInterpreterFastRead(s->interpreter,-1,&s->header);
            if(fast<0)return -1;
            if(fast){s->header_have=0;continue;}
            size_t needed=sizeof(s->header)+s->header.bytes;
            if(needed>s->capacity){
                size_t capacity=needed<=16u*1024u*1024u?(s->capacity?s->capacity:4096):needed;
                while(capacity<needed)capacity*=2;
                void *replacement=realloc(s->storage,capacity);if(!replacement)return -1;
                s->storage=replacement;s->capacity=capacity;
            }
            *s->storage=s->header;s->body_have=0;s->prepared=1;
            if(s->header.flags&DG_OUTPUT_ONLY){
                memset(s->storage+1,0,s->header.bytes);s->body_have=s->header.bytes;
            }
        }
        if(s->body_have<s->header.bytes){
            size_t take=s->header.bytes-s->body_have;if(take>n)take=n;
            if(take)memcpy((unsigned char *)(s->storage+1)+s->body_have,p,take);
            s->body_have+=take;p+=take;n-=take;
            if(s->body_have<s->header.bytes)break;
        }
        if(DGInterpret(s->interpreter,-1,s->storage))return -1;
        s->prepared=0;s->header_have=s->body_have=0;
        if(s->capacity>16u*1024u*1024u){free(s->storage);s->storage=NULL;s->capacity=0;}
    }
    return 0;
}
static ssize_t DGInlineWritev(void *opaque,const struct iovec *v,int count)
{
    struct DGInlineStream *s=opaque;size_t total=0;
    if(!s || !DGInlineVectorSize(v,count,&total)){errno=EINVAL;return -1;}
    if(!total)return 0;
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    struct DGCommandProfileStamp profile=DGCommandProfileBegin();
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    uint64_t batch_at=DGCommandTimingBegin(),lock_at=batch_at;
#endif
    pthread_mutex_lock(&s->lock);
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    DGCommandTimingEnd("endpoint-lock",0,0,0,lock_at);
#endif
    int failed=s->dead || s->cancelled;
    CGLContextObj previous=CGLGetCurrentContext();
    struct DGInlineStream *old=dg_inline_host_stream;dg_inline_host_stream=s;
    for(int i=0;!failed && i<count;++i)
        if(v[i].iov_len)failed=DGInlinePush(s,v[i].iov_base,v[i].iov_len);
    // A native context must never stay current on a vCPU which can next
    // execute another endpoint. Only its endpoint lock serializes ownership.
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    uint64_t restore_at=DGCommandTimingBegin();
#endif
    if(CGLGetCurrentContext()!=previous && CGLSetCurrentContext(previous))failed=1;
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    DGCommandTimingEnd("context-restore",0,0,0,restore_at);
#endif
    dg_inline_host_stream=old;
    if(failed)s->dead=1;
    pthread_cond_broadcast(&s->readable);pthread_mutex_unlock(&s->lock);
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    DGCommandProfileEnd(profile,0,(uint32_t)(total>UINT32_MAX?UINT32_MAX:total),1);
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    DGCommandTimingEnd("inline-batch",0,0,(uint32_t)(total>UINT32_MAX?UINT32_MAX:total),batch_at);
#endif
    if(failed){errno=EPIPE;return -1;}return (ssize_t)total;
}
static ssize_t DGInlineWrite(void *s,const void *p,size_t n)
{struct iovec v={(void *)p,n};return DGInlineWritev(s,&v,1);}
static ssize_t DGInlineReadv(void *opaque,const struct iovec *v,int count)
{
    struct DGInlineStream *s=opaque;size_t total=0;
    if(!s || !DGInlineVectorSize(v,count,&total)){errno=EINVAL;return -1;}
    if(!total)return 0;
    pthread_mutex_lock(&s->lock);
    while(!s->first && !s->dead && !s->cancelled){
        int error=pthread_cond_wait(&s->readable,&s->lock);
        if(error){pthread_mutex_unlock(&s->lock);errno=error;return -1;}
    }
    if(s->cancelled){pthread_mutex_unlock(&s->lock);return 0;}
    size_t done=0;
    for(int i=0;i<count && s->first;++i){
        unsigned char *p=v[i].iov_base;size_t left=v[i].iov_len;
        while(left && s->first){
            struct DGInlineOutput *out=s->first;size_t n=out->bytes-out->at;if(n>left)n=left;
            memcpy(p,out->data+out->at,n);out->at+=n;p+=n;left-=n;done+=n;s->queued-=n;
            if(out->at==out->bytes){s->first=out->next;if(!s->first)s->last=NULL;--s->queued_nodes;free(out);}
        }
    }
    pthread_mutex_unlock(&s->lock);return (ssize_t)done;
}
static ssize_t DGInlineRead(void *s,void *p,size_t n)
{struct iovec v={p,n};return DGInlineReadv(s,&v,1);}
static int DGInlineShutdown(void *opaque)
{
    struct DGInlineStream *s=opaque;if(!s){errno=EINVAL;return -1;}
    pthread_mutex_lock(&s->lock);s->cancelled=1;
    pthread_cond_broadcast(&s->readable);pthread_mutex_unlock(&s->lock);return 0;
}
static int DGInlineClose(void *opaque)
{
    struct DGInlineStream *s=opaque;if(!s){errno=EINVAL;return -1;}
    // Caller has retired its I/O references. Complete all accepted GPU uses
    // before the owning guest can release pinned pages or a source backing.
    CGLContextObj previous=CGLGetCurrentContext();
    for(unsigned i=1;i<s->interpreter->next;++i)
        if(previous==s->interpreter->contexts[i].cgl){previous=NULL;break;}
    DGInterpreterDestroy(s->interpreter);
    if(previous)CGLSetCurrentContext(previous);
    while(s->first){struct DGInlineOutput *out=s->first;s->first=out->next;free(out);}
    free(s->storage);pthread_cond_destroy(&s->readable);pthread_mutex_destroy(&s->lock);free(s);
    __atomic_fetch_sub(&dg_inline_count,1,__ATOMIC_ACQ_REL);return 0;
}
