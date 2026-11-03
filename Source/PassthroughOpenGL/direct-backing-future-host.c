/* Explicit first prerequisite of GPU-to-GPU deferred backing. Called in the
 * real producer stream BEFORE publishing the raw-RAM completion ticket.
 * The ticket retains BOTH the pool identity and exact immutable image, so
 * producer close or a later published epoch cannot silently replace it.
 * No CPU mapping/readback, borrowed live context or new polling worker.
 * This does not implement guest alias reservation or CPU permission release.
 */
#ifndef VIM_BACKING_ASYNC_EXPERIMENT
#error Future backing requires independently owned asynchronous completion
#endif
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
static int DGBackingTicketSealCPU(struct DGHostContext *c,struct DGBackingTicket *ticket)
{
    if(!c || !c->vram || !c->vram->scatter_plan || !ticket || ticket->publish_future ||
       ticket->future_source || ticket->future_version || !c->shared_surface ||
       !c->drawable || !c->drawable->scope_canonical)return 0;
    uint32_t lease=DGSurfaceAcquire(c,c->vram->object,c->vram->incarnation,
        c->drawable->width,c->drawable->height);
    struct DGSurfaceLease *p=lease?DGSurfaceFind(c,lease):NULL;
    if(!p || p->resource!=c->shared_surface)return 0;
    // CPU-only deferred completion still needs an immutable source when the
    // producer selects/draws something else. Do NOT publish a GPU-future
    // resource/epoch: no GPU consumer or writer continuation may borrow it.
    ticket->future_version=DGSurfaceSnapshotCreate(c,p,c->vram->epoch);
    return ticket->future_version!=NULL;
}
static int DGBackingTicketScatter(struct DGBackingTicket *p)
{
    int good=p->future_version && DGScatterCopyCompletedSurface(p->scatter_plan,p->future_version->storage);
    if(good){
        __atomic_fetch_add(&dg_scatter_stats[1],1,__ATOMIC_RELAXED);
        __atomic_fetch_add(&dg_scatter_stats[2],(uint64_t)p->scatter_plan->width*p->scatter_plan->height*4,__ATOMIC_RELAXED);
    }
    return good;
}
#endif
static volatile int dg_backing_publication_batching=1; // Research comparison; no per-frame environment lookup.
static uint64_t dg_backing_batched_publications;
/* Both copies remain, as do the immutable version fence and final RAM-writer
 * fence. Only a redundant submission BETWEEN same-producer commands goes
 * away. Hold the source lock until the seal has flushed the whole prefix;
 * never expose an updated epoch whose commands have not been submitted. */
static struct DGSurfaceSnapshot *DGBackingPublishSealed(struct DGHostContext *c,
    struct DGSurfaceLease *p,uint64_t epoch)
{
    struct DGSharedSurface *s=p->resource;
    struct DGSurfaceSnapshot *version=NULL,*old=NULL;
    pthread_mutex_lock(&s->access);
    int eligible=!s->retired && s->epoch<epoch;
    if(eligible){
        GLenum error=DGSurfacePublishRectCommands(c,p,0,0,s->width,s->height,0,0,0,0,0);
        if(!error){
            s->epoch=epoch;version=DGSurfaceSnapshotCreateLocked(c,p,epoch,1);
        }
        if(version){
            old=s->published_version;s->published_version=version;
            __sync_add_and_fetch(&version->refs,1); // independent completion-ticket reference
            __atomic_fetch_add(&dg_backing_batched_publications,1,__ATOMIC_RELAXED);
        }else{
            // Even a failed snapshot allocation may follow a submitted copy
            // prefix. Submit it before invalidating/unlocking; End's existing
            // failure path drains actual GPU work before caller retirement.
            glFlush();__atomic_store_n(&s->retired,1,__ATOMIC_RELEASE);
        }
    }
    pthread_mutex_unlock(&s->access);DGSurfaceSnapshotDestroy(old);return version;
}
static int DGBackingTicketSealFuture(struct DGHostContext *c,struct DGBackingTicket *ticket,
    uint64_t object,uint64_t incarnation,uint64_t cpu_epoch,int width,int height)
{
    if(!ticket || ticket->future_source || ticket->future_version || !object || !incarnation ||
       !cpu_epoch || cpu_epoch==UINT64_MAX || width<1 || height<1 ||
       !c->drawable || c->drawable->width!=width || c->drawable->height!=height ||
       CGLGetCurrentContext()!=c->cgl)return 0;
    uint32_t lease=DGSurfaceAcquire(c,object,incarnation,width,height);
    struct DGSurfaceLease *p=lease?DGSurfaceFind(c,lease):NULL;if(!p)return 0;
    struct DGSharedSurface *s=p->resource;
    pthread_mutex_lock(&s->access);
    int valid=!s->retired && s->epoch<=cpu_epoch;
    pthread_mutex_unlock(&s->access);if(!valid)return 0;
    /* Reuse ordinary publication/sealing validation and native GL ordering.
     * The explicit future epoch is never labelled CPU-current. The guest's
     * CPU exclusion must remain owned by its independent pending job. */
    struct DGSurfaceSnapshot *batched=NULL;
    if(dg_backing_publication_batching){
        GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
        batched=DGBackingPublishSealed(c,p,cpu_epoch+1);if(!batched)return 0;
    }else{
        struct DGMessage m={.op=DG_SURFACE_PUBLISH};struct DGReply r={0};
        m.args[0]=lease;m.args[3]=cpu_epoch+1;
        if(DGSurfaceCommand(c,&m,&r) || r.error)return 0;
        m=(struct DGMessage){.op=DG_SURFACE_SEAL_VERSION,.args={lease,cpu_epoch+1}};r=(struct DGReply){0};
        if(DGSurfaceCommand(c,&m,&r) || r.error)return 0;
    }
    struct DGSharedSurface *held=DGSurfaceRetain(c->surface_namespace,object,incarnation,width,height);
    if(!held){DGSurfaceSnapshotDestroy(batched);return 0;}
    pthread_mutex_lock(&s->access);
    struct DGSurfaceSnapshot *v=batched?batched:s->published_version;
    valid=!s->retired && s->epoch==cpu_epoch+1 && v && v->epoch==cpu_epoch+1;
    if(valid){
        if(!batched)__sync_add_and_fetch(&v->refs,1);
        ticket->future_source=held;ticket->future_version=v;
#ifdef VIM_BACKING_WRITER_EXPERIMENT
        struct DGVRAMDrawableBacking *b=c->vram;
        if(b && b->object==object && b->incarnation==incarnation && b->epoch==cpu_epoch && b->kind==1){
            uint64_t identity[]={b->offset,b->length,b->width,b->height,b->pitch,
                b->object,b->incarnation,cpu_epoch+1,b->kind};
            memcpy(ticket->writer_backing,identity,sizeof(identity));
        }
#endif
    }
    pthread_mutex_unlock(&s->access);
    if(!valid){DGSurfaceSnapshotDestroy(batched);DGSurfacePut(held);}
    return valid;
}

/* GPU consumer submission, not a CPU completion operation. Retain the exact
 * ticket image under the registry lock, then drop that lock BEFORE native GL.
 * Kernel reader reservation must prevent ticket retirement during lookup;
 * the local snapshot reference independently covers actual submission. */
static int DGBackingFutureTexture(struct DGHostContext *c,const struct DGMessage *m,struct DGReply *r)
{
    if(!c || m->bytes || m->flags!=DG_REPLY || !m->args[0] || !m->args[1] ||
       !m->args[2] || !m->args[3] || m->args[4]>UINT32_MAX ||
       !m->args[5] || !m->args[6] || m->args[5]>8192 || m->args[6]>8192){
        r->error=GL_INVALID_VALUE;return 0;
    }
    for(unsigned i=7;i<16;++i)if(m->args[i]){r->error=GL_INVALID_VALUE;return 0;}
    if(c->inside_begin){r->error=GL_INVALID_OPERATION;return 0;}
    struct DGSurfaceSnapshot *v=NULL;
    pthread_mutex_lock(&dg_backing_ticket_lock);
    struct DGBackingTicket *p=dg_backing_tickets;
    while(p && !DGBackingTicketMatches(p,c->surface_namespace,m->args[0]))p=p->next;
    if(p && p->future_source && p->future_version &&
       p->future_source->resource==m->args[1] && p->future_source->incarnation==m->args[2] &&
       p->future_version->epoch==m->args[3] && m->args[5]<=(uint64_t)p->future_version->width &&
       m->args[6]<=(uint64_t)p->future_version->height){
        v=p->future_version;__sync_add_and_fetch(&v->refs,1);
    }
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    if(!v){r->error=GL_INVALID_OPERATION;return 0;}
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    r->error=DGSurfaceSnapshotCopyTexture(c,v,(GLenum)m->args[4],(int)m->args[5],(int)m->args[6]);
    DGSurfaceSnapshotDestroy(v);return 0;
}
