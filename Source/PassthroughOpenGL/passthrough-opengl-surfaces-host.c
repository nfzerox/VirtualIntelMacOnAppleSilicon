/* Included by passthrough-opengl-host.c. A retained, VM-boot-scoped resource pool,
 * independent of CGL object sharing groups. CGLIOSurface.h specifies the live
 * producer flush -> consumer bind contract measured on physical Mavericks.
 * No mapped guest address, CGS numeric ID, or IOSurface global ID is a key.
 *
 * One renderer endpoint serves one research VM. The namespace is NOT an
 * authentication boundary. Production multiplexing needs trusted VM scoping.
 * All GL names below belong to their context and are destroyed there. Pool
 * references are leases, not an unbounded cache; disconnect destroys them.
 */
#define DG_SURFACE_LIMIT 256u
#define DG_SURFACE_LEASE_LIMIT 4096u
#define DG_SURFACE_BYTE_LIMIT (256u*1024u*1024u)
struct DGSurfaceSnapshot;
static void DGSurfaceSnapshotDestroy(struct DGSurfaceSnapshot *);
static void DGSnapshotPoolClose(struct DGHostContext *);
struct DGSharedSurface {
    uint64_t namespace[2],resource,incarnation;
    int width,height;size_t bytes;unsigned refs,format;
    IOSurfaceRef surface;
    uint64_t epoch;int retired;pthread_mutex_t access;
    struct DGSurfaceSnapshot *published_version;
    struct DGSharedSurface *next;
};
/* A lease and any drawable in THIS owning context may retain the same native
 * texture registration. IOSurface identity alone does not keep a GL name
 * alive. Never borrow a lease's unowned texture or share this record between
 * independent CGL contexts; command execution owns these reference updates. */
struct DGSurfaceRegistration {GLuint texture;unsigned refs;struct DGPrivateTextureWatch texture_watch;};
static volatile int dg_surface_registration_reuse=1;
static uint64_t dg_surface_registration_stats[4]; // imports, shared attachments, retired private targets, live registrations
static void DGSurfaceRegistrationPut(struct DGSurfaceRegistration *p)
{
    if(!p || --p->refs)return;
    glDeleteTextures(1,&p->texture);free(p);
    __atomic_fetch_sub(&dg_surface_registration_stats[3],1,__ATOMIC_RELAXED);
}
struct DGSurfaceLease {
    uint32_t name;GLuint texture,framebuffer;
    struct DGPrivateTextureWatch texture_watch;
    struct DGSurfaceRegistration *registration;
    struct DGSharedSurface *resource;
    struct DGSurfaceLease *next;
};
static pthread_mutex_t dg_surface_lock=PTHREAD_MUTEX_INITIALIZER;
static struct DGSharedSurface *dg_surfaces;
static size_t dg_surface_bytes;
static unsigned dg_surface_count,dg_surface_leases;
static int DGDrawableWritable(struct DGHostContext *c)
{
    // No GL query on the normal draw path. A retired source must not be
    // resurrected by a late clear/draw through an attached default buffer.
    if(!c->shared_surface || !__atomic_load_n(&c->shared_surface->retired,__ATOMIC_ACQUIRE))return 1;
    GLint draw=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    if((GLuint)draw!=c->framebuffer)return 1;
    if(!c->error)c->error=GL_INVALID_OPERATION;return 0;
}
static struct DGSharedSurface *DGSurfaceRetainTyped(const uint64_t namespace[2],uint64_t key,uint64_t incarnation,int width,int height,unsigned format)
{
    struct DGSharedSurface *s=NULL;
    unsigned element=VIMGLPixelBytes(format);if(!element)return NULL;
    pthread_mutex_lock(&dg_surface_lock);
    if(dg_surface_leases>=DG_SURFACE_LEASE_LIMIT) goto out;
    for(s=dg_surfaces;s;s=s->next)
        if(s->resource==key && s->incarnation==incarnation &&
           !memcmp(s->namespace,namespace,sizeof(s->namespace))) break;
    if(s) {
        if(s->width!=width || s->height!=height || s->format!=format) { s=NULL;goto out; }
        ++s->refs;++dg_surface_leases;goto out;
    }
    /* Bounded, overflow-checked BGRA8 allocation, including actual padding. */
    size_t row=((size_t)width*element+63)&~(size_t)63,bytes=row*(size_t)height;
    if(dg_surface_count>=DG_SURFACE_LIMIT || bytes>DG_MAX_BYTES ||
       bytes>DG_SURFACE_BYTE_LIMIT-dg_surface_bytes) goto out;
    CFMutableDictionaryRef properties=CFDictionaryCreateMutable(kCFAllocatorDefault,0,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!properties) goto out;
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int64_t values[]={width,height,element,(int64_t)row,(int64_t)bytes,DGSurfaceNativeFormat(format)};
    int good=1;
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i) {
        CFNumberRef value=CFNumberCreate(kCFAllocatorDefault,kCFNumberSInt64Type,&values[i]);
        if(!value) { good=0;break; }
        CFDictionarySetValue(properties,keys[i],value);CFRelease(value);
    }
    IOSurfaceRef surface=good ? IOSurfaceCreate(properties) : NULL;CFRelease(properties);
    if(!surface) goto out;
    bytes=IOSurfaceGetAllocSize(surface);
    if(bytes>DG_MAX_BYTES || bytes>DG_SURFACE_BYTE_LIMIT-dg_surface_bytes ||
       IOSurfaceLock(surface,0,NULL)!=kIOReturnSuccess) { CFRelease(surface);goto out; }
    /* Initialize once, never reupload on texture bind. No stale allocation
     * data is exposed if a consumer arrives before the first producer draw. */
    void *address=IOSurfaceGetBaseAddress(surface);
    if(address) memset(address,0,bytes);
    kern_return_t unlocked=IOSurfaceUnlock(surface,0,NULL);
    if(!address || unlocked!=kIOReturnSuccess) { CFRelease(surface);goto out; }
    s=calloc(1,sizeof(*s));if(!s) { CFRelease(surface);goto out; }
    if(pthread_mutex_init(&s->access,NULL)) { CFRelease(surface);free(s);s=NULL;goto out; }
    memcpy(s->namespace,namespace,sizeof(s->namespace));
    s->resource=key;s->incarnation=incarnation;s->width=width;s->height=height;
    s->bytes=bytes;s->surface=surface;s->format=format;s->refs=1;s->next=dg_surfaces;dg_surfaces=s;
    dg_surface_bytes+=bytes;++dg_surface_count;++dg_surface_leases;
out:
    pthread_mutex_unlock(&dg_surface_lock);return s;
}
static struct DGSharedSurface *DGSurfaceRetain(const uint64_t namespace[2],uint64_t key,uint64_t incarnation,int width,int height)
{return DGSurfaceRetainTyped(namespace,key,incarnation,width,height,VIM_GL_PIXEL_BGRA8);}
static void DGSurfacePut(struct DGSharedSurface *s)
{
    pthread_mutex_lock(&dg_surface_lock);
    --dg_surface_leases;
    if(!--s->refs) {
        struct DGSharedSurface **link=&dg_surfaces;
        while(*link!=s) link=&(*link)->next;
        *link=s->next;dg_surface_bytes-=s->bytes;--dg_surface_count;
        DGSurfaceSnapshotDestroy(s->published_version);
        pthread_mutex_destroy(&s->access);CFRelease(s->surface);free(s);
    }
    pthread_mutex_unlock(&dg_surface_lock);
}
/* A CPU-service hold may retain an existing producer allocation, never create
 * one. Otherwise a typo/stale incarnation silently creates a blank resource
 * and masks a broken ownership handoff. Pool lock serializes lookup vs release. */
static struct DGSharedSurface *DGSurfaceLookupRetain(const uint64_t namespace[2],
    uint64_t key,uint64_t incarnation,int width,int height)
{
    struct DGSharedSurface *found=NULL;
    pthread_mutex_lock(&dg_surface_lock);
    if(dg_surface_leases<DG_SURFACE_LEASE_LIMIT)
        for(struct DGSharedSurface *s=dg_surfaces;s;s=s->next)
            if(s->resource==key && s->incarnation==incarnation &&
               !memcmp(s->namespace,namespace,sizeof(s->namespace))) {
                if(s->width==width && s->height==height &&
                   !__atomic_load_n(&s->retired,__ATOMIC_ACQUIRE)) {
                    ++s->refs;++dg_surface_leases;found=s;
                }
                break;
            }
    pthread_mutex_unlock(&dg_surface_lock);return found;
}
static struct DGSurfaceLease *DGSurfaceFind(struct DGHostContext *c,uint64_t name)
{
    for(struct DGSurfaceLease *p=c->surface_leases;p;p=p->next) if(p->name==name) return p;
    return NULL;
}
static int DGSurfaceRelease(struct DGHostContext *c,uint64_t name)
{
    struct DGSurfaceLease **link=&c->surface_leases;
    while(*link && (*link)->name!=name) link=&(*link)->next;
    if(!*link) return 0;
    struct DGSurfaceLease *p=*link;*link=p->next;
    if(p->framebuffer) glDeleteFramebuffersEXT(1,&p->framebuffer);
    DGSurfaceRegistrationPut(p->registration);
    DGPrivateTextureWatchForget(&p->texture_watch);
    DGSurfacePut(p->resource);free(p);return 1;
}
static void DGDestroySurfaceLeases(struct DGHostContext *c)
{
    if(c->snapshot_read_framebuffer)glDeleteFramebuffersEXT(1,&c->snapshot_read_framebuffer);
    if(c->snapshot_draw_framebuffer)glDeleteFramebuffersEXT(1,&c->snapshot_draw_framebuffer);
    c->snapshot_read_framebuffer=c->snapshot_draw_framebuffer=0;
    DGSnapshotPoolClose(c);
    while(c->surface_leases)DGSurfaceRelease(c,c->surface_leases->name);
    DGCompletionQueuePut(c->completion_queue);c->completion_queue=NULL;
}
static uint32_t DGSurfaceAcquireTyped(struct DGHostContext *c,uint64_t resource,uint64_t incarnation,int width,int height,unsigned format)
{
    unsigned count=0;
    for(struct DGSurfaceLease *p=c->surface_leases;p;p=p->next) {
        ++count;
        if(p->resource->resource==resource && p->resource->incarnation==incarnation)
            return p->resource->width==width && p->resource->height==height && p->resource->format==format ? p->name : 0;
    }
    if(count>=DG_SURFACE_LIMIT || c->next_surface_lease==UINT32_MAX) return 0;
    struct DGSurfaceLease *p=calloc(1,sizeof(*p));if(!p) return 0;
    p->resource=DGSurfaceRetainTyped(c->surface_namespace,resource,incarnation,width,height,format);
    if(!p->resource) { free(p);return 0; }
    GLint texture,read,draw;
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGenTextures(1,&p->texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,p->texture);
    CGLError error=p->texture ? DGPrivateTextureImport(c->cgl,GL_TEXTURE_RECTANGLE_ARB,DGSurfaceGLInternal(format),
        width,height,DGSurfaceGLFormat(format),DGSurfaceGLType(format),p->resource->surface,0) : kCGLBadAlloc;
    int good=0;
    if(!error) {
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glGenFramebuffersEXT(1,&p->framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,p->framebuffer);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,p->texture,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        good=p->framebuffer && glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT &&
            glGetError()==GL_NO_ERROR;
    }
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    if(good){
        p->registration=calloc(1,sizeof(*p->registration));
        if(p->registration){
            p->registration->texture=p->texture;p->registration->refs=1;
            DGPrivateTextureWatchName(&p->registration->texture_watch,&p->registration->texture);
            DGPrivateTextureWatchName(&p->texture_watch,&p->texture);
            __atomic_fetch_add(&dg_surface_registration_stats[0],1,__ATOMIC_RELAXED);
            __atomic_fetch_add(&dg_surface_registration_stats[3],1,__ATOMIC_RELAXED);
        }else good=0;
    }
    if(!good) {
        if(p->framebuffer) glDeleteFramebuffersEXT(1,&p->framebuffer);
        if(p->texture) glDeleteTextures(1,&p->texture);
        DGSurfacePut(p->resource);free(p);return 0;
    }
    p->name=++c->next_surface_lease;p->next=c->surface_leases;c->surface_leases=p;return p->name;
}
static uint32_t DGSurfaceAcquire(struct DGHostContext *c,uint64_t resource,uint64_t incarnation,int width,int height)
{return DGSurfaceAcquireTyped(c,resource,incarnation,width,height,VIM_GL_PIXEL_BGRA8);}
/* Publish a local drawing view into a persistent surface without CPU pixels.
 * Coordinates are GL bottom-up; flip reverses only the destination rectangle.
 * Callers serialize resource access and publish an epoch after all clip pieces.
 * This primitive alone does NOT make guest CPU access coherent. */
static GLenum DGSurfacePublishRectCommands(struct DGHostContext *c,struct DGSurfaceLease *p,
    int x,int y,int width,int height,int destination_x,int destination_y,int flip,int back,int submit)
{
    struct DGSharedSurface *s=p->resource;
    if(!c->drawable || x<0 || y<0 || width<=0 || height<=0 || destination_x<0 || destination_y<0 ||
       width>c->drawable->width || height>c->drawable->height ||
       x>c->drawable->width-width || y>c->drawable->height-height ||
       width>s->width || height>s->height || destination_x>s->width-width || destination_y>s->height-height ||
       (back && !c->double_buffer)) return GL_INVALID_OPERATION;
    if(c->shared_surface==s) {
        if(flip || back || x!=destination_x || y!=destination_y)return GL_INVALID_OPERATION;
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
        if(c->drawable->scope_canonical)
            __atomic_fetch_add(&dg_scope_canonical_stats[2],1,__ATOMIC_RELAXED);
#endif
        // These are already the same pixels. A self-blit is both wasteful
        // and undefined; retain the native producer-flush ordering instead.
        if(submit)glFlush();return glGetError();
    }
    GLint read,draw,source_read;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c->framebuffer);
    glGetIntegerv(GL_READ_BUFFER,&source_read);
    glReadBuffer(back ? GL_COLOR_ATTACHMENT1_EXT : GL_COLOR_ATTACHMENT0_EXT);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,p->framebuffer);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glBlitFramebufferEXT(x,y,x+width,y+height,destination_x,destination_y+(flip?height:0),
        destination_x+width,destination_y+(flip?0:height),
        GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glReadBuffer((GLenum)source_read);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    if(scissor) glEnable(GL_SCISSOR_TEST);if(srgb) glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    /* CGLIOSurface.h: publish to a different context by flushing, not by
     * downloading/reuploading or unconditionally stalling with glFinish. */
    if(submit)glFlush();return glGetError();
}
static GLenum DGSurfacePublishRect(struct DGHostContext *c,struct DGSurfaceLease *p,
    int x,int y,int width,int height,int destination_x,int destination_y,int flip,int back)
{
    return DGSurfacePublishRectCommands(c,p,x,y,width,height,destination_x,destination_y,flip,back,1);
}
static GLenum DGSurfacePublish(struct DGHostContext *c,struct DGSurfaceLease *p,int flip,int back)
{
    if(!c->drawable || c->drawable->width!=p->resource->width || c->drawable->height!=p->resource->height)
        return GL_INVALID_OPERATION;
    return DGSurfacePublishRect(c,p,0,0,p->resource->width,p->resource->height,0,0,flip,back);
}
static GLenum DGSurfaceUpload(struct DGHostContext *c,struct DGSurfaceLease *p,const void *pixels)
{
    (void)c;
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    const GLenum keys[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS,GL_UNPACK_ROW_BYTES_APPLE,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};
    GLint values[7],texture,buffer;
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
    for(unsigned i=0;i<7;++i) { glGetIntegerv(keys[i],&values[i]);glPixelStorei(keys[i],i?0:4); }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,p->texture);
    glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,p->resource->width,p->resource->height,
        DGSurfaceGLFormat(p->resource->format),DGSurfaceGLType(p->resource->format),pixels);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)buffer);
    for(unsigned i=0;i<7;++i) glPixelStorei(keys[i],values[i]);
    DGRestoreColorTransfer(&transfer);
    glFlush();return glGetError();
}
static GLenum DGSurfaceRetire(struct DGSurfaceLease *p)
{
    GLint draw;GLfloat color[4];GLboolean mask[4],scissor=glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGetFloatv(GL_COLOR_CLEAR_VALUE,color);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,p->framebuffer);
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(color[0],color[1],color[2],color[3]);glColorMask(mask[0],mask[1],mask[2],mask[3]);
    if(scissor)glEnable(GL_SCISSOR_TEST);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    glFlush();return glGetError();
}
static GLenum DGSurfaceDrawable(struct DGHostContext *c,struct DGSurfaceLease *p)
{
    struct DGSharedSurface *s=p->resource;
    if(!c->drawable || c->double_buffer || c->drawable->width!=s->width ||
       c->drawable->height!=s->height)return GL_INVALID_OPERATION;
    pthread_mutex_lock(&s->access);
    if(s->retired) { pthread_mutex_unlock(&s->access);return GL_INVALID_OPERATION; }
    if(c->shared_surface==s) { pthread_mutex_unlock(&s->access);return GL_NO_ERROR; }
    // A drawable owns a pool reference independently of a context lease.
    // Otherwise releasing a lease could split one kernel identity into two
    // allocations while the old drawable still writes the first allocation.
    struct DGSharedSurface *held=DGSurfaceRetainTyped(c->surface_namespace,s->resource,s->incarnation,s->width,s->height,s->format);
    if(!held) { pthread_mutex_unlock(&s->access);return GL_OUT_OF_MEMORY; }
    GLint texture=0,read=0,draw=0;
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glFlush();
    int reuse=dg_surface_registration_reuse && p->registration;
    GLuint color=reuse?p->texture:0;
    CGLError imported=kCGLNoError;
    if(!reuse){
        glGenTextures(1,&color);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,color);
        imported=color ? DGPrivateTextureImport(c->cgl,GL_TEXTURE_RECTANGLE_ARB,DGSurfaceGLInternal(s->format),
            s->width,s->height,DGSurfaceGLFormat(s->format),DGSurfaceGLType(s->format),s->surface,0) : kCGLBadAlloc;
        if(!imported)__atomic_fetch_add(&dg_surface_registration_stats[0],1,__ATOMIC_RELAXED);
    }
    GLenum error=GL_NO_ERROR;
    if(!imported) {
        if(!reuse){
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        }
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c->framebuffer);
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,color,0);
        if(glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)error=GL_INVALID_OPERATION;
    } else error=GL_INVALID_OPERATION;
    GLenum native=glGetError();if(native && !error)error=native;
    if(error) {
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c->framebuffer);
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,c->color,0);
        if(color && !reuse)glDeleteTextures(1,&color);DGSurfacePut(held);
    } else {
        GLuint old_color=c->color;IOSurfaceRef old_storage=c->color_storage;
        struct DGSharedSurface *old_shared=c->shared_surface;
        struct DGSurfaceRegistration *old_registration=c->drawable->surface_registration;
        /* Retire BEFORE changing c->shared_surface: the old private target
         * is eligible for the existing bounded, alias-aware cache. The new
         * canonical target is NOT private storage and must never enter it. */
        if(old_registration){DGSurfaceRegistrationPut(old_registration);if(old_storage)CFRelease(old_storage);}
        else {
            DGRetireStorageTexture(c,old_storage,old_color,s->width,s->height,old_shared==NULL);
            if(!old_shared && old_storage)__atomic_fetch_add(&dg_surface_registration_stats[2],1,__ATOMIC_RELAXED);
        }
        c->drawable->surface_registration=reuse?p->registration:NULL;
        if(reuse){++p->registration->refs;
            __atomic_fetch_add(&dg_surface_registration_stats[1],1,__ATOMIC_RELAXED);}
        c->color=c->drawable->color=color;
        DGPrivateTextureWatchName(&c->color_watch,&c->color);
        DGPrivateTextureWatchName(&c->drawable->color_watch,&c->drawable->color);
        c->color_storage=c->drawable->color_storage=(IOSurfaceRef)CFRetain(s->surface);
        c->shared_surface=c->drawable->shared_surface=held;
        if(c->drawable->prepared){
            c->drawable->prepared=0;
            __atomic_fetch_add(&dg_prepared_drawable_stats[1],1,__ATOMIC_RELAXED);
        }
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
        // An ordinary retained-family attachment must not inherit the prior
        // canonical-scope eligibility. Its own typed owner sets this only
        // after matching backing identity and epoch in DGVRAMRenderCanonical.
        c->drawable->scope_canonical=0;
#endif
        if(dg_diagnostics && c->surface_debug_count++<128)
            fprintf(stderr,"direct retained drawing context=%p drawable=%u color=%u storage=%p object=%llu incarnation=%llu epoch=%llu size=%d,%d\n",
                (void *)c,c->drawable->name,color,(void *)s->surface,s->resource,s->incarnation,s->epoch,s->width,s->height);
        if(old_shared)DGSurfacePut(old_shared);
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);
    pthread_mutex_unlock(&s->access);return error;
}
#include "passthrough-opengl-surface-snapshot-host.c"
#include "passthrough-opengl-surface-copy-host.c"
#include "direct-display-sender-host.c"
static int DGSurfaceCommandImpl(struct DGHostContext *c,struct DGMessage *m,struct DGReply *r)
{
    if((m->bytes && m->op!=DG_SURFACE_UPLOAD && m->op!=DG_SURFACE_PUBLISH_REGIONS && m->op!=DG_SURFACE_COPY_REGIONS && m->op!=DG_SURFACE_SEALED_REGIONS) || c->inside_begin || !(c->surface_namespace[0]|c->surface_namespace[1])) {
        r->error=GL_INVALID_OPERATION;return 0;
    }
    if(m->op==DG_SURFACE_ACQUIRE) {
        if(!m->args[0] || !m->args[1] || !m->args[2] || !m->args[3] || m->args[2]>8192 || m->args[3]>8192 ||
           m->args[4]>VIM_GL_PIXEL_RGBA16) {
            r->error=GL_INVALID_VALUE;return 0;
        }
        r->result=DGSurfaceAcquireTyped(c,m->args[0],m->args[1],(int)m->args[2],(int)m->args[3],(unsigned)m->args[4]);
        if(!r->result) r->error=GL_INVALID_OPERATION;
        return 0;
    }
    struct DGSurfaceLease *p=DGSurfaceFind(c,m->args[0]);
    if(!p) { r->error=GL_INVALID_VALUE;return 0; }
    int seal=m->op==DG_SURFACE_SEALED_REGIONS;
    if(seal){
        if(m->args[7]>1){r->error=GL_INVALID_VALUE;return 0;}
        for(unsigned i=8;i<16;++i)if(m->args[i]){r->error=GL_INVALID_VALUE;return 0;}
    }
    /* High precision currently uses coherent completed publication, not the
     * BGRA8-only immutable/deferred/copy/display codecs. Reject such requests
     * before mutating pixels or an epoch, never silently quantize a version. */
    if(p->resource->format && m->op!=DG_SURFACE_RELEASE && m->op!=DG_SURFACE_PUBLISH &&
       m->op!=DG_SURFACE_BIND && m->op!=DG_SURFACE_EPOCH && m->op!=DG_SURFACE_UPLOAD &&
       m->op!=DG_SURFACE_RETIRE && m->op!=DG_SURFACE_DRAWABLE){r->error=GL_INVALID_OPERATION;return 0;}
    if(m->op==DG_SURFACE_COPY_REGIONS || (seal && m->args[7]))r->error=DGSurfaceCopyRegionsMode(c,p,m,seal);
    else if(m->op==DG_SURFACE_PRESENT_VERSION) {
        pthread_mutex_lock(&p->resource->access);
        struct DGSurfaceSnapshot *v=p->resource->published_version;
        if(v && v->epoch==m->args[1] && !p->resource->retired)__sync_add_and_fetch(&v->refs,1);else v=NULL;
        pthread_mutex_unlock(&p->resource->access);
        r->error=v?DGDisplaySubmitVersion(c,v,&r->result):GL_INVALID_VALUE;
        DGSurfaceSnapshotDestroy(v);
    }
    else if(m->op==DG_SURFACE_SEAL_VERSION) {
        struct DGSurfaceSnapshot *version=DGSurfaceSnapshotCreate(c,p,m->args[1]);
        if(!version) { r->error=GL_INVALID_OPERATION;return 0; }
        pthread_mutex_lock(&p->resource->access);
        if(p->resource->retired || p->resource->epoch!=version->epoch)r->error=GL_INVALID_OPERATION;
        else {
            struct DGSurfaceSnapshot *old=p->resource->published_version;
            p->resource->published_version=version;version=old;
        }
        pthread_mutex_unlock(&p->resource->access);
        DGSurfaceSnapshotDestroy(version);
    }
    else if(m->op==DG_SURFACE_VERSION_TEXTURE) {
        pthread_mutex_lock(&p->resource->access);
        struct DGSurfaceSnapshot *v=p->resource->published_version;
        if(v && v->epoch==m->args[1])__sync_add_and_fetch(&v->refs,1);else v=NULL;
        pthread_mutex_unlock(&p->resource->access);
        if(!v || m->args[3]>8192 || m->args[4]>8192)r->error=GL_INVALID_VALUE;
        else r->error=DGSurfaceSnapshotCopyTexture(c,v,(GLenum)m->args[2],(int)m->args[3],(int)m->args[4]);
        DGSurfaceSnapshotDestroy(v);
    }
    else if(m->op==DG_SURFACE_RELEASE) DGSurfaceRelease(c,p->name);
    else if(m->op==DG_SURFACE_DRAWABLE)r->error=DGSurfaceDrawable(c,p);
    else if(m->op==DG_SURFACE_IMPORT_REGION){
        struct DGSharedSurface *s=p->resource;
        uint64_t x=m->args[2],y=m->args[3],w=m->args[4],h=m->args[5];
        uint64_t dx=m->args[7],dy=m->args[8],patch=m->args[9];
        if(!c->drawable || !m->args[1] || !w || !h || m->args[6]>1 || patch>1 ||
           (!patch && (dx || dy || w!=(uint64_t)c->drawable->width || h!=(uint64_t)c->drawable->height)) ||
           w>(uint64_t)c->drawable->width || h>(uint64_t)c->drawable->height ||
           dx>(uint64_t)c->drawable->width-w || dy>(uint64_t)c->drawable->height-h ||
           w>(uint64_t)s->width || h>(uint64_t)s->height || x>(uint64_t)s->width-w || y>(uint64_t)s->height-h ||
           c->shared_surface==s){r->error=GL_INVALID_VALUE;return 0;}
        pthread_mutex_lock(&s->access);
        if(s->retired || s->epoch!=m->args[1])r->error=GL_INVALID_OPERATION;
        else {
            GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
            GLint read=0,draw=0,source_read=0,destination_draw=0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
            GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);glGetIntegerv(GL_READ_BUFFER,&source_read);
            glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c->framebuffer);glGetIntegerv(GL_DRAW_BUFFER,&destination_draw);
            glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
            glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
            glBlitFramebufferEXT((GLint)x,(GLint)y,(GLint)(x+w),(GLint)(y+h),
                (GLint)dx,(GLint)(dy+(m->args[6]?h:0)),(GLint)(dx+w),(GLint)(dy+(m->args[6]?0:h)),GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glReadBuffer(source_read);glDrawBuffer(destination_draw);
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);
            if(scissor)glEnable(GL_SCISSOR_TEST);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB_EXT);
            glFlush();
            r->error=glGetError();
            if(!r->error){c->drawable->alias_dirty=1;++c->drawable->write_serial;}
        }
        pthread_mutex_unlock(&s->access);
    }
    else if(m->op==DG_SURFACE_PUBLISH_REGIONS || seal) {
        struct DGSharedSurface *s=p->resource;
        unsigned count=(unsigned)m->args[3];
        if(!m->args[1] || m->args[2]<=m->args[1] || !m->args[3] || m->args[3]>DG_SURFACE_REGION_LIMIT ||
           m->args[4]>1 || m->args[5]>1 || m->args[6]!=1 ||
           m->bytes!=count*sizeof(struct DGSurfaceRegion) || !c->drawable ||
           (m->args[5] && !c->double_buffer)) { r->error=GL_INVALID_VALUE;return 0; }
        const struct DGSurfaceRegion *regions=(const void *)(m+1);
        /* Validate the complete clip before modifying any pixel. Reject
         * overlaps so clip-list ordering cannot silently change semantics. */
        for(unsigned i=0;i<count;++i) {
            const struct DGSurfaceRegion *a=&regions[i];
            /* Explicit shared drawing is published in place, not copied.
             * Reject transformations before any write/epoch advance. This
             * does NOT relax the by-value import's self-alias rejection.
             */
            if(c->shared_surface==s && (m->args[4] || m->args[5] ||
               a->x!=a->destination_x || a->y!=a->destination_y)) {
                r->error=GL_INVALID_VALUE;return 0;
            }
            if(a->x<0 || a->y<0 || a->width<=0 || a->height<=0 || a->destination_x<0 || a->destination_y<0 ||
               a->width>c->drawable->width || a->height>c->drawable->height ||
               a->x>c->drawable->width-a->width || a->y>c->drawable->height-a->height ||
               a->width>s->width || a->height>s->height ||
               a->destination_x>s->width-a->width || a->destination_y>s->height-a->height) {
                r->error=GL_INVALID_VALUE;return 0;
            }
            for(unsigned j=0;j<i;++j) {
                const struct DGSurfaceRegion *b=&regions[j];
                if(a->destination_x<b->destination_x+b->width && b->destination_x<a->destination_x+a->width &&
                   a->destination_y<b->destination_y+b->height && b->destination_y<a->destination_y+a->height) {
                    r->error=GL_INVALID_VALUE;return 0;
                }
            }
        }
        struct DGSurfaceSnapshot *old=NULL;
        pthread_mutex_lock(&s->access);
        if(s->retired || s->epoch!=m->args[1])r->error=GL_INVALID_OPERATION;
        else {
            for(unsigned i=0;i<count && !r->error;++i) {
                const struct DGSurfaceRegion *a=&regions[i];
                r->error=DGSurfacePublishRectCommands(c,p,a->x,a->y,a->width,a->height,
                    a->destination_x,a->destination_y,(int)m->args[4],(int)m->args[5],0);
            }
            /* One validated clip is ONE publication under this resource
             * lock. Submit all its rectangles together, before advancing the
             * epoch/unlocking. Also submit a partially executed failed clip:
             * retirement must not leave queued writes hidden from consumers.
             * Single-rectangle/publication callers keep their normal Flush.
             * No Finish/fence, CPU ownership or snapshot ordering is removed. */
            if(!seal || r->error){
                glFlush();
                GLenum submitted=glGetError();if(!r->error)r->error=submitted;
            }
            if(!r->error){
                s->epoch=m->args[2];
                if(seal){
                    struct DGSurfaceSnapshot *v=DGSurfaceSnapshotCreateLocked(c,p,s->epoch,1);
                    if(v){old=s->published_version;s->published_version=v;}
                    else {glFlush();r->error=GL_INVALID_OPERATION;}
                }
            }
            if(r->error)__atomic_store_n(&s->retired,1,__ATOMIC_RELEASE); // No coherent version after partial GPU failure.
        }
        pthread_mutex_unlock(&s->access);DGSurfaceSnapshotDestroy(old);
    }
    else if(m->op==DG_SURFACE_PUBLISH) {
        if(m->args[1]>1 || m->args[2]>1) r->error=GL_INVALID_VALUE;
        else {
            pthread_mutex_lock(&p->resource->access);
            if(p->resource->retired || (!m->args[3] && p->resource->epoch)) r->error=GL_INVALID_OPERATION;
            else if(!m->args[3] || m->args[3]>=p->resource->epoch) {
                r->error=DGSurfacePublish(c,p,(int)m->args[1],(int)m->args[2]);
                if(!r->error) p->resource->epoch=m->args[3];
            }
            pthread_mutex_unlock(&p->resource->access);
        }
    } else if(m->op==DG_SURFACE_BIND) {
        GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
        GLenum internal=(GLenum)m->args[1];
        uint64_t width=m->args[2],height=m->args[3];
        if(!width && !height) { width=p->resource->width;height=p->resource->height; }
        if(!texture || !width || !height || width>(uint64_t)p->resource->width || height>(uint64_t)p->resource->height ||
           !DGSurfaceInternalMatches(p->resource->format,internal))
            r->error=GL_INVALID_VALUE;
        else {
            if(dg_diagnostics && c->surface_debug_count++<128)
                fprintf(stderr,"direct retained binding context=%p texture=%d storage=%p object=%llu incarnation=%llu epoch=%llu size=%llu,%llu backing=%d,%d\n",
                    (void *)c,texture,(void *)p->resource->surface,p->resource->resource,p->resource->incarnation,
                    p->resource->epoch,width,height,p->resource->width,p->resource->height);
            if(CGLTexImageIOSurface2D(c->cgl,GL_TEXTURE_RECTANGLE_ARB,internal,(GLsizei)width,
                (GLsizei)height,DGSurfaceGLFormat(p->resource->format),DGSurfaceGLType(p->resource->format),p->resource->surface,0))
                r->error=GL_INVALID_OPERATION;
            else if(!DGRegisterAlias(c,(GLuint)texture,p->resource->surface))r->error=GL_OUT_OF_MEMORY;
        }
    } else if(m->op==DG_SURFACE_EPOCH) {
        pthread_mutex_lock(&p->resource->access);r->result=p->resource->epoch;pthread_mutex_unlock(&p->resource->access);
    } else if(m->op==DG_SURFACE_UPLOAD) {
        struct DGSharedSurface *s=p->resource;
        if(!m->args[1] || m->args[2]!=(uint64_t)s->width || m->args[3]!=(uint64_t)s->height ||
            (uint64_t)s->width*s->height*VIMGLPixelBytes(s->format)!=m->bytes) r->error=GL_INVALID_VALUE;
        else {
            pthread_mutex_lock(&s->access);
            if(s->retired) r->error=GL_INVALID_OPERATION;
            else if(m->args[1]>s->epoch) { r->error=DGSurfaceUpload(c,p,m+1);if(!r->error) s->epoch=m->args[1]; }
            pthread_mutex_unlock(&s->access);
        }
    } else if(m->op==DG_SURFACE_RETIRE) {
        pthread_mutex_lock(&p->resource->access);
        if(!p->resource->retired) { r->error=DGSurfaceRetire(p);if(!r->error)__atomic_store_n(&p->resource->retired,1,__ATOMIC_RELEASE); }
        pthread_mutex_unlock(&p->resource->access);
    } else return -1;
    return 0;
}
static int DGSurfaceCommand(struct DGHostContext *c,struct DGMessage *m,struct DGReply *r)
{
    if(c->inside_begin) { r->error=GL_INVALID_OPERATION;return 0; }
    /* Do not report an earlier application error as a failed private
     * publication, or report a new native failure as a successful one. */
    GLenum prior=glGetError();if(prior && !c->error) c->error=prior;
    int result=DGSurfaceCommandImpl(c,m,r);
    GLenum error=glGetError();if(error && !r->error) r->error=error;
    return result;
}
/* Connection-owned CF references: no GL names/context switching, and no
 * borrowing a live context on another guest thread during glBegin/glEnd. */
struct DGSurfaceHold { uint32_t name;struct DGSharedSurface *surface;struct DGSurfaceSnapshot *version;struct DGSurfaceHold *next; };
static void DGDestroySurfaceHolds(struct DGSurfaceHold **holds)
{
    while(*holds) { struct DGSurfaceHold *p=*holds;*holds=p->next;DGSurfaceSnapshotDestroy(p->version);DGSurfacePut(p->surface);free(p); }
}
static int DGSurfaceHoldCommand(const uint64_t namespace[2],struct DGSurfaceHold **holds,uint32_t *next,
    struct DGMessage *m,struct DGReply *r)
{
    if(m->context || (m->bytes && m->op!=DG_SURFACE_HOLD_READ) || !(namespace[0]|namespace[1])) { r->error=GL_INVALID_VALUE;return 0; }
    if(m->op==DG_SURFACE_PRESENT_READY){
        if(m->flags!=DG_REPLY){r->error=GL_INVALID_VALUE;return 0;}
        r->result=DGDisplaySenderReady(holds);return 0;
    }
    if(m->op==DG_SURFACE_HOLD_PRESENT){
        struct DGSurfaceHold *hold=*holds;
        for(;hold && hold->name!=m->args[0];hold=hold->next){}
        if(m->flags!=DG_REPLY || !hold || !hold->version ||
           !m->args[1] || hold->version->epoch!=m->args[1]){
            r->error=GL_INVALID_VALUE;return 0;
        }
        // The exact sealed version carries its producer completion. This
        // connection has no CGL context and never reaches into the publisher.
        r->error=DGDisplaySubmitSealed(holds,hold->version,&r->result);return 0;
    }
    if(m->op==DG_SURFACE_HOLD_READ) {
        struct DGSurfaceHold *hold=*holds;
        for(;hold && hold->name!=m->args[0];hold=hold->next) {}
        if(!hold || hold->surface->format!=VIM_GL_PIXEL_BGRA8 || m->flags!=(DG_REPLY|DG_OUTPUT_ONLY) || !m->args[1] ||
           !m->args[4] || !m->args[5] || m->args[4]>8192 || m->args[5]>8192 ||
           m->args[4]*m->args[5]*4!=m->bytes) { r->error=GL_INVALID_VALUE;return 0; }
        struct DGSharedSurface *s=hold->surface;
        uint64_t x=m->args[2],y=m->args[3],width=m->args[4],height=m->args[5];
        if(width>(uint64_t)s->width || height>(uint64_t)s->height ||
           x>(uint64_t)s->width-width || y>(uint64_t)s->height-height) { r->error=GL_INVALID_VALUE;return 0; }
        pthread_mutex_lock(&s->access);
        IOSurfaceRef storage=hold->version?hold->version->storage:s->surface;
        if(hold->version ? hold->version->epoch!=m->args[1] : (s->retired || s->epoch!=m->args[1]))r->error=GL_INVALID_OPERATION;
        else if(IOSurfaceLock(storage,kIOSurfaceLockReadOnly,NULL)!=kIOReturnSuccess)r->error=GL_INVALID_OPERATION;
        else {
            /* Publishers flush before committing the epoch. The synchronized
             * CPU lock waits for those GPU writes; never use AvoidSync here.
             * This route requires no current or borrowed CGL context. */
            const unsigned char *base=IOSurfaceGetBaseAddress(storage);
            size_t pitch=IOSurfaceGetBytesPerRow(storage),allocation=IOSurfaceGetAllocSize(storage);
            if(!base || pitch<(size_t)s->width*4 || pitch>DG_MAX_BYTES ||
               (y+height-1)*pitch+(x+width)*4>allocation)r->error=GL_INVALID_OPERATION;
            else for(size_t row=0;row<height;++row)
                memcpy((unsigned char *)(m+1)+row*width*4,base+(y+row)*pitch+x*4,(size_t)width*4);
            if(IOSurfaceUnlock(storage,kIOSurfaceLockReadOnly,NULL)!=kIOReturnSuccess)r->error=GL_INVALID_OPERATION;
        }
        pthread_mutex_unlock(&s->access);return 0;
    }
    if(m->op==DG_SURFACE_UNHOLD) {
        struct DGSurfaceHold **link=holds;
        while(*link && (*link)->name!=m->args[0]) link=&(*link)->next;
        if(!*link) { r->error=GL_INVALID_VALUE;return 0; }
        struct DGSurfaceHold *p=*link;*link=p->next;DGSurfaceSnapshotDestroy(p->version);DGSurfacePut(p->surface);free(p);return 0;
    }
    unsigned count=0;for(struct DGSurfaceHold *p=*holds;p;p=p->next) ++count;
    if(!m->args[0] || !m->args[1] || !m->args[2] || !m->args[3] ||
       m->args[2]>8192 || m->args[3]>8192 || count>=256 || *next==UINT32_MAX) {
        r->error=GL_INVALID_VALUE;return 0;
    }
    struct DGSurfaceHold *p=calloc(1,sizeof(*p));
    if(!p) { r->error=GL_OUT_OF_MEMORY;return 0; }
    p->surface=DGSurfaceLookupRetain(namespace,m->args[0],m->args[1],(int)m->args[2],(int)m->args[3]);
    if(!p->surface) { free(p);r->error=GL_INVALID_OPERATION;return 0; }
    if(m->args[4]) {
        pthread_mutex_lock(&p->surface->access);
        struct DGSurfaceSnapshot *v=p->surface->published_version;
        if(v && v->epoch==m->args[4]) { __sync_add_and_fetch(&v->refs,1);p->version=v; }
        pthread_mutex_unlock(&p->surface->access);
        if(!p->version) { DGSurfacePut(p->surface);free(p);r->error=GL_INVALID_OPERATION;return 0; }
    }
    p->name=++*next;p->next=*holds;*holds=p;r->result=p->name;return 0;
}
