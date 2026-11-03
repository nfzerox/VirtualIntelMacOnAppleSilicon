/* Imported IOSurface writes outlive FBO bindings. Cache actual native draw
 * attachments, invalidated by binding/attachment/name changes, not guessed
 * application state. Ordinary contexts without imports add no GL queries.
 * Only a new GPU write serial can require CPU publication. */
static int DGVRAMWait(void);
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
static uint64_t dg_scoped_alias_cpu_waits;
#endif
/* A CPU texture upload through a live alias is a CPU-ownership boundary,
 * not merely a later GPU command. Physical Intel Mavericks reproduces an
 * old producer draw overwriting it despite Flush AND server-side waits.
 * Actual producer completion BEFORE the upload is required. Only matching
 * active scoped writers in this serialized endpoint participate; ordinary
 * draws, unrelated textures and contexts without scopes add no native query
 * or fence. No CPU gate is released by this internal ordering operation. */
static int DGPrepareScopedTextureCPUWrite(struct DGHostContext *c,GLenum target)
{
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    struct DGAliasGroup *g=c->alias_group;int scoped=0;
    if(c->inside_begin || target!=GL_TEXTURE_RECTANGLE_ARB || !g || !g->aliases)return 1;
    for(struct DGHostContext *p=g->members;p;p=p->alias_member_next)
        if(p!=c && p->vram_render_serial && p->color_storage){scoped=1;break;}
    if(!scoped)return 1;
    GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    struct DGAlias *alias=g->aliases;
    for(;alias;alias=alias->next)if(alias->texture==(GLuint)texture && !alias->source_storage)break;
    if(!alias)return 1;
    for(struct DGHostContext *p=g->members;p;p=p->alias_member_next){
        if(p==c || !p->vram_render_serial || !p->color_storage ||
           IOSurfaceGetID(p->color_storage)!=alias->storage)continue;
        int good=CGLSetCurrentContext(p->cgl)==kCGLNoError;
        if(good){
            GLenum prior=glGetError();if(prior && !p->error)p->error=prior;
            good=DGVRAMWait();
            GLenum error=glGetError();if(error){if(!p->error)p->error=error;good=0;}
        }
        if(CGLSetCurrentContext(c->cgl)!=kCGLNoError)good=0;
        if(!good){if(!c->error)c->error=GL_INVALID_OPERATION;return 0;}
        __atomic_fetch_add(&dg_scoped_alias_cpu_waits,1,__ATOMIC_RELAXED);
    }
#else
    (void)c;(void)target;
#endif
    return 1;
}
static void DGMarkIOSurfaceAlias(struct DGHostContext *c,struct DGAlias *p)
{
    struct DGAliasGroup *g=c->alias_group;
    if(g->iosurface_write_clock!=UINT64_MAX)p->iosurface_write_serial=++g->iosurface_write_clock;
    p->iosurface_writer=c;p->iosurface_submitted_serial=0;
}
/* Native Flush submits only the CURRENT context. A different shared context's
 * Flush cannot acknowledge queued writes whose producer has not flushed yet.
 * Track that CPU/GPU ownership boundary without switching/flushing all peers. */
static void DGSubmitIOSurfaceWrites(struct DGHostContext *c)
{
    struct DGAliasGroup *g=c->alias_group;
    if(c->inside_begin||!g||!g->iosurface_import_count)return;
    for(struct DGAlias *p=g->aliases;p;p=p->next)
        if(p->iosurface_import&&p->iosurface_writer==c)
            p->iosurface_submitted_serial=p->iosurface_write_serial;
}
static void DGMarkIOSurfaceTextureWrite(struct DGHostContext *c,GLenum target)
{
    struct DGAliasGroup *g=c->alias_group;
    if(c->inside_begin||target!=GL_TEXTURE_RECTANGLE_ARB||!g||!g->iosurface_import_count)return;
    GLint name=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&name);
    for(struct DGAlias *p=g->aliases;p;p=p->next)
        if(p->texture==(GLuint)name&&p->iosurface_import){DGMarkIOSurfaceAlias(c,p);break;}
}
static void DGMarkIOSurfaceWrites(struct DGHostContext *c)
{
    struct DGAliasGroup *g=c->alias_group;unsigned all=c->iosurface_mark_all;c->iosurface_mark_all=0;
    if(c->inside_begin||!g||!g->iosurface_import_count)return;
    if(all||c->iosurface_draw_dirty||c->iosurface_draw_generation!=g->generation){
        GLint framebuffer=0;c->iosurface_draw_count=0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&framebuffer);
        if(framebuffer&&(GLuint)framebuffer!=c->framebuffer){
            GLint maximum=0;glGetIntegerv(all?GL_MAX_COLOR_ATTACHMENTS_EXT:GL_MAX_DRAW_BUFFERS,&maximum);
            if(maximum<0||maximum>32){c->error=GL_INVALID_OPERATION;return;}
            for(GLint i=0;i<maximum;++i){
                GLint attachment=GL_COLOR_ATTACHMENT0_EXT+i,type=0,name=0;
                if(!all)glGetIntegerv(GL_DRAW_BUFFER0+i,&attachment);
                if(attachment<GL_COLOR_ATTACHMENT0_EXT||attachment>=GL_COLOR_ATTACHMENT0_EXT+32)continue;
                glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER,attachment,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,&type);
                if(type!=GL_TEXTURE)continue;
                glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER,attachment,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&name);
                for(struct DGAlias *p=g->aliases;p;p=p->next)if(p->texture==(GLuint)name&&p->iosurface_import){
                    c->iosurface_draw_textures[c->iosurface_draw_count++]=(GLuint)name;break;
                }
            }
        }
        c->iosurface_draw_generation=g->generation;c->iosurface_draw_dirty=all;
    }
    for(unsigned i=0;i<c->iosurface_draw_count;++i)
        for(struct DGAlias *p=g->aliases;p;p=p->next)
            if(p->texture==c->iosurface_draw_textures[i]&&p->iosurface_import){DGMarkIOSurfaceAlias(c,p);break;}
}
static int DGReadIOSurfaceTexture(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    uint64_t width=m->args[2],height=m->args[3],stride=m->args[4];
    uint64_t full_height=m->args[7]?m->args[7]:height,start=m->args[8];
    uint64_t element_bytes=m->args[6]?m->args[6]:4;
    if((element_bytes!=1&&element_bytes!=2&&element_bytes!=4&&element_bytes!=8&&element_bytes!=16)||
       (m->flags!=DG_REPLY&&m->flags!=(DG_REPLY|DG_OUTPUT_ONLY))||!width||!height||width>8192||full_height>8192||
       start>=full_height||height>full_height-start||(!m->args[7]&&start)||stride<width*element_bytes||
       stride>DG_MAX_BYTES||(height-1)*stride+width*element_bytes!=m->bytes)return -1;
    unsigned char *out=DGHostPointer(m,5,m->bytes);if(!out)return -1;
    if(c->inside_begin){reply->error=GL_INVALID_OPERATION;return 0;}
    struct DGAlias *p=c->alias_group->aliases;
    for(;p;p=p->next)if(p->texture==m->args[0]&&p->iosurface_import)break;
    /* A changed/deleted alias or a newer writer invalidates this observation.
     * No output copy/ack is allowed; a later flush reports the current serial. */
    if(!p||!m->args[1]||p->iosurface_write_serial!=m->args[1]||
       p->iosurface_submitted_serial!=m->args[1]||!p->owned_storage)return 0;
    IOSurfaceRef surface=p->owned_storage;
    if(IOSurfaceGetWidth(surface)!=width||IOSurfaceGetHeight(surface)!=full_height||
       IOSurfaceGetBytesPerElement(surface)!=element_bytes)return -1;
    glFlush();
    kern_return_t error=IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
    if(!error){
        const unsigned char *in=IOSurfaceGetBaseAddress(surface);
        size_t pitch=IOSurfaceGetBytesPerRow(surface),allocation=IOSurfaceGetAllocSize(surface);
        if(!in||pitch<width*element_bytes||pitch>allocation/full_height)error=kIOReturnBadArgument;
        else for(size_t y=0;y<height;++y)memcpy(out+y*stride,in+(start+y)*pitch,(size_t)width*element_bytes);
        kern_return_t unlock=IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);if(!error)error=unlock;
    }
    if(error)reply->error=GL_INVALID_OPERATION;else reply->result=1;
    return 0;
}
