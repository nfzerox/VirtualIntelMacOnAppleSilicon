/* Private, explicitly gated ownership experiment. A completed later write
 * to the SAME excluded CPU backing can satisfy earlier write completions;
 * it does not replace their immutable GPU images. No timer/delay, guessed
 * completion, early CPU gate restoration, or new guest/kernel ABI.
 *
 * Continuation already proves one retained guest CPU gate and queues the
 * predecessor's native GPU fence. Only published successors participate.
 * A returned write completion never promises that RAM still contains that
 * historical image after a later write; duplicate Wait already has this
 * rule. CPU bytes must really contain the latest covered completed image.
 */
static int DGBackingTicketCanCoalesce(struct DGBackingTicket *newer,struct DGBackingTicket *older)
{
    if(!newer || !older || newer->cpu_predecessor!=older || newer->name<=older->name ||
       !newer->publish_future || !older->publish_future || !newer->future_source ||
       newer->future_source!=older->future_source || !newer->future_version || !older->future_version ||
       !newer->fence || !older->fence || memcmp(newer->namespace_id,older->namespace_id,16) ||
       __atomic_load_n(&newer->scatter_failed,__ATOMIC_ACQUIRE) ||
       __atomic_load_n(&older->scatter_failed,__ATOMIC_ACQUIRE))return 0;
    for(unsigned i=0;i<9;++i){
        if(i==7){if(older->writer_backing[i]==UINT64_MAX || newer->writer_backing[i]!=older->writer_backing[i]+1)return 0;}
        else if(newer->writer_backing[i]!=older->writer_backing[i])return 0;
    }
    struct DGScatterPlan *a=newer->scatter_plan,*b=older->scatter_plan;
    if(!a || !b || a->ram!=b->ram || a->length!=b->length || a->width!=b->width ||
       a->height!=b->height || a->pitch!=b->pitch || a->count!=b->count ||
       memcmp(a->offsets,b->offsets,(size_t)a->count*sizeof(*a->offsets)))return 0;
    // Malformed/failed CPU sources must not acquire a success witness merely
    // because a newer snapshot exists. These are immutable native descriptors,
    // not CPU pixel reads or an ownership acquisition.
    struct DGSurfaceSnapshot *versions[]={newer->future_version,older->future_version};
    for(unsigned i=0;i<2;++i){
        IOSurfaceRef s=versions[i]->storage;
        if(!s || IOSurfaceGetPlaneCount(s) || IOSurfaceGetBytesPerElement(s)!=4 ||
           IOSurfaceGetPixelFormat(s)!=0x42475241 || IOSurfaceGetWidth(s)<a->width ||
           IOSurfaceGetHeight(s)<a->height)return 0;
        size_t row=IOSurfaceGetBytesPerRow(s),bytes=IOSurfaceGetAllocSize(s);
        if(row<(size_t)a->width*4 || (uint64_t)(a->height-1)*row+(uint64_t)a->width*4>bytes)return 0;
    }
    return 1;
}
static struct DGBackingTicket *DGBackingTicketLatestRetain(struct DGBackingTicket *requested)
{
    // Resolve at most the CURRENT committed chain. Never chase a producer
    // that has not published its fence, or wait for a future writer to appear.
    struct DGBackingTicket *latest=requested;
    pthread_mutex_lock(&dg_backing_ticket_lock);
    for(unsigned depth=0;depth<255 && !__atomic_load_n(&latest->completed,__ATOMIC_ACQUIRE) &&
        latest->cpu_successor;++depth){
        struct DGBackingTicket *next=dg_backing_tickets;
        while(next && !DGBackingTicketMatches(next,latest->namespace_id,latest->cpu_successor))next=next->next;
        if(!next || !DGBackingTicketCanCoalesce(next,latest))break;
        latest=next;
    }
    __sync_add_and_fetch(&latest->refs,1);
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    return latest;
}
static int DGBackingTicketWaitCoalesced(struct DGBackingTicket *requested)
{
    unsigned redirects=0;
resolve:;
    struct DGBackingTicket *latest=DGBackingTicketLatestRetain(requested);
    if(latest!=requested)__atomic_fetch_add(&dg_cpu_coalescing_stats[3],1,__ATOMIC_RELAXED);

    // All locks descend in monotonic ticket-name order. In particular an old
    // notification NEVER holds its lock while following a newer successor.
    // Existing ordered fallback also locks only towards older predecessors.
    struct DGBackingTicket *locked[256];unsigned count=0;int eligible=1;
    for(struct DGBackingTicket *p=latest;p;){
        pthread_mutex_lock(&p->scatter_lock);locked[count++]=p;
        if(__atomic_load_n(&p->completed,__ATOMIC_ACQUIRE))break;
        struct DGBackingTicket *older=p->cpu_predecessor;
        if(!older || __atomic_load_n(&older->completed,__ATOMIC_ACQUIRE))break;
        if(count==256 || !DGBackingTicketCanCoalesce(p,older)){eligible=0;break;}
        p=older;
    }
    int good=__atomic_load_n(&latest->completed,__ATOMIC_ACQUIRE);
    unsigned pending=0;
    for(unsigned i=0;i<count;++i)pending+=!__atomic_load_n(&locked[i]->completed,__ATOMIC_ACQUIRE);
    if(eligible && !good){
        GLenum status;do{status=DGCompletionQueueWait(latest->queue,latest->fence);}while(status==GL_TIMEOUT_EXPIRED);
        good=status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED;
        if(good && redirects<255){
            // A successor can publish while this job is blocked on the OLD
            // native fence. Recheck before touching CPU bytes. Release EVERY
            // descending lock before following a newer name, or another
            // completion job waiting on us could form a cycle. The finite
            // bound is outstanding tickets, not a timeout or batching timer.
            struct DGBackingTicket *next=DGBackingTicketLatestRetain(latest);
            int advanced=next!=latest;DGBackingTicketPut(next);
            if(advanced){
                while(count)pthread_mutex_unlock(&locked[--count]->scatter_lock);
                DGBackingTicketPut(latest);++redirects;goto resolve;
            }
        }
        if(good)good=DGBackingTicketScatter(latest);
        if(good){
            // The newest fence includes each accepted GPU predecessor. The
            // one actual scatter is finished before ANY covered callback can
            // report success, including an old notification that ran first.
            for(unsigned i=0;i<count;++i)__atomic_store_n(&locked[i]->completed,1,__ATOMIC_RELEASE);
            if(pending>1){
                __atomic_fetch_add(&dg_cpu_coalescing_stats[0],1,__ATOMIC_RELAXED);
                __atomic_fetch_add(&dg_cpu_coalescing_stats[1],pending-1,__ATOMIC_RELAXED);
                __atomic_fetch_add(&dg_cpu_coalescing_stats[2],(uint64_t)(pending-1)*latest->scatter_plan->width*latest->scatter_plan->height*4,__ATOMIC_RELAXED);
            }
        }else __atomic_store_n(&latest->scatter_failed,1,__ATOMIC_RELEASE);
    }
    while(count)pthread_mutex_unlock(&locked[--count]->scatter_lock);
    // Unknown layouts/relationships retain the original CPU-ordered path;
    // that path deliberately does NOT follow successors while holding locks.
    int error=eligible?(good?0:EIO):DGBackingTicketWaitOrdered(latest);
    if(!error && !__atomic_load_n(&requested->completed,__ATOMIC_ACQUIRE))error=EIO;
    DGBackingTicketPut(latest);return error;
}
