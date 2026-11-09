/* Canonical framebuffer/surface blits. Snapshot before the first write: a
 * sequence of overlapping moves must read one immutable source epoch, not
 * accidentally consume pixels overwritten by an earlier rectangle. Uses the
 * existing bounded GPU snapshot allocator; no CPU image/lock/readback. */
static GLenum DGSurfaceCopyRegionsMode(struct DGHostContext *c,struct DGSurfaceLease *p,const struct DGMessage *m,int seal)
{
    struct DGSharedSurface *s=p->resource;
    unsigned count=(unsigned)m->args[3];
    if(!m->args[1] || m->args[2]<=m->args[1] || !m->args[3] || m->args[3]>DG_SURFACE_REGION_LIMIT ||
       m->args[4] || m->args[5] || m->args[6]!=1 || m->bytes!=count*sizeof(struct DGSurfaceRegion))
        return GL_INVALID_VALUE;
    const struct DGSurfaceRegion *regions=(const void *)(m+1);
    for(unsigned i=0;i<count;++i) {
        const struct DGSurfaceRegion *a=&regions[i];
        if(a->x<0 || a->y<0 || a->destination_x<0 || a->destination_y<0 || a->width<=0 || a->height<=0 ||
           a->width>s->width || a->height>s->height || a->x>s->width-a->width || a->y>s->height-a->height ||
           a->destination_x>s->width-a->width || a->destination_y>s->height-a->height)return GL_INVALID_VALUE;
        for(unsigned j=0;j<i;++j) {
            const struct DGSurfaceRegion *b=&regions[j];
            if(a->destination_x<b->destination_x+b->width && b->destination_x<a->destination_x+a->width &&
               a->destination_y<b->destination_y+b->height && b->destination_y<a->destination_y+a->height)
                return GL_INVALID_VALUE;
        }
    }
    /* This temporary source never leaves the producer's CGL stream. Keep
     * the immutable copy for overlapping moves, but no intermediate native
     * Flush/fence is needed between its creation and these dependent blits. */
    struct DGSurfaceSnapshot *source=DGSurfaceSnapshotCreateCommands(c,p,m->args[1],0);
    if(!source)return GL_INVALID_OPERATION;
    pthread_mutex_lock(&s->access);
    GLenum error=GL_NO_ERROR;int flushed=0;struct DGSurfaceSnapshot *old=NULL;
    if(s->retired || s->epoch!=m->args[1])error=GL_INVALID_OPERATION;
    else {
        GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
        GLint texture=0,read=0,draw=0,destination_draw=0;
        glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
        GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
        GLuint f=0;int good=source->native_texture!=0;
        glGenFramebuffersEXT(1,&f);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,f);
        glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,source->native_texture,0);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        good=good && f && glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,p->framebuffer);
        glGetIntegerv(GL_DRAW_BUFFER,&destination_draw);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        DGSetBlitScissor(c,GL_FALSE);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
        error=glGetError();if(!good && !error)error=GL_INVALID_OPERATION;
        int wrote=0;
        for(unsigned i=0;i<count && !error;++i) {
            const struct DGSurfaceRegion *a=&regions[i];wrote=1;
            glBlitFramebufferEXT(a->x,a->y,a->x+a->width,a->y+a->height,
                a->destination_x,a->destination_y,a->destination_x+a->width,a->destination_y+a->height,
                GL_COLOR_BUFFER_BIT,GL_NEAREST);
            error=glGetError();
        }
        glDrawBuffer(destination_draw);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
        if(scissor)DGSetBlitScissor(c,GL_TRUE);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB_EXT);
        if(f)glDeleteFramebuffersEXT(1,&f);
        GLenum cleanup=glGetError();if(!error)error=cleanup;
        /* The compound path's seal submits this SAME producer prefix, with
         * its completion fence. Keep exclusion/lock through that submission;
         * do not expose a pending epoch between two commands/Flush calls. */
        if(!seal || error){
            glFlush();flushed=1;GLenum submitted=glGetError();if(!error)error=submitted;
        }
        if(!error){
            /* The transient image has no foreign reader or exported fence.
             * Its last read is queued and the temporary FBO is deleted. As
             * with separate copy/seal calls, return it before allocating the
             * final image. Same-producer ordering protects reuse without an
             * extra full-screen allocation or an intermediate GPU submission. */
            if(seal){DGSurfaceSnapshotDestroy(source);source=NULL;}
            s->epoch=m->args[2];
            if(seal){
                struct DGSurfaceSnapshot *v=DGSurfaceSnapshotCreateLocked(c,p,s->epoch,1);
                if(v){old=s->published_version;s->published_version=v;flushed=1;}
                else error=GL_INVALID_OPERATION;
            }
            if(c->shared_surface==s && c->drawable){c->drawable->alias_dirty=1;++c->drawable->write_serial;}
        }
        if(error && wrote)__atomic_store_n(&s->retired,1,__ATOMIC_RELEASE);
    }
    /* The snapshot may have queued a prefix before the second epoch check.
     * Submit that prefix even when the source was concurrently superseded. */
    if(!flushed)glFlush();
    pthread_mutex_unlock(&s->access);DGSurfaceSnapshotDestroy(old);DGSurfaceSnapshotDestroy(source);return error;
}
static GLenum DGSurfaceCopyRegions(struct DGHostContext *c,struct DGSurfaceLease *p,const struct DGMessage *m)
{return DGSurfaceCopyRegionsMode(c,p,m,0);}
