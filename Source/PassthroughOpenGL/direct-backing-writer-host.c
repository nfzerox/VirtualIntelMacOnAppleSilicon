/* Explicit GPU writer continuation prototype. The trusted guest MUST retain
 * its prepared owner and transfer the real whole-object CPU gate first.
 * This operation supplies no CPU pointer/readback/new memory registration.
 * Only an exact already registered client-storage target in the SAME native
 * CGL share group is eligible; foreign groups/cache misses synchronously fall
 * back. GLsync names are not valid across unrelated native share groups.
 */
static uint64_t dg_backing_writer_continuations;
static uint64_t dg_writer_refusals[4]; // attempts, cache miss, foreign group, Begin refusal
static uint64_t dg_writer_errors[4],dg_writer_error_fields[4]; // state, missing ticket, mismatched ticket, native wait
static void DGBackingWriterError(unsigned kind,uint64_t fields)
{
    __atomic_fetch_add(&dg_writer_errors[kind],1,__ATOMIC_RELAXED);
    __atomic_fetch_or(&dg_writer_error_fields[kind],fields,__ATOMIC_RELAXED);
}
static int DGBackingWriterCached(struct DGHostContext *c,const struct DGMessage *m)
{
    struct DGVRAMDrawableBacking *entries[]={c->vram,c->vram_spares[0],c->vram_spares[1],c->vram_spares[2]};
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);++i){
        struct DGVRAMDrawableBacking *b=entries[i];
        if(!DGVRAMBackingMatches(b,m) || b->native_width!=b->width || b->epoch>m->args[12])continue;
        if(i){
            // Switching a cached target must have room for the displaced
            // current registration. Otherwise the ordinary LRU evicts via
            // DGVRAMWait, hiding a CPU wait in this GPU-only admission.
            if(!dg_vram_backing_reuse || c->vram_spare_bytes<b->length)return 0;
            if(c->vram && (c->vram->length>DG_VRAM_SPARE_BYTES ||
               c->vram_spare_bytes-b->length>DG_VRAM_SPARE_BYTES-c->vram->length))return 0;
        }
        return 1;
    }
    return 0;
}
static int DGBackingWriterContinue(struct DGHostContext *c,const struct DGMessage *m,struct DGReply *r)
{
    __atomic_fetch_add(&dg_writer_refusals[0],1,__ATOMIC_RELAXED);
    if(!c || m->flags!=(DG_REPLY|DG_INPUT_ONLY) || m->bytes || !m->args[5] ||
       m->args[6] || m->args[9] || m->args[13]!=1 || m->args[14] || !m->args[15] ||
       m->args[15]<=c->vram_render_last_serial || c->vram_render_serial || c->vram_render_drawable ||
       c->inside_begin || !c->drawable){
        uint64_t fields=(!c?1u:0) | (m->flags!=(DG_REPLY|DG_INPUT_ONLY)?2u:0) |
            (m->bytes?4u:0) | (!m->args[5]?8u:0) | ((m->args[6] || m->args[9] || m->args[13]!=1 || m->args[14])?16u:0) |
            (!m->args[15]?32u:0) | (c && m->args[15]<=c->vram_render_last_serial?64u:0) |
            (c && (c->vram_render_serial || c->vram_render_drawable)?128u:0) |
            (c && c->inside_begin?256u:0) | (c && !c->drawable?512u:0);
        DGBackingWriterError(0,fields);r->error=GL_INVALID_OPERATION;return 0;
    }
    struct DGMessage begin=*m;begin.op=DG_BACKING_RENDER_BEGIN;begin.args[5]=0;
    // Refuse BEFORE a driver can dereference pending CPU pages, even when
    // their raw vector was once present in another context's registration.
    if(!DGBackingWriterCached(c,&begin)){
        __atomic_fetch_add(&dg_writer_refusals[1],1,__ATOMIC_RELAXED);return 0;
    }
    uint64_t identity[]={m->args[0],m->args[1],m->args[2],m->args[3],m->args[4],
        m->args[10],m->args[11],m->args[12],m->args[13]};
    struct DGBackingTicket *ticket=NULL;int found=0;uint64_t mismatch=0;
    pthread_mutex_lock(&dg_backing_ticket_lock);
    for(struct DGBackingTicket *p=dg_backing_tickets;p;p=p->next){
        if(!DGBackingTicketMatches(p,c->surface_namespace,m->args[5]))continue;
        found=1;
        if(p->future_source && p->future_version && p->fence &&
           !memcmp(p->writer_backing,identity,sizeof(identity))){ticket=p;__sync_add_and_fetch(&p->refs,1);}
        else {
            mismatch=(!p->future_source?1ull<<16:0) | (!p->future_version?1ull<<17:0) | (!p->fence?1ull<<18:0);
            for(unsigned i=0;i<9;++i)if(p->writer_backing[i]!=identity[i])mismatch|=1ull<<i;
        }
        break;
    }
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    if(!ticket){DGBackingWriterError(found?2:1,mismatch);r->error=GL_INVALID_OPERATION;return 0;}
    if(CGLGetShareGroup(c->cgl)!=CGLGetShareGroup(ticket->queue->context)){
        __atomic_fetch_add(&dg_writer_refusals[2],1,__ATOMIC_RELAXED);
        DGBackingTicketPut(ticket);return 0;
    }
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    if(dg_backing_cpu_coalescing){
        pthread_mutex_lock(&dg_backing_ticket_lock);int fork=ticket->cpu_successor!=0;
        pthread_mutex_unlock(&dg_backing_ticket_lock);
        for(struct DGHostContext *p=c->alias_group->members;!fork && p;p=p->alias_member_next)
            if(p->scatter_predecessor==ticket && p->vram_render_serial)fork=1;
        if(fork){__atomic_fetch_add(&dg_writer_refusals[3],1,__ATOMIC_RELAXED);DGBackingTicketPut(ticket);return 0;}
    }
#endif
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    // The fence covers the producer's LAST RAM writer, not merely its earlier
    // sealed image. No glClientWaitSync/Finish/CPU ownership acquisition here.
    glWaitSync(ticket->fence,0,GL_TIMEOUT_IGNORED);
    GLenum wait_error=glGetError();
    if(wait_error){DGBackingWriterError(3,wait_error);DGBackingTicketPut(ticket);r->error=GL_INVALID_OPERATION;return 0;}
    int error=DGVRAMRenderBeginImpl(c,&begin,r,1);
    if(!error && !r->error && r->result==1){
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
        // Retain CPU ordering independently from the already-queued GPU
        // dependency. A signalled GPU fence is NOT a completed CPU scatter.
        if(c->vram->scatter_plan){
            c->scatter_predecessor=ticket;__sync_add_and_fetch(&ticket->refs,1);
        }
#endif
        c->vram_render_last_serial=c->vram_render_serial=m->args[15];
        __atomic_fetch_add(&dg_vram_render_begins,1,__ATOMIC_RELAXED);
        __atomic_fetch_add(&dg_backing_writer_continuations,1,__ATOMIC_RELAXED);
    }else{
        __atomic_fetch_add(&dg_writer_refusals[3],1,__ATOMIC_RELAXED);
        // An attachment failure can have submitted native work. Complete its
        // actual prefix before a caller may cancel the transferred CPU gate.
        glFinish();
    }
    DGBackingTicketPut(ticket);return error;
}
