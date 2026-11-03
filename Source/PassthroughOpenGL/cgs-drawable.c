/* Research CGS attachment/publication glue. Native registration, orientation
 * and private sharing follow accelerated Mavericks measurements. The older
 * mode-1 software backing remains a labelled fallback, not the acceleration
 * oracle. Retained GPU resources are gated separately while cropped primary
 * views and the remaining interoperability contracts are qualified. */
#include <CoreGraphics/CoreGraphics.h>
#include <math.h>
#include <sys/sysctl.h>
#include <uuid/uuid.h>
#include "guest-resource-namespace.h"
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
#include "../vendor/source/VMsvga2/vminclude/vim_gl_surface_service.h"
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "passthrough-opengl-surfaces.h"
#include "passthrough-opengl-read-mask.h"
#include "gli-cpu-copy.h"
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#include "direct-client-profile.h"
#endif
extern int CGSGetSurfaceBounds(int,int,int,CGRect *);
/* 13F1911 i386 at 0x5ff97 writes MOVSS through the fourth argument;
 * CGFloat is float there, double in x86_64. Never use double * for both. */
extern int CGSGetSurfaceResolution(int,int,int,CGFloat *,CGSize *);
extern int CGSNewRegionWithRect(const CGRect *,void **);
extern int CGSReleaseRegion(void *);
extern int CGSNewWindow(int,int,float,float,void *,int *);
extern int CGSReleaseWindow(int,int);
extern int CGSSetWindowDepth(int,int,int);
extern int CGSBindSurface(int,int,int,int,int,int);
extern int CGSLockWindowBits(int,int,CGRect *,int *,void **,int *);
extern int CGSUnlockWindowBits(int,int,void *);
extern int CGSFlushSurface(int,int,int,void *);
/* The fifth gliInitializeLibrary argument is CGL's bind callback, not a
 * generic unlock hook. Its signature and CGSBindSurface(mode=2) call are
 * measured in native Mavericks; use the provided callback, never its address.
 */
typedef IOReturn (*VIMNativeBindSurface)(int,int,uint32_t *,uint32_t,io_service_t);
static VIMNativeBindSurface vim_native_bind_surface;
static io_service_t vim_native_accelerator;
#include "gli-accelerator-service.h"
struct VIMPrimaryReadMask {
    struct DGReadMaskRect rects[VIM_SURFACE_CLIP_RECTS];
    struct DGReadMaskInterval intervals[VIM_SURFACE_CLIP_RECTS];
    struct DGReadSpan spans[VIM_GL_MASK_MAX_SPANS];
};

struct VIMCGSDrawable {
    int connection,window,surface,backing,locked,width,height;
    void *region;
    unsigned char *readback;
    void *storage_key; // Opaque private GPU-target identity; not a CPU image.
    io_connect_t framebuffer,updates;
    mach_vm_address_t vram;
    mach_vm_size_t vram_size;
    struct VIMFramebufferLayout layout;
    int accelerator_surface,unflipped,native_window,metadata_layout;
    int region_view,origin_x,origin_y;
    uint32_t gpu_family_root; // Native display family; identity still comes from kernel state.
    int read_x,read_y,read_width,read_height;
    int scatter_unavailable;
    struct VIMSurfaceClip *clip;
    struct VIMPrimaryReadMask *read_mask;
    unsigned clip_traces,direct_present_traces;
    void *backend_context;
    uint32_t gpu_lease;uint64_t gpu_object,gpu_incarnation;
    int gpu_family_direct; // Explicit shared drawable, never a by-value import.
    int shared_vram_active;
    uint64_t vram_object,vram_incarnation;
    struct DGVRAMRect *vram_rects;
    struct DGSurfaceRegion *gpu_regions;
    struct VIMSurfaceRAM *ram_reply;
    int shared_memory_kind; // 0=unknown, 1=VRAM, 2=prepared client/GMR RAM
    /* Version already represented by this private GPU allocation. Ordinary
     * native windows keep queued drawing across a same-drawable attachment;
     * a read-only CPU acquisition must not restore their older CPU image. */
    struct VIMSurfaceState imported_state;
    int backing_render_active;
    uint64_t cache_object,cache_incarnation;
    struct VIMCGSDrawable *cache_next;
    unsigned pixel_format; // Canonical typed pbuffer format; zero is old BGRA8.
};
/* The finite x64 upload probe decodes only these owned fields. Fail its build
 * contract if this layout changes; never infer Apple private structure offsets. */
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(struct VIMCGSDrawable,width)==20,"owned drawable probe width");
_Static_assert(offsetof(struct VIMCGSDrawable,unflipped)==108,"owned drawable probe orientation");
_Static_assert(offsetof(struct VIMCGSDrawable,region_view)==120,"owned drawable probe view");
#endif
static int VIMPrimaryClipCoversView(const struct VIMCGSDrawable *d);
static void VIMRetainCPUPublishedDrawable(struct VIMContext *,struct VIMCGSDrawable *,
    const struct VIMFramebufferLayout *,const struct VIMSurfaceState *);
static volatile int vim_backing_render_scopes=1; // debugger-only A/B; peer capability still required
static volatile uint64_t vim_backing_render_stats[4]; // attempts, begins, ends, unsupported
static int VIMEndScopedDrawable(GLIContext opaque)
{
    struct VIMContext *c=(void*)opaque;struct VIMCGSDrawable *d=c?c->drawable:NULL;
    if(!d || !d->backing_render_active)return 1;
    if(!VIMBindGLI(opaque))return 0;
    struct VIMSurfaceState committed={0};
    if(DGEndBackingRender(c->backend,&committed)!=1){c->error=GL_INVALID_OPERATION;return 0;}
    d->backing_render_active=0;d->shared_vram_active=0;
    __sync_fetch_and_add(&vim_backing_render_stats[2],1);
    GLenum prior=c->error;c->error=0;
    VIMRetainCPUPublishedDrawable(c,d,&d->layout,&committed);
    int good=!c->error;if(prior)c->error=prior;
    if(d->native_window)CGSFlushSurface(d->connection,d->window,d->surface,NULL);
    return good; // a pre-existing GL error is not an attach/switch failure
}
/* Only a canonical full-coverage view may draw directly into a GPU family.
 * Partial clips need independent storage: GL may touch pixels outside the
 * publication clip. Never turn DGImportSurfaceRegion's copy contract into
 * aliasing or let a cached full view silently become a partially clipped one.
 */
static int VIMFullGPUFamilyView(const struct VIMCGSDrawable *d)
{
    return d && d->gpu_family_root && d->unflipped && !d->origin_x && !d->origin_y &&
        d->width==(int)d->layout.width && d->height==(int)d->layout.height &&
        VIMPrimaryClipCoversView(d);
}
static int VIMDrawableBindingCompatible(const struct VIMCGSDrawable *old,const struct VIMCGSDrawable *fresh)
{return !old->gpu_family_direct || VIMFullGPUFamilyView(fresh);}
// Source-owned, same-process comparison only. Primary sharing is unchanged.
// The next attachment chooses the route; never switch during a transaction.
static volatile int vim_aux_shared=1;
// Debugger-only comparison of the former image-sized identity allocation.
// No environment polling or extra work in the normal one-byte-key path.
static volatile int vim_private_storage_full_buffer=0;
static void *VIMAllocatePrivateStorageKey(int width,int height)
{return vim_private_storage_full_buffer?calloc((size_t)width*height,4):malloc(1);}
static int VIMDrawableMappingMatches(struct VIMCGSDrawable *d,const struct VIMSurfaceMapping *mapping)
{
    struct VIMFramebufferLayout geometry=mapping->layout;
    /* A GPU-only acquisition supplies geometry, not a CPU mapping offset.
     * The first later CPU acquisition establishes that offset explicitly. */
    if(!mapping->address || d->metadata_layout)geometry.offset=d->layout.offset;
    if(memcmp(&geometry,&d->layout,sizeof(geometry)))return 0;
    if(mapping->address && d->metadata_layout){d->layout.offset=mapping->layout.offset;d->metadata_layout=0;}
    return 1;
}
static int VIMPreservesOrdinaryDrawable(const struct VIMContext *c,const struct VIMCGSDrawable *d)
{
    return !c->format->double_buffer && d->accelerator_surface && !d->unflipped &&
        d->surface!=1 && !d->gpu_family_root && !d->region_view && !d->origin_x && !d->origin_y &&
        d->width>0 && d->height>0 && d->width<=(int)d->layout.width && d->height<=(int)d->layout.height;
}
static void VIMRememberOrdinaryDrawable(struct VIMContext *c,struct VIMCGSDrawable *d,
    const struct VIMSurfaceState *state)
{
    if(!c->error && VIMPreservesOrdinaryDrawable(c,d) && state->version==VIM_SURFACE_STATE_VERSION &&
       state->object && state->incarnation && state->content_epoch &&
       !(state->flags&VIM_SURFACE_STATE_CPU_WRITER))d->imported_state=*state;
    else memset(&d->imported_state,0,sizeof(d->imported_state));
}
static void VIMInvalidateImportedDrawables(struct VIMContext *c)
{
    if(c->drawable)memset(&c->drawable->imported_state,0,sizeof(c->drawable->imported_state));
    if(c->primary_drawable)memset(&c->primary_drawable->imported_state,0,sizeof(c->primary_drawable->imported_state));
    for(struct VIMCGSDrawable *d=c->spare_drawables;d;d=d->cache_next)
        memset(&d->imported_state,0,sizeof(d->imported_state));
}
static int VIMEnsureReadback(struct VIMCGSDrawable *d)
{
    if(!d->readback)d->readback=calloc((size_t)d->width*d->height,4);
    return d->readback!=NULL;
}
static IOReturn VIMUnlockSurfaceMode(io_connect_t connection,int read_only,struct VIMSurfaceState *state)
{
    static int state_api=-1;
    struct VIMSurfaceState temporary;IOReturn error=kIOReturnUnsupported;
    uint64_t mode=read_only?VIM_SURFACE_UNLOCK_READ_ONLY:0;size_t size=sizeof(temporary);
    if(__atomic_load_n(&state_api,__ATOMIC_RELAXED)!=0)
        error=IOConnectCallMethod(connection,kIOVM2DUnlockSurfaceBacking,&mode,1,NULL,0,NULL,NULL,&temporary,&size);
    if(!error) {
        if(size!=sizeof(temporary) || temporary.version!=VIM_SURFACE_STATE_VERSION) return kIOReturnBadArgument;
        __atomic_store_n(&state_api,1,__ATOMIC_RELAXED);
        if(state) *state=temporary;return 0;
    }
    if(error!=kIOReturnUnsupported && error!=kIOReturnBadArgument) return error;
    // Old research providers pay for detection once, not on every transfer.
    __atomic_store_n(&state_api,0,__ATOMIC_RELAXED);
    if(state) memset(state,0,sizeof(*state));
    uint64_t options=0,flags=0;uint32_t count=1;
    return IOConnectCallScalarMethod(connection,kIOVM2DUnlockMemory,&options,1,&flags,&count);
}
static void VIMUnlockSurface(io_connect_t connection) { VIMUnlockSurfaceMode(connection,1,NULL); }
static IOReturn VIMLockSurface(io_connect_t connection,struct VIMSurfaceMapping *mapping)
{
    memset(mapping,0,sizeof(*mapping));size_t size=sizeof(*mapping);
    IOReturn error=IOConnectCallStructMethod(connection,kIOVM2DLockSurfaceBacking,NULL,0,mapping,&size);
    if(error==kIOReturnCannotLock){
        /* Renderer publication needs acquisition, not a dropped GL frame.
         * The paired provider waits atomically for the other context owner.
         * Old providers preserve their original error; never spin/retry. */
        size=sizeof(*mapping);
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
        IOReturn waited=DGClientProfileWaitSurfaceBacking(connection,mapping,&size);
#else
        IOReturn waited=IOConnectCallStructMethod(connection,kIOVM2DWaitSurfaceBacking,NULL,0,mapping,&size);
#endif
        VIMGLITrace("surface acquisition contention connection=%x waited=%x",connection,waited);
        if(waited!=kIOReturnUnsupported && waited!=kIOReturnBadArgument)error=waited;
    }
    if(error) return error;
    const struct VIMFramebufferLayout *l=&mapping->layout;
    if(size!=sizeof(*mapping) || l->version!=VIM_FRAMEBUFFER_LAYOUT_VERSION || !mapping->address ||
       (l->bits_per_pixel!=32 && l->bits_per_pixel!=64) || !l->width || !l->height || l->width>8192 || l->height>8192 ||
       l->row_bytes<(uint64_t)l->width*(l->bits_per_pixel/8) || l->offset>mapping->length ||
       (uint64_t)(l->height-1)*l->row_bytes+(uint64_t)l->width*(l->bits_per_pixel/8)>mapping->length-l->offset) {
        VIMUnlockSurface(connection);return kIOReturnBadArgument;
    }
    return kIOReturnSuccess;
}
static IOReturn VIMLockTextureSurface(io_connect_t connection,struct VIMSurfaceMapping *mapping)
{
    if(DGSupportsSurfaceVersionTextures()) {
        memset(mapping,0,sizeof(*mapping));size_t size=sizeof(*mapping);
        IOReturn error=IOConnectCallStructMethod(connection,kIOVM2DLockGPUSurface,NULL,0,mapping,&size);
        if(!error) {
            const struct VIMFramebufferLayout *l=&mapping->layout;
            if(size==sizeof(*mapping) && !mapping->address && !mapping->length &&
               l->version==VIM_FRAMEBUFFER_LAYOUT_VERSION && l->bits_per_pixel==32 &&
               l->width && l->height && l->width<=8192 && l->height<=8192 &&
               l->row_bytes>=(uint64_t)l->width*4)return kIOReturnSuccess;
            VIMUnlockSurface(connection);return kIOReturnBadArgument;
        }
        // A pending producer owns the CPU exclusion bit too. A failed GPU
        // try-lock is NOT a failed drawable: use the existing atomic CPU
        // acquisition/wait path. It still rejects same-thread recursion;
        // no lock is waived and no unfinished RAM is exposed.
        if(error!=kIOReturnUnsupported && error!=kIOReturnNotReady && error!=kIOReturnBadArgument &&
           error!=kIOReturnCannotLock)return error;
    }
    return VIMLockSurface(connection,mapping);
}
/* A pending loan is immutable READ metadata/image access, never a writable
 * drawable/CPU mapping. Explicit writer attach uses ONLY its geometry here;
 * a separate retained kernel claim is required before any GPU writing. */
static IOReturn VIMLockFutureTextureMetadata(io_connect_t connection,struct VIMSurfaceMapping *mapping)
{
        memset(mapping,0,sizeof(*mapping));size_t size=sizeof(*mapping);
        IOReturn error=IOConnectCallStructMethod(connection,kIOVM2DLockFutureGPUSurface,NULL,0,mapping,&size);
        if(!error){
            const struct VIMFramebufferLayout *l=&mapping->layout;
            if(size==sizeof(*mapping) && !mapping->address && !mapping->length && !l->offset &&
               l->version==VIM_FRAMEBUFFER_LAYOUT_VERSION && l->bits_per_pixel==32 && l->width && l->height &&
               l->width<=8192 && l->height<=8192 && l->row_bytes>=(uint64_t)l->width*4)return kIOReturnSuccess;
            VIMUnlockSurface(connection);return kIOReturnBadArgument;
        }
        return error;
}
static IOReturn VIMLockReadTextureSurface(io_connect_t connection,struct VIMSurfaceMapping *mapping)
{
    if(DGSupportsBackingFutureTextures()){
        IOReturn error=VIMLockFutureTextureMetadata(connection,mapping);
        if(!error)return error;
        if(error!=kIOReturnUnsupported && error!=kIOReturnNotReady && error!=kIOReturnBadArgument)return error;
    }
    return VIMLockTextureSurface(connection,mapping);
}
#include "surface-pixel-format.h"
#include "cgs-shared-storage.c"
#include "cgs-typed-pbuffer.c"
static void VIMDestroyDrawable(struct VIMCGSDrawable *d)
{
    if(!d) return;
    if(d->backing) {
        if(d->locked) CGSUnlockWindowBits(d->connection,d->backing,NULL);
        CGSReleaseWindow(d->connection,d->backing);
    }
    if(d->region) CGSReleaseRegion(d->region);
    if(d->vram) VIMIOConnectUnmapMemory64(d->framebuffer,110,mach_task_self(),d->vram);
    if(d->framebuffer) IOServiceClose(d->framebuffer);
    if(d->updates) IOServiceClose(d->updates);
    if(d->gpu_lease) DGReleaseSurfaceInContext(d->backend_context,d->gpu_lease);
    DGReleaseBuffer(d->backend_context,d->storage_key);
    free(d->storage_key);free(d->readback);free(d->clip);free(d->read_mask);free(d->vram_rects);free(d->gpu_regions);free(d->ram_reply);free(d);
}
#if defined(VIM_GLI_CONNECTION_REUSE_EXPERIMENT)
/* UNQUALIFIED latency candidate: held-step trials miss the first movement;
 * the restored baseline can miss it too, so causality is not established.
 * Keep outside normal builds pending actual event/detach investigation;
 * eliminating service calls is not end-to-end acceptance.
 * An attach still performs set_surface's fresh provider lookup and all geometry,
 * clip, identity and ownership queries. Recycle only an unused accelerator
 * connection, after binding adoption or drawing-view retirement. Changing
 * drawing-view geometry must not force another service open. Explicitly
 * unbind its retained surface;
 * never cache a CPU lock, mapping, surface ID or pixel/version promise here.
 * Context-local, bounded to two ports: real display access adopts a binding
 * then retires another view at detach. A single slot closes/reopens one port
 * each cycle (verified with bounded IOKit caller stacks). Failed attaches
 * retain normal teardown. This does not pool textures' owning connections.
 */
static volatile int vim_reuse_drawable_connection=1; // Same-process debugger comparison.
static volatile uint64_t vim_drawable_connection_stats[3]; // reuse, recycle, rejected reset.
static void VIMRecycleDrawable(struct VIMContext *c,struct VIMCGSDrawable *d)
{
    if(vim_reuse_drawable_connection && c->idle_drawable_connection_count<2 && d->updates &&
       d->accelerator_surface && !d->locked && !d->backing && !d->framebuffer && !d->vram) {
        uint64_t args[]={0,0};uint32_t record[11]={0};size_t size=sizeof(record);
        IOReturn error=IOConnectCallMethod(d->updates,kIOVM2DSetSurface,args,2,NULL,0,NULL,NULL,record,&size);
        if(!error) {
            c->idle_drawable_connections[c->idle_drawable_connection_count++]=d->updates;d->updates=0;
            __atomic_fetch_add(&vim_drawable_connection_stats[1],1,__ATOMIC_RELAXED);
        } else __atomic_fetch_add(&vim_drawable_connection_stats[2],1,__ATOMIC_RELAXED);
    }
    VIMDestroyDrawable(d);
}
#else
#define VIMRecycleDrawable(context,drawable) VIMDestroyDrawable(drawable)
#endif
/* Retain allocations, NOT surface contents or a reusable CGS number. Every
 * hit below was freshly rebound/locked and carries a lifetime-unique kernel
 * object/incarnation. Its current CPU/GPU contents are imported as usual.
 * Inactive views have no held CPU lock; retirement still drains their ordered
 * backend commands. A bounded LRU avoids allocating/dropping framebuffer
 * storage on each ordinary/primary drawing-view switch. */
static volatile int vim_drawable_cache_override=-1; // Same-process debugger A/B, never persisted.
static volatile uint64_t vim_drawable_cache_stats[3]; // hits, stores, evictions; no logging/polling.
static int VIMDrawableCacheEnabled(void)
{return vim_drawable_cache_override>0 || (vim_drawable_cache_override<0 && getenv("VIM_GL_DRAWABLE_CACHE") && !getenv("VIM_GL_DISABLE_DRAWABLE_CACHE"));}
static uint64_t VIMDrawableStorageCost(struct VIMContext *c,struct VIMCGSDrawable *d)
{
    // Logical color/back/depth/stencil storage plus any explicit CPU readback;
    // native row alignment/driver bookkeeping is additional to this estimate.
    unsigned bytes=4u*(1u+!!c->format->double_buffer+!!d->readback);
    if(c->format->depth || c->format->stencil)bytes+=4;
    return (uint64_t)d->width*d->height*bytes;
}
static void VIMRetireDrawable(struct VIMContext *c,struct VIMCGSDrawable *d)
{
    if(!d)return;
    const uint64_t budget=128u*1024u*1024u;
    uint64_t bytes=VIMDrawableStorageCost(c,d);
    if(!d->accelerator_surface || !d->cache_object || !d->cache_incarnation ||
       d->locked || !d->storage_key || bytes>budget || !VIMDrawableCacheEnabled()) {
        VIMRecycleDrawable(c,d);return;
    }
    d->cache_next=c->spare_drawables;c->spare_drawables=d;
    __atomic_fetch_add(&vim_drawable_cache_stats[1],1,__ATOMIC_RELAXED);
    unsigned count=0;uint64_t used=0;struct VIMCGSDrawable **link=&c->spare_drawables;
    while(*link) {
        struct VIMCGSDrawable *view=*link;uint64_t size=VIMDrawableStorageCost(c,view);
        if(count>=8 || size>budget-used) {
            *link=view->cache_next;view->cache_next=NULL;VIMRecycleDrawable(c,view);
            __atomic_fetch_add(&vim_drawable_cache_stats[2],1,__ATOMIC_RELAXED);
        } else {count++;used+=size;link=&view->cache_next;}
    }
}
static int VIMSameDrawableView(const struct VIMCGSDrawable *a,const struct VIMCGSDrawable *b)
{
    return a->cache_object && a->cache_object==b->cache_object && a->cache_incarnation==b->cache_incarnation &&
        a->surface==b->surface && a->unflipped==b->unflipped && a->region_view==b->region_view &&
        a->width==b->width && a->height==b->height && !memcmp(&a->layout,&b->layout,sizeof(a->layout)) &&
        VIMDrawableBindingCompatible(a,b);
}
static void VIMAdoptDrawableBinding(struct VIMCGSDrawable *old,struct VIMCGSDrawable *fresh)
{
    // Always use the just-bound connection. Its fresh provider lookup, not a
    // retained port or CGS ID, establishes the newly attached surface lifetime.
    io_connect_t previous=old->updates;old->updates=fresh->updates;fresh->updates=previous;
    old->origin_x=fresh->origin_x;old->origin_y=fresh->origin_y;
    old->gpu_family_root=fresh->gpu_family_root;
    old->cache_object=fresh->cache_object;old->cache_incarnation=fresh->cache_incarnation;
    old->native_window=fresh->native_window;old->connection=fresh->connection;old->window=fresh->window;
    free(old->clip);old->clip=fresh->clip;fresh->clip=NULL;
}
static void VIMReplaceDrawable(struct VIMContext *c,struct VIMCGSDrawable *d)
{
    if(c->drawable && c->drawable!=d && c->drawable!=c->primary_drawable) VIMRetireDrawable(c,c->drawable);
    c->drawable=d;
    if(d && d->accelerator_surface && d->surface==1) {
        if(c->primary_drawable!=d) VIMRetireDrawable(c,c->primary_drawable);
        c->primary_drawable=d;
        void (*preserve)(void)=(void *)DGGetProcAddress("DGPreserveCurrentBuffer");
        if(preserve && VIMBindGLI((GLIContext)c)) preserve();
    }
}
/* The research provider's shadow allocation is also written by CPU Quartz.
 * Unlike a real shared-memory GPU drawable, the host GL texture is a separate
 * allocation. Import the current shadow before a primary GPU batch so that
 * full readback does not erase CPU-composited layers outside its damage.
 * This deliberately expensive bring-up synchronization is not the final
 * dirty-region/shared-resource transport. */
static int VIMQueryRAMBacking(struct VIMCGSDrawable *d,int dirty,uint32_t first,
    struct VIMSurfaceVRAM *backing,unsigned *total)
{
    if(!DGSupportsRAMDrawables())return 0;
    if(!d->ram_reply)d->ram_reply=calloc(1,sizeof(*d->ram_reply));
    if(!d->ram_reply)return 0;
    uint64_t args[]={!!dirty,first};size_t size=sizeof(*d->ram_reply);
    IOReturn error=IOConnectCallMethod(d->updates,kIOVM2DGetRAMSurface,args,2,NULL,0,NULL,NULL,d->ram_reply,&size);
    const struct VIMSurfaceRAM *r=d->ram_reply;
    if(error || size!=sizeof(*r) || r->backing.version!=VIM_SURFACE_RAM_VERSION || r->reserved ||
       r->first!=first || !r->total || r->total>16384 || r->backing.length!=(uint64_t)r->total*4096 ||
       r->count>VIM_SURFACE_RAM_PAGE_CHUNK || (first==UINT32_MAX?r->count!=0:
           (first>=r->total || !r->count || r->count>r->total-first)))return 0;
    *backing=r->backing;if(total)*total=r->total;return 1;
}
static int VIMQuerySharedBacking(struct VIMCGSDrawable *d,int dirty,struct VIMSurfaceVRAM *backing,unsigned *total)
{
    if(d->shared_memory_kind==2 && VIMQueryRAMBacking(d,dirty,UINT32_MAX,backing,total))return 1;
    if(dirty && d->shared_memory_kind==2)return 0;
    uint64_t mode=!!dirty;size_t size=sizeof(*backing);
    IOReturn error=IOConnectCallMethod(d->updates,kIOVM2DGetVRAMSurface,&mode,1,NULL,0,NULL,NULL,backing,&size);
    if(!error && size==sizeof(*backing) && backing->version==VIM_SURFACE_VRAM_VERSION){
        d->shared_memory_kind=1;if(total)*total=0;return 1;
    }
    if(dirty)return 0;
    if(VIMQueryRAMBacking(d,0,UINT32_MAX,backing,total)){d->shared_memory_kind=2;return 1;}
    return 0;
}
static int VIMImportSharedBacking(struct VIMCGSDrawable *d,const struct VIMSurfaceVRAM *backing,unsigned total)
{
    if(backing->version!=VIM_SURFACE_RAM_VERSION)
        return DGImportVRAMDrawable(backing,d->origin_x,d->origin_y,d->width,d->height,!d->unflipped);
    int imported=DGImportRAMDrawable(backing,d->origin_x,d->origin_y,d->width,d->height,!d->unflipped,NULL,0);
    if(imported!=-2)return imported;
    if(!total || total>16384)return 0;
    uint64_t *pages=malloc((size_t)total*sizeof(*pages));if(!pages)return 0;
    int good=1;
    for(unsigned first=0;first<total;){
        struct VIMSurfaceVRAM part={0};unsigned count=0;
        if(!VIMQueryRAMBacking(d,0,first,&part,&count) || count!=total || memcmp(&part,backing,sizeof(part))){good=0;break;}
        memcpy(pages+first,d->ram_reply->pages,d->ram_reply->count*sizeof(*pages));first+=d->ram_reply->count;
    }
    imported=good?DGImportRAMDrawable(backing,d->origin_x,d->origin_y,d->width,d->height,!d->unflipped,pages,total):0;
    free(pages);return imported;
}
/* Acquire one coherent family epoch. CPU mappings exist only while genuinely
 * seeding a new/CPU-written version; normal composition uses metadata locks.
 * The caller owns the returned lock even when validation fails. */
static int VIMPrepareGPUFamily(struct VIMCGSDrawable *d,struct VIMSurfaceMapping *mapping,
    struct VIMSurfaceState *state,uint64_t *epoch)
{
    int good=VIMDrawableMappingMatches(d,mapping) && VIMCGSState(d->updates,state);
    if(good && d->gpu_lease && (d->gpu_object!=state->object || d->gpu_incarnation!=state->incarnation)) {
        DGReleaseSurface(d->gpu_lease);d->gpu_lease=0;
    }
    if(good && !d->gpu_lease) {
        d->gpu_lease=DGAcquireSurface(state->object,state->incarnation,d->layout.width,d->layout.height);
        d->gpu_object=state->object;d->gpu_incarnation=state->incarnation;
    }
    good=good && d->gpu_lease && DGSurfaceEpoch(d->gpu_lease,epoch);
    if(good && *epoch<state->content_epoch) {
        if(!mapping->address) {
            int finished=VIMCGSReadFinished(d->updates,state);
            /* A zero layout denotes no outstanding lock to the caller. */
            memset(mapping,0,sizeof(*mapping));
            if(!finished)return 0;
            if(VIMLockSurface(d->updates,mapping)){memset(mapping,0,sizeof(*mapping));return 0;}
            good=VIMDrawableMappingMatches(d,mapping) && VIMCGSState(d->updates,state) &&
                state->object==d->gpu_object && state->incarnation==d->gpu_incarnation;
        }
        if(good)good=DGUploadSurface(d->gpu_lease,(void *)(uintptr_t)(mapping->address+mapping->layout.offset),
            d->layout.width,d->layout.height,mapping->layout.row_bytes,state->content_epoch);
        if(good)*epoch=state->content_epoch;
    }
    return good && *epoch==state->content_epoch;
}
static CGLError VIMImportAcceleratorBacking(struct VIMContext *c,struct VIMCGSDrawable *d)
{
    /* The region view starts with the corresponding CPU shadow rectangle,
     * just as a native shared drawable does. Compatibility full-allocation import
     * remains opt-in; it did not fix the missing drawing-view geometry. */
    if(!d || !d->accelerator_surface) return kCGLNoError;
    if(d->pixel_format)return VIMImportTypedPBuffer(c,d);
    if(d->surface==1 && !d->gpu_family_root && !d->region_view && !getenv("VIM_PRIMARY_IMPORT")) return kCGLNoError;
    if(!VIMBindGLI((GLIContext)c)) return kCGLBadDrawable;
    // Full auxiliary RAM only. Cropped/primary/GPU-family and flipped window
    // contracts retain their existing routes. The transport owns its own
    // pin/lock; d->updates is never donated or left locked on its behalf.
    if(vim_backing_render_scopes && !c->native_renderer_kind && !c->format->double_buffer &&
       d->surface!=1 && !d->gpu_family_root && !d->region_view && d->unflipped &&
       !d->origin_x && !d->origin_y && !d->layout.offset &&
       d->width==(int)d->layout.width && d->height==(int)d->layout.height && DGSupportsBackingRenderScopes()){
        __sync_fetch_and_add(&vim_backing_render_stats[0],1);
        int scoped=DGBeginBackingRender((uint32_t)d->surface,&d->layout);
        if(scoped>=0){
            if(!scoped){VIMGLITrace("backing scope refused surface=%x size=%d,%d pitch=%u",d->surface,d->width,d->height,d->layout.row_bytes);return kCGLBadDrawable;}
            d->backing_render_active=1;d->shared_vram_active=0;
            __sync_fetch_and_add(&vim_backing_render_stats[1],1);return kCGLNoError;
        }
        __sync_fetch_and_add(&vim_backing_render_stats[3],1);
    }
    if(d->gpu_family_root) {
        if(!DGSupportsSurfaceRegionImport() || !DGSupportsSurfaceRegions() || !DGSupportsSurfaceVersionTextures())return kCGLBadDrawable;
        struct VIMSurfaceMapping mapping;struct VIMSurfaceState state={0};uint64_t epoch=0;
        if(VIMLockTextureSurface(d->updates,&mapping))return kCGLBadDrawable;
        int direct=VIMFullGPUFamilyView(d) && !c->format->double_buffer && DGSupportsSurfaceDrawablePublication() &&
            !getenv("VIM_GL_DISABLE_DIRECT_FAMILY");
        int good=(!d->gpu_family_direct || direct) && VIMPrepareGPUFamily(d,&mapping,&state,&epoch);
        if(good) {
            if(direct) {
                good=DGAttachSurfaceDrawable(d->gpu_lease);
                if(good)d->gpu_family_direct=1;
            } else good=DGImportSurfaceRegion(d->gpu_lease,epoch,d->origin_x,d->origin_y,d->width,d->height,!d->unflipped);
        }
        if(mapping.layout.version && VIMUnlockSurfaceMode(d->updates,1,NULL))good=0;
        return good?kCGLNoError:kCGLBadDrawable;
    }
    int retained=d->unflipped && !c->format->double_buffer && !d->region_view && !d->origin_x && !d->origin_y &&
        d->width==(int)d->layout.width && d->height==(int)d->layout.height &&
        VIMGLProductFeature("VIM_RETAINED_CGS_SURFACES") && DGSupportsSurfaceDrawables();
    struct VIMSurfaceMapping mapping;
    IOReturn locked=retained?VIMLockTextureSurface(d->updates,&mapping):VIMLockSurface(d->updates,&mapping);
    if(locked){VIMGLITrace("backing lock failed=%x retained=%d",locked,retained);return kCGLBadDrawable;}
    /* A metadata-only GPU lock has no mapped allocation/offset. The drawing
     * geometry must match, but an absent CPU offset is not a layout change. */
    if(!VIMDrawableMappingMatches(d,&mapping)) {
        VIMGLITrace("backing mapping changed surface=%x",d->surface);
        VIMUnlockSurface(d->updates);return kCGLBadDrawable;
    }
    struct VIMSurfaceState imported_state={0};
    if(VIMPreservesOrdinaryDrawable(c,d) && VIMCGSState(d->updates,&imported_state) &&
       d->imported_state.version && d->imported_state.object==imported_state.object &&
       d->imported_state.incarnation==imported_state.incarnation &&
       d->imported_state.content_epoch==imported_state.content_epoch){
        /* Identity and CPU freshness are checked under the real provider
         * lock, not inferred from a recycled CGS ID/address. Keep both the
         * private GPU pixels and its existing shared-publication route. */
        return VIMUnlockSurfaceMode(d->updates,1,NULL)?kCGLBadDrawable:kCGLNoError;
    }
    memset(&d->imported_state,0,sizeof(d->imported_state));
    d->shared_vram_active=0;
    /* The coherent shared-memory transfer is a backing contract, not a
     * primary-surface-ID contract. Auxiliary client-backed drawing targets
     * must remain CPU-current too; the same pinned-page GPU copy can satisfy
     * that without downloading pixels through the command stream. Keep the
     * established primary cases and qualify auxiliary single buffers only.
     */
    if((d->surface==1 || (!c->format->double_buffer && vim_aux_shared)) && mapping.address &&
       VIMGLSharedRowsSupported(mapping.layout.width,mapping.layout.height,mapping.layout.row_bytes) &&
       VIMGLProductFeature("VIM_SHARED_VRAM_SURFACES") && DGSupportsVRAMDrawables()) {
        struct VIMSurfaceVRAM backing={0};unsigned total=0;
        if(VIMQuerySharedBacking(d,0,&backing,&total) && backing.row_bytes==mapping.layout.row_bytes) {
            int imported=VIMImportSharedBacking(d,&backing,total);
            if(imported>=0) {
                if(imported){d->shared_vram_active=1;d->vram_object=backing.state.object;d->vram_incarnation=backing.state.incarnation;}
                int unlocked=VIMCGSReadFinished(d->updates,&backing.state);
                if(imported && unlocked)VIMRememberOrdinaryDrawable(c,d,&backing.state);
                return imported && unlocked?kCGLNoError:kCGLBadDrawable;
            }
        }
    }
    if(retained) {
        struct VIMSurfaceState state={0};int good=VIMCGSState(d->updates,&state);
        if(!good)VIMGLITrace("retained state invalid version=%u flags=%x object=%llu incarnation=%llu epoch=%llu",state.version,state.flags,(unsigned long long)state.object,(unsigned long long)state.incarnation,(unsigned long long)state.content_epoch);
        if(good && d->gpu_lease && (d->gpu_object!=state.object || d->gpu_incarnation!=state.incarnation)) {
            DGReleaseSurface(d->gpu_lease);d->gpu_lease=0;
        }
        if(good && !d->gpu_lease) {
            d->gpu_lease=DGAcquireSurface(state.object,state.incarnation,d->width,d->height);
            if(!d->gpu_lease){uuid_t ns;VIMGLITrace("surface acquire failed object=%llu incarnation=%llu namespace=%d",(unsigned long long)state.object,(unsigned long long)state.incarnation,VIMGetResourceNamespace(ns));}
            d->gpu_object=state.object;d->gpu_incarnation=state.incarnation;
        }
        uint64_t epoch=0;
        good=good && d->gpu_lease && DGSurfaceEpoch(d->gpu_lease,&epoch);
        // An unchanged CPU epoch must not overwrite newer, unflushed GPU
        // drawing in this very allocation. CPU publication is independent.
        if(good && epoch<state.content_epoch) {
            /* A missing/stale host allocation cannot be seeded from a
             * metadata-only mapping. Explicitly materialize before fallback. */
            if(!mapping.address) {
                good=VIMCGSReadFinished(d->updates,&state);
                if(!good)return kCGLBadDrawable;
                if(VIMLockSurface(d->updates,&mapping))return kCGLBadDrawable;
                good=VIMCGSState(d->updates,&state) &&
                    VIMDrawableMappingMatches(d,&mapping) &&
                    state.object==d->gpu_object && state.incarnation==d->gpu_incarnation;
            }
            if(good)good=DGUploadSurface(d->gpu_lease,(void *)(uintptr_t)(mapping.address+mapping.layout.offset),
                d->width,d->height,mapping.layout.row_bytes,state.content_epoch);
        }
        if(good)good=DGAttachSurfaceDrawable(d->gpu_lease);
        good=VIMCGSReadFinished(d->updates,&state) && good;
        if(!good)VIMGLITrace("retained backing failed surface=%x lease=%llu epoch=%llu state=%llu",d->surface,(unsigned long long)d->gpu_lease,(unsigned long long)epoch,(unsigned long long)state.content_epoch);
        return good?kCGLNoError:kCGLBadDrawable;
    }
    const void *pixels=(void *)(uintptr_t)(mapping.address+mapping.layout.offset+
        (uint64_t)d->origin_y*mapping.layout.row_bytes+(uint64_t)d->origin_x*4);
    // Physical native GL retains the existing surface through both ordinary
    // and unflipped attachments; only the GL-facing row orientation differs.
    int imported=DGImportDrawable(pixels,d->width,d->height,mapping.layout.row_bytes,!d->unflipped);
    int unlocked=!VIMUnlockSurfaceMode(d->updates,1,NULL);
    if(imported && unlocked)VIMRememberOrdinaryDrawable(c,d,&imported_state);
    return imported && unlocked ? kCGLNoError : kCGLBadDrawable;
}
static CGLError VIMCreateFullscreen(struct VIMContext *c,const uint32_t *record)
{
    if(!record) return kCGLBadDisplay;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ != 1040
    // The later compositor record selects an IOFramebuffer in word three.
    // This provider supports its single display only.
    if(record[3]) return kCGLBadDisplay;
#else
    /* Native Tiger CGLSetFullScreen allocates a20-byte drawable header whose
     * data pointer addresses only its final4bytes. Intel950 accepts that
     * opaque nonnull cookie and selects the framebuffer from its context;
     * it does not read a later OS's16-byte record. GLI already validated the
     * selected screen in gliAttachDrawableWithOptions. Reading record[3]
     * here was out-of-bounds and rejected valid captured fullscreen contexts.
     * Keep the actual provider/layout/map checks below, not forced success. */
#endif
    io_service_t fb=VIMIOServiceForClass("VMsvga2");
    if(!fb) return kCGLBadDisplay;
    struct VIMFramebufferLayout layout={0};
    if(!VIMIOCopyDataProperty(fb,VIM_FRAMEBUFFER_LAYOUT_KEY,&layout,sizeof(layout)) ||
        layout.version!=VIM_FRAMEBUFFER_LAYOUT_VERSION || layout.bits_per_pixel!=32 ||
        !layout.width || !layout.height || layout.width>8192 || layout.height>8192 ||
        layout.row_bytes<(uint64_t)layout.width*4) { IOObjectRelease(fb);return kCGLBadDisplay; }
    if(c->drawable && c->drawable->framebuffer &&
        !memcmp(&layout,&c->drawable->layout,sizeof(layout))) return kCGLNoError;
    struct VIMCGSDrawable *d=calloc(1,sizeof(*d));
    if(d) d->backend_context=c->backend;
    if(!d) { IOObjectRelease(fb);return kCGLBadAlloc; }
    IOReturn error=IOServiceOpen(fb,mach_task_self(),kIOFBSharedConnectType,&d->framebuffer);
    IOObjectRelease(fb);
    // Memory type 110 is kIOFBVRAMMemory in the matching IOGraphics OSS.
    if(!error) error=VIMIOConnectMapMemory64(d->framebuffer,110,mach_task_self(),&d->vram,&d->vram_size,kIOMapAnywhere);
    if(error || !d->vram || (uint64_t)layout.offset+(uint64_t)layout.row_bytes*layout.height>d->vram_size) {
        VIMDestroyDrawable(d);return kCGLBadDrawable;
    }
    io_service_t accel=VIMIOServiceForClass("VMsvga2Accel");
    if(accel) { IOServiceOpen(accel,mach_task_self(),2,&d->updates);IOObjectRelease(accel); }
    if(!d->updates) { VIMDestroyDrawable(d);return kCGLBadDrawable; }
    d->width=(int)layout.width;d->height=(int)layout.height;d->layout=layout;
    d->readback=calloc((size_t)d->width*d->height,4);
    void *pixels=VIMAllocatePrivateStorageKey(d->width,d->height);
    if(!d->readback || !pixels) { free(pixels);VIMDestroyDrawable(d);return kCGLBadAlloc; }
    CGLError e=VIMSetPrivateDrawable(c,d->width,d->height,pixels);
    if(e) { free(pixels);VIMDestroyDrawable(d);return e; }
    d->storage_key=pixels;VIMReplaceDrawable(c,d);
    void (*external)(GLboolean)=(void *)DGGetProcAddress("DGSetExternalPresent");
    if(external && VIMBindGLI((GLIContext)c)) external(GL_TRUE);
    return kCGLNoError;
}
static CGLError VIMCreateDrawable(struct VIMContext *c,const int *ids)
{
    CGRect bounds={0};
    if(!ids || CGSGetSurfaceBounds(ids[0],ids[1],ids[2],&bounds)) return kCGLBadDrawable;
    if(!isfinite(bounds.size.width) || !isfinite(bounds.size.height) ||
       bounds.size.width<1 || bounds.size.height<1 ||
       bounds.size.width>8192 || bounds.size.height>8192) return kCGLBadValue;
    struct VIMCGSDrawable *d=calloc(1,sizeof(*d));
    if(d) d->backend_context=c->backend;
    if(!d) return kCGLBadAlloc;
    d->connection=ids[0];d->window=ids[1];d->surface=ids[2];
    d->width=(int)bounds.size.width;d->height=(int)bounds.size.height;
    d->readback=calloc((size_t)d->width*d->height,4);
    if(!d->readback) { VIMDestroyDrawable(d);return kCGLBadAlloc; }
    int error=CGSNewRegionWithRect(&bounds,&d->region);
    if(!error) error=CGSNewWindow(d->connection,2,0,0,d->region,&d->backing);
    if(!error) error=CGSSetWindowDepth(d->connection,d->backing,4);
    if(!error) error=CGSBindSurface(d->connection,d->window,d->surface,1,0,d->backing);
    if(error) { VIMDestroyDrawable(d);return kCGLBadDrawable; }
    /* Host GL default framebuffer dimensions must follow the drawable. */
    void *pixels=VIMAllocatePrivateStorageKey(d->width,d->height);
    if(!pixels) { VIMDestroyDrawable(d);return kCGLBadAlloc; }
    CGLError e=VIMSetPrivateDrawable(c,d->width,d->height,pixels);
    if(e) { free(pixels);VIMDestroyDrawable(d);return e; }
    d->storage_key=pixels;VIMReplaceDrawable(c,d);
    void (*external)(GLboolean)=(void *)DGGetProcAddress("DGSetExternalPresent");
    if(external && VIMBindGLI((GLIContext)c)) external(GL_TRUE);
    return kCGLNoError;
}
static CGLError VIMCreateAcceleratorDrawable(struct VIMContext *c,const uint32_t *ids,int unflipped)
{
    if(!ids || !ids[2]) return kCGLBadDrawable;
    struct VIMCGSDrawable *d=calloc(1,sizeof(*d));
    if(d) d->backend_context=c->backend;
    if(!d) return kCGLBadAlloc;
    IOReturn error=0;
#if defined(VIM_GLI_CONNECTION_REUSE_EXPERIMENT)
    if(vim_reuse_drawable_connection && c->idle_drawable_connection_count) {
        unsigned index=--c->idle_drawable_connection_count;
        d->updates=c->idle_drawable_connections[index];c->idle_drawable_connections[index]=0;
        __atomic_fetch_add(&vim_drawable_connection_stats[0],1,__ATOMIC_RELAXED);
    } else
#endif
    {
        io_service_t service=vim_renderer_accelerator;
        int discovered=!service;
        if(discovered)service=VIMIOServiceForClass("VMsvga2Accel");
        error=service ? IOServiceOpen(service,mach_task_self(),2,&d->updates) : kIOReturnNotFound;
        if(service && discovered)IOObjectRelease(service);
    }
    VIMGLITrace("surface open=%x",error);
    // Bind the existing CGS surface without changing its pixel format.
    // 0x100 is not a mapping hint: VMsvga22DContext changes the surface to
    // A8R8G8B8, which breaks native primary framebuffer-copy operations.
    uint64_t args[]={ids[2],0x800};uint32_t record[11]={0};size_t size=sizeof(record);
    if(!error) error=IOConnectCallMethod(d->updates,kIOVM2DSetSurface,args,2,NULL,0,NULL,NULL,record,&size);
    VIMGLITrace("surface set=%x",error);
#if VIM_GLI_DISPATCH_COUNT == 757 || VIM_GLI_DISPATCH_COUNT == 764 || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    if(!error && unflipped && !vim_native_bind_surface) {
        /* Actual8R4088,9J3050 and10J3250 cglsAttachPBuffer pass a complete record
         * to type5a. Its RGBA8 mode0x24/shape1 has an empty visibility rect,
         * unlike a visible GA surface. Native Intel attaches the independent
         * allocation by ID; our 2D provider also needs its exact source size.
         * Never derive a display-sized allocation or reinterpret a window
         * attachment as this record. Both period widths were disassembled.
         * Tiger's44-byte record puts width/height BEFORE the accelerator;
         * the later record puts them after it. Its759-slot dispatch layout
         * does not imply either CGLS field order.
         * Its native WindowServer CI temporary (e.g.200x1 RGBA8) otherwise
         * has no fixed source: cglsTexImagePBuffer fails with BadDrawable,
         * and CI recursively converts the same bitmap until stack exhaustion.
         * Public CGL pbuffer tests do not enter this server-only branch. */
        struct PeriodPBuffer {
            uint64_t reserved;uint32_t identifier,target,internal,level;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
            void *surface;uint32_t width,height,accelerator;uint8_t mode,padding[3];
#else
            void *surface;uint32_t accelerator,width,height;uint8_t mode,padding[3];
#endif
        };
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
        _Static_assert(sizeof(struct PeriodPBuffer)==44 && offsetof(struct PeriodPBuffer,width)==28 &&
            offsetof(struct PeriodPBuffer,accelerator)==36,"actual8R4088 i386 CGLS pbuffer record");
#else
        _Static_assert(offsetof(struct PeriodPBuffer,width)==28+sizeof(void *),"period pbuffer width ABI");
#endif
        const struct PeriodPBuffer *pb=(const void *)ids;
        if(pb->reserved || pb->identifier<=2 || !pb->width || !pb->height ||
           pb->width>8192 || pb->height>8192 ||
           !((pb->mode==0x24 && (pb->internal==GL_RGBA8 || pb->internal==GL_RGBA)) ||
             (pb->mode==0x2b && pb->internal==GL_RGBA16)))error=kIOReturnUnsupported;
        else {
            uint64_t extent[]={kIOBlitFixedSource,pb->width,pb->height};
            error=IOConnectCallScalarMethod(d->updates,kIOVM2DScaleSurface,extent,3,NULL,NULL);
        }
        VIMGLITrace("period pbuffer mode=%x extent=%u,%u scale=%x",pb->mode,pb->width,pb->height,error);
    }
#endif
    if(!error && !unflipped && (ids[0] || ids[1])) {
        // Native CGL asks this same CGS API for pixel dimensions after attach.
        // Our GL driver must establish the fixed source in VMsvga2, as its
        // existing GA AllocateSurface does; a fresh window has no source yet.
        CGFloat resolution=1;CGSize pixels=CGSizeZero;
#if VIM_GLI_DISPATCH_COUNT <= 764
        // Leopard/Snow have no CGSGetSurfaceResolution export or HiDPI scale.
        // Its native bounds are already the pixel dimensions. Do not leave a
        // lazy import of the newer API: ordinary app attachment reaches it
        // even though offscreen capability checks do not.
        /* A literal zero needs no external data import. Apple's Rosetta1
         * native ApplicationServices bridge exports the CGS calls below,
         * but not CoreGraphics' CGRectZero data symbol. */
        CGRect bounds={0};
        int query=CGSGetSurfaceBounds(ids[0],ids[1],ids[2],&bounds);
        pixels=bounds.size;
#else
        int query=CGSGetSurfaceResolution(ids[0],ids[1],ids[2],&resolution,&pixels);
#endif
        if(query || !isfinite(pixels.width) || !isfinite(pixels.height) ||
           pixels.width<1 || pixels.height<1 || pixels.width>8192 || pixels.height>8192 ||
           floor(pixels.width)!=pixels.width || floor(pixels.height)!=pixels.height)
            error=kIOReturnBadArgument;
        else {
            uint64_t scale[]={kIOBlitFixedSource,(uint64_t)pixels.width,(uint64_t)pixels.height};
            error=IOConnectCallScalarMethod(d->updates,kIOVM2DScaleSurface,scale,3,NULL,NULL);
        }
        VIMGLITrace("native source resolution=%g pixels=%g,%g query=%d scale=%x",resolution,pixels.width,pixels.height,query,error);
    }
    struct VIMSurfaceMapping mapping;
    if(!error) {
        struct VIMSurfaceGeometry geometry={0};size_t bytes=sizeof(geometry);
        IOReturn query=IOConnectCallStructMethod(d->updates,kIOVM2DGetSurfaceGeometry,NULL,0,&geometry,&bytes);
        if(!query && bytes==sizeof(geometry) && geometry.version==VIM_SURFACE_GEOMETRY_VERSION)
            d->gpu_family_root=geometry.reserved;
    }
    if(!error) {
        // This initial acquisition only reads geometry/identity, then unlocks.
        // The explicit writer-chain path can inspect a pending immutable loan
        // without first forcing RAM CPU-current. No writable/CPU use is made
        // through that read loan; DGBeginBackingRender claims its own gate.
        if(DGSupportsBackingWriterScopesInContext(c->backend) && unflipped && ids[2]!=1 && !d->gpu_family_root){
            error=VIMLockFutureTextureMetadata(d->updates,&mapping);
            if(error==kIOReturnUnsupported || error==kIOReturnNotReady || error==kIOReturnBadArgument)
                error=VIMLockTextureSurface(d->updates,&mapping);
        }else error=d->gpu_family_root || (unflipped && ids[2]!=1 && VIMGLProductFeature("VIM_RETAINED_CGS_SURFACES") &&
            DGSupportsSurfaceDrawables())?VIMLockTextureSurface(d->updates,&mapping):VIMLockSurface(d->updates,&mapping);
        if(!error) {
            d->layout=mapping.layout;
            if(d->layout.bits_per_pixel!=32 &&
               !VIMSurfaceFormat(d->updates,&mapping,&d->pixel_format))error=kIOReturnUnsupported;
            d->metadata_layout=!mapping.address;
            d->width=(int)d->layout.width;d->height=(int)d->layout.height;
            if(!unflipped && !ids[0] && !ids[1] && ids[2]!=1 && !d->gpu_family_root) {
                // Native client-backed single-rectangle drawing size can be
                // smaller than its padded allocation. This server attachment
                // has no client CGS window to query. Keep CPU layout untouched.
                struct VIMSurfaceDrawingSize drawing={0};size_t bytes=sizeof(drawing);
                IOReturn query=IOConnectCallStructMethod(d->updates,kIOVM2DGetSurfaceDrawingSize,
                    NULL,0,&drawing,&bytes);
                if(!query && bytes==sizeof(drawing) && drawing.width && drawing.height &&
                   drawing.width<=d->layout.width && drawing.height<=d->layout.height) {
                    d->width=(int)drawing.width;d->height=(int)drawing.height;
                }
            }
            if(ids[2]==1 || d->gpu_family_root) {
                struct VIMSurfaceGeometry geometry={0};size_t bytes=sizeof(geometry);
                IOReturn query=IOConnectCallStructMethod(d->updates,kIOVM2DGetSurfaceGeometry,
                    NULL,0,&geometry,&bytes);
                if(!query) {
                    if(bytes!=sizeof(geometry) || geometry.version!=VIM_SURFACE_GEOMETRY_VERSION ||
                       geometry.source_width!=d->layout.width || geometry.source_height!=d->layout.height ||
                       geometry.x<0 || geometry.y<0 || !geometry.width || !geometry.height ||
                       (uint64_t)geometry.x+geometry.width>d->layout.width ||
                       (uint64_t)geometry.y+geometry.height>d->layout.height) error=kIOReturnBadArgument;
                    else {
                        d->region_view=1;d->origin_x=geometry.x;d->origin_y=geometry.y;
                        d->width=(int)geometry.width;d->height=(int)geometry.height;
                        d->clip=calloc(1,sizeof(*d->clip));bytes=sizeof(*d->clip);
                        error=d->clip ? IOConnectCallStructMethod(d->updates,kIOVM2DGetSurfaceClip,
                            NULL,0,d->clip,&bytes) : kIOReturnNoMemory;
                        // set_shape clears the region and succeeds for an
                        // empty visibility shape while retaining geometry.
                        if(error==kIOReturnNotReady && d->clip) {
                            memset(d->clip,0,sizeof(*d->clip));
                            d->clip->version=VIM_SURFACE_CLIP_VERSION;error=0;
                        }
                        if(!error && (bytes!=sizeof(*d->clip) || d->clip->version!=VIM_SURFACE_CLIP_VERSION ||
                           d->clip->count>VIM_SURFACE_CLIP_RECTS)) error=kIOReturnBadArgument;
                    }
                } else if(query!=kIOReturnUnsupported && query!=kIOReturnBadArgument) error=query;
            }
            struct VIMSurfaceState identity={0};
            if(!error && VIMDrawableCacheEnabled() && VIMCGSState(d->updates,&identity)) {
                d->cache_object=identity.object;d->cache_incarnation=identity.incarnation;
            }
            VIMUnlockSurface(d->updates);
        }
    }
    VIMGLITrace("accelerator surface=%x error=%x size=%u,%u pitch=%u",ids[2],error,
        d->layout.width,d->layout.height,d->layout.row_bytes);
    if(error) { VIMDestroyDrawable(d);return kCGLBadDrawable; }
    d->surface=(int)ids[2];d->accelerator_surface=1;d->unflipped=unflipped;
    if(ids[2]==1 && c->primary_drawable &&
       d->unflipped==c->primary_drawable->unflipped &&
       d->region_view==c->primary_drawable->region_view &&
       d->width==c->primary_drawable->width && d->height==c->primary_drawable->height &&
       !memcmp(&c->primary_drawable->layout,&d->layout,sizeof(d->layout)) &&
       VIMDrawableBindingCompatible(c->primary_drawable,d)) {
        VIMAdoptDrawableBinding(c->primary_drawable,d);
        VIMRecycleDrawable(c,d);d=c->primary_drawable;
        CGLError e=VIMSetPrivateDrawable(c,d->width,d->height,d->storage_key);
        if(e) return e;
        VIMReplaceDrawable(c,d);
        void (*external)(GLboolean)=(void *)DGGetProcAddress("DGSetExternalPresent");
        if(external && VIMBindGLI((GLIContext)c)) external(GL_TRUE);
        return VIMImportAcceleratorBacking(c,d);
    }
    if(ids[2]!=1 && c->drawable && c->drawable->accelerator_surface && c->drawable->surface==(int)ids[2] &&
       c->drawable->unflipped==unflipped &&
       c->drawable->width==d->width && c->drawable->height==d->height &&
       c->drawable->region_view==d->region_view && c->drawable->gpu_family_root==d->gpu_family_root &&
       !memcmp(&c->drawable->layout,&d->layout,sizeof(d->layout)) &&
       VIMDrawableBindingCompatible(c->drawable,d)) {
        VIMAdoptDrawableBinding(c->drawable,d);VIMRecycleDrawable(c,d);return VIMImportAcceleratorBacking(c,c->drawable);
    }
    if(VIMDrawableCacheEnabled()) {
        struct VIMCGSDrawable **link=&c->spare_drawables;
        while(*link) {
            struct VIMCGSDrawable *cached=*link;
            if(!VIMSameDrawableView(cached,d)){link=&cached->cache_next;continue;}
            *link=cached->cache_next;cached->cache_next=NULL;
            __atomic_fetch_add(&vim_drawable_cache_stats[0],1,__ATOMIC_RELAXED);
            VIMAdoptDrawableBinding(cached,d);VIMRecycleDrawable(c,d);d=cached;
            CGLError e=VIMSetPrivateDrawable(c,d->width,d->height,d->storage_key);
            if(e){VIMDestroyDrawable(d);return e;}
            VIMReplaceDrawable(c,d);
            void (*external)(GLboolean)=(void *)DGGetProcAddress("DGSetExternalPresent");
            if(external && VIMBindGLI((GLIContext)c))external(GL_TRUE);
            return VIMImportAcceleratorBacking(c,d);
        }
    }
    // Prepared scatter/direct presentation writes guest backing directly.
    // Allocate the full-size heap readback only if a fallback or diagnostic
    // actually needs it, rather than on every changing primary drawing view.
    void *pixels=VIMAllocatePrivateStorageKey(d->width,d->height);
    if(!pixels) { VIMDestroyDrawable(d);return kCGLBadAlloc; }
    int prepared=vim_backing_render_scopes && !c->native_renderer_kind && !c->format->double_buffer &&
        unflipped && d->surface!=1 && !d->gpu_family_root && !d->region_view &&
        !d->origin_x && !d->origin_y && !d->layout.offset &&
        d->width==(int)d->layout.width && d->height==(int)d->layout.height;
    CGLError e=VIMSetPrivateDrawableMode(c,d->width,d->height,pixels,prepared);
    if(e) { VIMGLITrace("private drawable failed=%d prepared=%d size=%d,%d",e,prepared,d->width,d->height);free(pixels);VIMDestroyDrawable(d);return e; }
    d->storage_key=pixels;VIMReplaceDrawable(c,d);
    void (*external)(GLboolean)=(void *)DGGetProcAddress("DGSetExternalPresent");
    if(external && VIMBindGLI((GLIContext)c)) external(GL_TRUE);
    return VIMImportAcceleratorBacking(c,d);
}
static CGLError VIMCreateNativeWindowDrawable(struct VIMContext *c,const uint32_t *ids)
{
    if(!ids || !vim_native_bind_surface || !vim_native_accelerator) return kCGLBadDrawable;
    uint32_t record[6]={ids[0],ids[1],ids[2],0,0,0};
    // Actual 10J3250 Intel gldAttachDrawable masks its pixel-format mode by
    // 0x7f803f before the native bind callback: ARGB8888 window mode is 0x24,
    // with neither later double-buffer nor GPU-companion bits. Passing the
    // Mavericks 0x824 here makes Snow's native CGXBindSurface fail before a
    // client can draw. Keep the measured later-OS contract unchanged.
#if VIM_GLI_DISPATCH_COUNT <= 764
    // Leopard's actual 9L30 native driver masks by 0x803f in both widths
    // before this callback (x64 gldAttachDrawable+952, i386+997). Like Snow,
    // its ARGB8888 bind is 0x24: later buffer/companion bits are not allowed.
    uint32_t flags=0x24;
#else
    // Physical ARGB8888: single-buffer bind flags 0x824; double-buffer 0xc24.
    uint32_t flags=c->format->double_buffer ? 0xc24 : 0x824;
#endif
    IOReturn error=vim_native_bind_surface(ids[0],ids[1],&record[2],flags,vim_native_accelerator);
    VIMGLITrace("native window bind=%x surface=%x accelerator=%x",error,record[2],vim_native_accelerator);
    if(error) return kCGLBadDrawable;
    CGLError result=VIMCreateAcceleratorDrawable(c,record,0);
    if(!result) {
        c->drawable->native_window=1;c->drawable->connection=ids[0];c->drawable->window=ids[1];
    }
    return result;
}
/* CoreGraphics cglsTexImagePBuffer2D uses gliSetInteger(0x3e5), not the
 * public CGL IOSurface route. The stock 10.9 GLEngine decodes this eight-word
 * record as surface/target/internal/width/height/unused/flags/mipmaps and
 * imports BGRA/UNSIGNED_INT_8_8_8_8_REV. Implement that surface contract rather
 * than silently leaving an empty texture for compositor background blur. */
static CGLError VIMTextureFromAcceleratorSurface(struct VIMContext *c,unsigned operation,const uint32_t *a)
{
    if(!a[0] || (a[1]!=GL_TEXTURE_RECTANGLE_ARB && a[1]!=GL_TEXTURE_2D) ||
       (a[2]!=GL_RGBA && a[2]!=GL_RGBA8 && a[2]!=GL_RGB && a[2]!=GL_RGB8 && a[2]!=GL_RGBA16) ||
       !a[3] || !a[4] || a[3]>8192 || a[4]>8192) return kCGLBadValue;
    if(!VIMBindGLI((GLIContext)c)) return kCGLBadContext;
    // Measured native menu-bar transaction: 3e5, flags 404, source equals
    // the currently attached 1280x22 accelerator drawable. Earlier GL draws
    // are still on the GPU; importing its stale CPU shadow loses them.
    struct VIMCGSDrawable *source=c->drawable;
    /* Native14F27 Intel: 3e5/GL_BACK on an actual double-buffered window is
     * a LIVE destination texture, including unflushed writes and key300/1.
     * ImageKit uses this for subpixel text. A CPU/front-buffer snapshot loses
     * the just-drawn destination and produces corrupt text on resize. Do not
     * map GL_BACK to front on a single-buffer context (native returns zero). */
    if(operation==0x3e5 && a[6]==GL_BACK && !a[7] && source && source->accelerator_surface &&
       c->format->double_buffer && a[0]==(uint32_t)source->surface &&
       a[1]==GL_TEXTURE_RECTANGLE_ARB && a[3]<=(uint32_t)source->width &&
       a[4]<=(uint32_t)source->height && DGSupportsBackDrawableAliases()) {
        GLuint texture=VIMAppleTexture((GLIContext)c,a[1]);
        if(!DGBindBackDrawableTexture(a[2],a[3],a[4],!source->unflipped))return kCGLBadDrawable;
        VIMForgetCGSTextures((GLIContext)c,1,&texture);return kCGLNoError;
    }
    if(source && source->pixel_format==VIM_GL_PIXEL_RGBA16 && source->unflipped &&
       !c->format->double_buffer && a[0]==(uint32_t)source->surface &&
       a[1]==GL_TEXTURE_RECTANGLE_ARB && a[3]<=(uint32_t)source->width && a[4]<=(uint32_t)source->height &&
       (operation!=0x3e5 || ((a[6]==0x400 || a[6]==0x404) && !a[7]))) {
        GLuint texture=VIMAppleTexture((GLIContext)c,a[1]);
        if(!DGBindDrawableTexture(a[2],a[3],a[4]))return kCGLBadDrawable;
        VIMForgetCGSTextures((GLIContext)c,1,&texture);return kCGLNoError;
    }
    if((operation==0x3e6 || operation==0x3e7 || (operation==0x3e5 && (a[6]==0x400 || a[6]==0x404) && !a[7])) &&
       source && source->accelerator_surface && !source->unflipped &&
       !c->format->double_buffer && a[0]==(uint32_t)source->surface &&
       a[1]==GL_TEXTURE_RECTANGLE_ARB && a[3]<=(uint32_t)source->width &&
       a[4]<=(uint32_t)source->height && DGSupportsFlippedDrawableAliases()) {
        if((a[3]!=(uint32_t)source->width || a[4]!=(uint32_t)source->height) &&
           !DGSupportsCroppedFlippedDrawableAliases())return kCGLBadDrawable;
        /* Native Intel orders unflushed writes for five-word 3e6/3e7 and
         * front/no-mipmap 3e5. Opposite-row storage needs the existing
         * ordered GPU mirror, not the previous published surface version.
         * Actual 10K549 WindowServer uses 3e6 for the menu's surface; the
         * physical Intel crop/barrier fixture confirms the same live view. */
        /* Snow also imports primary surface 1 with a stack/menu-sized 3e7
         * record while that clipped view is attached (native Intel trace).
         * The backing was already imported with origin_x/y at attachment.
         * Alias that local drawable, not the full primary's CPU origin;
         * applying the screen offset again would be equally incorrect. */
        GLuint texture=VIMAppleTexture((GLIContext)c,a[1]);
        if(!DGBindFlippedDrawableTexture(a[2],a[3],a[4]))return kCGLBadDrawable;
        VIMForgetCGSTextures((GLIContext)c,1,&texture);return kCGLNoError;
    }
    if((operation==0x3e5 || operation==0x3e6 || operation==0x3e7) && source && source->accelerator_surface &&
       (source->surface==1 || source->gpu_family_root ||
        (!source->region_view && !source->origin_x && !source->origin_y &&
         source->width==(int)source->layout.width && source->height==(int)source->layout.height)) &&
       source->unflipped && !c->format->double_buffer && (a[0]==(uint32_t)source->surface || a[0]==source->gpu_family_root) &&
       (operation==0x3e6 || operation==0x3e7 || ((a[6]==0x400 || a[6]==0x404) && !a[7])) && a[1]==GL_TEXTURE_RECTANGLE_ARB &&
       a[3]<=(uint32_t)source->width && a[4]<=(uint32_t)source->height &&
       VIMGLProductFeature("VIM_RETAINED_CGS_SURFACES") && DGSupportsDrawableAliases()) {
        // A private self-import orders current queued drawing, even when
        // the kernel still holds an older immutable published version. The
        // physical Intel driver returns those unflushed writes for
        // 3e5, 3e6 and 3e7. Import the live current view, not the last flush's
        // snapshot. Primary views remain local, not full-screen CPU images.
        GLuint texture=VIMAppleTexture((GLIContext)c,a[1]);
        if(!DGBindDrawableTexture(a[2],a[3],a[4]))return kCGLBadDrawable;
        VIMForgetCGSTextures((GLIContext)c,1,&texture);return kCGLNoError;
    }
    if(operation==0x3e5 && source && source->accelerator_surface && source->unflipped &&
       !c->format->double_buffer && a[0]==(uint32_t)source->surface &&
       a[6]==0x404 && a[3]<=(uint32_t)source->width && a[4]<=(uint32_t)source->height &&
       !(VIMGLProductFeature("VIM_RETAINED_CGS_SURFACES") && DGSupportsSurfaceEpochs() && DGSupportsSurfaceHolds() &&
         a[1]==GL_TEXTURE_RECTANGLE_ARB && !a[7] && !source->region_view && !source->origin_x && !source->origin_y &&
         a[3]<=source->layout.width && a[4]<=source->layout.height &&
         ((a[3]==source->layout.width && a[4]==source->layout.height) || DGSupportsSurfaceViews())))
        return DGSnapshotDrawableTexture(a[1],a[2],a[3],a[4],a[7]) ? kCGLNoError : kCGLBadDrawable;
    io_connect_t connection=0;
    io_service_t service=VIMIOServiceForClass("VMsvga2Accel");
    IOReturn error=service ? IOServiceOpen(service,mach_task_self(),2,&connection) : kIOReturnNotFound;
    if(service) IOObjectRelease(service);
    uint64_t args[]={a[0],0x800};uint32_t record[11]={0};size_t size=sizeof(record);
    if(!error) error=IOConnectCallMethod(connection,kIOVM2DSetSurface,args,2,NULL,0,NULL,NULL,record,&size);
    struct VIMSurfaceMapping mapping;
    int locked=0;
    int retained_candidate=VIMGLProductFeature("VIM_RETAINED_CGS_SURFACES") && DGSupportsSurfaceEpochs() && DGSupportsSurfaceHolds() &&
        a[1]==GL_TEXTURE_RECTANGLE_ARB && (operation!=0x3e5 || ((a[6]==0x400 || a[6]==0x404) && !a[7]));
    if(!error) { error=retained_candidate?VIMLockReadTextureSurface(connection,&mapping):VIMLockSurface(connection,&mapping);locked=!error; }
    if(!error && mapping.layout.bits_per_pixel==64) {
        if(a[1]==GL_TEXTURE_RECTANGLE_ARB && a[3]==mapping.layout.width && a[4]==mapping.layout.height &&
           (operation!=0x3e5 || ((a[6]==0x400 || a[6]==0x404) && !a[7])))
            return VIMImportCGSTexture(c,a,connection,&mapping);
        // The compatibility CPU codec below is BGRA8-only. An unsupported
        // typed view must not reinterpret its eight-byte pixels as four.
        VIMUnlockSurface(connection);IOServiceClose(connection);return kCGLBadDrawable;
    }
    if(!error && VIMGLProductFeature("VIM_RETAINED_CGS_SURFACES") && DGSupportsSurfaceEpochs() && DGSupportsSurfaceHolds() &&
       a[1]==GL_TEXTURE_RECTANGLE_ARB && a[3]<=mapping.layout.width && a[4]<=mapping.layout.height &&
       ((a[3]==mapping.layout.width && a[4]==mapping.layout.height) || (a[0]!=1 && DGSupportsSurfaceViews())) &&
       (operation!=0x3e5 || ((a[6]==0x400 || a[6]==0x404) && !a[7]))) {
        // Physical 3e5 and 3e7 both retain a live alias, including when
        // importing the current unflipped drawable before glFlush. Publish
        // earlier GPU commands first, without reading back stale CPU pixels.
        // A cropped primary view is not a full-resource publication.
        if(mapping.address && source && source->accelerator_surface && source->unflipped &&
           !c->format->double_buffer && a[0]==(uint32_t)source->surface &&
           !source->region_view && !source->origin_x && !source->origin_y &&
           source->width==(int)mapping.layout.width && source->height==(int)mapping.layout.height) {
            struct VIMSurfaceState state={0};
            int good=VIMCGSState(connection,&state);
            if(good && source->gpu_lease && (source->gpu_object!=state.object || source->gpu_incarnation!=state.incarnation)) {
                DGReleaseSurface(source->gpu_lease);source->gpu_lease=0;
            }
            if(good && !source->gpu_lease) {
                source->gpu_lease=DGAcquireSurface(state.object,state.incarnation,source->width,source->height);
                source->gpu_object=state.object;source->gpu_incarnation=state.incarnation;
            }
            good=good && source->gpu_lease && DGPublishSurfaceEpoch(source->gpu_lease,0,0,state.content_epoch);
            if(!good) {
                VIMCGSFailure("ordered GPU import",VIMAppleTexture((GLIContext)c,a[1]),0,&state);
                VIMUnlockSurface(connection);IOServiceClose(connection);return kCGLBadDrawable;
            }
        }
        CGLError result=VIMImportCGSTexture(c,a,connection,&mapping);
        // The import owns/closes this connection, including its mapping.
        return result;
    }
    // A metadata-only acquisition must never enter the CPU import fallback.
    if(!error && !mapping.address) {
        VIMUnlockSurface(connection);locked=0;
        error=VIMLockSurface(connection,&mapping);locked=!error;
    }
    VIMGLITrace("texture op=%x surface=%x target=%x size=%u,%u flags=%x lock=%x",operation,a[0],a[1],a[3],a[4],operation==0x3e5?a[6]:0x1100,error);
    CGLError result=kCGLBadDrawable;
    if(!error && a[3]<=mapping.layout.width && a[4]<=mapping.layout.height && !(mapping.layout.row_bytes%4)) {
        /* Physical Mavericks samples both 3e5/404 and 3e7/1100 in the
         * resource's unflipped row order. Do not confuse a private surface
         * import with the ordinary GL window's flipped drawable coordinates.
         * GLEngine's color 3e6 route shares the same import implementation.
         */
        const unsigned char *pixels=(void *)(uintptr_t)(mapping.address+mapping.layout.offset);
        GLint saved[7];
        if(!DGGetUnpackSnapshot(saved)) {
            glGetIntegerv(GL_UNPACK_ROW_LENGTH,&saved[0]);glGetIntegerv(GL_UNPACK_ALIGNMENT,&saved[1]);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&saved[2]);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS,&saved[3]);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&saved[4]);
            glGetIntegerv(GL_UNPACK_SWAP_BYTES,&saved[5]);glGetIntegerv(GL_UNPACK_LSB_FIRST,&saved[6]);
        }
        GLint row=saved[0],alignment=saved[1],unpack=saved[2],skip_rows=saved[3],skip_pixels=saved[4],swap_bytes=saved[5],lsb_first=saved[6];
        GLint apple_rows=VIMClearAppleRows(0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,(GLint)(mapping.layout.row_bytes/4));
        glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glPixelStorei(GL_UNPACK_SWAP_BYTES,0);glPixelStorei(GL_UNPACK_LSB_FIRST,0);
        glTexImage2D(a[1],0,(GLint)a[2],(GLsizei)a[3],(GLsizei)a[4],0,
            GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,pixels);
        if(operation==0x3e5 && a[7]) glGenerateMipmapEXT(a[1]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,row);glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,skip_rows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,skip_pixels);
        glPixelStorei(GL_UNPACK_SWAP_BYTES,swap_bytes);glPixelStorei(GL_UNPACK_LSB_FIRST,lsb_first);
        VIMRestoreAppleRows(0,apple_rows);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack);
        result=kCGLNoError;
    }
    if(locked) VIMUnlockSurface(connection);
    if(connection) IOServiceClose(connection);
    return result;
}
static void VIMCopySurfaceRect(struct VIMCGSDrawable *d,const struct VIMSurfaceMapping *mapping,
    int x,int y,int width,int height)
{
    int right=x+width,bottom=y+height;
    if(x<0) x=0;if(y<0) y=0;
    if(right>d->width) right=d->width;if(bottom>d->height) bottom=d->height;
    /* readback may contain only the enclosing native visibility rectangle.
     * Convert its GL row interval to backing row order before intersecting. */
    int read_top=d->unflipped ? d->read_y : d->height-d->read_y-d->read_height;
    if(x<d->read_x) x=d->read_x;if(y<read_top) y=read_top;
    if(right>d->read_x+d->read_width) right=d->read_x+d->read_width;
    if(bottom>read_top+d->read_height) bottom=read_top+d->read_height;
    if(right<=x || bottom<=y) return;
    unsigned char *pixels=(void *)(uintptr_t)(mapping->address+mapping->layout.offset);
    for(int row=y;row<bottom;++row)
        memcpy(pixels+(size_t)(row+d->origin_y)*mapping->layout.row_bytes+(size_t)(x+d->origin_x)*4,
            // Orientation follows the measured attachment mode, not surface
            // ID. Native ordinary windows and type-5a targets share backing.
            d->readback+((size_t)((d->unflipped ? row : d->height-1-row)-d->read_y)*d->read_width+
                x-d->read_x)*4,(size_t)(right-x)*4);
}
static int VIMPrimaryClipCoversView(const struct VIMCGSDrawable *d)
{
    const struct VIMSurfaceClip *clip=d->clip;
    if(!d->region_view || !clip || clip->count!=1 || clip->version!=VIM_SURFACE_CLIP_VERSION ||
       clip->origin_x < -32768 || clip->origin_x>32767 || clip->origin_y < -32768 || clip->origin_y>32767)
        return 0;
    const struct VIMSurfaceRect *r=clip->rects;
    int64_t x=(int64_t)r->x-clip->origin_x-d->origin_x;
    int64_t y=(int64_t)r->y-clip->origin_y-d->origin_y;
    return r->w>0 && r->h>0 && x<=0 && y<=0 && x+r->w>=d->width && y+r->h>=d->height;
}
static void VIMPrimaryReadbackRect(struct VIMCGSDrawable *d)
{
    /* Use only the clip captured with this CGXGLAccelAccess transaction,
     * never a later mutable provider clip or guessed GL damage. Keep full
     * reads for ordinary windows, old peers, empty/invalid clips and explicit
     * paired diagnostics. The subsequent copies still honor each clip rect. */
    const struct VIMSurfaceClip *clip=d->clip;
    if(!d->region_view || !clip || !clip->count || clip->count>VIM_SURFACE_CLIP_RECTS ||
       clip->version!=VIM_SURFACE_CLIP_VERSION || clip->origin_x < -32768 || clip->origin_x>32767 ||
       clip->origin_y < -32768 || clip->origin_y>32767 ||
       !DGSupportsDrawableReadRects() || getenv("VIM_GL_FULL_READBACK")) return;
    int64_t left=d->width,top=d->height,right=0,bottom=0;
    for(unsigned i=0;i<clip->count;++i) {
        const struct VIMSurfaceRect *r=&clip->rects[i];
        if(r->w<=0 || r->h<=0) continue;
        int64_t x=(int64_t)r->x-clip->origin_x-d->origin_x;
        int64_t y=(int64_t)r->y-clip->origin_y-d->origin_y;
        int64_t end_x=x+r->w,end_y=y+r->h;
        if(x<0)x=0;if(y<0)y=0;
        if(end_x>d->width)end_x=d->width;if(end_y>d->height)end_y=d->height;
        if(end_x<=x || end_y<=y)continue;
        if(x<left)left=x;if(y<top)top=y;
        if(end_x>right)right=end_x;if(end_y>bottom)bottom=end_y;
    }
    if(right<=left || bottom<=top)return;
    d->read_x=(int)left;d->read_width=(int)(right-left);d->read_height=(int)(bottom-top);
    d->read_y=d->unflipped ? (int)top : d->height-(int)bottom;
}
static int VIMSingleClipReadback(struct VIMCGSDrawable *d)
{
    const struct VIMSurfaceClip *clip=d->clip;
    if(!d->region_view || !clip || clip->version!=VIM_SURFACE_CLIP_VERSION || clip->count!=1 ||
       clip->origin_x < -32768 || clip->origin_x>32767 || clip->origin_y < -32768 || clip->origin_y>32767)return 0;
    const struct VIMSurfaceRect *r=clip->rects;
    int64_t x=(int64_t)r->x-clip->origin_x-d->origin_x,y=(int64_t)r->y-clip->origin_y-d->origin_y;
    int64_t right=x+r->w,bottom=y+r->h;
    if(x<0)x=0;if(y<0)y=0;if(right>d->width)right=d->width;if(bottom>d->height)bottom=d->height;
    int top=d->unflipped?d->read_y:d->height-d->read_y-d->read_height;
    return r->w>0 && r->h>0 && right>x && bottom>y && x==d->read_x && y==top &&
        right-x==d->read_width && bottom-y==d->read_height;
}
static int VIMPlanPrimaryReadMask(struct VIMCGSDrawable *d)
{
    const struct VIMSurfaceClip *clip=d->clip;
    if(!d->region_view || !clip || clip->version!=VIM_SURFACE_CLIP_VERSION || clip->count<2 ||
       clip->count>VIM_SURFACE_CLIP_RECTS || clip->origin_x < -32768 || clip->origin_x>32767 ||
       clip->origin_y < -32768 || clip->origin_y>32767)return -1;
    if(!d->read_mask)d->read_mask=calloc(1,sizeof(*d->read_mask));
    if(!d->read_mask)return -1;
    unsigned used=0;
    for(unsigned i=0;i<clip->count;++i) {
        const struct VIMSurfaceRect *r=&clip->rects[i];
        if(r->w<=0 || r->h<=0)continue;
        int64_t x=(int64_t)r->x-clip->origin_x-d->origin_x;
        int64_t y=(int64_t)r->y-clip->origin_y-d->origin_y;
        int64_t right=x+r->w,bottom=y+r->h;
        if(x<0)x=0;if(y<0)y=0;if(right>d->width)right=d->width;if(bottom>d->height)bottom=d->height;
        if(right<=x || bottom<=y)continue;
        // Only the immutable Access transaction's captured clip is used.
        // Convert backing coordinates to the exact packed GPU read stream.
        int64_t gy=d->unflipped?y:d->height-bottom,gy_end=gy+bottom-y;
        if(x<d->read_x)x=d->read_x;
        if(right>d->read_x+d->read_width)right=d->read_x+d->read_width;
        if(gy<d->read_y)gy=d->read_y;
        if(gy_end>d->read_y+d->read_height)gy_end=d->read_y+d->read_height;
        if(right<=x || gy_end<=gy)continue;
        d->read_mask->rects[used++]=(struct DGReadMaskRect){(int)(x-d->read_x),(int)(gy-d->read_y),
            (int)(right-x),(int)(gy_end-gy)};
    }
    return DGPlanReadMask(d->read_width,d->read_height,d->read_mask->rects,used,
        d->read_mask->intervals,d->read_mask->spans,VIM_GL_MASK_MAX_SPANS);
}
static IOReturn VIMCopyPrimaryClip(struct VIMCGSDrawable *d,const struct VIMSurfaceMapping *mapping)
{
    if(!d->clip) d->clip=calloc(1,sizeof(*d->clip));
    if(!d->clip) return kIOReturnNoMemory;
    size_t bytes=sizeof(*d->clip);
    // Geometry and clip belong to one CGXGLAccelAccess transaction. A late
    // region can already describe a subsequent CPU operation, not this draw.
    IOReturn error=d->region_view ? kIOReturnSuccess :
        IOConnectCallStructMethod(d->updates,kIOVM2DGetSurfaceClip,NULL,0,d->clip,&bytes);
    /* Earlier research providers do not have the appended method. Keep their
     * existing behavior until the paired EFI is selected, not a guessed clip. */
    if(error==kIOReturnUnsupported || error==kIOReturnBadArgument) {
        VIMCopySurfaceRect(d,mapping,0,0,d->width,d->height);return kIOReturnSuccess;
    }
    if(error) return error;
    const struct VIMSurfaceClip *clip=d->clip;
    if(bytes!=sizeof(*clip) || clip->version!=VIM_SURFACE_CLIP_VERSION || clip->count>VIM_SURFACE_CLIP_RECTS ||
       clip->origin_x < -32768 || clip->origin_x>32767 || clip->origin_y < -32768 || clip->origin_y>32767)
        return kIOReturnBadArgument;
    if(d->clip_traces++<12) {
        VIMGLITrace("primary clip count=%u origin=%d,%d",clip->count,clip->origin_x,clip->origin_y);
        for(unsigned i=0;i<clip->count && i<8;++i) VIMGLITrace("clip[%u]=%d,%d %dx%d",i,
            clip->rects[i].x,clip->rects[i].y,clip->rects[i].w,clip->rects[i].h);
    }
    for(unsigned i=0;i<clip->count;++i) {
        const struct VIMSurfaceRect *r=&clip->rects[i];
        if(r->w>0 && r->h>0) VIMCopySurfaceRect(d,mapping,
            r->x-clip->origin_x-d->origin_x,r->y-clip->origin_y-d->origin_y,r->w,r->h);
    }
    return kIOReturnSuccess;
}
// Shared-memory patch path; diagnostic override does not persist.
static volatile int vim_shared_copy_patches=1;
static IOReturn VIMImportCPUSurfaceCopy(uint32_t surface,const IOAccelDeviceRegion *region,
    int32_t destination_x,int32_t destination_y)
{
    /* The physical Intel path shares the 2D/GL allocation. Our separate drawable must
     * receive just the completed copy, at this point in the command stream.
     * A full reimport here destroys earlier unflushed GL drawing elsewhere;
     * a deferred reimport at present also reorders subsequent GL drawing. */
    struct VIMContext *c=(void *)VIMPreviousGLI();
    struct VIMCGSDrawable *d=c?c->drawable:NULL;
    if(!d || !d->accelerator_surface || ((uint32_t)d->surface!=surface && d->gpu_family_root!=surface)) return kIOReturnSuccess;
    if(d->pixel_format)return kIOReturnUnsupported; // This 2D copy codec is BGRA8-only.
    if(!region || region->num_rects>4096 || !VIMBindGLI((GLIContext)c)) return kIOReturnBadArgument;
    struct VIMSurfaceMapping mapping;
    IOReturn error=VIMLockSurface(d->updates,&mapping);
    if(error) return error;
    if(!VIMDrawableMappingMatches(d,&mapping)) {
        VIMUnlockSurface(d->updates);return kIOReturnBadArgument;
    }
    int64_t dx=(int64_t)destination_x-region->bounds.x,dy=(int64_t)destination_y-region->bounds.y;
    if(vim_shared_copy_patches && d->shared_vram_active && d->surface==1 &&
       region->num_rects<=510) {
        if(!d->vram_rects)d->vram_rects=calloc(VIM_SURFACE_CLIP_RECTS,sizeof(*d->vram_rects));
        struct VIMSurfaceVRAM backing={0};unsigned count=0;int valid=d->vram_rects!=NULL;
        for(uint32_t i=0;valid && i<region->num_rects;++i){
            const IOAccelBounds *r=&region->rect[i];
            if(r->w<0 || r->h<0){valid=0;break;}
            int64_t x=(int64_t)r->x+dx,y=(int64_t)r->y+dy,right=x+r->w,bottom=y+r->h;
            if(x<0)x=0;if(y<0)y=0;
            if(right>d->width)right=d->width;if(bottom>d->height)bottom=d->height;
            if(right>x && bottom>y)d->vram_rects[count++]=(struct DGVRAMRect){(int)x,(int)y,(int)(right-x),(int)(bottom-y)};
        }
        if(valid && !count)return VIMUnlockSurfaceMode(d->updates,1,NULL);
        if(valid && VIMQuerySharedBacking(d,0,&backing,NULL) &&
           backing.row_bytes==mapping.layout.row_bytes && backing.state.object==d->vram_object &&
           backing.state.incarnation==d->vram_incarnation) {
            int imported=DGImportSharedDrawableRects(&backing,d->origin_x,d->origin_y,
                d->width,d->height,!d->unflipped,d->vram_rects,count);
            if(imported>=0){
                IOReturn unlocked=VIMUnlockSurfaceMode(d->updates,1,NULL);
                return unlocked?unlocked:imported?kIOReturnSuccess:kIOReturnError;
            }
            // Unsupported before touching storage: retain the exact old
            // per-rectangle upload, never reimport the whole drawing view.
        }
    }
    for(uint32_t i=0;i<region->num_rects;++i) {
        const IOAccelBounds *r=&region->rect[i];
        if(r->w<0 || r->h<0) { error=kIOReturnBadArgument;break; }
        int64_t x=(int64_t)r->x+dx,y=(int64_t)r->y+dy;
        int64_t right=x+r->w,bottom=y+r->h;
        if(x<0)x=0;if(y<0)y=0;
        if(right>d->width)right=d->width;if(bottom>d->height)bottom=d->height;
        if(right<=x || bottom<=y)continue;
        unsigned width=(unsigned)(right-x),height=(unsigned)(bottom-y);
        const void *pixels=(void *)(uintptr_t)(mapping.address+mapping.layout.offset+
            (uint64_t)(d->origin_y+y)*mapping.layout.row_bytes+(uint64_t)(d->origin_x+x)*4);
        int gpu_y=d->unflipped?(int)y:d->height-(int)bottom;
        if(!DGImportDrawablePatch(pixels,(int)x,gpu_y,(int)width,(int)height,
            mapping.layout.row_bytes,!d->unflipped)) { error=kIOReturnError;break; }
    }
    VIMUnlockSurface(d->updates);
    VIMGLITrace("2D copy coherence surface=%x rects=%u destination=%d,%d view=%d,%d %dx%d error=%x",
        surface,region->num_rects,destination_x,destination_y,d->origin_x,d->origin_y,d->width,d->height,error);
    return error;
}
static uint64_t vim_surface_namespace[2];
static pthread_once_t vim_surface_namespace_once=PTHREAD_ONCE_INIT;
static void VIMInitSurfaceNamespace(void)
{
    uuid_t value;
    if(VIMGetResourceNamespace(value))memcpy(vim_surface_namespace,value,16);
}
/* The successful compound operation has already sealed this exact epoch.
 * Only the kernel ownership transaction remains; no second native submission. */
static IOReturn VIMCommitSealedGPUFamily(struct VIMCGSDrawable *d,const struct VIMSurfaceState *state,uint64_t epoch)
{
    pthread_once(&vim_surface_namespace_once,VIMInitSurfaceNamespace);
    if(!(vim_surface_namespace[0]|vim_surface_namespace[1]))return kIOReturnError;
    struct VIMSurfaceMaterialize request={0};struct VIMSurfaceState after={0};
    memcpy(request.namespace_id,vim_surface_namespace,16);
    request.object=state->object;request.incarnation=state->incarnation;
    request.cpu_epoch=state->content_epoch;request.gpu_epoch=epoch;
    size_t size=sizeof(after);
    IOReturn error=IOConnectCallStructMethod(d->updates,kIOVM2DDeferSurface,&request,sizeof(request),&after,&size);
    if(!error && (size!=sizeof(after) || !(after.flags&VIM_SURFACE_STATE_GPU_NEWER) || after.content_epoch!=epoch))
        error=kIOReturnError;
    return error;
}
/* Optional GA pre-copy route. Eligibility derives from the locked provider's
 * geometry/state, not a WindowServer name or reusable CGS ID. Coordinates are
 * exactly the existing GA/kernel CopyRegion contract: source is framebuffer
 * space, destination is relative to this surface's mapped drawing view. */
static uint32_t VIMTryGPUFramebufferCopy(io_connect_t connection,uint32_t surface,
    const IOAccelDeviceRegion *region,int32_t destination_x,int32_t destination_y,IOReturn *status)
{
    struct VIMContext *c=(void *)VIMPreviousGLI();
    struct VIMCGSDrawable *d=c?c->drawable:NULL;
    if(!status || !d || (!d->gpu_family_root && (!c->primary_drawable || !c->primary_drawable->gpu_family_root)))return 0;
    struct VIMSurfaceGeometry geometry={0};size_t bytes=sizeof(geometry);
    IOReturn error=IOConnectCallStructMethod(connection,kIOVM2DGetSurfaceGeometry,NULL,0,&geometry,&bytes);
    if(error || bytes!=sizeof(geometry) || geometry.version!=VIM_SURFACE_GEOMETRY_VERSION || !geometry.reserved)return 0;
    /* This opcode is an IN-PLACE copy within one canonical display family.
     * It cannot source the framebuffer into an unrelated destination, or a
     * different family. Let the kernel's coherent framebuffer-read route
     * handle that contract instead of copying the destination onto itself. */
    uint32_t family=d->gpu_family_root?d->gpu_family_root:c->primary_drawable->gpu_family_root;
    if(geometry.reserved!=family)return 0;
    *status=kIOReturnBadArgument;
    if(!region || region->num_rects>DG_SURFACE_REGION_LIMIT || region->bounds.w<0 || region->bounds.h<0 ||
       geometry.x<0 || geometry.y<0 || !geometry.width || !geometry.height ||
       (uint64_t)geometry.x+geometry.width>geometry.source_width ||
       (uint64_t)geometry.y+geometry.height>geometry.source_height)return 1;
    if(!VIMBindGLI((GLIContext)c) || !DGSupportsSurfaceCopies() || !DGSupportsSurfacePatchImport()) {
        *status=kIOReturnUnsupported;return 1;
    }
    if(!d->gpu_regions)d->gpu_regions=calloc(DG_SURFACE_REGION_LIMIT,sizeof(*d->gpu_regions));
    if(!d->gpu_regions){*status=kIOReturnNoMemory;return 1;}
    int64_t dx=(int64_t)destination_x-region->bounds.x,dy=(int64_t)destination_y-region->bounds.y;
    unsigned count=0,n=region->num_rects?region->num_rects:1;
    for(unsigned i=0;i<n;++i) {
        const IOAccelBounds *r=region->num_rects?&region->rect[i]:&region->bounds;
        if(r->w<0 || r->h<0)return 1;
        int64_t x=r->x,y=r->y,right=x+r->w,bottom=y+r->h;
        if(x<0)x=0;if(y<0)y=0;if(x+dx<0)x=-dx;if(y+dy<0)y=-dy;
        if(right>geometry.source_width)right=geometry.source_width;
        if(bottom>geometry.source_height)bottom=geometry.source_height;
        if(right+dx>geometry.width)right=geometry.width-dx;
        if(bottom+dy>geometry.height)bottom=geometry.height-dy;
        if(right<=x || bottom<=y)continue;
        d->gpu_regions[count++]=(struct DGSurfaceRegion){(int)x,(int)y,(int)(right-x),(int)(bottom-y),
            geometry.x+(int)(x+dx),geometry.y+(int)(y+dy)};
    }
    if(!count){*status=kIOReturnSuccess;return 1;}
    struct VIMSurfaceMapping mapping;struct VIMSurfaceState state={0};uint64_t epoch=0;
    error=VIMLockTextureSurface(connection,&mapping);
    if(error){*status=error;return 1;}
    struct VIMCGSDrawable target={0};target.updates=connection;target.layout=mapping.layout;
    target.metadata_layout=!mapping.address;target.gpu_family_root=geometry.reserved;
    int good=target.layout.width==geometry.source_width && target.layout.height==geometry.source_height &&
        VIMPrepareGPUFamily(&target,&mapping,&state,&epoch) && epoch<UINT64_MAX;
    uint64_t next=epoch+1;
    /* An explicitly shared full drawable already sees this in-place copy.
     * Reimporting its own storage is invalid and would be a redundant blit.
     * Require exact storage lifetime, not just a reusable display-family ID.
     */
    if(good && d->gpu_family_direct && d->gpu_family_root==geometry.reserved &&
       (d->gpu_object!=target.gpu_object || d->gpu_incarnation!=target.gpu_incarnation))good=0;
    good=good && DGPublishSealedSurfaceRegions(target.gpu_lease,d->gpu_regions,count,0,0,epoch,next,1);
    /* A same-thread GA copy can occur between GL draws. Mirror only its
     * destination patches, never reimport the whole local drawing view. */
    for(unsigned i=0;good && !d->gpu_family_direct && d->gpu_family_root==geometry.reserved && i<count;++i) {
        const struct DGSurfaceRegion *r=&d->gpu_regions[i];
        int x=r->destination_x-d->origin_x,y=r->destination_y-d->origin_y;
        int right=x+r->width,bottom=y+r->height;
        if(x<0)x=0;if(y<0)y=0;if(right>d->width)right=d->width;if(bottom>d->height)bottom=d->height;
        if(right<=x || bottom<=y)continue;
        good=DGImportSurfacePatch(target.gpu_lease,next,d->origin_x+x,d->origin_y+y,right-x,bottom-y,
            x,d->unflipped?y:d->height-bottom,!d->unflipped);
    }
    if(good){error=VIMCommitSealedGPUFamily(&target,&state,next);good=!error;}
    else error=kIOReturnError;
    if(mapping.layout.version){IOReturn unlocked=VIMUnlockSurfaceMode(connection,1,NULL);if(unlocked){error=unlocked;good=0;}}
    if(target.gpu_lease)DGReleaseSurface(target.gpu_lease);
    *status=good?kIOReturnSuccess:error;
    (void)surface;return 1;
}
/* The native display flush, NOT glFlush, submits the retained scanout version.
 * Publish only the current composition clip into the full GPU family resource.
 * Its other pixels survive changing views and primary/companion attachments. */
static void VIMPublishGPUFamily(struct VIMContext *c,struct VIMCGSDrawable *d)
{
    if(!DGSupportsSurfaceRegionImport() || !DGSupportsSurfaceRegions() || !DGSupportsSurfaceVersionTextures()) {
        c->error=GL_INVALID_OPERATION;return;
    }
    pthread_once(&vim_surface_namespace_once,VIMInitSurfaceNamespace);
    if(!(vim_surface_namespace[0]|vim_surface_namespace[1]) || !d->clip ||
       d->clip->version!=VIM_SURFACE_CLIP_VERSION || d->clip->count>VIM_SURFACE_CLIP_RECTS) {
        c->error=GL_INVALID_OPERATION;return;
    }
    if(!d->gpu_regions)d->gpu_regions=calloc(DG_SURFACE_REGION_LIMIT,sizeof(*d->gpu_regions));
    if(!d->gpu_regions){c->error=GL_OUT_OF_MEMORY;return;}
    unsigned count=0;
    for(unsigned i=0;i<d->clip->count;++i) {
        const struct VIMSurfaceRect *r=&d->clip->rects[i];
        int64_t x=(int64_t)r->x-d->clip->origin_x-d->origin_x;
        int64_t y=(int64_t)r->y-d->clip->origin_y-d->origin_y;
        int64_t right=x+r->w,bottom=y+r->h;
        if(r->w<0 || r->h<0){c->error=GL_INVALID_OPERATION;return;}
        if(x<0)x=0;if(y<0)y=0;if(right>d->width)right=d->width;if(bottom>d->height)bottom=d->height;
        if(right<=x || bottom<=y)continue;
        if(count==DG_SURFACE_REGION_LIMIT){c->error=GL_INVALID_OPERATION;return;}
        int source_y=d->unflipped?(int)y:d->height-(int)bottom;
        d->gpu_regions[count++]=(struct DGSurfaceRegion){(int)x,source_y,(int)(right-x),(int)(bottom-y),
            d->origin_x+(int)x,d->origin_y+(int)y};
    }
    if(!count)return; // Empty visibility shape is not a whole-view overwrite.
    struct VIMSurfaceMapping mapping;struct VIMSurfaceState state={0};uint64_t epoch=0;
    if(VIMLockTextureSurface(d->updates,&mapping)){c->error=GL_INVALID_OPERATION;return;}
    int good=VIMPrepareGPUFamily(d,&mapping,&state,&epoch) && epoch<UINT64_MAX;
    uint64_t next=epoch+1;
    good=good && next>state.content_epoch &&
        DGPublishSealedSurfaceRegions(d->gpu_lease,d->gpu_regions,count,!d->unflipped,0,epoch,next,0) &&
        !VIMCommitSealedGPUFamily(d,&state,next);
    if(mapping.layout.version && VIMUnlockSurfaceMode(d->updates,1,NULL))good=0;
    if(!good)c->error=GL_INVALID_OPERATION;
}
// Same-process diagnostic override; the qualified negative cache defaults on.
// No environment lookup, new I/O, polling or logging on a cache hit.
static volatile int vim_defer_rejection_cache=1;
static int VIMTryDeferredPresent(struct VIMContext *c,struct VIMCGSDrawable *d,int back)
{
    if(!VIMGLProductFeature("VIM_DEFERRED_CGS_SURFACES") || !DGSupportsSurfaceVersionTextures() ||
       !d->accelerator_surface || d->surface==1 || d->region_view || d->origin_x || d->origin_y ||
       d->width!=(int)d->layout.width || d->height!=(int)d->layout.height)return 0;
    pthread_once(&vim_surface_namespace_once,VIMInitSurfaceNamespace);
    if(!(vim_surface_namespace[0]|vim_surface_namespace[1]))return 0;
    struct VIMSurfaceMapping mapping;struct VIMSurfaceState state={0},after={0};
    if(VIMLockTextureSurface(d->updates,&mapping))return 0;
    int good=VIMCGSState(d->updates,&state) && state.content_epoch<UINT64_MAX &&
        mapping.layout.width==(uint32_t)d->width && mapping.layout.height==(uint32_t)d->height;
    if(good && vim_defer_rejection_cache &&
       VIMDeferWasUnsupported(&c->defer_rejections,state.object,state.incarnation)) {
        // Do not publish/seal a GPU version that this exact backing cannot
        // accept. The caller still performs its normal completed readback.
        if(VIMUnlockSurfaceMode(d->updates,1,NULL)) { c->error=GL_INVALID_OPERATION;return 1; }
        return 0;
    }
    if(good && d->gpu_lease && (d->gpu_object!=state.object || d->gpu_incarnation!=state.incarnation)) {
        DGReleaseSurface(d->gpu_lease);d->gpu_lease=0;
    }
    if(good && !d->gpu_lease) {
        d->gpu_lease=DGAcquireSurface(state.object,state.incarnation,d->width,d->height);
        d->gpu_object=state.object;d->gpu_incarnation=state.incarnation;
    }
    uint64_t epoch=state.content_epoch+1;
    good=good && d->gpu_lease && DGPublishSurfaceEpoch(d->gpu_lease,!d->unflipped,back,epoch) &&
        DGSealSurfaceVersion(d->gpu_lease,epoch);
    if(good) {
        struct VIMSurfaceMaterialize request={0};memcpy(request.namespace_id,vim_surface_namespace,16);
        request.object=state.object;request.incarnation=state.incarnation;
        request.cpu_epoch=state.content_epoch;request.gpu_epoch=epoch;
        size_t size=sizeof(after);
        IOReturn deferred=IOConnectCallStructMethod(d->updates,kIOVM2DDeferSurface,&request,sizeof(request),&after,&size);
        // Busy/readers, changed epochs and transport errors are transient:
        // never suppress their later retries. Only a real unsupported reply
        // is remembered, and never as successful GPU ownership transfer.
        if(deferred==kIOReturnUnsupported && vim_defer_rejection_cache)
            VIMDeferRememberUnsupported(&c->defer_rejections,state.object,state.incarnation);
        good=!deferred &&
            size==sizeof(after) && (after.flags&VIM_SURFACE_STATE_GPU_NEWER) && after.content_epoch==epoch;
    }
    IOReturn unlocked=VIMUnlockSurfaceMode(d->updates,1,NULL);
    if(unlocked) { c->error=GL_INVALID_OPERATION;return 1; }
    if(good)VIMRememberOrdinaryDrawable(c,d,&after);
    if(good && d->native_window)CGSFlushSurface(d->connection,d->window,d->surface,NULL);
    return good;
}
/* Preserve the existing completed/future -> retained-texture handoff regardless
 * of how the CPU backing was written. A coherent GPU write into pinned guest
 * RAM must not leave other contexts sampling an older retained GPU epoch.
 * GPU_PENDING explicitly names a FUTURE epoch, not CPU-current pixels. The
 * same native producer context orders its private materialization/publication;
 * kernel source waiters remain blocked until its actual completion event.
 */
static void VIMRetainCPUPublishedDrawable(struct VIMContext *c,struct VIMCGSDrawable *d,
    const struct VIMFramebufferLayout *layout,const struct VIMSurfaceState *committed)
{
    /* Our own completed/future publication advances the provider version
     * without invalidating this allocation's still-newer queued drawing. */
    if(!memcmp(layout,&d->layout,sizeof(*layout)))VIMRememberOrdinaryDrawable(c,d,committed);
    else memset(&d->imported_state,0,sizeof(d->imported_state));
    // Future publication/sealing already happened in the real producer
    // stream before its independent ticket was exposed. Do not republish
    // the current (possibly retargeted) drawable over that exact version.
    if(committed->flags&VIM_SURFACE_STATE_GPU_FUTURE_SEALED)return;
    if(c->error || !VIMGLProductFeature("VIM_RETAINED_CGS_SURFACES") || !DGSupportsSurfaceEpochs() ||
       d->region_view || d->origin_x || d->origin_y || d->width!=(int)layout->width ||
       d->height!=(int)layout->height || committed->version!=VIM_SURFACE_STATE_VERSION ||
       (committed->flags&VIM_SURFACE_STATE_CPU_WRITER))return;
    if(d->gpu_lease && (d->gpu_object!=committed->object || d->gpu_incarnation!=committed->incarnation)) {
        DGReleaseSurface(d->gpu_lease);d->gpu_lease=0;
    }
    if(!d->gpu_lease) {
        d->gpu_lease=DGAcquireSurface(committed->object,committed->incarnation,d->width,d->height);
        d->gpu_object=committed->object;d->gpu_incarnation=committed->incarnation;
    }
    if(!d->gpu_lease || !DGPublishSurfaceEpoch(d->gpu_lease,!d->unflipped,0,committed->content_epoch)){
        VIMGLITrace("present failure stage=retain-cpu surface=%x lease=%u object=%llu/%llu epoch=%llu flags=%x view=%dx%d",
            d->surface,d->gpu_lease,(unsigned long long)committed->object,(unsigned long long)committed->incarnation,
            (unsigned long long)committed->content_epoch,committed->flags,d->width,d->height);
        c->error=GL_INVALID_OPERATION;
    }
}
static int VIMTryVRAMPresent(struct VIMContext *c,struct VIMCGSDrawable *d)
{
    if(!d->shared_vram_active)return 0;
    if(!d->vram_rects)d->vram_rects=calloc(VIM_SURFACE_CLIP_RECTS,sizeof(*d->vram_rects));
    if(!d->vram_rects){c->error=GL_OUT_OF_MEMORY;return 1;}
    unsigned count=0;
    if(d->region_view || (d->surface==1 && getenv("VIM_CLIP_PRESENT"))) {
        const struct VIMSurfaceClip *clip=d->clip;
        if(!clip || clip->version!=VIM_SURFACE_CLIP_VERSION || clip->count>VIM_SURFACE_CLIP_RECTS){c->error=GL_INVALID_OPERATION;return 1;}
        for(unsigned i=0;i<clip->count;++i){
            const struct VIMSurfaceRect *r=&clip->rects[i];
            int64_t x=(int64_t)r->x-clip->origin_x-d->origin_x,y=(int64_t)r->y-clip->origin_y-d->origin_y;
            int64_t right=x+r->w,bottom=y+r->h;
            if(x<0)x=0;if(y<0)y=0;if(right>d->width)right=d->width;if(bottom>d->height)bottom=d->height;
            if(r->w<=0 || r->h<=0 || right<=x || bottom<=y)continue;
            d->vram_rects[count++]=(struct DGVRAMRect){(int)x,(int)y,(int)(right-x),(int)(bottom-y)};
        }
    } else d->vram_rects[count++]=(struct DGVRAMRect){0,0,d->width,d->height};
    struct VIMSurfaceMapping mapping;
    IOReturn locked=VIMLockSurface(d->updates,&mapping);
    if(locked){
        /* Failure-only ownership diagnostic. GetSurfaceState is restricted
         * to the actual owning 2D client; success distinguishes our own
         * unbalanced acquisition from another client's surface contention. */
        if(getenv("VIM_GLI_TRACE")){
            struct VIMSurfaceState held={0};int owned=VIMCGSState(d->updates,&held);
            VIMGLITrace("present failure stage=shared-lock surface=%x status=%x client_owned=%d object=%llu/%llu flags=%x",
                d->surface,locked,owned,(unsigned long long)held.object,(unsigned long long)held.incarnation,held.flags);
        }
        c->error=GL_INVALID_OPERATION;return 1;
    }
    struct VIMSurfaceVRAM backing={0};
    int queried=VIMQuerySharedBacking(d,0,&backing,NULL);
    int matches=queried&&VIMDrawableMappingMatches(d,&mapping);
    if(!queried || !matches ||
       backing.state.object!=d->vram_object || backing.state.incarnation!=d->vram_incarnation){
        VIMGLITrace("present failure stage=shared-identity surface=%x query=%d matches=%d kind=%d expected=%llu/%llu actual=%llu/%llu view=%dx%d layout=%ux%u/%u current=%ux%u/%u",
            d->surface,queried,matches,d->shared_memory_kind,(unsigned long long)d->vram_object,
            (unsigned long long)d->vram_incarnation,(unsigned long long)backing.state.object,
            (unsigned long long)backing.state.incarnation,d->width,d->height,
            d->layout.width,d->layout.height,d->layout.row_bytes,
            mapping.layout.width,mapping.layout.height,mapping.layout.row_bytes);
        VIMUnlockSurface(d->updates);c->error=GL_INVALID_OPERATION;return 1;
    }
    int written=DGPublishVRAMDrawable(&backing,d->origin_x,d->origin_y,d->width,d->height,!d->unflipped,d->vram_rects,count);
    if(written<0){VIMUnlockSurface(d->updates);d->shared_vram_active=0;return 0;}
    /* A failed GPU operation may have changed a prefix. Notify QEMU and
     * advance CPU backing state even on failure, just like partial readback. */
    struct VIMSurfaceVRAM dirty={0};
    int notified=VIMQuerySharedBacking(d,1,&dirty,NULL);
    int good=written && notified &&
        dirty.state.object==backing.state.object && dirty.state.incarnation==backing.state.incarnation;
    struct VIMSurfaceState committed={0};
    IOReturn unlocked=VIMUnlockSurfaceMode(d->updates,0,&committed);if(unlocked)good=0;
    if(!good){
        VIMGLITrace("present failure stage=shared-commit surface=%x written=%d notified=%d unlock=%x before=%llu/%llu after=%llu/%llu epoch=%llu committed=%llu",
            d->surface,written,notified,unlocked,(unsigned long long)backing.state.object,
            (unsigned long long)backing.state.incarnation,(unsigned long long)dirty.state.object,
            (unsigned long long)dirty.state.incarnation,(unsigned long long)backing.state.content_epoch,
            (unsigned long long)committed.content_epoch);
        c->error=GL_INVALID_OPERATION;
    }
    if(d->surface!=1)VIMRetainCPUPublishedDrawable(c,d,&mapping.layout,&committed);
    if(d->native_window)CGSFlushSurface(d->connection,d->window,d->surface,NULL);
    return 1;
}
/* Caller has bound this context. Preserve both framebuffer selections and
 * copy only color, exactly as a double-buffered drawable swap does. */
static void VIMCopyDrawableBackToFront(struct VIMContext *c)
{
    GLint read_fb,draw_fb,read_buffer,draw_buffer;
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fb);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_fb);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER,0);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER,0);
    glGetIntegerv(GL_READ_BUFFER,&read_buffer);glGetIntegerv(GL_DRAW_BUFFER,&draw_buffer);
    glReadBuffer(GL_BACK);glDrawBuffer(GL_FRONT);glDisable(GL_SCISSOR_TEST);
    void (*blit)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum)=
        (void *)DGGetProcAddress("glBlitFramebuffer");
    blit(0,0,c->width,c->height,0,0,c->width,c->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glReadBuffer(read_buffer);glDrawBuffer(draw_buffer);
    if(scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER,(GLuint)read_fb);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER,(GLuint)draw_fb);
}
static void VIMPresentDrawableKnown(GLIContext opaque,int known_framebuffer,GLint draw_framebuffer)
{
    struct VIMContext *c=(struct VIMContext *)opaque;
    struct VIMCGSDrawable *d=c ? c->drawable : NULL;
    if(!d || !VIMBindGLI(opaque)) return;
    if(!known_framebuffer)glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_framebuffer);
    if(draw_framebuffer) return; /* An offscreen finish is not a window swap. */
    if(d->pixel_format){VIMPresentTypedPBuffer(c,d);return;}
    if(d->backing_render_active){VIMEndScopedDrawable(opaque);return;}
    if(d->gpu_family_root){VIMPublishGPUFamily(c,d);return;}
    int publish_back=0;
#if VIM_GLI_DISPATCH_COUNT == 764
    if(d->accelerator_surface && d->unflipped && d->surface>2 && c->format->double_buffer) {
        // Snow's CGLS pbuffer flush exposes its selected color buffer to
        // the texture consumer without an intervening swap. Window drawables
        // still publish front. A custom FBO has already returned above.
        GLint selected=GL_NONE;glGetIntegerv(GL_DRAW_BUFFER,&selected);
        publish_back=selected==GL_BACK || selected==GL_BACK_LEFT;
        if(publish_back){
            if(VIMTryDeferredPresent(c,d,1))return;
            // Old peers and unsupported deferred backings still use the
            // existing completed front-buffer readback after a GPU copy.
            VIMCopyDrawableBackToFront(c);
        }
    }
#endif
    if(VIMTryVRAMPresent(c,d))return;
    if(!publish_back && VIMTryDeferredPresent(c,d,0))return;
    d->read_x=d->read_y=0;d->read_width=d->width;d->read_height=d->height;
    int32_t damage[4]={0,0,d->width,d->height};
    if(d->accelerator_surface && d->surface==1 && getenv("VIM_GL_DAMAGE_PRESENT")) {
        if(!DGDrawableDamage(damage,1)) { c->error=GL_INVALID_OPERATION;return; }
        if(damage[0]<0 || damage[1]<0 || damage[2]<0 || damage[3]<0 ||
           (int64_t)damage[0]+damage[2]>d->width || (int64_t)damage[1]+damage[3]>d->height) {
            c->error=GL_INVALID_OPERATION;return;
        }
        if(!damage[2] || !damage[3]) return;
        VIMGLITrace("native GL damage=%d,%d %dx%d",damage[0],damage[1],damage[2],damage[3]);
    }
    if(d->accelerator_surface && d->surface==1)VIMPrimaryReadbackRect(d);
    int partial=d->read_x || d->read_y || d->read_width!=d->width || d->read_height!=d->height;
    int clipped=d->surface==1 && (d->region_view || getenv("VIM_CLIP_PRESENT"));
    int full_clip=!clipped || VIMPrimaryClipCoversView(d);
    int direct=d->accelerator_surface && d->unflipped && !partial && full_clip && !d->origin_x && !d->origin_y &&
        d->width==(int)d->layout.width && d->height==(int)d->layout.height &&
        d->layout.row_bytes==(uint64_t)d->width*4 && !getenv("VIM_GL_FULL_READBACK");
    int scatter=!direct && !d->scatter_unavailable && d->accelerator_surface && DGSupportsScatterReads() &&
        (DGSupportsRAMScatterReads() || getenv("VIM_GL_SCATTER_READBACK")) && !getenv("VIM_GL_DISABLE_SCATTER") &&
        !getenv("VIM_GL_FULL_READBACK") && !getenv("VIM_GL_DAMAGE_PRESENT");
    int mask_count=0;
    if(scatter && clipped && !VIMSingleClipReadback(d)) {
        if(DGSupportsMaskedScatterReads() && !getenv("VIM_GL_DISABLE_MASKED_SCATTER"))
            mask_count=VIMPlanPrimaryReadMask(d);
        if(mask_count<=0)scatter=0;
    }
    // Keep the existing short surface-lock interval on all fallback paths.
    if(!direct && !scatter) {
        if(!VIMEnsureReadback(d)) { c->error=GL_OUT_OF_MEMORY;return; }
        int read=partial ? DGReadDrawableRect(d->readback,d->read_x,d->read_y,d->read_width,d->read_height) :
            DGReadDrawable(d->readback,d->width,d->height);
        if(!read) { c->error=GL_INVALID_OPERATION;return; }
    }
    if(d->accelerator_surface) {
        struct VIMSurfaceMapping mapping;
        IOReturn error=VIMLockSurface(d->updates,&mapping);
        VIMGLITrace("surface present id=%x lock=%x",d->surface,error);
        if(error) { c->error=GL_INVALID_OPERATION;return; }
        if(VIMDrawableMappingMatches(d,&mapping)) {
            /* Canonical full unflipped targets need neither an intermediate
             * task heap image nor a second emulated row copy. Keep the lock
             * through receive/commit. For a primary drawing view, one clip
             * enclosing that entire view is required; never overwrite holes
             * containing CPU pixels with an enclosing GPU rectangle. */
            int needs_copy=!direct;
            if(direct) {
                int read=DGReadDrawable((void *)(uintptr_t)(mapping.address+mapping.layout.offset),d->width,d->height);
                if(!read)c->error=GL_INVALID_OPERATION;
                if(d->direct_present_traces++<4)VIMGLITrace("direct mapped present surface=%x size=%dx%d read=%d",
                    d->surface,d->width,d->height,read);
                // Commit below even after failure: receive may have written a prefix.
            } else if(scatter) {
                int top=d->unflipped?d->read_y:d->height-d->read_y-d->read_height;
                void *target=(void *)(uintptr_t)(mapping.address+mapping.layout.offset+
                    (uint64_t)(top+d->origin_y)*mapping.layout.row_bytes+(uint64_t)(d->read_x+d->origin_x)*4);
                int read=mask_count>0 ? DGReadDrawableRectMasked(target,mapping.layout.row_bytes,!d->unflipped,
                    d->read_x,d->read_y,d->read_width,d->read_height,d->read_mask->spans,(unsigned)mask_count) :
                    DGReadDrawableRectRows(target,mapping.layout.row_bytes,!d->unflipped,
                    d->read_x,d->read_y,d->read_width,d->read_height);
                needs_copy=read<0;
                if(getenv("VIM_GL_SCATTER_VERIFY")) {
                    // Diagnostic only: compare this adapter's own transfer
                    // with an independent ordinary GPU read, not a screen
                    // capture or an assumed native-driver contract.
                    static unsigned verified;
                    int mismatches=0,reference=0;
                    if(read==1 && VIMEnsureReadback(d)) {
                        reference=DGReadDrawableRect(d->readback,d->read_x,d->read_y,d->read_width,d->read_height);
                        if(reference && mask_count>0)for(int i=0;i<mask_count;++i) {
                            struct DGReadSpan span=d->read_mask->spans[i];
                            for(uint32_t offset=span.offset,remaining=span.bytes;remaining;) {
                                uint32_t row_bytes=(uint32_t)d->read_width*4,row=offset/row_bytes,column=offset%row_bytes;
                                uint32_t bytes=remaining<row_bytes-column?remaining:row_bytes-column;
                                uint32_t destination=d->unflipped?row:d->read_height-1-row;
                                if(memcmp((char *)target+(size_t)destination*mapping.layout.row_bytes+column,
                                    d->readback+offset,bytes))++mismatches;
                                offset+=bytes;remaining-=bytes;
                            }
                        }
                        if(reference && mask_count<=0)for(int row=0;row<d->read_height;++row) {
                            int source=d->unflipped?row:d->read_height-1-row;
                            if(memcmp((char *)target+(size_t)row*mapping.layout.row_bytes,
                               d->readback+(size_t)source*d->read_width*4,(size_t)d->read_width*4))++mismatches;
                        }
                    }
                    if(__sync_fetch_and_add(&verified,1)<32) {
                        char message[384];snprintf(message,sizeof(message),
                            "VIM scatter verify surface=%x flipped=%d origin=%d,%d view=%dx%d read=%d,%d %dx%d result=%d reference=%d mismatched_rows=%d mask_spans=%d",
                            d->surface,!d->unflipped,d->origin_x,d->origin_y,d->width,d->height,
                            d->read_x,d->read_y,d->read_width,d->read_height,read,reference,mismatches,mask_count);
                        VIMGLStderr("%s\n",message);syslog(LOG_ERR,"%s",message);
                    }
                    if(read==1 && (!reference || mismatches))c->error=GL_INVALID_OPERATION;
                }
                if(read<0) {
                    // This backing cannot use the prepared VRAM path. Do
                    // not retry expensive preparation on every flush, and
                    // restore the established read-before-lock fallback.
                    // No read command or pixel write was submitted yet.
                    d->scatter_unavailable=1;
                    if(VIMUnlockSurfaceMode(d->updates,1,NULL)) { c->error=GL_INVALID_OPERATION;return; }
                    if(!VIMEnsureReadback(d)) { c->error=GL_OUT_OF_MEMORY;return; }
                    read=partial?DGReadDrawableRect(d->readback,d->read_x,d->read_y,d->read_width,d->read_height):
                        DGReadDrawable(d->readback,d->width,d->height);
                    if(!read) { c->error=GL_INVALID_OPERATION;return; }
                    if(VIMLockSurface(d->updates,&mapping)) { c->error=GL_INVALID_OPERATION;return; }
                    if(!VIMDrawableMappingMatches(d,&mapping)) { needs_copy=0;read=0; }
                }
                if(!read)c->error=GL_INVALID_OPERATION;
            }
            if(needs_copy)
            {
            if(d->surface==1 && (d->region_view || getenv("VIM_CLIP_PRESENT"))) {
                if(VIMCopyPrimaryClip(d,&mapping)) c->error=GL_INVALID_OPERATION;
            } else VIMCopySurfaceRect(d,&mapping,damage[0],damage[1],damage[2],damage[3]);
            }
        } else c->error=GL_INVALID_OPERATION;
        struct VIMSurfaceState committed={0};
        IOReturn unlocked=VIMUnlockSurfaceMode(d->updates,0,&committed);
        if(unlocked) c->error=GL_INVALID_OPERATION;
        VIMRetainCPUPublishedDrawable(c,d,&mapping.layout,&committed);
        if(d->native_window) CGSFlushSurface(d->connection,d->window,d->surface,NULL);
        // WindowServer owns surface_flush/region presentation, not this context.
        return;
    }
    if(d->framebuffer) {
        for(int y=0;y<d->height;++y)
            memcpy((void *)(uintptr_t)(d->vram+d->layout.offset+(size_t)y*d->layout.row_bytes),
                d->readback+(size_t)(d->height-1-y)*d->width*4,(size_t)d->width*4);
        const uint32_t rect[]={0,0,(uint32_t)d->width,(uint32_t)d->height};
        IOReturn e=IOConnectCallStructMethod(d->updates,kIOVM2DUpdateFramebuffer,rect,sizeof(rect),NULL,NULL);
        if(e) c->error=GL_INVALID_OPERATION;
        return;
    }
    CGRect bounds;int depth=0,strides[4]={0};void *planes[4]={0};
    int e=CGSLockWindowBits(d->connection,d->backing,&bounds,&depth,planes,strides);
    int stride=strides[0];void *pixels=planes[0];
    VIMGLITrace("present lock=%d depth=%d stride=%d bits=%p bounds=%g,%g",e,depth,stride,pixels,bounds.size.width,bounds.size.height);
    if(e) { c->error=GL_INVALID_OPERATION;return; }
    d->locked=1;
    if(pixels && depth==4 && stride>=d->width*4 && bounds.size.height>=d->height) {
        for(int y=0;y<d->height;++y)
            memcpy((char *)pixels+(size_t)y*stride,
                d->readback+(size_t)(d->height-1-y)*d->width*4,(size_t)d->width*4);
        if(getenv("VIM_GLI_TRACE")) {
            const unsigned char *p=(unsigned char *)pixels+(size_t)(d->height/2)*stride+(size_t)(d->width/2)*4;
            VIMGLITrace("window CPU backing id=%u center BGRA=%u,%u,%u,%u",d->backing,p[0],p[1],p[2],p[3]);
        }
    } else c->error=GL_INVALID_OPERATION;
    CGSUnlockWindowBits(d->connection,d->backing,d->region);d->locked=0;
    CGSFlushSurface(d->connection,d->window,d->surface,NULL);
}
// Debugger-only same-boot A/B, not an environment-polled runtime option.
static volatile int vim_compound_flush=1;
static void VIMGLIFlushMode(GLIContext ctx,int finish,int render_only)
{
    struct VIMContext *c=(struct VIMContext *)ctx;
    if(!c)return;
    int publish_drawable=1;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    // Snow's CI renderer uses flush_render_APPLE between filter passes,
    // including while the primary contains only the background layers.
    // CGXGLAccelFinish ends that lease with ordinary Flush, not GL Finish.
    // Submit every render flush and imported-surface write, but publish the
    // primary shadow only at the ordinary Flush/Finish/Swap boundary. Never
    // suppress an offscreen or native-window backing update.
    if(render_only && c->drawable && c->drawable->accelerator_surface &&
       c->drawable->surface==1)publish_drawable=0;
#else
    (void)render_only;
#endif
    // Pure offscreen contexts and Snow's intermediate primary render flush
    // need neither window publication nor an imported IOSurface readback.
    // With no imported surfaces, the draw-FBO query has no consumer: keep
    // Flush asynchronous rather than round-tripping for unused metadata.
    // The ordinary primary boundary still publishes/awaits CPU visibility,
    // and Finish retains its actual completion wait even on this path.
    if((!c->drawable || !publish_drawable) &&
       !__atomic_load_n(&c->group->surface_count,__ATOMIC_ACQUIRE)){
        if(finish)VIMDispatch_finish(ctx);else VIMDispatch_flush(ctx);
        return;
    }
    struct VIMSavedBinding previous=VIMSaveGLI(ctx);if(!VIMBindGLI(ctx)){VIMDiscardGLI(previous);return;}
    uint32_t draw_framebuffer=0;
    if(__atomic_load_n(&c->group->surface_count,__ATOMIC_ACQUIRE)){
        struct DGIosurfaceWrite local[DG_IOSURFACE_INLINE_WRITES],*writes=local;unsigned count=0;
        pthread_mutex_lock(&c->group->lock);
        // Another shared context may retire the last import before this lock.
        // No command was sent in that case: continue through the normal flush,
        // rather than treating a zero-capacity observation as a failed flush.
        unsigned capacity=c->group->surface_count;
#if VIM_HAS_IOSURFACE
        writes=VIMSurfaceWriteBuffer(capacity,local);
        if(!writes){
            if(!c->error)c->error=GL_OUT_OF_MEMORY;
            pthread_mutex_unlock(&c->group->lock);VIMRestoreGLI(previous,ctx);
            if(finish)VIMDispatch_finish(ctx);else VIMDispatch_flush(ctx);return;
        }
#endif
        int observed=capacity?DGFlushIOSurfaceState(finish,&draw_framebuffer,writes,capacity,&count):-1;
        if(observed>=0){
#if VIM_HAS_IOSURFACE
            if(observed)VIMPublishIOSurfaceWritesLocked(ctx,writes,count);
#endif
            if(writes!=local)free(writes);
            pthread_mutex_unlock(&c->group->lock);
            VIMCheckBackendError(ctx,finish?"finish":"flush",0,0);
            VIMRestoreGLI(previous,ctx);
            if(observed && publish_drawable)VIMPresentDrawableKnown(ctx,1,(GLint)draw_framebuffer);
            return;
        }
        if(writes!=local)free(writes);
        pthread_mutex_unlock(&c->group->lock);
    }
    int result=vim_compound_flush?DGFlushDrawableState(finish,&draw_framebuffer):-1;
    if(result<0){
        VIMRestoreGLI(previous,ctx);
        if(finish)VIMDispatch_finish(ctx);else VIMDispatch_flush(ctx);
        if(publish_drawable)VIMPresentDrawableKnown(ctx,0,0);return;
    }
    if(!result){
        // The backend owns the one error; duplicating it here would make a
        // subsequent glGetError report the same failure twice.
        VIMRestoreGLI(previous,ctx);return;
    }
    // This helper changes only READ bindings and restores them. Its ordered
    // draw-FBO observation therefore also remains valid for presentation.
    VIMFlushSurfacesForFramebuffer(ctx,1,(GLint)draw_framebuffer);
    VIMCheckBackendError(ctx,finish?"finish":"flush",0,0);
    VIMRestoreGLI(previous,ctx);
    if(publish_drawable)VIMPresentDrawableKnown(ctx,1,(GLint)draw_framebuffer);
}
static void VIMGLIFlush(GLIContext ctx) { VIMGLIFlushMode(ctx,0,0); }
static void VIMGLIFinish(GLIContext ctx) { VIMGLIFlushMode(ctx,1,0); }
static void VIMGLIFlushRender(GLIContext ctx) { VIMGLIFlushMode(ctx,0,1); }
static void VIMGLIFinishRender(GLIContext ctx) { VIMGLIFlushMode(ctx,1,1); }
static void VIMGLISwap(GLIContext ctx)
{
    struct VIMContext *c=(void *)ctx;
    if(c->format->double_buffer && VIMBindGLI(ctx)) VIMCopyDrawableBackToFront(c);
    VIMGLIFinish(ctx);
}
