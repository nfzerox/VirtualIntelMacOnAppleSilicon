/* Private origin-zero rectangle imports. Native Mavericks demonstrates live
 * sharing across CGL share groups. Kernel identity/epochs, not CGS numbers
 * or CPU addresses, identify the retained host resource. This research path
 * is opt-in until primary drawing views and the remaining private formats qualify.
 */
struct VIMCGSLease {
    void *backend;uint32_t lease;uint64_t object,incarnation,seen_epoch;
    int snapshot;
    struct VIMCGSLease *next;
};
struct VIMCGSTexture {
    GLuint texture;GLenum internal;uint32_t width,height,backing_width,backing_height;
    io_connect_t connection;
    void *hold;uint64_t held_object,held_incarnation;
    int retired;
    unsigned pixel_format;
    struct VIMCGSLease *leases;
    struct VIMCGSTexture *next;
};
static void VIMCGSFailure(const char *stage,GLuint texture,IOReturn error,const struct VIMSurfaceState *state)
{
    static unsigned emitted;
    if(!getenv("VIM_GLI_TRACE") || __atomic_load_n(&emitted,__ATOMIC_RELAXED)>=16 ||
       __sync_fetch_and_add(&emitted,1)>=16) return;
    syslog(LOG_NOTICE,"VIM CGS %s texture=%u error=%x object=%llx incarnation=%llu epoch=%llu flags=%x",
        stage,texture,error,state->object,state->incarnation,state->content_epoch,state->flags);
}
static void VIMFreeCGSTexture(struct VIMCGSTexture *s)
{
    while(s->leases) {
        struct VIMCGSLease *p=s->leases;s->leases=p->next;
        DGReleaseSurfaceInContext(p->backend,p->lease);free(p);
    }
    DGReleaseSurfaceHold(s->hold);
    if(s->connection) IOServiceClose(s->connection);free(s);
}
static void VIMForgetCGSTextures(GLIContext ctx,GLsizei count,const GLuint *names)
{
    struct VIMShareGroup *g=((struct VIMContext *)ctx)->group;
    if(!__atomic_load_n(&g->cgs_count,__ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&g->lock);
    struct VIMCGSTexture **link=&g->cgs_textures;
    while(*link) {
        int remove=0;
        for(GLsizei i=0;i<count;++i) if((*link)->texture==names[i]) { remove=1;break; }
        if(remove) {
            struct VIMCGSTexture *old=*link;*link=old->next;VIMFreeCGSTexture(old);
            __atomic_sub_fetch(&g->cgs_count,1,__ATOMIC_RELEASE);
        } else link=&(*link)->next;
    }
    pthread_mutex_unlock(&g->lock);
}
static void VIMDestroyCGSContext(struct VIMContext *c)
{
    struct VIMShareGroup *g=c->group;
    pthread_mutex_lock(&g->lock);
    for(struct VIMCGSTexture *s=g->cgs_textures;s;s=s->next) {
        struct VIMCGSLease **link=&s->leases;
        while(*link) {
            struct VIMCGSLease *p=*link;
            if(p->backend==c->backend) { *link=p->next;DGReleaseSurfaceInContext(p->backend,p->lease);free(p); }
            else link=&p->next;
        }
    }
    if(g->refs==1) {
        while(g->cgs_textures) { struct VIMCGSTexture *s=g->cgs_textures;g->cgs_textures=s->next;VIMFreeCGSTexture(s); }
        __atomic_store_n(&g->cgs_count,0,__ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g->lock);
}
static int VIMCGSState(io_connect_t connection,struct VIMSurfaceState *state)
{
    size_t bytes=sizeof(*state);memset(state,0,bytes);
    IOReturn error=IOConnectCallStructMethod(connection,kIOVM2DGetSurfaceState,NULL,0,state,&bytes);
    return !error && bytes==sizeof(*state) && state->version==VIM_SURFACE_STATE_VERSION &&
        state->object && state->incarnation && state->content_epoch &&
        (state->flags&VIM_SURFACE_STATE_CONTEXT_LOCKED) && !(state->flags&VIM_SURFACE_STATE_CPU_WRITER);
}
/* Called with this binding's kernel mapping and its GL share-group lock.
 * The kernel snapshot is not a promise about a later concurrent writer.
 * A post-read unlock snapshot below must still match this epoch. */
static int VIMUpdateCGSLocked(struct VIMContext *c,struct VIMCGSTexture *s,
    const struct VIMSurfaceMapping *mapping,const struct VIMSurfaceState *state,int initial)
{
    if(mapping->layout.width!=s->backing_width || mapping->layout.height!=s->backing_height) return 0;
    void *new_hold=NULL;
    int need_hold=!s->hold || s->held_object!=state->object || s->held_incarnation!=state->incarnation;
    struct VIMCGSLease *p=s->leases;
    for(;p;p=p->next) if(p->backend==c->backend) break;
    if(!p) {
        p=calloc(1,sizeof(*p));if(!p) { DGReleaseSurfaceHold(new_hold);return 0; }
        p->backend=c->backend;p->next=s->leases;s->leases=p;
    }
    int rebind=initial || need_hold;
    if(p->lease && (p->object!=state->object || p->incarnation!=state->incarnation)) {
        DGReleaseSurface(p->lease);p->lease=0;p->seen_epoch=0;
    }
    if(!p->lease) {
        p->lease=DGAcquireTypedSurface(state->object,state->incarnation,s->backing_width,s->backing_height,s->pixel_format);
        p->object=state->object;p->incarnation=state->incarnation;rebind=1;
        if(!p->lease) goto fail;
    }
    // Service holds are lookup-only. Establish producer/consumer storage
    // through the GL lease first, then retain it across context destruction.
    if(need_hold) {
        new_hold=DGCreateSurfaceHold(c->backend,state->object,state->incarnation,s->backing_width,s->backing_height);
        if(!new_hold)goto fail;
    }
    if(state->flags&VIM_SURFACE_STATE_GPU_PENDING){
        if(mapping->address || mapping->length || !(state->flags&VIM_SURFACE_STATE_GPU_FUTURE_SEALED))goto fail;
        if(rebind || !p->snapshot || p->seen_epoch!=state->content_epoch){
            struct VIMSurfaceFutureRead future={0};size_t size=sizeof(future);
            if(IOConnectCallStructMethod(s->connection,kIOVM2DGetFutureGPUSurface,NULL,0,&future,&size) ||
               size!=sizeof(future) || memcmp(&future.state,state,sizeof(*state)))goto fail;
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,s->texture);
            if(!DGCopyBackingFutureTexture(future.ticket,state,s->internal,s->width,s->height))goto fail;
        }
        p->snapshot=1;p->seen_epoch=state->content_epoch;rebind=0;
    } else if(state->flags&VIM_SURFACE_STATE_GPU_NEWER) {
        if(mapping->address || mapping->length)goto fail;
        if(rebind || !p->snapshot || p->seen_epoch!=state->content_epoch) {
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,s->texture);
            if(!DGCopySurfaceVersionTexture(p->lease,state->content_epoch,s->internal,s->width,s->height))goto fail;
        }
        p->snapshot=1;p->seen_epoch=state->content_epoch;
        rebind=0;
    } else {
    if(!mapping->address)goto fail;
    rebind|=p->snapshot;
    if(p->seen_epoch!=state->content_epoch) {
        uint64_t epoch=0;
        if(!DGSurfaceEpoch(p->lease,&epoch)) goto fail;
        if(epoch<state->content_epoch) {
            const void *pixels=(const void *)(uintptr_t)(mapping->address+mapping->layout.offset);
            if(!DGUploadSurface(p->lease,pixels,s->backing_width,s->backing_height,mapping->layout.row_bytes,state->content_epoch)) goto fail;
        }
        p->seen_epoch=state->content_epoch;
    }
    if(rebind) {
        int view=s->width!=s->backing_width || s->height!=s->backing_height;
        int surface_bound=view ? DGBindSurfaceView(p->lease,s->internal,s->width,s->height) : DGBindSurface(p->lease,s->internal);
        if(!surface_bound) goto fail;
    }
    p->snapshot=0;
    }
    if(new_hold) {
        DGReleaseSurfaceHold(s->hold);s->hold=new_hold;s->held_object=state->object;s->held_incarnation=state->incarnation;
    }
    // CGLIOSurface requires consumer bind AFTER the publishing stream flush.
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,s->texture);return 1;
fail:
    DGReleaseSurfaceHold(new_hold);return 0;
}
static int VIMCGSReadFinished(io_connect_t connection,const struct VIMSurfaceState *before)
{
    struct VIMSurfaceState after={0};
    return !VIMUnlockSurfaceMode(connection,1,&after) && after.object==before->object &&
        after.incarnation==before->incarnation && after.content_epoch==before->content_epoch &&
        !(after.flags&VIM_SURFACE_STATE_CPU_WRITER);
}
static CGLError VIMImportCGSTexture(struct VIMContext *c,const uint32_t *a,io_connect_t connection,
    const struct VIMSurfaceMapping *mapping)
{
    struct VIMSurfaceState state={0};struct VIMShareGroup *g=c->group;
    GLuint name=VIMAppleTexture((GLIContext)c,a[1]);
    struct VIMCGSTexture *s=calloc(1,sizeof(*s));
    if(!s || !name || !VIMCGSState(connection,&state)) {
        VIMCGSFailure("import preflight",name,0,&state);
        free(s);VIMUnlockSurface(connection);IOServiceClose(connection);return kCGLBadDrawable;
    }
    s->connection=connection;s->texture=name;s->internal=a[2];s->width=a[3];s->height=a[4];
    s->backing_width=mapping->layout.width;s->backing_height=mapping->layout.height;
    if(mapping->layout.bits_per_pixel!=32 && !VIMSurfaceFormat(connection,mapping,&s->pixel_format)) {
        free(s);VIMUnlockSurface(connection);IOServiceClose(connection);return kCGLBadDrawable;
    }
    pthread_mutex_lock(&g->lock);
    int good=g->cgs_count<256 && VIMUpdateCGSLocked(c,s,mapping,&state,1);
    good=VIMCGSReadFinished(connection,&state) && good;
    if(good) {
        struct VIMCGSTexture **link=&g->cgs_textures;
        while(*link && (*link)->texture!=name) link=&(*link)->next;
        if(*link) { struct VIMCGSTexture *old=*link;*link=old->next;VIMFreeCGSTexture(old);
            __atomic_sub_fetch(&g->cgs_count,1,__ATOMIC_RELEASE); }
        s->next=g->cgs_textures;g->cgs_textures=s;__atomic_add_fetch(&g->cgs_count,1,__ATOMIC_RELEASE);
    } else { VIMCGSFailure("import transfer",name,0,&state);VIMFreeCGSTexture(s); }
    pthread_mutex_unlock(&g->lock);return good?kCGLNoError:kCGLBadDrawable;
}
static void VIMRefreshCGSTexture(GLIContext ctx,GLenum target,GLuint texture)
{
    struct VIMContext *c=(void *)ctx;struct VIMShareGroup *g=c->group;
    if(c->inside_begin || !texture || target!=GL_TEXTURE_RECTANGLE_ARB || !__atomic_load_n(&g->cgs_count,__ATOMIC_ACQUIRE)) return;
    struct VIMSavedBinding previous=VIMSaveGLI(ctx);if(!VIMBindGLI(ctx)){VIMDiscardGLI(previous);return;}
    pthread_mutex_lock(&g->lock);
    struct VIMCGSTexture *s=g->cgs_textures;
    for(;s;s=s->next) if(s->texture==texture) break;
    if(s && !s->retired) {
        struct VIMSurfaceMapping mapping;struct VIMSurfaceState state={0};
        IOReturn error=VIMLockReadTextureSurface(s->connection,&mapping);
        int good=0;
        if(error==kIOReturnOffline && s->hold) {
            // Physical NVIDIA keeps the imported dimensions but returns
            // transparent pixels after destruction of the CGS source.
            uint32_t lease=DGAcquireTypedSurface(s->held_object,s->held_incarnation,s->backing_width,s->backing_height,s->pixel_format);
            good=lease && DGRetireSurface(lease);
            if(good) {
                glBindTexture(target,texture);
                good=(s->width==s->backing_width && s->height==s->backing_height) ?
                    DGBindSurface(lease,s->internal) : DGBindSurfaceView(lease,s->internal,s->width,s->height);
            }
            if(lease)DGReleaseSurface(lease);
            if(good) { s->retired=1;glBindTexture(target,texture); }
        } else {
            good=!error && VIMCGSState(s->connection,&state) && VIMUpdateCGSLocked(c,s,&mapping,&state,0);
            if(!error) good=VIMCGSReadFinished(s->connection,&state) && good;
        }
        if(!good) { VIMCGSFailure("rebind",texture,error,&state);c->error=GL_INVALID_OPERATION; }
    }
    pthread_mutex_unlock(&g->lock);VIMRestoreGLI(previous,ctx);
}
static GLuint VIMCGSBeginRedefine(GLIContext ctx,GLenum target,GLint level)
{
    struct VIMContext *c=(void *)ctx;struct VIMShareGroup *g=c->group;
    if(c->inside_begin || level || target!=GL_TEXTURE_RECTANGLE_ARB ||
       (!__atomic_load_n(&g->cgs_count,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&g->surface_count,__ATOMIC_ACQUIRE))) return 0;
    GLuint texture=VIMAppleTexture(ctx,target);int found=0,imported=0;
    pthread_mutex_lock(&g->lock);
    for(struct VIMCGSTexture *s=g->cgs_textures;s;s=s->next) if(s->texture==texture) { found=1;break; }
    for(struct VIMSurface *s=g->surfaces;s;s=s->next)if(s->texture==texture){found=imported=1;break;}
    pthread_mutex_unlock(&g->lock);
    if(!found) return 0;
    if(imported)VIMFlushBeforeTextureDelete(ctx,1,&texture);
    GLenum earlier=VIMFunction_get_error();if(earlier && !c->error)c->error=earlier;
    return texture;
}
static void VIMCGSEndRedefine(GLIContext ctx,GLuint texture)
{
    struct VIMContext *c=(void *)ctx;GLenum error=VIMFunction_get_error();
    if(error) { if(!c->error)c->error=error; }
    else {VIMForgetCGSTextures(ctx,1,&texture);VIMForgetIOSurfaces(ctx,1,&texture);}
}
