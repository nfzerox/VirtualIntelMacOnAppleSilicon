/* Included by passthrough-opengl-host.c. Private, Intel-qualified research path.
 * An aligned complete backing stays shared with QEMU. Arbitrary drawing
 * views/clip pieces move with GPU blits, never CPU pixel IPC/readback.
 * Guest CPU exclusion remains held until our GPU transaction completes. */
#include "../vendor/qemu/hw/display/vim-vram-memory.h"
#include <sys/stat.h>
#include <fcntl.h>
struct DGVRAMDrawableBacking {
    uint64_t object,incarnation,offset,length,epoch;
    uint32_t width,height,pitch,kind;
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    uint32_t native_width;
#endif
    GLuint texture,framebuffer;
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
    IOSurfaceRef experiment_storage;
    int experiment_cpu_locked;
#endif
    struct VIMVRAMMemoryMapping alias;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    struct DGScatterPlan *scatter_plan;
#endif
};
static struct VIMVRAMMemoryMapping dg_vram;
static struct VIMVRAMMemoryMapping dg_ram;
static uint64_t dg_vram_stats[4];
static pthread_once_t dg_vram_once=PTHREAD_ONCE_INIT;
static void DGVRAMInitialize(void)
{
    const char *path=getenv("VIM_OPENGL_VRAM_ENDPOINT");if(!path || !*path)return;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return;
    struct stat st;unsigned char bytes[4096];ssize_t used=0;
    int good=!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==geteuid() &&
        !(st.st_mode&0077) && st.st_size>0 && st.st_size<(off_t)sizeof(bytes);
    while(good && used<st.st_size){
        ssize_t n=read(fd,bytes+used,(size_t)(st.st_size-used));
        if(n<0 && errno==EINTR)continue;if(n<=0){good=0;break;}used+=n;
    }
    close(fd);if(!good)return;
    CFDataRef data=CFDataCreate(NULL,bytes,used);
    CFPropertyListRef plist=data?CFPropertyListCreateWithData(NULL,data,kCFPropertyListImmutable,NULL,NULL):NULL;
    if(data)CFRelease(data);
    if(plist && CFGetTypeID(plist)==CFDictionaryGetTypeID()){
        CFTypeRef version=CFDictionaryGetValue(plist,CFSTR("Version"));
        CFTypeRef renderer=CFDictionaryGetValue(plist,CFSTR("RendererPID"));
        CFTypeRef producer=CFDictionaryGetValue(plist,CFSTR("ProducerPID"));
        CFTypeRef service=CFDictionaryGetValue(plist,CFSTR("Service"));
        int64_t v=0,r=0,p=0;char name[BOOTSTRAP_MAX_NAME_LEN];
        if(version && CFGetTypeID(version)==CFNumberGetTypeID() &&
           renderer && CFGetTypeID(renderer)==CFNumberGetTypeID() &&
           producer && CFGetTypeID(producer)==CFNumberGetTypeID() &&
           service && CFGetTypeID(service)==CFStringGetTypeID() &&
           CFNumberGetValue(version,kCFNumberSInt64Type,&v) && v==1 &&
           CFNumberGetValue(renderer,kCFNumberSInt64Type,&r) && r==getpid() &&
           CFNumberGetValue(producer,kCFNumberSInt64Type,&p) && p>0 && p<=INT32_MAX &&
           CFStringGetCString(service,name,sizeof(name),kCFStringEncodingUTF8)){
            kern_return_t kr=VIMVRAMMemoryImport(name,(pid_t)p,&dg_vram,2000);
            fprintf(stderr,"Shared anonymous VRAM import status=%x bytes=%llu\n",kr,(unsigned long long)dg_vram.bytes);
            char ramName[BOOTSTRAP_MAX_NAME_LEN];
            if(snprintf(ramName,sizeof(ramName),"%s.ram",name)<(int)sizeof(ramName)){
                kr=VIMVRAMMemoryImport(ramName,(pid_t)p,&dg_ram,2000);
                fprintf(stderr,"Shared anonymous RAM import status=%x bytes=%llu regions=%u\n",kr,
                    (unsigned long long)dg_ram.bytes,dg_ram.region_count);
            }
        }
    }
    if(plist)CFRelease(plist);
}
static int DGVRAMAvailable(void)
{pthread_once(&dg_vram_once,DGVRAMInitialize);return dg_vram.address!=0 || dg_ram.address!=0;}
static int DGVRAMWait(void);
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
static void DGVRAMExperimentDrawCopy(struct DGHostContext *,const struct DGMessage *,unsigned);
#endif
static volatile int dg_vram_backing_reuse=1; // Process-local diagnostic A/B; default on.
/* Owned-renderer same-boot A/B only; never changes completion/ownership. */
static volatile int dg_vram_private_copy_state=1;
#define DG_VRAM_SPARE_BYTES (UINT64_C(32)*1024*1024)
static void DGVRAMDeleteBacking(struct DGVRAMDrawableBacking *b)
{
    if(!b)return;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    DGScatterPlanPut(b->scatter_plan);
#endif
    if(b->alias.address)DGVRAMWait();
    glDeleteFramebuffersEXT(1,&b->framebuffer);glDeleteTextures(1,&b->texture);
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
    if(b->experiment_storage){
        if(b->experiment_cpu_locked)IOSurfaceUnlock(b->experiment_storage,0,NULL);
        CFRelease(b->experiment_storage);
    }
#endif
    VIMVRAMMemoryUnmap(&b->alias);free(b);
}
static void DGVRAMDelete(struct DGHostContext *c)
{
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    DGBackingTicketPut(c->scatter_predecessor);c->scatter_predecessor=NULL;
#endif
    DGVRAMDeleteBacking(c->vram);c->vram=NULL;
    for(unsigned i=0;i<3;++i){DGVRAMDeleteBacking(c->vram_spares[i]);c->vram_spares[i]=NULL;}
    c->vram_spare_bytes=0;
}
static void DGVRAMRememberBacking(struct DGHostContext *c,struct DGVRAMDrawableBacking *b)
{
    if(!b)return;
    if(!dg_vram_backing_reuse || b->length>DG_VRAM_SPARE_BYTES){DGVRAMDeleteBacking(b);return;}
    // Only the owning CGL worker changes this bounded LRU. Every transfer
    // retains its original CPU exclusion/completion fence. Remembering a
    // registration does not authorize an old epoch or retain a GPU job.
    while(c->vram_spares[2] || c->vram_spare_bytes>DG_VRAM_SPARE_BYTES-b->length){
        struct DGVRAMDrawableBacking *old=c->vram_spares[0];
        if(!old){DGVRAMDeleteBacking(b);return;}
        c->vram_spare_bytes-=old->length;
        memmove(c->vram_spares,c->vram_spares+1,2*sizeof(c->vram_spares[0]));c->vram_spares[2]=NULL;
        DGVRAMDeleteBacking(old);
    }
    for(unsigned i=0;i<3;++i)if(!c->vram_spares[i]){
        c->vram_spares[i]=b;c->vram_spare_bytes+=b->length;return;
    }
    DGVRAMDeleteBacking(b);
}
static int DGVRAMBackingMatches(const struct DGVRAMDrawableBacking *b,const struct DGMessage *m)
{
    return b && b->offset==m->args[0] && b->length==m->args[1] &&
        b->width==m->args[2] && b->height==m->args[3] && b->pitch==m->args[4] &&
        b->kind==m->args[13] && b->object==m->args[10] && b->incarnation==m->args[11];
}
static void DGVRAMForgetObject(struct DGHostContext *c,uint64_t object)
{
    for(unsigned i=0;i<3;){
        struct DGVRAMDrawableBacking *b=c->vram_spares[i];
        if(!b || b->object!=object){++i;continue;}
        c->vram_spare_bytes-=b->length;
        memmove(c->vram_spares+i,c->vram_spares+i+1,(2-i)*sizeof(c->vram_spares[0]));
        c->vram_spares[2]=NULL;DGVRAMDeleteBacking(b);
    }
}
static int DGVRAMWaitForFence(GLsync fence)
{
    /* Both callers created/flushed this fence on the CURRENT queue (the
     * producer or the private textured-copy context). Finish can therefore
     * block on its real submitted work without another context/fence. This
     * changes waiting CPU cost, not the textured transfer/CPU visibility or
     * ownership contract. A zero-time check MUST still prove completion. */
    GLenum status=glClientWaitSync(fence,0,0);
    if(status==GL_TIMEOUT_EXPIRED && (dg_completion_wait_policy&2)){
        glFinish();status=glClientWaitSync(fence,0,0);
    }
    while(status==GL_TIMEOUT_EXPIRED)status=glClientWaitSync(fence,0,20000000);
    return status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED;
}
static int DGVRAMWait(void)
{
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
    __atomic_fetch_add(&dg_backing_sync_waits,1,__ATOMIC_RELAXED);
#endif
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
    if(!vim_vram_completion_fence){glFinish();return 1;}
    if(vim_vram_completion_fence==2){
        GLuint fence=0;glGenFencesAPPLE(1,&fence);if(!fence)return 0;
        glSetFenceAPPLE(fence);glFinishFenceAPPLE(fence);glDeleteFencesAPPLE(1,&fence);return 1;
    }
#endif
    GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!fence)return 0;
    glFlush();int good=DGVRAMWaitForFence(fence);
    glDeleteSync(fence);return good;
}
/* Zero is a negotiated unsupported layout, not permission to create a copy
 * and silently claim shared backing. This path is currently Intel compatibility GL. */
static int DGVRAMBacking(struct DGHostContext *c,const struct DGMessage *m,int creating)
{
    // No registration eviction or alias-page replacement during a borrowed
    // render transaction. It must end with real completion first.
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    if(c->vram_render_drawable)return 0;
#endif
    if(!DGVRAMAvailable() || c->profile!=kCGLOGLPVersion_Legacy)return 0;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    if(!renderer || !strstr(renderer,"Intel"))return 0;
    uint64_t offset=m->args[0],length=m->args[1],width=m->args[2],height=m->args[3],pitch=m->args[4],kind=m->args[13];
    if(!VIMGLSharedRowsSupported(width,height,pitch) || kind>1 || !length || length>DG_MAX_BYTES ||
       (kind?(offset || pitch*height>length || !dg_ram.address):
           ((offset&16383) || pitch*height>length || offset>dg_vram.bytes || length>dg_vram.bytes-offset || !dg_vram.address)) ||
       !m->args[10] || !m->args[11] || !m->args[12])return 0;
    struct DGVRAMDrawableBacking *b=c->vram;
    // A newer observed incarnation retires the old registration. The cache
    // must not resurrect an older generation merely because its size fits.
    if(b && b->object==m->args[10] && b->incarnation>m->args[11])return 0;
    for(unsigned i=0;i<3;++i)if(c->vram_spares[i] &&
       c->vram_spares[i]->object==m->args[10] && c->vram_spares[i]->incarnation>m->args[11])return 0;
    if(DGVRAMBackingMatches(b,m)
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
       && (!b->scatter_plan || creating==3)
#endif
       )return m->args[12]>=b->epoch;
    if(dg_vram_backing_reuse)for(unsigned i=0;i<3;++i){
        struct DGVRAMDrawableBacking *cached=c->vram_spares[i];
        if(!DGVRAMBackingMatches(cached,m))continue;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
        if(cached->scatter_plan && creating!=3)continue;
#endif
        // Reject stale requests before changing the active registration.
        if(m->args[12]<cached->epoch)return 0;
        c->vram_spare_bytes-=cached->length;
        memmove(c->vram_spares+i,c->vram_spares+i+1,(2-i)*sizeof(c->vram_spares[0]));
        c->vram_spares[2]=NULL;c->vram=cached;
        DGVRAMRememberBacking(c,b);++c->vram_backing_hits;return 1;
    }
    if(!creating)return 0;
    if(kind && !m->args[14])return 2; // metadata-only miss: request pinned pages once
    if(kind && m->args[14]*4096!=length)return 0;
    struct DGVRAMDrawableBacking *previous=b;
    b=calloc(1,sizeof(*b));if(!b)return 0;
    b->offset=offset;b->length=length;b->width=width;b->height=height;b->pitch=pitch;
    // Explicit render transactions need the REAL logical width so Clear
    // cannot touch padding. Ordinary snapshot/copy registrations retain the
    // previously qualified pitched-width layout, unchanged.
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    b->native_width=creating==2?(uint32_t)width:(uint32_t)(pitch/4);
#endif
    b->object=m->args[10];b->incarnation=m->args[11];b->kind=kind;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    if(creating==3 && kind){
        b->scatter_plan=DGScatterPlanCreate(&dg_ram,(const void *)(m+1),(unsigned)m->args[14],
            (uint32_t)width,(uint32_t)height,(uint32_t)pitch);
        if(b->scatter_plan){
            b->native_width=(uint32_t)width;c->vram=b;DGVRAMForgetObject(c,b->object);
            if(previous && previous->object==b->object)DGVRAMDeleteBacking(previous);
            else DGVRAMRememberBacking(c,previous);
            ++c->vram_backing_creates;__atomic_fetch_add(&dg_scatter_stats[0],1,__ATOMIC_RELAXED);return 1;
        }
        // An unsupported SG shape retains the original coherent native
        // registration path; never silently accept a copied pixel substitute.
        creating=2;b->native_width=(uint32_t)width;
    }
#endif
    if(kind && VIMSharedRAMMapPages(&dg_ram,(const void *)(m+1),(unsigned)m->args[14],&b->alias)){
        free(b);return 0;
    }
    GLint texture,buffer,read,draw;
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    const GLenum keys[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,
        GL_UNPACK_ROW_BYTES_APPLE,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST,GL_UNPACK_CLIENT_STORAGE_APPLE};
    GLint values[8];for(unsigned i=0;i<8;++i){glGetIntegerv(keys[i],&values[i]);glPixelStorei(keys[i],i==0?4:i==7?1:0);}
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    if(creating==2)glPixelStorei(GL_UNPACK_ROW_LENGTH,(GLint)(pitch/4));
#endif
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glGenTextures(1,&b->texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,b->texture);
    void *pixels=(void *)(uintptr_t)((kind?b->alias.address:dg_vram.address)+offset);
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
    if(vim_vram_iosurface){
        CFMutableDictionaryRef props=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        const CFStringRef names[]={CFSTR("IOSurfaceAddress"),kIOSurfaceWidth,kIOSurfaceHeight,
            kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
        int64_t numbers[]={(int64_t)(uintptr_t)pixels,(int64_t)(pitch/4),(int64_t)height,4,(int64_t)pitch,(int64_t)length,0x42475241};
        for(unsigned i=0;i<7;++i){CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,numbers+i);CFDictionarySetValue(props,names[i],n);CFRelease(n);}
        b->experiment_storage=IOSurfaceCreate(props);CFRelease(props);
        if(!b->experiment_storage || IOSurfaceGetBaseAddress(b->experiment_storage)!=pixels){
            fprintf(stderr,"Existing-memory IOSurface unavailable or not the supplied address\n");abort();
        }
        assert(!CGLTexImageIOSurface2D(c->cgl,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,(GLsizei)(pitch/4),(GLsizei)height,
            GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,b->experiment_storage,0));
    }else
#endif
    {
    glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_ARB,(GLsizei)(pitch*height),pixels);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_STORAGE_HINT_APPLE,GL_STORAGE_SHARED_APPLE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLsizei texture_width=(GLsizei)(pitch/4);
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    texture_width=(GLsizei)b->native_width;
#endif
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,texture_width,(GLsizei)height,0,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,pixels);
    }
    glGenFramebuffersEXT(1,&b->framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,b->framebuffer);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,b->texture,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    int good=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT && !glGetError();
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)buffer);
    for(unsigned i=0;i<8;++i)glPixelStorei(keys[i],values[i]);DGRestoreColorTransfer(&transfer);
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
    if(b->experiment_storage){
        if(IOSurfaceLock(b->experiment_storage,0,NULL))good=0;
        else b->experiment_cpu_locked=1;
    }
#endif
    if(!good){DGVRAMDeleteBacking(b);return 0;}
    c->vram=b;DGVRAMForgetObject(c,b->object);
    if(previous && previous->object==b->object)DGVRAMDeleteBacking(previous);
    else DGVRAMRememberBacking(c,previous);
    ++c->vram_backing_creates;return 1;
}
#include "direct-vram-copy-context-host.c"
#ifdef VIM_VRAM_COPYTEX_EXPERIMENT
#include "direct-vram-copytex-host.c"
#endif
#ifdef VIM_VRAM_STAGED_RESTORE_EXPERIMENT
#include "direct-vram-staged-restore-host.c"
#endif
// Diagnostic comparison only; the coherent publication path is the default.
static volatile int dg_vram_draw_publish=1;
static int DGVRAMTransfer(struct DGHostContext *c,const struct DGMessage *m,struct DGReply *reply)
{
    int publish=m->op==DG_VRAM_PUBLISH_DRAWABLE,patch=m->op==DG_VRAM_PATCH_DRAWABLE;
    int regions=publish || patch;
    if(!c || !c->drawable || c->inside_begin || !m->args[7] || !m->args[8] || m->args[9]>1 ||
       m->args[7]!=(uint64_t)c->drawable->width || m->args[8]!=(uint64_t)c->drawable->height ||
       m->args[7]>m->args[2] || m->args[8]>m->args[3] ||
       m->args[5]>m->args[2]-m->args[7] || m->args[6]>m->args[3]-m->args[8] ||
       m->args[13]>1 || m->args[14]>16384 ||
       (regions?(m->args[14] || (patch && !m->bytes) || m->bytes%sizeof(struct DGVRAMRect) || m->bytes>510*sizeof(struct DGVRAMRect)):
           (m->bytes!=m->args[14]*8 || (m->args[14] && !m->args[13]))))return -1;
    const struct DGVRAMRect *rects=(const void *)(m+1);unsigned count=regions?m->bytes/sizeof(*rects):0;
    for(unsigned i=0;i<count;++i)if(rects[i].x<0 || rects[i].y<0 || rects[i].width<=0 || rects[i].height<=0 ||
        (uint64_t)rects[i].x+rects[i].width>m->args[7] || (uint64_t)rects[i].y+rects[i].height>m->args[8])return -1;
    // A patch must refer to the already imported exact backing. A miss may
    // fall back before any GPU work, never recreate/reimport the whole view
    // over earlier unflushed drawing outside these completed CPU rectangles.
    int available=DGVRAMBacking(c,m,!regions);
    if(available!=1){reply->result=available;return 0;}
    if(patch){GLenum prior=glGetError();if(prior && !c->error)c->error=prior;}
    struct DGVRAMDrawableBacking *b=c->vram;
#ifdef VIM_VRAM_STAGED_RESTORE_EXPERIMENT
    int staged=DGVRAMStagedRestore(c,m);
    if(staged){
        if(staged<0){reply->error=GL_INVALID_OPERATION;return 0;}
        b->epoch=m->args[12];reply->result=1;
        c->drawable->alias_dirty=1;++c->drawable->write_serial;
        __atomic_fetch_add(&dg_vram_stats[0],1,__ATOMIC_RELAXED);
        __atomic_fetch_add(&dg_vram_stats[2],m->args[7]*m->args[8]*4,__ATOMIC_RELAXED);
        return 0;
    }
#endif
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
    if(vim_vram_cache_flush)sys_dcache_flush((void *)(uintptr_t)((b->kind?b->alias.address:dg_vram.address)+b->offset),(size_t)b->length);
    if(b->experiment_storage){
        if(!b->experiment_cpu_locked || IOSurfaceUnlock(b->experiment_storage,0,NULL))return -1;
        b->experiment_cpu_locked=0;
    }
#endif
    /* Textured publication owns a PRIVATE context. Changing the producer's
     * read/draw FBOs, buffers, scissor and sRGB state beforehand serves no
     * purpose: the helper samples c->color directly and restores the current
     * context itself. Keep the existing state transaction for blit/import and
     * laboratory alternatives that actually use the producer's framebuffer. */
    int private_publish=publish && dg_vram_draw_publish && dg_vram_private_copy_state;
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
    if(vim_vram_copy_draw)private_publish=0;
#endif
#ifdef VIM_VRAM_COPYTEX_EXPERIMENT
    if(dg_vram_copytex && !m->args[9])private_publish=0;
#endif
    int good;
    if(private_publish){
        int completed=0,copied=DGVRAMCopyDrawable(c,m,count,&completed);
        good=completed || DGVRAMWait();
        good=glGetError()==GL_NO_ERROR && good && copied;
    }else{
    GLint read,draw,sourceRead,destinationDraw;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,publish?c->framebuffer:b->framebuffer);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,publish?b->framebuffer:c->framebuffer);
    glGetIntegerv(GL_READ_BUFFER,&sourceRead);glGetIntegerv(GL_DRAW_BUFFER,&destinationDraw);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    DGSetBlitScissor(c,GL_FALSE);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    GLint x=(GLint)m->args[5],y=(GLint)m->args[6],w=(GLint)m->args[7],h=(GLint)m->args[8],flip=(GLint)m->args[9];
    int copied=1,copy_completed=0;
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
    if(vim_vram_copy_draw && !patch)DGVRAMExperimentDrawCopy(c,m,count);
    else
#endif
#ifdef VIM_VRAM_COPYTEX_EXPERIMENT
    if(publish && dg_vram_copytex && !flip)copied=DGVRAMCopyTexPublish(c,m,count);
    else
#endif
    if(publish && dg_vram_draw_publish)copied=DGVRAMCopyDrawable(c,m,count,&copy_completed);
    else if(patch)for(unsigned i=0;i<count;++i){
        const struct DGVRAMRect *r=rects+i;
        glBlitFramebufferEXT(x+r->x,y+r->y,x+r->x+r->width,y+r->y+r->height,
            r->x,flip?h-r->y:r->y,r->x+r->width,flip?h-r->y-r->height:r->y+r->height,
            GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    else if(!publish)glBlitFramebufferEXT(x,y,x+w,y+h,0,flip?h:0,w,flip?0:h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    else for(unsigned i=0;i<count;++i){
        const struct DGVRAMRect *r=&rects[i];
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
        if(flip && vim_vram_flip_destination){
            glBlitFramebufferEXT(r->x,h-r->y-r->height,r->x+r->width,h-r->y,
                x+r->x,y+r->y+r->height,x+r->x+r->width,y+r->y,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            continue;
        }
#endif
        glBlitFramebufferEXT(r->x,flip?h-r->y:r->y,r->x+r->width,flip?h-r->y-r->height:r->y+r->height,
            x+r->x,y+r->y,x+r->x+r->width,y+r->y+r->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    good=copy_completed || DGVRAMWait();good=glGetError()==GL_NO_ERROR && good && copied;
    glReadBuffer((GLenum)sourceRead);glDrawBuffer((GLenum)destinationDraw);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    if(scissor)DGSetBlitScissor(c,GL_TRUE);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    }
#ifdef VIM_VRAM_COMPLETION_EXPERIMENT
    if(vim_vram_cache_flush)sys_dcache_flush((void *)(uintptr_t)((b->kind?b->alias.address:dg_vram.address)+b->offset),(size_t)b->length);
    if(b->experiment_storage){
        glFlush();good=good && !IOSurfaceLock(b->experiment_storage,0,NULL);
        if(good)b->experiment_cpu_locked=1;
    }
#endif
    if(good){
        b->epoch=m->args[12];reply->result=1;
        if(!publish){c->drawable->alias_dirty=1;++c->drawable->write_serial;}
        uint64_t bytes=m->args[7]*m->args[8]*4;
        if(regions){bytes=0;for(unsigned i=0;i<count;++i)bytes+=(uint64_t)rects[i].width*rects[i].height*4;}
        __atomic_fetch_add(&dg_vram_stats[publish?1:0],1,__ATOMIC_RELAXED);
        __atomic_fetch_add(&dg_vram_stats[publish?3:2],bytes,__ATOMIC_RELAXED);
    }
    else reply->error=GL_INVALID_OPERATION;
    return 0;
}
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
#include "direct-vram-render-target-host.c"
#endif
