/* Explicit scoped rendering into a FULL, unflipped shared backing. Native
 * Intel controls qualify exact logical width + aligned pitch, not a wider
 * framebuffer with a viewport pretending to protect Clear's row padding.
 *
 * The explicit scope candidate negotiates this through its typed loan owner;
 * ordinary builds and by-value imports keep snapshot semantics. Never infer this contract
 * from ordinary by-value ImportDrawable, which must stay a snapshot.
 * Begin's caller owns CPU exclusion through End's completed GPU use. Source
 * registration and its page alias cannot change during that interval.
 * Backing provenance is the runtime's exported Mach memory-entry contract;
 * an arbitrary unexported private allocation is NOT equivalent on old XNU.
 */
static uint64_t dg_vram_render_read_only_ends,dg_vram_render_republished_ends;
static uint64_t dg_vram_render_begins,dg_vram_render_materializations;
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
/* One retained resource is BOTH private drawable fallback and canonical
 * published GPU storage. End still materializes and completes CPU pixels;
 * the later epoch publication becomes a same-resource flush, not another
 * blit. Never adopt an unrelated direct-family drawable or a newer version.
 * Ordinary imports and immutable sealed snapshots keep their contracts. */
static int DGVRAMRenderCanonical(struct DGHostContext *c,const struct DGMessage *m)
{
    struct DGHostDrawable *d=c->drawable;
    if(!(c->surface_namespace[0]|c->surface_namespace[1]))return 0;
    if(c->shared_surface && !d->scope_canonical)return 0;
    uint32_t lease=DGSurfaceAcquire(c,m->args[10],m->args[11],(int)m->args[2],(int)m->args[3]);
    struct DGSurfaceLease *p=lease?DGSurfaceFind(c,lease):NULL;if(!p)return 0;
    pthread_mutex_lock(&p->resource->access);
    int valid=!p->resource->retired && p->resource->epoch<=m->args[12];
    pthread_mutex_unlock(&p->resource->access);
    if(!valid){__atomic_fetch_add(&dg_scope_canonical_stats[3],1,__ATOMIC_RELAXED);return 0;}
    int reused=c->shared_surface==p->resource;
    GLenum error=DGSurfaceDrawable(c,p);
    if(error){if(!c->error)c->error=error;return 0;}
    d->scope_canonical=1;
    __atomic_fetch_add(&dg_scope_canonical_stats[reused?1:0],1,__ATOMIC_RELAXED);return 1;
}
#endif
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
static uint64_t dg_canonical_writer_stats[4]; // accepted, live aliases, identity misses, epoch misses
static uint64_t dg_cpu_canonical_stats[4]; // accepted, pending rejection, epoch rejection, identity rejection
/* Continue in the SAME native GPU storage, not a second RAM-backed target.
 * Only an exact already-canonical resource at the transferred GPU epoch is
 * eligible. The writer's previous-ticket server wait orders actual GPU use;
 * its kernel gate still owns RAM through End's real completed writeback.
 * A CPU-current Begin with live aliases may use the SAME resource only at
 * its exact already-current GPU epoch and with no unfinished matching ticket.
 * Its real CPU gate/registration are still required. Never seed pending RAM,
 * accept a stale epoch or turn a plain Begin into an implicit GPU continuation.
 */
static int DGVRAMCanonicalWriterTarget(struct DGHostContext *c,const struct DGMessage *m,int cached_only)
{
    if(!c->shared_surface || !c->drawable->scope_canonical)return 0;
    if(!cached_only && m->args[13]!=1)return 0;
    struct DGSharedSurface *s=c->shared_surface;
    if(s->resource!=m->args[10] || s->incarnation!=m->args[11] ||
       s->width!=(int)m->args[2] || s->height!=(int)m->args[3] || c->color_storage!=s->surface){
        __atomic_fetch_add(cached_only?&dg_canonical_writer_stats[2]:&dg_cpu_canonical_stats[3],1,__ATOMIC_RELAXED);return 0;
    }
    pthread_mutex_lock(&s->access);
    int eligible=!s->retired && s->epoch==m->args[12];
    pthread_mutex_unlock(&s->access);
    if(!eligible)__atomic_fetch_add(cached_only?&dg_canonical_writer_stats[3]:&dg_cpu_canonical_stats[2],1,__ATOMIC_RELAXED);
    if(eligible && !cached_only){
        pthread_mutex_lock(&dg_backing_ticket_lock);
        for(struct DGBackingTicket *p=dg_backing_tickets;p;p=p->next)
            if(!memcmp(p->namespace_id,c->surface_namespace,sizeof(p->namespace_id)) &&
               (!p->future_source || (p->future_source==s &&
                 (!p->writer_backing[7] || p->writer_backing[7]==m->args[12]))) &&
               !__atomic_load_n(&p->completed,__ATOMIC_ACQUIRE)){eligible=0;break;}
        pthread_mutex_unlock(&dg_backing_ticket_lock);
        if(!eligible)__atomic_fetch_add(&dg_cpu_canonical_stats[1],1,__ATOMIC_RELAXED);
    }
    return eligible;
}
#endif
#ifdef VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT
/* A real CPU-current loan may initialize the resource BEFORE its first
 * version, rather than pretend that zeroed/unversioned GPU pixels are current.
 * Never dereference pending RAM, overwrite a nonzero GPU epoch, or seed over
 * a retained published version. Existing version/alias rules stay intact. */
static int DGVRAMCanonicalInitializeCandidate(struct DGHostContext *c,const struct DGMessage *m,int cached_only)
{
    struct DGSharedSurface *s=c->shared_surface;
    if(cached_only || !dg_backing_scatter || m->args[13]!=1 || !m->args[12] ||
       !s || !c->drawable->scope_canonical || c->color_storage!=s->surface ||
       s->resource!=m->args[10] || s->incarnation!=m->args[11] ||
       s->width!=(int)m->args[2] || s->height!=(int)m->args[3])return 0;
    pthread_mutex_lock(&s->access);
    int eligible=!s->retired && !s->epoch && !s->published_version;
    pthread_mutex_unlock(&s->access);
    if(!eligible)return 0;
    uint32_t storage=IOSurfaceGetID(s->surface);
    if(c->alias_group)for(struct DGAlias *a=c->alias_group->aliases;a;a=a->next)
        if(a->storage==storage || a->source_storage==storage)return 0;
    pthread_mutex_lock(&dg_backing_ticket_lock);
    for(struct DGBackingTicket *p=dg_backing_tickets;p;p=p->next)
        if(!memcmp(p->namespace_id,c->surface_namespace,sizeof(p->namespace_id)) &&
           (!p->future_source || p->future_source==s) &&
           !__atomic_load_n(&p->completed,__ATOMIC_ACQUIRE)){eligible=0;break;}
    pthread_mutex_unlock(&dg_backing_ticket_lock);
    if(!eligible)__atomic_fetch_add(&dg_gather_stats[2],1,__ATOMIC_RELAXED);
    return eligible;
}
static int DGVRAMCanonicalInitialize(struct DGHostContext *c,const struct DGMessage *m,int cached_only)
{
    if(!c->vram->scatter_plan || !DGVRAMCanonicalInitializeCandidate(c,m,cached_only))return 0;
    struct DGSharedSurface *s=c->shared_surface;int good=0;
    pthread_mutex_lock(&s->access);
    if(!s->retired && !s->epoch && !s->published_version){
        glFlush(); // writable IOSurface lock must see all prior native users
        good=DGScatterGatherSurface(c->vram->scatter_plan,s->surface);
        if(good){
            s->epoch=m->args[12];c->drawable->alias_dirty=1;++c->drawable->write_serial;
            if(c->alias_group && c->alias_group->backing_write_serial!=UINT64_MAX)
                ++c->alias_group->backing_write_serial;
            __atomic_fetch_add(&dg_gather_stats[0],1,__ATOMIC_RELAXED);
            __atomic_fetch_add(&dg_gather_stats[1],(uint64_t)s->width*s->height*4,__ATOMIC_RELAXED);
        }else{
            // A writable-lock/copy prefix must not be replayed through a
            // different upload route or acquire a false current epoch.
            __atomic_store_n(&s->retired,1,__ATOMIC_RELEASE);
            __atomic_fetch_add(&dg_gather_stats[3],1,__ATOMIC_RELAXED);
        }
    }
    pthread_mutex_unlock(&s->access);return good?1:-1;
}
#endif
static int DGVRAMRenderMaterialize(struct DGHostContext *c)
{
    if(!c->vram_render_drawable || c->vram_render_materialized)return 1;
    struct DGHostDrawable *d=c->vram_render_drawable;
    if(c->drawable!=d || !c->vram || c->inside_begin)return 0;
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint read,draw,source_read,destination_draw;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
    if(!c->vram_render_framebuffer)glGenFramebuffersEXT(1,&c->vram_render_framebuffer);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,d->framebuffer);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c->vram_render_framebuffer);
    glGetIntegerv(GL_READ_BUFFER,&source_read);glGetIntegerv(GL_DRAW_BUFFER,&destination_draw);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,d->color,0);
    int good=glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    DGSetBlitScissor(c,GL_FALSE);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    if(good)glBlitFramebufferEXT(0,0,d->width,d->height,0,0,d->width,d->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glReadBuffer((GLenum)source_read);glDrawBuffer((GLenum)destination_draw);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
    GLenum error=glGetError();good=good && !error;
    if(good){
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,d->framebuffer);
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,d->color,0);
        c->color=d->color;c->color_storage=d->color_storage;c->vram_render_materialized=1;
        c->vram_materialized_group_serial=c->alias_group?c->alias_group->backing_write_serial:UINT64_MAX;
        c->vram_materialized_drawable_serial=d->write_serial;
        __atomic_fetch_add(&dg_vram_render_materializations,1,__ATOMIC_RELAXED);
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    if(scissor)DGSetBlitScissor(c,GL_TRUE);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    if(glGetError())good=0;
    return good;
}
static int DGVRAMRenderEndImpl(struct DGHostContext *c
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
    ,struct DGBackingTicket *ticket
#endif
)
{
    if(!c->vram_render_drawable)return 1;
    if(c->inside_begin || c->drawable!=c->vram_render_drawable)return 0;
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    int previously_materialized=c->vram_render_materialized;
    int wait=1;
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
    wait=ticket==NULL;
#endif
    int private_unchanged=c->vram_render_serial && c->alias_group &&
        c->vram_materialized_group_serial!=UINT64_MAX &&
        c->vram_materialized_group_serial==c->alias_group->backing_write_serial &&
        c->vram_materialized_drawable_serial==c->drawable->write_serial;
    int good=DGVRAMRenderMaterialize(c);
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
    /* Seal before a possible cross-context writeback. A rejected future
     * publication must not add another asynchronous RAM writer on failure. */
    if(good && ticket && ticket->publish_future)
        good=DGBackingTicketSealFuture(c,ticket,c->vram->object,c->vram->incarnation,
            c->vram->epoch,c->drawable->width,c->drawable->height);
#endif
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    if(good && c->vram->scatter_plan){
        if(ticket){
            // The sealed version is immutable, so later GPU writers cannot
            // change the source of this CPU materialization. CPU completion
            // is set by Wait ONLY after the ordered scatter, not this fence.
            if(!ticket->publish_future)good=DGBackingTicketSealCPU(c,ticket);
            if(good){
                ticket->scatter_plan=c->vram->scatter_plan;DGScatterPlanRetain(ticket->scatter_plan);
                ticket->cpu_predecessor=c->scatter_predecessor;c->scatter_predecessor=NULL;
            }
        }else{
            good=(!c->scatter_predecessor || !DGBackingTicketWait(c->scatter_predecessor)) && DGVRAMWait();
            if(good)good=DGScatterCopyCompletedSurface(c->vram->scatter_plan,c->color_storage);
            if(good){
                __atomic_fetch_add(&dg_scatter_stats[1],1,__ATOMIC_RELAXED);
                __atomic_fetch_add(&dg_scatter_stats[2],(uint64_t)c->drawable->width*c->drawable->height*4,__ATOMIC_RELAXED);
            }
        }
        if(good)__atomic_fetch_add(&dg_vram_render_republished_ends,1,__ATOMIC_RELAXED);
    }else
#endif
    if(good && previously_materialized && !private_unchanged){
        // A read/export can force an early private snapshot. Subsequent GL
        // writes then belong to that private target and MUST reach the CPU
        // backing at End too. Do not acknowledge only the earlier alias writes.
        struct {struct DGMessage m;struct DGVRAMRect rect;} p={0};
        p.m.op=DG_VRAM_PUBLISH_DRAWABLE;p.m.args[7]=c->drawable->width;p.m.args[8]=c->drawable->height;
        p.rect=(struct DGVRAMRect){0,0,c->drawable->width,c->drawable->height};
        int completed=0;good=DGVRAMCopyDrawable(c,&p.m,1,wait?&completed:NULL);
        if(good && wait && !completed)good=DGVRAMWait();
        if(good)__atomic_fetch_add(&dg_vram_render_republished_ends,1,__ATOMIC_RELAXED);
    }else if(good){
        // A read-only private snapshot did not supersede shared GPU pixels.
        // Skip only its redundant writeback, NEVER actual completion. Native
        // helper-only fixtures (no wire serial) retain the old conservative
        // route because their PassthroughOpenGL calls bypass packet write tracking.
        if(wait)good=DGVRAMWait();if(good && previously_materialized)
            __atomic_fetch_add(&dg_vram_render_read_only_ends,1,__ATOMIC_RELAXED);
    }
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
    if(good && ticket)good=DGBackingTicketPublish(ticket);
#endif
    if(!good){
        // Failure may have queued a prefix. Drain actual queues before a
        // caller can retire/unmap memory; never substitute a socket ACK.
        glFinish();
        struct DGHostDrawable *d=c->vram_render_drawable;
        GLint draw;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,d->framebuffer);
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,d->color,0);
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
        c->color=d->color;c->color_storage=d->color_storage;
    }
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    // Even an unsuccessful prefix must not leave an OLDER CPU writer racing
    // a caller which drains this scope. Failure still propagates/quarantines.
    if(c->scatter_predecessor){
        if(DGBackingTicketWait(c->scatter_predecessor))good=0;
        DGBackingTicketPut(c->scatter_predecessor);c->scatter_predecessor=NULL;
    }
#endif
    c->vram_render_drawable=NULL;c->vram_render_materialized=0;c->vram_render_serial=0;
    return good;
}
static int DGVRAMRenderEnd(struct DGHostContext *c)
{
    return DGVRAMRenderEndImpl(c
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
        ,NULL
#endif
    );
}
#ifdef VIM_BACKING_WRITER_EXPERIMENT
static uint64_t dg_writer_admission_refusals[4];
#endif
static int DGVRAMRenderRefuse(int cached_only,unsigned reason)
{
#ifdef VIM_BACKING_WRITER_EXPERIMENT
    if(cached_only && reason<4)__atomic_fetch_add(&dg_writer_admission_refusals[reason],1,__ATOMIC_RELAXED);
#else
    (void)cached_only;(void)reason;
#endif
    return 0;
}
static int DGVRAMRenderBeginImpl(struct DGHostContext *c,const struct DGMessage *m,struct DGReply *reply,int cached_only)
{
    reply->result=0;
    if(!c || !c->drawable || c->inside_begin || c->vram_render_drawable || c->double_buffer ||
       c->profile!=kCGLOGLPVersion_Legacy || m->args[5] || m->args[6] || m->args[9] ||
       m->args[7]!=(uint64_t)c->drawable->width || m->args[8]!=(uint64_t)c->drawable->height ||
       m->args[7]!=m->args[2] || m->args[8]!=m->args[3] || m->args[13]>1 || m->args[14]>16384 ||
       m->bytes!=m->args[14]*8)return DGVRAMRenderRefuse(cached_only,0);
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
    if(c->shared_surface && !c->drawable->scope_canonical)return DGVRAMRenderRefuse(cached_only,1);
#else
    if(c->shared_surface)return DGVRAMRenderRefuse(cached_only,1);
#endif
    int canonical_writer=0,live_alias=0;
    // Existing live exported aliases still name the private IOSurface. Do
    // not split one logical render target into independently changing copies.
    uint32_t storage=c->color_storage?IOSurfaceGetID(c->color_storage):0;
    if(c->alias_group)for(struct DGAlias *a=c->alias_group->aliases;a;a=a->next)
        if(storage && (a->storage==storage || a->source_storage==storage)){live_alias=1;break;}
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
    if(cached_only || live_alias)canonical_writer=DGVRAMCanonicalWriterTarget(c,m,cached_only);
#endif
    if(live_alias && !canonical_writer)return DGVRAMRenderRefuse(cached_only,2);
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    // Pending RAM MUST NOT be passed to TexImage/TextureRange to create a
    // registration. That native driver path can access the CPU mapping.
    int creating=cached_only?0:2;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    // Attach/validate the GPU resource before reconstructing RAM. Only its
    // EXACT canonical epoch may bypass native RAM registration. A CPU-newer
    // or otherwise unsupported backing keeps the original import path.
    if(!DGVRAMRenderCanonical(c,m))return DGVRAMRenderRefuse(cached_only,3);
    if(dg_backing_scatter && DGVRAMCanonicalWriterTarget(c,m,cached_only))creating=3;
#ifdef VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT
    else if(DGVRAMCanonicalInitializeCandidate(c,m,cached_only))creating=3;
#endif
#endif
    int available=DGVRAMBacking(c,m,creating);
    if(available!=1){reply->result=available;return DGVRAMRenderRefuse(cached_only,3);}
    struct DGVRAMDrawableBacking *b=c->vram;
#ifdef VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT
    if(DGVRAMCanonicalInitialize(c,m,cached_only)<0){reply->error=GL_INVALID_OPERATION;return 0;}
#endif
    // Reuse ONE registered native resource for these exact pages, not two
    // differently shaped client-storage textures covering the same address.
    // Never reinterpret an old pitched-width registration as an exact view.
    if(b->native_width!=b->width)return DGVRAMRenderRefuse(cached_only,3);
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
#ifndef VIM_BACKING_SCATTER_EXPERIMENT
    if(!DGVRAMRenderCanonical(c,m))return DGVRAMRenderRefuse(cached_only,3);
#endif
    /* A prepared/new private view can become an EXISTING canonical resource
     * here. Its old color (possibly zero) cannot identify live aliases of
     * that destination. Re-check after attachment before selecting RAM vs
     * canonical rendering; never reintroduce the live-alias target split. */
    storage=c->color_storage?IOSurfaceGetID(c->color_storage):0;live_alias=0;
    if(c->alias_group)for(struct DGAlias *a=c->alias_group->aliases;a;a=a->next)
        if(storage && (a->storage==storage || a->source_storage==storage)){live_alias=1;break;}
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
    if(cached_only || live_alias
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
       || b->scatter_plan
#endif
       )canonical_writer=DGVRAMCanonicalWriterTarget(c,m,cached_only);
#endif
    if(live_alias && !canonical_writer)return DGVRAMRenderRefuse(cached_only,2);
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    if(b->scatter_plan && !canonical_writer)return DGVRAMRenderRefuse(cached_only,3);
#endif
#endif
    GLint read,draw;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    GLuint target=canonical_writer?c->drawable->color:b->texture;
    int good=target!=0;
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c->framebuffer);
    if(good){
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
        if(b->scatter_plan)__atomic_fetch_add(&dg_scatter_stats[3],1,__ATOMIC_RELAXED);
#endif
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,target,0);
        good=glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT && !glGetError();
    }
    if(good){
        c->vram_render_drawable=c->drawable;c->vram_render_materialized=canonical_writer;
        c->color=target;c->color_storage=canonical_writer?c->drawable->color_storage:NULL;
        if(canonical_writer){
            c->vram_materialized_group_serial=c->alias_group?c->alias_group->backing_write_serial:UINT64_MAX;
            c->vram_materialized_drawable_serial=c->drawable->write_serial;
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
            if(cached_only){
                __atomic_fetch_add(&dg_canonical_writer_stats[0],1,__ATOMIC_RELAXED);
                if(live_alias)__atomic_fetch_add(&dg_canonical_writer_stats[1],1,__ATOMIC_RELAXED);
            }else __atomic_fetch_add(&dg_cpu_canonical_stats[0],1,__ATOMIC_RELAXED);
#endif
        }
        b->epoch=m->args[12];reply->result=1;
    }else {
        DGVRAMRenderRefuse(cached_only,3);
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,c->drawable->color,0);
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    if(glGetError())reply->error=GL_INVALID_OPERATION;
    return 0;
}
static int DGVRAMRenderBegin(struct DGHostContext *c,const struct DGMessage *m,struct DGReply *reply)
{return DGVRAMRenderBeginImpl(c,m,reply,0);}
/* Additive wire qualification. This token never grants a pin or a CPU lock:
 * the trusted guest client must own both through its matching kernel loan.
 * No ordinary import can start a borrowed scope. Reject a stale scope before
 * any materialization, backing replacement or GPU operation. */
static int DGVRAMRenderCommand(struct DGHostContext *c,const struct DGMessage *m,struct DGReply *reply)
{
    if(m->op==DG_BACKING_RENDER_BEGIN){
        if(m->flags!=(DG_REPLY|DG_INPUT_ONLY) || m->args[13]!=1 || !m->args[15] ||
           m->args[15]<=c->vram_render_last_serial || c->vram_render_serial){
            reply->error=GL_INVALID_OPERATION;return 0;
        }
        int error=DGVRAMRenderBegin(c,m,reply);
        if(!error && reply->result==1 && !reply->error){
            c->vram_render_last_serial=c->vram_render_serial=m->args[15];
            __atomic_fetch_add(&dg_vram_render_begins,1,__ATOMIC_RELAXED);
        }else if(!error && !reply->result){
            // A rejected native attachment can have created/queued an input
            // registration before failing. The caller's read-only loan release
            // is safe only after those actual uses, not a cancellation ACK.
            glFinish();
        }
        return error;
    }
    if(m->flags!=DG_REPLY || m->bytes || !m->args[0] ||
       m->args[0]!=c->vram_render_serial || !c->vram_render_drawable || c->inside_begin){
        reply->error=GL_INVALID_OPERATION;return 0;
    }
    unsigned first_unused=1;
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
    if(m->op==DG_BACKING_RENDER_DEFER && m->args[1]==1)first_unused=2;
#endif
    for(unsigned i=first_unused;i<16;++i)if(m->args[i]){reply->error=GL_INVALID_OPERATION;return 0;}
    if(m->op==DG_BACKING_RENDER_MATERIALIZE){
        if(DGVRAMRenderMaterialize(c))reply->result=1;
        else reply->error=GL_INVALID_OPERATION;
    }
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
    else if(m->op==DG_BACKING_RENDER_DEFER){
        if(!(c->surface_namespace[0]|c->surface_namespace[1])){
            reply->error=GL_INVALID_OPERATION;return 0;
        }
        struct DGBackingTicket *ticket=DGBackingTicketPrepare(c);
        if(!ticket){reply->error=GL_OUT_OF_MEMORY;return 0;}
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
        ticket->publish_future=m->args[1]==1;
#endif
        uint64_t name=ticket->name;
        if(DGVRAMRenderEndImpl(c,ticket))reply->result=name;
        else {DGBackingTicketCancel(ticket);reply->error=GL_INVALID_OPERATION;}
    }
#endif
    else{
        if(DGVRAMRenderEnd(c))reply->result=1;
        else reply->error=GL_INVALID_OPERATION;
    }
    return 0;
}
