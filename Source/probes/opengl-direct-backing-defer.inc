/* Real decoder + native GPU + independent context-free completion stream.
 * RAM stays exported/retained THROUGH actual Wait; first CPU reads have no
 * repairing GL operation. Kernel access gating is a separate prerequisite.
 * This does not enable asynchronous GLI ownership or qualify performance. */
struct backing_notification_result {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    unsigned calls;
    int status;
};
static void backing_notified(void *opaque,int status)
{
    struct backing_notification_result *result=opaque;
    pthread_mutex_lock(&result->lock);result->status=result->calls?(result->status|status):status;++result->calls;
    pthread_cond_signal(&result->condition);pthread_mutex_unlock(&result->lock);
}
static void qualify_backing_notification_blocked_by(const uint64_t namespace_id[2],uint64_t ticket,uint64_t blocker)
{
    const struct VIMGLBackingNotifyAPI *notify=VIMPassthroughOpenGLBackingNotifyGetAPI(VIM_GL_BACKING_NOTIFY_ABI,sizeof(*notify));
    assert(notify && notify->version==VIM_GL_BACKING_NOTIFY_ABI && notify->bytes==sizeof(*notify));
    assert(!VIMPassthroughOpenGLBackingNotifyGetAPI(0,sizeof(*notify)));
    assert(!VIMPassthroughOpenGLBackingNotifyGetAPI(VIM_GL_BACKING_NOTIFY_ABI,sizeof(*notify)-1));
    struct backing_notification_result result={.lock=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,.status=-1};
    uint64_t foreign[2]={namespace_id[0],999};
    assert(notify->subscribe(NULL,ticket,backing_notified,&result)==EINVAL);
    assert(notify->subscribe(foreign,ticket,backing_notified,&result)==EINVAL);
    assert(notify->subscribe(namespace_id,ticket,NULL,&result)==EINVAL);
    assert(notify->subscribe(namespace_id,0,backing_notified,&result)==EINVAL);
    assert(notify->release_completed(foreign,ticket)==EINVAL);
    assert(notify->release_completed(namespace_id,ticket)==EBUSY);
    /* Hold the REAL auxiliary context lock. Subscribe must return despite
     * its worker being unable to enter native completion. No fabricated
     * fence signal; actual GPU completion/pixels are still required below. */
    pthread_mutex_lock(&dg_backing_ticket_lock);
    struct DGBackingTicket *p=dg_backing_tickets;
    while(p && !DGBackingTicketMatches(p,namespace_id,blocker))p=p->next;
    assert(p);__sync_add_and_fetch(&p->refs,1);
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    pthread_mutex_lock(&p->queue->lock);
    assert(!notify->subscribe(namespace_id,ticket,backing_notified,&result));
    assert(notify->subscribe(namespace_id,ticket,backing_notified,&result)==EALREADY);
    pthread_mutex_lock(&result.lock);assert(!result.calls);pthread_mutex_unlock(&result.lock);
    pthread_mutex_unlock(&p->queue->lock);
    pthread_mutex_lock(&result.lock);
    while(!result.calls)pthread_cond_wait(&result.condition,&result.lock);
    assert(result.calls==1 && !result.status);
    pthread_mutex_unlock(&result.lock);DGBackingTicketPut(p);
    pthread_cond_destroy(&result.condition);pthread_mutex_destroy(&result.lock);
}
static void qualify_backing_notification(const uint64_t namespace_id[2],uint64_t ticket)
{qualify_backing_notification_blocked_by(namespace_id,ticket,ticket);}
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
struct backing_early_notification {
    struct DGBackingTicket *held;
    struct backing_notification_result result;
};
static void begin_early_backing_notification(struct backing_early_notification *early,
    const uint64_t namespace_id[2],uint64_t ticket)
{
    memset(early,0,sizeof(*early));
    assert(!pthread_mutex_init(&early->result.lock,NULL) && !pthread_cond_init(&early->result.condition,NULL));
    early->result.status=-1;
    pthread_mutex_lock(&dg_backing_ticket_lock);struct DGBackingTicket *p=dg_backing_tickets;
    while(p && !DGBackingTicketMatches(p,namespace_id,ticket))p=p->next;
    assert(p);early->held=p;__sync_add_and_fetch(&p->refs,1);pthread_mutex_unlock(&dg_backing_ticket_lock);
    pthread_mutex_lock(&p->queue->lock);
    assert(!DGBackingTicketSubscribe(namespace_id,ticket,backing_notified,&early->result));
    // Diagnostic start barrier ONLY: prove the old job acquired its actual
    // CPU-serialization lock before any successor is submitted. Native queue
    // remains locked, so it cannot copy or complete. No production polling.
    unsigned tries;int state=0;
    for(tries=0;tries<1000;++tries){
        state=pthread_mutex_trylock(&p->scatter_lock);
        if(state==EBUSY)break;
        assert(!state);pthread_mutex_unlock(&p->scatter_lock);usleep(1000);
    }
    assert(tries<1000 && state==EBUSY);
    pthread_mutex_lock(&early->result.lock);assert(!early->result.calls);pthread_mutex_unlock(&early->result.lock);
}
static void finish_early_backing_notification(struct backing_early_notification *early)
{
    pthread_mutex_unlock(&early->held->queue->lock);
    pthread_mutex_lock(&early->result.lock);
    while(!early->result.calls)pthread_cond_wait(&early->result.condition,&early->result.lock);
    assert(early->result.calls==1 && !early->result.status);pthread_mutex_unlock(&early->result.lock);
    DGBackingTicketPut(early->held);pthread_cond_destroy(&early->result.condition);pthread_mutex_destroy(&early->result.lock);
    puts("PASS old notification was already blocked on its native queue BEFORE63 successors published; resumed job reselects latest before CPU materialization");
}
static void qualify_backing_notifications_all(const uint64_t namespace_id[2],const uint64_t *tickets,unsigned count)
{
    assert(count && count<=64);
    const struct VIMGLBackingNotifyAPI *notify=VIMPassthroughOpenGLBackingNotifyGetAPI(VIM_GL_BACKING_NOTIFY_ABI,sizeof(*notify));assert(notify);
    struct DGCompletionQueue *queues[64];struct DGBackingTicket *held[64];unsigned queue_count=0;
    pthread_mutex_lock(&dg_backing_ticket_lock);
    for(unsigned i=0;i<count;++i){
        struct DGBackingTicket *p=dg_backing_tickets;
        while(p && !DGBackingTicketMatches(p,namespace_id,tickets[i]))p=p->next;
        assert(p);held[i]=p;__sync_add_and_fetch(&p->refs,1);
        unsigned j;for(j=0;j<queue_count;++j)if(queues[j]==p->queue)break;
        if(j==queue_count)queues[queue_count++]=p->queue;
    }
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    assert(queue_count==2); // The real alternating two-producer chain.
    for(unsigned i=0;i<queue_count;++i)pthread_mutex_lock(&queues[i]->lock);
    struct backing_notification_result result={.lock=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,.status=-1};
    for(unsigned i=0;i<count;++i)assert(!notify->subscribe(namespace_id,tickets[i],backing_notified,&result));
    pthread_mutex_lock(&result.lock);assert(!result.calls);pthread_mutex_unlock(&result.lock);
    for(unsigned i=0;i<queue_count;++i)pthread_mutex_unlock(&queues[i]->lock);
    pthread_mutex_lock(&result.lock);
    while(result.calls<count)pthread_cond_wait(&result.condition,&result.lock);
    assert(result.calls==count && !result.status);pthread_mutex_unlock(&result.lock);
    for(unsigned i=0;i<count;++i)DGBackingTicketPut(held[i]);
    pthread_cond_destroy(&result.condition);pthread_mutex_destroy(&result.lock);
    puts("PASS all64 simultaneous native notifications across two producer queues: exact one callback per ticket, no lock-cycle/early callback");
}
#endif
static void *backing_service(uint64_t space)
{
    void *service=api->open();assert(service);
    struct DGMessage m={.op=VIM_GL_SERVICE_HELLO,.flags=DG_REPLY};
    m.args[0]=VIM_GL_SERVICE_MAGIC;m.args[1]=VIM_GL_SERVICE_VERSION;
    m.args[3]=0x6173796e632d7465ULL;m.args[4]=space;
    struct DGReply reply=exchange(service,&m,NULL);
    assert(!reply.error && (reply.result&DG_CAP_BACKING_COMPLETION));return service;
}
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
/* Complete consumer wire path, with a different native CGL share group. No
 * private struct/IOSurface pointer is used to bind the producer's image. */
static void qualify_future_texture_wire(uint64_t space,uint64_t ticket,uint64_t object,int valid)
{
    void *stream=api->open();assert(stream);
    struct DGMessage m={.op=DG_HELLO,.flags=DG_REPLY,.bytes=sizeof(DG_LAYOUT)};
    m.args[0]=DG_MAGIC;m.args[1]=DG_VERSION;m.args[2]=1;
    m.args[3]=0x6173796e632d7465ULL;m.args[4]=space;
    struct DGReply r=exchange(stream,&m,DG_LAYOUT);
    assert(!r.error && (r.result&DG_CAP_BACKING_FUTURE_VERSION));
    m=(struct DGMessage){.op=DG_CREATE,.flags=DG_REPLY};r=exchange(stream,&m,NULL);
    assert(!r.error && r.result);unsigned context=(unsigned)r.result;
    uint32_t zero=0,texture=0,fbo=0;
    m=(struct DGMessage){.op=DG_OP_gen_textures,.context=context,.flags=DG_REPLY,.bytes=4,.args={1,1}};
    r=exchange_output(stream,&m,&zero,&texture);assert(!r.error && texture);
    m=(struct DGMessage){.op=DG_OP_bind_texture,.context=context,.args={GL_TEXTURE_RECTANGLE_ARB,texture}};
    send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_BACKING_FUTURE_TEXTURE,.context=context,.flags=DG_REPLY,
        .args={ticket,object,1,2,GL_RGBA8,W,H}};
    r=exchange(stream,&m,NULL);
    if(!valid){assert(r.error==GL_INVALID_OPERATION);assert(!api->close(stream));return;}
    if(r.error)fprintf(stderr,"future texture submission error=%x ticket=%llu object=%llu\n",r.error,
        (unsigned long long)ticket,(unsigned long long)object);
    assert(!r.error);
    /* Identity, epoch, unused fields and dimensions are checked even though
     * the caller already has a real ticket. They are not hints for lookup. */
    m.args[1]++;r=exchange(stream,&m,NULL);assert(r.error==GL_INVALID_OPERATION);m.args[1]--;
    m.args[2]++;r=exchange(stream,&m,NULL);assert(r.error==GL_INVALID_OPERATION);m.args[2]--;
    m.args[3]++;r=exchange(stream,&m,NULL);assert(r.error==GL_INVALID_OPERATION);m.args[3]--;
    m.args[7]=1;r=exchange(stream,&m,NULL);assert(r.error==GL_INVALID_VALUE);m.args[7]=0;
    m.args[5]=W+1;r=exchange(stream,&m,NULL);assert(r.error==GL_INVALID_OPERATION);m.args[5]=W;
    m=(struct DGMessage){.op=DG_OP_gen_framebuffers_EXT,.context=context,.flags=DG_REPLY,.bytes=4,.args={1,1}};
    r=exchange_output(stream,&m,&zero,&fbo);assert(!r.error && fbo);
    m=(struct DGMessage){.op=DG_OP_bind_framebuffer_EXT,.context=context,.args={GL_FRAMEBUFFER_EXT,fbo}};
    send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_OP_framebuffer_texture2D_EXT,.context=context,
        .args={GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0}};
    send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_OP_check_framebuffer_status_EXT,.context=context,.flags=DG_REPLY,.args={GL_FRAMEBUFFER_EXT}};
    r=exchange(stream,&m,NULL);assert(!r.error && r.result==GL_FRAMEBUFFER_COMPLETE_EXT);
    m=(struct DGMessage){.op=DG_OP_read_buffer,.context=context,.args={GL_COLOR_ATTACHMENT0_EXT}};
    send_bytes(stream,&m,sizeof(m));
    uint32_t image[W*H]={0};
    m=(struct DGMessage){.op=DG_OP_read_pixels,.context=context,.flags=DG_REPLY,.bytes=sizeof(image),
        .args={0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,1}};
    r=exchange_output(stream,&m,image,image);assert(!r.error);
    for(unsigned i=0;i<W*H;++i)assert(image[i]==0xffff0000u);
    m=(struct DGMessage){.op=DG_ERROR,.context=context,.flags=DG_REPLY};r=exchange(stream,&m,NULL);
    assert(!r.error && !r.result && !api->close(stream));
}
static uint32_t qualify_future_gpu_version(void *service,const uint64_t namespace_id[2],uint64_t ticket,
    uint64_t object,CGLContextObj sentinel)
{
    pthread_mutex_lock(&dg_backing_ticket_lock);
    struct DGBackingTicket *p=dg_backing_tickets;
    while(p && !DGBackingTicketMatches(p,namespace_id,ticket))p=p->next;
    assert(p && !p->completed && p->future_source && p->future_version);
    assert(p->future_source->resource==object && p->future_source->incarnation==1 &&
        p->future_version->epoch==2);
    struct DGSurfaceSnapshot *v=p->future_version;__sync_add_and_fetch(&v->refs,1);
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    struct DGMessage m={.op=DG_SURFACE_HOLD,.flags=DG_REPLY,.args={object,1,W,H,2}};
    m.args[4]=3;struct DGReply wrong=exchange(service,&m,NULL);assert(wrong.error==GL_INVALID_OPERATION);
    m.args[4]=2;m.args[1]=2;wrong=exchange(service,&m,NULL);assert(wrong.error==GL_INVALID_OPERATION);
    m.args[1]=1;
    struct DGReply r=exchange(service,&m,NULL);assert(!r.error && r.result && r.result<=UINT32_MAX);
    uint32_t held=(uint32_t)r.result;
    /* An INDEPENDENT CGL share group consumes the exact immutable IOSurface
     * through the real native producer-Flush/consumer-bind contract. No raw
     * loaned RAM is read, no CPU permission is granted, no completion callback
     * or compatibility Wait has yet run. ReadPixels is only the final probe's
     * CPU observation, not a normal GPU-consumer implementation. */
    CGLContextObj consumer=NULL;
    assert(!CGLCreateContext(CGLGetPixelFormat(sentinel),NULL,&consumer));
    assert(!CGLSetCurrentContext(consumer));GLuint texture=0,fbo=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    assert(!CGLTexImageIOSurface2D(consumer,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,W,H,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,v->storage,0));
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    unsigned char pixels[W*H*4];glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for(unsigned i=0;i<W*H;++i)assert(pixels[i*4]==255 && !pixels[i*4+1] && !pixels[i*4+2] && pixels[i*4+3]==255);
    assert(!glGetError());glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    assert(!CGLSetCurrentContext(sentinel));CGLDestroyContext(consumer);DGSurfaceSnapshotDestroy(v);
    return held;
}
#endif
static void qualify_deferred_backing(const uint64_t pages[Pages],volatile uint32_t *pixels,CGLContextObj sentinel)
{
    // Admission is bounded BEFORE touching a live scope or accepting GPU
    // ownership. Reservations consume the same budget as published tickets.
    struct DGHostContext budget={.cgl=sentinel,.surface_namespace={0x627564676574,1}};
    struct DGBackingTicket *reserved[256];
    for(unsigned i=0;i<256;++i){reserved[i]=DGBackingTicketPrepare(&budget);assert(reserved[i]);}
    assert(!DGBackingTicketPrepare(&budget));
    for(unsigned i=0;i<256;++i)DGBackingTicketCancel(reserved[i]);
    DGCompletionQueuePut(budget.completion_queue);
    assert(!dg_backing_ticket_count && !dg_backing_tickets && CGLGetCurrentContext()==sentinel);
    puts("PASS backing completion admission: 256 reserved/published slots, over-budget refusal and balanced cancellation");
    unsigned modes=3;
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
    modes=5;
#endif
    for(unsigned mode=0;mode<modes;++mode){
        uint64_t space=100+mode;
        void *service=backing_service(space),*stranger=backing_service(999),*stream=api->open();assert(stream);
        struct DGMessage m={.op=DG_HELLO,.flags=DG_REPLY,.bytes=sizeof(DG_LAYOUT)};
        m.args[0]=DG_MAGIC;m.args[1]=DG_VERSION;m.args[2]=1;
        m.args[3]=0x6173796e632d7465ULL;m.args[4]=space;
        struct DGReply reply=exchange(stream,&m,DG_LAYOUT);
        assert(!reply.error && (reply.result&DG_CAP_BACKING_COMPLETION));
        m=(struct DGMessage){.op=DG_CREATE,.flags=DG_REPLY};reply=exchange(stream,&m,NULL);
        assert(!reply.error && reply.result);unsigned context=(unsigned)reply.result;
        m=(struct DGMessage){.op=DG_RESIZE,.context=context,.flags=DG_REPLY};
        m.args[0]=W;m.args[1]=H;m.args[2]=1;reply=exchange(stream,&m,NULL);assert(!reply.error);
        memset((void*)pixels,0xa7,Bytes);
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
        if(mode>=3){
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)pixels[y*Pitch+x]=0xff000000u;
            clear(stream,context,0,0,0);
            m=(struct DGMessage){.op=DG_SURFACE_ACQUIRE,.context=context,.flags=DG_REPLY,
                .args={700+mode,1,W,H}};
            reply=exchange(stream,&m,NULL);assert(!reply.error && reply.result);
            m=(struct DGMessage){.op=DG_SURFACE_PUBLISH,.context=context,.flags=DG_REPLY,
                .args={reply.result,0,0,1}};
            reply=exchange(stream,&m,NULL);assert(!reply.error);
        }
#endif
        m=(struct DGMessage){.op=DG_BACKING_RENDER_BEGIN,.context=context,.flags=DG_REPLY|DG_INPUT_ONLY,.bytes=Pages*sizeof(uint64_t)};
        m.args[1]=Bytes;m.args[2]=m.args[7]=W;m.args[3]=m.args[8]=H;m.args[4]=Pitch*4;
        m.args[10]=700+mode;m.args[11]=m.args[12]=m.args[13]=m.args[15]=1;m.args[14]=Pages;
        reply=exchange(stream,&m,pages);assert(!reply.error && reply.result==1);
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
        if(mode>=3)assert(dg_scatter_stats[0] && dg_scatter_stats[3]);
#endif
        clear(stream,context,1,0,0);
        if(mode==1){
            m=(struct DGMessage){.op=DG_BACKING_RENDER_MATERIALIZE,.context=context,.flags=DG_REPLY,.args={1}};
            reply=exchange(stream,&m,NULL);assert(!reply.error && reply.result==1);
            clear(stream,context,0,0,1);
        }
        uint64_t waits=dg_backing_sync_waits,submitted=dg_backing_deferred_submissions;
        m=(struct DGMessage){.op=DG_BACKING_RENDER_DEFER,.context=context,.flags=DG_REPLY,.args={2}};
        reply=exchange(stream,&m,NULL);assert(reply.error==GL_INVALID_OPERATION && !reply.result);
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
        if(mode>=3)m.args[1]=1;
#endif
        m.args[0]=1;reply=exchange(stream,&m,NULL);assert(!reply.error && reply.result);
        uint64_t ticket=reply.result;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
        if(mode==3){
            // Explicit native fault injection BEFORE any callback exists:
            // an impossible CPU source geometry must NOT turn a signalled
            // GPU fence into CPU-complete status or allow ticket retirement.
            pthread_mutex_lock(&dg_backing_ticket_lock);
            struct DGBackingTicket *p=dg_backing_tickets;
            while(p && p->name!=ticket)p=p->next;
            assert(p && p->scatter_plan);__sync_add_and_fetch(&p->refs,1);
            pthread_mutex_unlock(&dg_backing_ticket_lock);
            uint32_t height=p->scatter_plan->height;p->scatter_plan->height=height+1;
            struct DGMessage failed={.op=DG_BACKING_COMPLETION_WAIT,.flags=DG_REPLY,.args={ticket}};
            struct DGReply rejected=exchange(service,&failed,NULL);
            assert(rejected.error==GL_INVALID_OPERATION && !rejected.result && !p->completed);
            assert(DGBackingTicketReleaseCompleted(p->namespace_id,ticket)==EBUSY);
            check_pixels(pixels,0xff000000u);
            // Undo ONLY the fixture's injected defect while retaining the
            // gate/ticket. This is not production retry or auto-recovery.
            p->scatter_plan->height=height;DGBackingTicketPut(p);
            puts("PASS injected CPU materialization failure: GPU completion alone cannot complete/release the ticket; CPU bytes unchanged");
        }
#endif
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
        uint32_t future_hold=0;
        const uint64_t future_namespace[2]={0x6173796e632d7465ULL,space};
        if(mode>=3)future_hold=qualify_future_gpu_version(service,future_namespace,ticket,700+mode,sentinel);
        if(mode>=3){
            qualify_future_texture_wire(space,ticket,700+mode,1);
            qualify_future_texture_wire(999,ticket,700+mode,0);
        }
#endif
        assert(dg_backing_deferred_submissions==submitted+1 && dg_backing_sync_waits==waits);
        m=(struct DGMessage){.op=DG_BACKING_COMPLETION_CHECK,.flags=DG_REPLY,.args={ticket}};
        reply=exchange(service,&m,NULL);assert(!reply.error && reply.result==1);
        reply=exchange(stranger,&m,NULL);assert(reply.error==GL_INVALID_VALUE);
        if(mode==0 || mode==3){
            // The producer can already select/draw another target. Waiting
            // for the earlier ticket must not borrow that live target or
            // read its later green pixels into the original red backing.
            m=(struct DGMessage){.op=DG_RESIZE,.context=context,.flags=DG_REPLY};
            m.args[0]=W;m.args[1]=H;m.args[2]=2;
            reply=exchange(stream,&m,NULL);assert(!reply.error);
            clear(stream,context,0,1,0);
            m=(struct DGMessage){.op=DG_ERROR,.context=context,.flags=DG_REPLY};
            reply=exchange(stream,&m,NULL);assert(!reply.error && !reply.result);
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
            if(mode==3){
                // Supersede the resource's latest published image with GREEN.
                // Consumers of the earlier ticket must still receive RED.
                m=(struct DGMessage){.op=DG_SURFACE_ACQUIRE,.context=context,.flags=DG_REPLY,
                    .args={700+mode,1,W,H}};
                reply=exchange(stream,&m,NULL);assert(!reply.error && reply.result);
                uint64_t lease=reply.result;
                m=(struct DGMessage){.op=DG_SURFACE_PUBLISH,.context=context,.flags=DG_REPLY,.args={lease,0,0,3}};
                reply=exchange(stream,&m,NULL);assert(!reply.error);
                m=(struct DGMessage){.op=DG_SURFACE_SEAL_VERSION,.context=context,.flags=DG_REPLY,.args={lease,3}};
                reply=exchange(stream,&m,NULL);assert(!reply.error);
            }
#endif
        }
        // Async acknowledgment is NOT permission to read/reuse guest RAM.
        m=(struct DGMessage){.op=DG_BACKING_COMPLETION_RELEASE,.flags=DG_REPLY,.args={ticket}};
        reply=exchange(service,&m,NULL);assert(reply.error==GL_INVALID_OPERATION);
        m.op=DG_BACKING_COMPLETION_WAIT;m.args[1]=1;
        reply=exchange(service,&m,NULL);assert(reply.error==GL_INVALID_VALUE);
        m.args[1]=0;reply=exchange(stranger,&m,NULL);assert(reply.error==GL_INVALID_VALUE);
        if(mode==2 || mode==4){assert(!api->close(stream));stream=NULL;}
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
        if(mode>=3)qualify_future_texture_wire(space,ticket,700+mode,1);
#endif
        const uint64_t namespace_id[2]={0x6173796e632d7465ULL,space};
        qualify_backing_notification(namespace_id,ticket);
        // FIRST CPU observation follows the notification itself, BEFORE the
        // compatibility WAIT command. It cannot hide an early notification.
        check_pixels(pixels,mode==1?0xff0000ffu:0xffff0000u);
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
        if(future_hold){
            uint32_t image[W*H];struct DGMessage read={.op=DG_SURFACE_HOLD_READ,
                .flags=DG_REPLY|DG_OUTPUT_ONLY,.bytes=sizeof(image),.args={future_hold,2,0,0,W,H}};
            struct DGReply exact=exchange_output(service,&read,NULL,image);assert(!exact.error);
            for(unsigned i=0;i<W*H;++i)assert(image[i]==0xffff0000u);
            read=(struct DGMessage){.op=DG_SURFACE_UNHOLD,.flags=DG_REPLY,.args={future_hold}};
            exact=exchange(service,&read,NULL);assert(!exact.error);
        }
#endif
        reply=exchange(service,&m,NULL);assert(!reply.error && reply.result==1);
        check_pixels(pixels,mode==1?0xff0000ffu:0xffff0000u);
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
        if(mode>=3){
            uint32_t saved=pixels[0];pixels[0]=0xff315279u;
            reply=exchange(service,&m,NULL);assert(!reply.error && reply.result==1);
            assert(pixels[0]==0xff315279u);pixels[0]=saved;
            puts("PASS duplicate completed Wait preserves subsequent CPU writes, without stale scatter replay");
        }
#endif
        reply=exchange(service,&m,NULL);assert(!reply.error && reply.result==1); // idempotent actual completion
        m.op=DG_BACKING_COMPLETION_RELEASE;
        if(mode==2)assert(!DGBackingTicketReleaseCompleted(namespace_id,ticket));
        else {reply=exchange(service,&m,NULL);assert(!reply.error && reply.result==1);}
        reply=exchange(service,&m,NULL);assert(reply.error==GL_INVALID_VALUE);
        m.op=DG_BACKING_COMPLETION_WAIT;reply=exchange(service,&m,NULL);assert(reply.error==GL_INVALID_VALUE);
        m.op=DG_BACKING_COMPLETION_CHECK;reply=exchange(service,&m,NULL);assert(reply.error==GL_INVALID_VALUE);
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
        if(mode>=3)qualify_future_texture_wire(space,ticket,700+mode,0);
#endif
        assert(DGBackingTicketSubscribe(namespace_id,ticket,backing_notified,NULL)==EINVAL);
        if(stream){
            m=(struct DGMessage){.op=DG_BACKING_RENDER_END,.context=context,.flags=DG_REPLY,.args={1}};
            reply=exchange(stream,&m,NULL);assert(reply.error==GL_INVALID_OPERATION);
            assert(!api->close(stream));
        }
        assert(!api->close(service) && !api->close(stranger) && !api->live_count());
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
        DGSnapshotCleanupDrain();
        assert(!dg_surfaces && !dg_surface_leases && !dg_surface_count && !dg_surface_bytes && !dg_snapshot_bytes);
#endif
        assert(!dg_backing_ticket_count && !dg_backing_tickets && CGLGetCurrentContext()==sentinel);
        printf("PASS independent backing completion %s: nonblocking one-shot notification, exact first CPU pixels/padding BEFORE Wait, stale/foreign/duplicate subscriptions and bounded release\n",
            mode==0?"shared writes":mode==1?"private later writes":mode==2?"after producer close":
            mode==3?"future GPU version before completion":"future GPU version after producer close");
    }
}
