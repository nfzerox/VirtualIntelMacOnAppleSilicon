/* Mavericks CGL/GLI ABI adapter to native host OpenGL.
 * The target SDK owns the CGLContext/GLI ABI. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <CoreFoundation/CoreFoundation.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
#include <IOSurface/IOSurface.h>
#define VIM_HAS_IOSURFACE 1
#else
#define VIM_HAS_IOSURFACE 0
#endif
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "period-stderr.h"
#include <string.h>
#include <syslog.h>
#include <math.h>
#include "cgs-defer-rejections.h"
#include "direct-iosurface-writes.h"
#include "direct-native-renderers.h"
#include "iosurface-element-layout.h"

/* The GL transport backend owns host context/drawable lifetime. */
extern void DGReleaseBuffer(void *,void *);
extern int DGQueryRendererMemory(GLint *,GLint *);
extern int DGDrawableDamage(int32_t *,int);
extern int DGReadDrawable(void *,int,int);
extern int DGImportDrawable(const void *,int,int,size_t,int);
extern int DGSnapshotDrawableTexture(GLenum,GLint,int,int,int);
extern int DGImportIOSurfaceTexture(unsigned,int,int,const void *,size_t);
extern int DGImportIOSurfaceTextureFormat(unsigned,unsigned,unsigned,int,int,const void *,size_t);
extern int DGFlushIOSurfaceState(int,uint32_t *,struct DGIosurfaceWrite *,unsigned,unsigned *);
extern int DGReadIOSurfaceTextureElements(GLuint,uint64_t,unsigned,unsigned,void *,size_t,unsigned);
typedef void *DGContextHandle;
extern DGContextHandle DGCreateContext(const int *,DGContextHandle);
extern GLboolean DGMakeCurrent(DGContextHandle,void *,GLenum,GLsizei,GLsizei);
extern GLboolean DGMakeCurrentPrepared(DGContextHandle,void *,GLsizei,GLsizei);
extern void DGDestroyContext(DGContextHandle);
extern void (*DGGetProcAddress(const char *))(void);
extern void DGPixelStore(GLint,GLint);
extern GLenum DGGetVertexAttribPointer(void *,GLuint,GLenum,void **);
#define DG_CONTEXT_FORMAT 0x22
#define DG_CONTEXT_DEPTH_BITS 0x30
#define DG_CONTEXT_STENCIL_BITS 0x31
#define DG_CONTEXT_PROFILE 0x33
#define DG_CONTEXT_COMPAT_PROFILE 0x35
#define DG_CONTEXT_CONTEXT_MAJOR_VERSION 0x36
#define DG_CONTEXT_CONTEXT_MINOR_VERSION 0x37
#define DG_CONTEXT_ROW_LENGTH 0x10
#define DG_CONTEXT_VIM_DOUBLEBUFFER 0x56494d01
#define DG_CONTEXT_VIM_PROFILE 0x56494d02

struct _CGLPixelFormatObject { unsigned refs; GLint depth,stencil; int accelerated,double_buffer;CGLOpenGLProfile profile; };
struct VIMSurface {
    GLuint texture;
    GLenum target,internal,format,type;
    unsigned element_bytes,plane;
    GLsizei width,height;
    void *surface; // retained CF object; never populated before IOSurface exists
    uint32_t seed;
    uint64_t gpu_serial;
    struct VIMSurface *next;
};
struct VIMAppleObject;
struct VIMCGSTexture;
struct VIMShareGroup { unsigned refs,surface_count,cgs_count; struct VIMSurface *surfaces; struct VIMCGSTexture *cgs_textures; struct VIMAppleObject *objects; pthread_mutex_t lock; uint64_t framebuffer_epoch __attribute__((aligned(8))); };
struct VIMFramebufferAttachment { GLuint framebuffer,texture;uint64_t epoch;int valid; };
struct VIMPixelTransfer { GLint length,alignment;int pack,changed; };
struct VIMCGSDrawable;
struct VIMAppleFence;
struct VIMContext {
    struct _CGLContextObject api; /* Must remain first: real published layout. */
    DGContextHandle backend;
    CGLPixelFormatObj format;
    struct VIMShareGroup *group;
    struct VIMFramebufferAttachment attachments[32];
    pthread_mutex_t lock;
    unsigned refs;
    unsigned binding_refs,retired; /* CPU metadata only; never retains GPU work. */
    unsigned binding_uses;
    pthread_mutex_t binding_guard;
    pthread_cond_t binding_idle;
    unsigned char *buffer;
    int width,height,rowbytes,owns_buffer,private_drawable;
    GLenum error;
    uint64_t unsupported[16];
    uintptr_t client_storage;
    GLint plugin_policy,swap_interval,display_lists,mp_swaps,swap_limit;
    struct VIMCGSDrawable *drawable;
    struct VIMCGSDrawable *primary_drawable;
    struct VIMCGSDrawable *spare_drawables;
#if defined(VIM_GLI_CONNECTION_REUSE_EXPERIMENT)
    mach_port_t idle_drawable_connections[2]; /* Unbound 2D clients, GLI-owned. */
    unsigned idle_drawable_connection_count;
#endif
    struct VIMDeferRejections defer_rejections;
    unsigned debug_initialized,debug_messages;
    GLuint next_fence;
    struct VIMAppleFence *fences;
    unsigned inside_begin,diagnostic_checks,diagnostic_errors;
    unsigned diagnostic_textures,diagnostic_texture_events;
    unsigned native_renderer_kind,native_renderer_screens,native_renderer_kinds;
    GLint pack_row_bytes,unpack_row_bytes,unpack_client_storage,transform_hint;
};
static void VIMGetVertexAttribPointer(GLIContext opaque,GLuint index,GLenum pname,void **out)
{
    struct VIMContext *c=(void *)opaque;
    GLenum error=c->inside_begin?GL_INVALID_OPERATION:DGGetVertexAttribPointer(c->backend,index,pname,out);
    if(error && !c->error)c->error=error;
}
#include "period-thread-storage.h"
#if VIM_PERIOD_PTHREAD_TLS
struct VIMThreadBinding {CGLContextObj current;DGContextHandle backend;GLIContext bound;};
VIM_DEFINE_PERIOD_STORAGE(struct VIMThreadBinding,vim_thread)
#define current (vim_thread_storage()->current)
#define backend_current (vim_thread_storage()->backend)
#define bound (vim_thread_storage()->bound)
#else
static __thread CGLContextObj current;
static __thread DGContextHandle backend_current;
static __thread GLIContext bound;
#endif
static pthread_key_t vim_binding_key;
static pthread_once_t vim_binding_once=PTHREAD_ONCE_INIT;
static void VIMReleaseBindingMetadata(void *opaque)
{
    struct VIMContext *c=opaque;
    if(c && !__sync_sub_and_fetch(&c->binding_refs,1)) {
        pthread_cond_destroy(&c->binding_idle);
        pthread_mutex_destroy(&c->binding_guard);
        pthread_mutex_destroy(&c->lock);free(c);
    }
}
static void VIMCreateBindingKey(void)
{ if(pthread_key_create(&vim_binding_key,VIMReleaseBindingMetadata))abort(); }
static void VIMCacheBinding(struct VIMContext *c)
{
    if(bound==(GLIContext)c)return;
    struct VIMContext *old=(void *)bound;
    if(c)__sync_add_and_fetch(&c->binding_refs,1);
    bound=(GLIContext)c;backend_current=c?c->backend:NULL;
    pthread_setspecific(vim_binding_key,c);
    VIMReleaseBindingMetadata(old);
}
struct VIMSavedBinding { GLIContext context;int retained; };
static struct VIMSavedBinding VIMSaveGLI(GLIContext used)
{
    struct VIMSavedBinding saved={bound,bound && bound!=used};
    if(saved.retained)__sync_add_and_fetch(&((struct VIMContext *)bound)->binding_refs,1);
    return saved;
}
static void VIMDiscardGLI(struct VIMSavedBinding saved)
{ if(saved.retained)VIMReleaseBindingMetadata(saved.context); }
static void VIMEndBindingUse(struct VIMContext *c)
{
    if(!__sync_sub_and_fetch(&c->binding_uses,1) &&
       __atomic_load_n(&c->retired,__ATOMIC_ACQUIRE)) {
        pthread_mutex_lock(&c->binding_guard);
        pthread_cond_broadcast(&c->binding_idle);
        pthread_mutex_unlock(&c->binding_guard);
    }
}
static int VIMBeginBindingUse(struct VIMContext *c)
{
    __sync_add_and_fetch(&c->binding_uses,1);
    if(!__atomic_load_n(&c->retired,__ATOMIC_ACQUIRE))return 1;
    VIMEndBindingUse(c);return 0;
}
static pthread_once_t vim_dispatch_once=PTHREAD_ONCE_INIT;
static struct VIMContext *VIM(CGLContextObj c) { return (struct VIMContext *)c; }
#if defined(VIM_GLI_PLUGIN)
static int VIMEndScopedDrawable(GLIContext);
#endif
static int VIMEndPreviousGLI(void)
{
    struct VIMContext *previous=(void *)bound;if(!previous)return 1;
    /* GLI contexts are explicit. A cached thread binding does not own their
     * API lifetime: Core Image can delete one on another worker. Keep only
     * its small CPU record alive. A bounded active-use pin lets destruction
     * wait WITHOUT holding context/share-group locks around GPU work.
     * Same-context GL calls take no pin, mutex or reference increment. */
    int live=VIMBeginBindingUse(previous),good=1;
#if defined(VIM_GLI_PLUGIN)
    if(live)good=VIMEndScopedDrawable((GLIContext)previous);
#endif
    if(live)VIMEndBindingUse(previous);else VIMCacheBinding(NULL);
    return good;
}
static int VIMBindGLI(GLIContext opaque)
{
    struct VIMContext *c=(struct VIMContext *)opaque;
    if(!c) return 0;
    if(bound!=opaque || backend_current!=c->backend) {
#if defined(VIM_GLI_PLUGIN)
        // A different context must not inherit an auxiliary surface's CPU
        // exclusion. Publish/commit the previous owner before migration.
        if(!VIMEndPreviousGLI())return 0;
#endif
        if(!DGMakeCurrent(c->backend,c->buffer,GL_UNSIGNED_BYTE,c->width,c->height)) return 0;
        DGPixelStore(DG_CONTEXT_ROW_LENGTH,c->rowbytes/4);
        VIMCacheBinding(c);backend_current=c->backend;
    }
    if(!c->debug_initialized) {
        c->debug_initialized=1;
        c->diagnostic_textures=getenv("VIM_GLI_TRACE_TEXTURES")!=NULL;
        if(getenv("VIM_GLI_CHECK_ERRORS")) {
            c->diagnostic_checks=1;
        }
    }
    return 1;
}
static GLenum VIMTakeError(GLIContext opaque)
{
    struct VIMContext *c=(struct VIMContext *)opaque;
    GLenum e=c->error; c->error=0; return e;
}
static GLIContext VIMPreviousGLI(void)
{
    return bound && !__atomic_load_n(&((struct VIMContext *)bound)->retired,__ATOMIC_ACQUIRE)?bound:NULL;
}
static void VIMRestoreGLI(struct VIMSavedBinding saved,GLIContext used)
{
    GLIContext previous=saved.context;
    if(previous==used)return;
    if(previous) {
        struct VIMContext *c=(void *)previous;
        if(VIMBeginBindingUse(c)){VIMBindGLI(previous);VIMEndBindingUse(c);}
    }
    else {
#ifndef VIM_GLI_PLUGIN
        DGMakeCurrent(NULL,NULL,GL_UNSIGNED_BYTE,0,0);VIMCacheBinding(NULL);
#endif
        /* GLI has explicit contexts, not CGL TLS. Keep the last backend binding
         * on this thread; the next wrapper always binds its own context.
         * Unbinding after every glVertex would put a flush on the hot path. */
    }
    VIMDiscardGLI(saved);
}
static void VIMUnsupported(GLIContext opaque,unsigned index)
{
    struct VIMContext *c=(struct VIMContext *)opaque;
    if(!c->error) c->error=GL_INVALID_OPERATION;
    if(!(c->unsupported[index/64] & (UINT64_C(1)<<(index%64)))) {
        c->unsupported[index/64] |= UINT64_C(1)<<(index%64);
        VIMGLStderr("VIM CGL: unsupported GLI slot %u\n",index);
        if(getenv("VIM_GLI_TRACE")) syslog(LOG_NOTICE,"VIM CGL: unsupported GLI slot %u",index);
    }
}
static void VIMFlushSurfaces(GLIContext opaque);
static void VIMFlushSurfacesForFramebuffer(GLIContext,int,GLint);
static void VIMFlushBeforeTextureDelete(GLIContext,GLsizei,const GLuint *);
static void VIMBindSurfaceTexture(GLIContext,GLenum,GLuint);
static void VIMBindSurfaceFramebuffer(GLIContext,GLenum,GLuint);
static void VIMForgetTextures(GLIContext opaque,GLsizei count,const GLuint *textures);
static void VIMForgetIOSurfaces(GLIContext,GLsizei,const GLuint *);
#if defined(VIM_PASSTHROUGH_OPENGL_BACKEND) && defined(VIM_GLI_PLUGIN)
static void VIMRefreshCGSTexture(GLIContext,GLenum,GLuint);
static void VIMForgetCGSTextures(GLIContext,GLsizei,const GLuint *);
static void VIMDestroyCGSContext(struct VIMContext *);
static void VIMDestroyGLIDrawables(struct VIMContext *);
static GLuint VIMCGSBeginRedefine(GLIContext,GLenum,GLint);
static void VIMCGSEndRedefine(GLIContext,GLuint);
#endif
static void VIMForgetAppleObjects(GLIContext,GLenum,GLsizei,const GLuint *);
static int VIMPreparePixelTransfer(GLIContext,GLenum,GLenum,int,struct VIMPixelTransfer *);
static void VIMRestorePixelTransfer(const struct VIMPixelTransfer *);
static void VIMCheckBackendError(GLIContext opaque,const char *entry,unsigned argument,unsigned value)
{
    struct VIMContext *c=(void *)opaque;
    if(!c->diagnostic_checks || c->inside_begin) return;
    // The standalone CGL proof also exports a current-context glGetError.
    // Query the backend directly, not the wrapper's unrelated native-CGL TLS.
    GLenum (*raw_error)(void)=(void *)DGGetProcAddress("glGetError");
    GLenum error=raw_error();
    if(error) {
        if(!c->error) c->error=error;
        if(c->diagnostic_errors++<32) {
            VIMGLStderr("VIM GL error=%x after %s (%x,%x)\n",error,entry,argument,value);
            syslog(LOG_NOTICE,"VIM GL error=%x after %s (%x,%x)",error,entry,argument,value);
        }
    }
}
static int VIMTraceTextures(GLIContext opaque)
{
    struct VIMContext *c=(void *)opaque;
    return c->diagnostic_textures && c->diagnostic_texture_events++<1024;
}
static int VIMTraceMappings(GLIContext opaque)
{
    static unsigned emitted;
    if(!((struct VIMContext *)opaque)->diagnostic_checks)return 0;
    return __sync_fetch_and_add(&emitted,1)<64;
}
static void VIMInvalidateFramebufferAttachments(GLIContext opaque)
{
    // Native GL still validates every mutation. Even rejected mutations
    // invalidate conservatively; never infer successful attachment setters.
    struct VIMContext *c=(void *)opaque;
    if(c)__atomic_add_fetch(&c->group->framebuffer_epoch,1,__ATOMIC_RELEASE);
}
#include "passthrough-opengl-gli-generated.inc"
#include "apple-fence.c"
/* Internal surface transfers must not inherit a client's APPLE byte pitch,
 * which takes precedence over the ordinary GL row-length setting. */
static GLint VIMClearAppleRows(int pack)
{
    GLint value=0;
    GLenum key=pack ? GL_PACK_ROW_BYTES_APPLE : GL_UNPACK_ROW_BYTES_APPLE;
    glGetIntegerv(key,&value);glPixelStorei(key,0);
    return value;
}
static void VIMRestoreAppleRows(int pack,GLint value)
{
    glPixelStorei(pack ? GL_PACK_ROW_BYTES_APPLE : GL_UNPACK_ROW_BYTES_APPLE,value);
}
#include "apple-storage.c"

CGLError CGLChoosePixelFormat(const CGLPixelFormatAttribute *a,CGLPixelFormatObj *out,GLint *count)
{
    if(!out || !count) return kCGLBadAddress;
    *out=NULL; *count=0;
    CGLPixelFormatObj p=calloc(1,sizeof(*p));
    if(!p) return kCGLBadAlloc;
    p->refs=1;p->profile=kCGLOGLPVersion_Legacy;
    p->accelerated=1;
    for(unsigned i=0;a && a[i];++i) {
        switch(a[i]) {
        case kCGLPFADepthSize: p->depth=(GLint)a[++i]; break;
        case kCGLPFAStencilSize: p->stencil=(GLint)a[++i]; break;
        case kCGLPFAColorSize: if((int)a[++i]>32) goto unsupported; break;
        case kCGLPFAAlphaSize: if((int)a[++i]>8) goto unsupported; break;
        case kCGLPFAOpenGLProfile:
            p->profile=(CGLOpenGLProfile)a[++i];
            if(p->profile!=kCGLOGLPVersion_Legacy && p->profile!=kCGLOGLPVersion_3_2_Core)goto unsupported;
            break;
        case kCGLPFASamples: case kCGLPFASampleBuffers:
            if(a[++i]) goto unsupported; break;
        case kCGLPFAAccelerated: if(!p->accelerated) goto unsupported; break;
        case kCGLPFADoubleBuffer:p->double_buffer=1;break;
        case kCGLPFABackingStore:break; // Copy-swap retains the back buffer.
        case kCGLPFADisplayMask: ++i; break;
        case kCGLPFAAllowOfflineRenderers: case kCGLPFAAllRenderers:
        case kCGLPFANoRecovery: case kCGLPFAClosestPolicy:
        case kCGLPFAMinimumPolicy: case kCGLPFAMaximumPolicy:
        case kCGLPFASupportsAutomaticGraphicsSwitching: break;
        default: goto unsupported;
        }
    }
    *out=p; *count=1; return kCGLNoError;
unsupported:
    free(p); return kCGLBadPixelFormat;
}
CGLPixelFormatObj CGLRetainPixelFormat(CGLPixelFormatObj p) { if(p) __sync_add_and_fetch(&p->refs,1); return p; }
void CGLReleasePixelFormat(CGLPixelFormatObj p) { if(p && !__sync_sub_and_fetch(&p->refs,1)) free(p); }
GLuint CGLGetPixelFormatRetainCount(CGLPixelFormatObj p) { return p ? p->refs : 0; }
CGLError CGLDestroyPixelFormat(CGLPixelFormatObj p) { CGLReleasePixelFormat(p); return kCGLNoError; }
CGLError CGLDescribePixelFormat(CGLPixelFormatObj p,GLint screen,CGLPixelFormatAttribute a,GLint *v)
{
    if(!p || screen) return kCGLBadPixelFormat;
    if(!v) return kCGLBadAddress;
    switch(a) {
    case kCGLPFAAccelerated: *v=p->accelerated; break;
    case kCGLPFAColorSize: *v=24; break;
    case kCGLPFAAlphaSize: *v=8; break;
    case kCGLPFADepthSize: *v=p->depth; break;
    case kCGLPFAStencilSize: *v=p->stencil; break;
    case kCGLPFAVirtualScreenCount: case kCGLPFADisplayMask: *v=1; break;
    case kCGLPFAOpenGLProfile: *v=p->profile; break;
    case kCGLPFADoubleBuffer:*v=p->double_buffer;break;
    case kCGLPFABackingStore:*v=1;break;
    case kCGLPFASamples: case kCGLPFASampleBuffers: *v=0; break;
    default: return kCGLBadAttribute;
    }
    return kCGLNoError;
}
CGLError CGLCreateContext(CGLPixelFormatObj pf,CGLContextObj share,CGLContextObj *out)
{
    if(!out) return kCGLBadAddress;
    *out=NULL;
    if(!pf) return kCGLBadPixelFormat;
    if(share && (pf->profile==kCGLOGLPVersion_Legacy)!=(VIM(share)->format->profile==kCGLOGLPVersion_Legacy))
        return kCGLBadMatch;
    struct VIMContext *c=calloc(1,sizeof(*c));
    if(!c) return kCGLBadAlloc;
    int attrs[]={DG_CONTEXT_FORMAT,GL_RGBA,DG_CONTEXT_DEPTH_BITS,pf->depth,
        DG_CONTEXT_STENCIL_BITS,pf->stencil,DG_CONTEXT_PROFILE,DG_CONTEXT_COMPAT_PROFILE,
        DG_CONTEXT_CONTEXT_MAJOR_VERSION,2,DG_CONTEXT_CONTEXT_MINOR_VERSION,1,
        DG_CONTEXT_VIM_DOUBLEBUFFER,pf->double_buffer,
        DG_CONTEXT_VIM_PROFILE,pf->profile,
        0};
    c->backend=DGCreateContext(attrs,share ? VIM(share)->backend : NULL);
    if(!c->backend) { free(c); return kCGLBadAlloc; }
    c->buffer=calloc(64*64,4);
    if(!c->buffer) { DGDestroyContext(c->backend); free(c); return kCGLBadAlloc; }
    c->width=c->height=64; c->rowbytes=256; c->owns_buffer=1;
    c->format=CGLRetainPixelFormat(pf); c->refs=1;c->swap_limit=1;
    if(share) {
        c->group=VIM(share)->group;
        __sync_add_and_fetch(&c->group->refs,1);
    } else {
        c->group=calloc(1,sizeof(*c->group));
        if(!c->group) {
            DGDestroyContext(c->backend); free(c->buffer);
            CGLReleasePixelFormat(c->format); free(c); return kCGLBadAlloc;
        }
        c->group->refs=1;
        pthread_mutex_init(&c->group->lock,NULL);
    }
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma); pthread_mutexattr_settype(&ma,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&c->lock,&ma); pthread_mutexattr_destroy(&ma);
    c->api.rend=(GLIContext)c;
    c->binding_refs=1;
    pthread_mutex_init(&c->binding_guard,NULL);pthread_cond_init(&c->binding_idle,NULL);
    pthread_once(&vim_binding_once,VIMCreateBindingKey);
    pthread_once(&vim_dispatch_once,VIMResolveDispatch);
    VIMPopulateDispatch(&c->api.disp);
    c->api.disp.bind_texture=VIMBindSurfaceTexture;
    c->api.disp.bind_framebuffer_EXT=VIMBindSurfaceFramebuffer;
    c->api.disp.get_vertex_attrib_pointerv_ARB=VIMGetVertexAttribPointer;
    VIMInstallAppleTextureParameters(&c->api.disp);
    // Guest PIDs are accounting labels, not host PIDs. The host has no public
    // entry for this private ABI; keep metadata here instead of slot 972's
    // unsupported-error path, which the live compositor actually reaches.
#if VIM_GLI_HAS_RESPONSIBLE_PROCESS
    c->api.disp.label_object_with_responsible_process_APPLE=VIMAppleResponsible;
#endif
    *out=&c->api;
    return kCGLNoError;
}
CGLContextObj CGLGetCurrentContext(void) { return current; }
CGLError CGLSetCurrentContext(CGLContextObj ctx)
{
    if(ctx && !VIMBindGLI(ctx->rend)) return kCGLBadContext;
    if(!ctx) { DGMakeCurrent(NULL,NULL,GL_UNSIGNED_BYTE,0,0);VIMCacheBinding(NULL); }
    current=ctx;
    return kCGLNoError;
}
CGLContextObj CGLRetainContext(CGLContextObj c) { if(c) __sync_add_and_fetch(&VIM(c)->refs,1); return c; }
GLuint CGLGetContextRetainCount(CGLContextObj c) { return c ? VIM(c)->refs : 0; }
void CGLReleaseContext(CGLContextObj ctx)
{
    if(!ctx || __sync_sub_and_fetch(&VIM(ctx)->refs,1)) return;
    struct VIMContext *c=VIM(ctx);
    /* Do not hold this context while finishing a different thread binding. */
    if(bound!=ctx->rend) { VIMEndPreviousGLI();VIMCacheBinding(NULL); }
    pthread_mutex_lock(&c->binding_guard);
    __atomic_store_n(&c->retired,1,__ATOMIC_RELEASE);
    while(__atomic_load_n(&c->binding_uses,__ATOMIC_ACQUIRE))
        pthread_cond_wait(&c->binding_idle,&c->binding_guard);
    pthread_mutex_unlock(&c->binding_guard);
#ifdef VIM_GLI_PLUGIN
    VIMDestroyGLIDrawables(c);
#endif
    VIMFlushSurfaces(ctx->rend);
#if defined(VIM_PASSTHROUGH_OPENGL_BACKEND) && defined(VIM_GLI_PLUGIN)
    VIMDestroyCGSContext(c);
#endif
    VIMDestroyAppleFences(c);
    if(current==ctx) CGLSetCurrentContext(NULL);
    if(bound==ctx->rend)VIMCacheBinding(NULL);
    if(backend_current==c->backend) backend_current=NULL;
    DGDestroyContext(c->backend);
    if(!__sync_sub_and_fetch(&c->group->refs,1)) {
        struct VIMSurface *s=c->group->surfaces;
        while(s) { struct VIMSurface *next=s->next; CFRelease(s->surface); free(s); s=next; }
        VIMDestroyAppleObjects(c->group);
        pthread_mutex_destroy(&c->group->lock); free(c->group);
    }
    CGLReleasePixelFormat(c->format);
    if(c->owns_buffer) free(c->buffer);
    VIMReleaseBindingMetadata(c);
}
CGLError CGLDestroyContext(CGLContextObj c) { CGLReleaseContext(c); return kCGLNoError; }
CGLPixelFormatObj CGLGetPixelFormat(CGLContextObj c) { return c ? VIM(c)->format : NULL; }
CGLError CGLSetOffScreen(CGLContextObj ctx,GLsizei w,GLsizei h,GLint row,void *pixels)
{
    if(!ctx) return kCGLBadContext;
    if(!pixels || w<1 || h<1 || w>INT32_MAX/4 || row<w*4 || row%4) return kCGLBadValue;
    struct VIMContext *c=VIM(ctx);
    struct VIMSavedBinding previous=VIMSaveGLI(ctx->rend);
    if(!DGMakeCurrent(c->backend,pixels,GL_UNSIGNED_BYTE,w,h)) {VIMDiscardGLI(previous);return kCGLBadOffScreen;}
    void (*external)(GLboolean)=(void *)DGGetProcAddress("DGSetExternalPresent");
    if(external) external(GL_FALSE);
    DGPixelStore(DG_CONTEXT_ROW_LENGTH,row/4);
    if(c->owns_buffer) {
        DGReleaseBuffer(c->backend,c->buffer);
        free(c->buffer);
    }
    c->buffer=pixels; c->width=w; c->height=h; c->rowbytes=row; c->owns_buffer=0;c->private_drawable=0;
    VIMCacheBinding(c);backend_current=c->backend;
    VIMRestoreGLI(previous,ctx->rend);
    return kCGLNoError;
}
#ifdef VIM_GLI_PLUGIN
/* A GL-driver window target has GPU storage, not a client offscreen CPU image.
 * The direct backend only uses this stable cookie as drawable identity (it
 * never reads/writes DGContext.pixels). Do not allocate/zero width*height*4
 * guest bytes merely to manufacture that identity. Real CGLSetOffScreen above
 * retains its separate caller-memory contract. Pixel imports/readbacks use
 * the actual locked surface or explicit readback allocation, never this key. */
static CGLError VIMSetPrivateDrawableMode(struct VIMContext *c,GLsizei w,GLsizei h,void *key,int prepared)
{
    if(!c||!key||w<1||h<1||w>8192||h>8192)return kCGLBadValue;
    struct VIMSavedBinding previous=VIMSaveGLI(c->api.rend);
    if(!(prepared?DGMakeCurrentPrepared(c->backend,key,w,h):
         DGMakeCurrent(c->backend,key,GL_UNSIGNED_BYTE,w,h))){VIMDiscardGLI(previous);return kCGLBadDrawable;}
    DGPixelStore(DG_CONTEXT_ROW_LENGTH,w);
    if(c->owns_buffer){DGReleaseBuffer(c->backend,c->buffer);free(c->buffer);}
    c->buffer=key;c->width=w;c->height=h;c->rowbytes=w*4;c->owns_buffer=0;c->private_drawable=1;
    VIMCacheBinding(c);backend_current=c->backend;
    VIMRestoreGLI(previous,c->api.rend);return kCGLNoError;
}
static CGLError VIMSetPrivateDrawable(struct VIMContext *c,GLsizei w,GLsizei h,void *key)
{return VIMSetPrivateDrawableMode(c,w,h,key,0);}
#endif
CGLError CGLGetOffScreen(CGLContextObj ctx,GLsizei *w,GLsizei *h,GLint *row,void **pixels)
{
    if(!ctx) return kCGLBadContext;
    // Physical Mavericks Intel CGL returns success with no offscreen image
    // for an ordinary GPU window; never expose our private identity cookie.
    if(VIM(ctx)->private_drawable) {
        if(w)*w=0;if(h)*h=0;if(row)*row=0;if(pixels)*pixels=NULL;return kCGLNoError;
    }
    if(w) *w=VIM(ctx)->width; if(h) *h=VIM(ctx)->height;
    if(row) *row=VIM(ctx)->rowbytes; if(pixels) *pixels=VIM(ctx)->buffer;
    return kCGLNoError;
}
CGLError CGLLockContext(CGLContextObj c) { return c && !pthread_mutex_lock(&VIM(c)->lock) ? kCGLNoError : kCGLBadContext; }
CGLError CGLUnlockContext(CGLContextObj c) { return c && !pthread_mutex_unlock(&VIM(c)->lock) ? kCGLNoError : kCGLBadContext; }
CGLError CGLGetVirtualScreen(CGLContextObj c,GLint *screen) { if(!c) return kCGLBadContext; if(!screen) return kCGLBadAddress; *screen=0; return kCGLNoError; }
CGLError CGLSetVirtualScreen(CGLContextObj c,GLint screen) { return c && !screen ? kCGLNoError : kCGLBadDisplay; }
void CGLGetVersion(GLint *major,GLint *minor) { if(major) *major=1; if(minor) *minor=3; }
#ifndef VIM_GLI_PLUGIN
GLenum glGetError(void) { return current ? VIMDispatch_get_error(current->rend) : GL_NO_ERROR; }
#endif
const char *CGLErrorString(CGLError error)
{
    switch(error) { case kCGLNoError:return "no error"; case kCGLBadPixelFormat:return "unsupported pixel format";
        case kCGLBadAlloc:return "renderer allocation failed"; case kCGLBadContext:return "invalid context";
        case kCGLBadValue:return "invalid value"; case kCGLBadDrawable:return "unsupported drawable";
        default:return "unsupported CGL operation"; }
}
/* Initial correctness transport: upload CPU IOSurface bytes, then read GPU
 * completion back at Finish. This is explicitly NOT the final shared-backing
 * implementation; it establishes the CGL surface contract before a kext. */
#if VIM_HAS_IOSURFACE
CGLError CGLTexImageIOSurface2D(CGLContextObj ctx,GLenum target,GLenum internal,
    GLsizei width,GLsizei height,GLenum format,GLenum type,IOSurfaceRef surface,GLuint plane)
{
    if(!ctx) return kCGLBadContext;
    if(!surface)return kCGLBadValue;
    unsigned element_bytes=DGIOSurfaceTransferBytes(internal,format,type);
    int packed=DGIOSurfacePacked422(internal,format,type);
    // The period IOSurface plane APIs also cover ordinary non-planar storage
    // at plane zero. Never interpret a planar buffer's metadata header as pixels.
    size_t stride=IOSurfaceGetBytesPerRowOfPlane(surface,plane);
    if((target!=GL_TEXTURE_2D && target!=GL_TEXTURE_RECTANGLE_ARB) ||
       (element_bytes<4 && target!=GL_TEXTURE_RECTANGLE_ARB) ||
       !element_bytes || width<1 || height<1 || (packed&&(width&1)) ||
       (size_t)width>IOSurfaceGetWidthOfPlane(surface,plane) || (size_t)height>IOSurfaceGetHeightOfPlane(surface,plane) ||
       (!packed&&IOSurfaceGetBytesPerElementOfPlane(surface,plane)!=element_bytes) ||
       stride<(uint64_t)width*element_bytes || stride%element_bytes ||
       stride/element_bytes>INT32_MAX) return kCGLBadValue;
    struct VIMSavedBinding previous=VIMSaveGLI(ctx->rend);
    if(!VIMBindGLI(ctx->rend)){VIMDiscardGLI(previous);return kCGLBadContext;}
    GLint texture=0;
    glGetIntegerv(target==GL_TEXTURE_2D ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    if(!texture) { VIMRestoreGLI(previous,ctx->rend); return kCGLBadValue; }
    GLuint replacing_texture=(GLuint)texture;
    VIMFlushBeforeTextureDelete(ctx->rend,1,&replacing_texture);
    struct VIMSurface *s=calloc(1,sizeof(*s));
    if(!s) { VIMRestoreGLI(previous,ctx->rend); return kCGLBadAlloc; }
    if(IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL)) {
        free(s); VIMRestoreGLI(previous,ctx->rend); return kCGLBadDrawable;
    }
    int native=target==GL_TEXTURE_RECTANGLE_ARB?DGImportIOSurfaceTextureFormat(internal,format,type,width,height,
        IOSurfaceGetBaseAddressOfPlane(surface,plane),stride):-1;
    if(native<0){
        // Only an old peer/unsupported target needs generic GL pixel-store
        // conversion. The canonical native operation ignores and preserves
        // those states without synchronous save/restore queries.
        GLint row=0,alignment=0,unpack=0,skip_rows=0,skip_pixels=0;
        glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skip_rows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip_pixels);
        GLint apple_rows=VIMClearAppleRows(0);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,(GLint)(stride/element_bytes));
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glTexImage2D(target,0,(GLint)internal,width,height,0,format,type,IOSurfaceGetBaseAddressOfPlane(surface,plane));
        glPixelStorei(GL_UNPACK_ROW_LENGTH,row);glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,skip_rows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,skip_pixels);
        VIMRestoreAppleRows(0,apple_rows);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack);
    }
    s->seed=IOSurfaceGetSeed(surface);
    IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
    if(!native){free(s);VIMRestoreGLI(previous,ctx->rend);return kCGLBadDrawable;}
    s->texture=(GLuint)texture;s->target=target;s->internal=internal;s->width=width;s->height=height;
    s->format=format;s->type=type;s->element_bytes=element_bytes;s->plane=plane;
    s->surface=(IOSurfaceRef)CFRetain(surface);
    struct VIMShareGroup *group=VIM(ctx)->group;
    pthread_mutex_lock(&group->lock);
    int replacing=0;
    struct VIMSurface **cursor=&group->surfaces;
    while(*cursor) {
        if((*cursor)->texture==s->texture) {
            struct VIMSurface *old=*cursor; *cursor=old->next;
            CFRelease(old->surface); free(old); replacing=1;break;
        }
        cursor=&(*cursor)->next;
    }
    s->next=group->surfaces; group->surfaces=s;
    if(!replacing) __atomic_add_fetch(&group->surface_count,1,__ATOMIC_RELEASE);
    pthread_mutex_unlock(&group->lock);
    VIMRestoreGLI(previous,ctx->rend);
    return kCGLNoError;
}
/* CGLIOSurface.h requires live bindings at explicit texture/FBO rebinds.
 * The seed is shared IOSurface metadata, not a timer, kernel poll, or GPU
 * round trip. Only changed CPU backing incurs an upload. Caller holds the
 * share-group lock and has bound this renderer context. */
static void VIMRefreshSurface(struct VIMSurface *s)
{
    if(s->seed==IOSurfaceGetSeed(s->surface)) return;
    uint32_t seed=0;
    if(IOSurfaceLock(s->surface,kIOSurfaceLockReadOnly,&seed)) return;
    if(s->seed!=seed) {
        GLint texture;
        glGetIntegerv(s->target==GL_TEXTURE_2D ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
        glBindTexture(s->target,s->texture);
        int native=s->target==GL_TEXTURE_RECTANGLE_ARB?DGImportIOSurfaceTextureFormat(s->internal,s->format,s->type,s->width,s->height,
            IOSurfaceGetBaseAddressOfPlane(s->surface,s->plane),IOSurfaceGetBytesPerRowOfPlane(s->surface,s->plane)):-1;
        if(native<0){
            GLint row,alignment,unpack,skip_rows,skip_pixels;
            glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skip_rows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip_pixels);
            GLint apple_rows=VIMClearAppleRows(0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
            glPixelStorei(GL_UNPACK_ROW_LENGTH,(GLint)(IOSurfaceGetBytesPerRowOfPlane(s->surface,s->plane)/s->element_bytes));
            glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
            glTexSubImage2D(s->target,0,0,0,s->width,s->height,s->format,
                s->type,IOSurfaceGetBaseAddressOfPlane(s->surface,s->plane));
            glPixelStorei(GL_UNPACK_ROW_LENGTH,row);glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
            glPixelStorei(GL_UNPACK_SKIP_ROWS,skip_rows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,skip_pixels);
            VIMRestoreAppleRows(0,apple_rows);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack);
        }
        glBindTexture(s->target,(GLuint)texture);
        if(native)s->seed=seed;
        else if(!((struct VIMContext *)bound)->error)((struct VIMContext *)bound)->error=GL_INVALID_OPERATION;
    }
    IOSurfaceUnlock(s->surface,kIOSurfaceLockReadOnly,NULL);
}
#endif
static void VIMBindSurfaceTexture(GLIContext ctx,GLenum target,GLuint texture)
{
    VIMDispatch_bind_texture(ctx,target,texture);
#if defined(VIM_PASSTHROUGH_OPENGL_BACKEND) && defined(VIM_GLI_PLUGIN)
    VIMRefreshCGSTexture(ctx,target,texture);
#endif
#if VIM_HAS_IOSURFACE
    struct VIMShareGroup *group=((struct VIMContext *)ctx)->group;
    if(!texture || !__atomic_load_n(&group->surface_count,__ATOMIC_ACQUIRE)) return;
    struct VIMSavedBinding previous=VIMSaveGLI(ctx);if(!VIMBindGLI(ctx)){VIMDiscardGLI(previous);return;}
    pthread_mutex_lock(&group->lock);
    for(struct VIMSurface *s=group->surfaces;s;s=s->next)
        if(s->texture==texture && s->target==target) { VIMRefreshSurface(s);break; }
    pthread_mutex_unlock(&group->lock);VIMRestoreGLI(previous,ctx);
#endif
}
static void VIMBindSurfaceFramebuffer(GLIContext ctx,GLenum target,GLuint framebuffer)
{
    VIMDispatch_bind_framebuffer_EXT(ctx,target,framebuffer);
#if VIM_HAS_IOSURFACE
    struct VIMShareGroup *group=((struct VIMContext *)ctx)->group;
    if(!framebuffer || ((struct VIMContext *)ctx)->inside_begin ||
       !__atomic_load_n(&group->surface_count,__ATOMIC_ACQUIRE) ||
       (target!=GL_FRAMEBUFFER_EXT && target!=GL_READ_FRAMEBUFFER_EXT && target!=GL_DRAW_FRAMEBUFFER_EXT)) return;
    struct VIMSavedBinding previous=VIMSaveGLI(ctx);if(!VIMBindGLI(ctx)){VIMDiscardGLI(previous);return;}
    pthread_mutex_lock(&group->lock);
    struct VIMFramebufferAttachment *entry=&((struct VIMContext *)ctx)->attachments[framebuffer%32];
    uint64_t epoch=__atomic_load_n(&group->framebuffer_epoch,__ATOMIC_ACQUIRE);
    GLint name=0;
    if(entry->valid&&entry->framebuffer==framebuffer&&entry->epoch==epoch){
        // This name was confirmed bound by native GL, and no attachment or
        // object deletion has occurred since. A valid rebind cannot change
        // its attachment. Only this surface's shared seed needs checking.
        name=(GLint)entry->texture;
    }else{
        int changed=0;
        for(struct VIMSurface *s=group->surfaces;s;s=s->next)
            if(s->seed!=IOSurfaceGetSeed(s->surface)){changed=1;break;}
        if(!changed){pthread_mutex_unlock(&group->lock);VIMRestoreGLI(previous,ctx);return;}
        GLint type=0,actual=0;
        glGetIntegerv(target==GL_READ_FRAMEBUFFER_EXT?GL_READ_FRAMEBUFFER_BINDING:GL_DRAW_FRAMEBUFFER_BINDING,&actual);
        glGetFramebufferAttachmentParameterivEXT(target,GL_COLOR_ATTACHMENT0_EXT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,&type);
        if(type==GL_TEXTURE)
            glGetFramebufferAttachmentParameterivEXT(target,GL_COLOR_ATTACHMENT0_EXT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&name);
        // A failed Bind must not seed a cache under the rejected name.
        if((GLuint)actual==framebuffer&&epoch==__atomic_load_n(&group->framebuffer_epoch,__ATOMIC_ACQUIRE)){
            *entry=(struct VIMFramebufferAttachment){framebuffer,(GLuint)name,epoch,1};
        }
    }
    if(name)for(struct VIMSurface *s=group->surfaces;s;s=s->next)
        if(s->texture==(GLuint)name){VIMRefreshSurface(s);break;}
    pthread_mutex_unlock(&group->lock);VIMRestoreGLI(previous,ctx);
#endif
}
void glFinish(void)
{
    if(VIMFunction_finish) VIMFunction_finish();
    if(bound) VIMFlushSurfaces(bound);
}
void glFlush(void)
{
    if(VIMFunction_flush) VIMFunction_flush();
    if(bound) VIMFlushSurfaces(bound);
}
static void VIMFlushSurfaces(GLIContext opaque)
{ VIMFlushSurfacesForFramebuffer(opaque,0,0); }
/* Caller owns the group lock and has selected this backend context. Each
 * serial refers to a retained imported texture, not its former FBO name.
 * A completed serial is not copied again over subsequent CPU writes. Callers
 * must obey native CGL's producer-Flush-before-CPU-lock ownership contract. */
#if VIM_HAS_IOSURFACE
static struct DGIosurfaceWrite *VIMSurfaceWriteBuffer(unsigned capacity,struct DGIosurfaceWrite *local)
{
    if(capacity>DG_IOSURFACE_WRITE_LIMIT)return NULL;
    return capacity<=DG_IOSURFACE_INLINE_WRITES?local:malloc((size_t)capacity*sizeof(*local));
}
static void VIMPublishIOSurfaceWritesLocked(GLIContext opaque,const struct DGIosurfaceWrite *writes,unsigned count)
{
    struct VIMShareGroup *group=((struct VIMContext *)opaque)->group;
    for(unsigned i=0;i<count;++i)for(struct VIMSurface *s=group->surfaces;s;s=s->next){
        if(s->texture!=writes[i].texture||s->gpu_serial==writes[i].serial)continue;
        uint32_t seed=0;
        if(IOSurfaceLock(s->surface,0,&seed))break;
        if(DGReadIOSurfaceTextureElements(s->texture,writes[i].serial,s->width,s->height,
                   IOSurfaceGetBaseAddressOfPlane(s->surface,s->plane),IOSurfaceGetBytesPerRowOfPlane(s->surface,s->plane),s->element_bytes))
            s->gpu_serial=writes[i].serial;
        IOSurfaceUnlock(s->surface,0,NULL);
        s->seed=IOSurfaceGetSeed(s->surface);
        break;
    }
}
#endif
static void VIMFlushSurfacesForFramebuffer(GLIContext opaque,int known_framebuffer,GLint framebuffer)
{
#if VIM_HAS_IOSURFACE
    if(((struct VIMContext *)opaque)->inside_begin)return;
    struct VIMShareGroup *group=((struct VIMContext *)opaque)->group;
    pthread_mutex_lock(&group->lock);
    if(!group->surfaces) { pthread_mutex_unlock(&group->lock); return; }
    struct VIMSavedBinding previous=VIMSaveGLI(opaque);
    if(!VIMBindGLI(opaque)){VIMDiscardGLI(previous);pthread_mutex_unlock(&group->lock);return;}
    struct DGIosurfaceWrite local[DG_IOSURFACE_INLINE_WRITES],*writes=VIMSurfaceWriteBuffer(group->surface_count,local);
    if(!writes){
        if(!((struct VIMContext *)opaque)->error)((struct VIMContext *)opaque)->error=GL_OUT_OF_MEMORY;
        pthread_mutex_unlock(&group->lock);VIMRestoreGLI(previous,opaque);VIMDispatch_flush(opaque);return;
    }
    unsigned count=0;uint32_t binding=0;
    int result=DGFlushIOSurfaceState(0,&binding,writes,group->surface_count,&count);
    if(result>=0){
        if(result)VIMPublishIOSurfaceWritesLocked(opaque,writes,count);
        if(writes!=local)free(writes);
        pthread_mutex_unlock(&group->lock);VIMRestoreGLI(previous,opaque);return;
    }
    if(writes!=local)free(writes);
    /* Old peers retain the old current-FBO route. No command is replayed
     * after a transport error: only explicit lack of capability returns -1. */
    GLint name=0,type=0,previous_read=0,read_buffer=0;
    if(!known_framebuffer)glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);
    if(framebuffer) {
        glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0_EXT,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,&type);
        glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0_EXT,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&name);
    }
    int reading=0;
    for(struct VIMSurface *s=group->surfaces;type==GL_TEXTURE && s;s=s->next) {
        if(s->texture!=(GLuint)name) continue;
        if(!reading) {
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous_read);
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER,(GLuint)framebuffer);
            glGetIntegerv(GL_READ_BUFFER,&read_buffer);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);reading=1;
        }
        GLint row,alignment,pack,skip_rows,skip_pixels;
        glGetIntegerv(GL_PACK_ROW_LENGTH,&row); glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);
        glGetIntegerv(GL_PACK_SKIP_ROWS,&skip_rows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip_pixels);
        if(!IOSurfaceLock(s->surface,0,NULL)) {
            GLint apple_rows=VIMClearAppleRows(1);
            glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
            glPixelStorei(GL_PACK_ROW_LENGTH,(GLint)(IOSurfaceGetBytesPerRowOfPlane(s->surface,s->plane)/s->element_bytes));
            glPixelStorei(GL_PACK_ALIGNMENT,1);
            glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
            glReadPixels(0,0,s->width,s->height,
                s->format,s->type,IOSurfaceGetBaseAddressOfPlane(s->surface,s->plane));
            glPixelStorei(GL_PACK_ROW_LENGTH,row); glPixelStorei(GL_PACK_ALIGNMENT,alignment);
            glPixelStorei(GL_PACK_SKIP_ROWS,skip_rows);glPixelStorei(GL_PACK_SKIP_PIXELS,skip_pixels);
            VIMRestoreAppleRows(1,apple_rows);
            glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);
            IOSurfaceUnlock(s->surface,0,NULL);
            s->seed=IOSurfaceGetSeed(s->surface);
        }
    }
    if(reading) { glReadBuffer((GLenum)read_buffer);glBindFramebufferEXT(GL_READ_FRAMEBUFFER,(GLuint)previous_read); }
    pthread_mutex_unlock(&group->lock);VIMRestoreGLI(previous,opaque);
#else
    (void)opaque;(void)known_framebuffer;(void)framebuffer;
#endif
}
static void VIMFlushBeforeTextureDelete(GLIContext ctx,GLsizei count,const GLuint *textures)
{
    if(!ctx||count<1||!textures||((struct VIMContext *)ctx)->inside_begin)return;
    struct VIMShareGroup *group=((struct VIMContext *)ctx)->group;
    if(!__atomic_load_n(&group->surface_count,__ATOMIC_ACQUIRE))return;
    int affected=0;pthread_mutex_lock(&group->lock);
    for(struct VIMSurface *s=group->surfaces;s&&!affected;s=s->next)
        for(GLsizei i=0;i<count;++i)if(s->texture==textures[i]){affected=1;break;}
    pthread_mutex_unlock(&group->lock);if(affected)VIMFlushSurfaces(ctx);
}
static void VIMForgetTextures(GLIContext opaque,GLsizei count,const GLuint *textures)
{
    if(!opaque || count<1 || !textures) return;
    VIMForgetAppleObjects(opaque,GL_TEXTURE,count,textures);
#if defined(VIM_PASSTHROUGH_OPENGL_BACKEND) && defined(VIM_GLI_PLUGIN)
    VIMForgetCGSTextures(opaque,count,textures);
#endif
    VIMForgetIOSurfaces(opaque,count,textures);
}
static void VIMForgetIOSurfaces(GLIContext opaque,GLsizei count,const GLuint *textures)
{
    struct VIMShareGroup *group=((struct VIMContext *)opaque)->group;
    pthread_mutex_lock(&group->lock);
    struct VIMSurface **p=&group->surfaces;
    while(*p) {
        int remove=0;
        for(GLsizei i=0;i<count;++i) if((*p)->texture==textures[i]) { remove=1; break; }
        if(remove) {
            struct VIMSurface *old=*p; *p=old->next; CFRelease(old->surface); free(old);
            __atomic_sub_fetch(&group->surface_count,1,__ATOMIC_RELEASE);
        } else p=&(*p)->next;
    }
    pthread_mutex_unlock(&group->lock);
}
void glDeleteTextures(GLsizei n,const GLuint *textures)
{
    VIMFlushBeforeTextureDelete(bound,n,textures);
    if(VIMFunction_delete_textures) VIMFunction_delete_textures(n,textures);
    VIMForgetTextures(bound,n,textures);
}
