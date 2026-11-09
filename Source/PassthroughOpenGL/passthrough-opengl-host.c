/* Desktop OpenGL executor for the isolated PassthroughOpenGL proof. One serialized
 * interpreter owns every CGL context on a client connection. Shared guest contexts
 * create actual shared CGL contexts, not a second GL implementation. */
#if defined(VIM_BACKING_FUTURE_EXPERIMENT) && !defined(VIM_BACKING_ASYNC_EXPERIMENT)
#error Future backing requires VIM_BACKING_ASYNC_EXPERIMENT
#endif
#if defined(VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT) && \
    (!defined(VIM_VRAM_RENDER_TARGET_EXPERIMENT) || !defined(VIM_BACKING_CANONICAL_EXPERIMENT) || \
     !defined(VIM_BACKING_ASYNC_EXPERIMENT) || !defined(VIM_BACKING_FUTURE_EXPERIMENT) || \
     !defined(VIM_BACKING_WRITER_EXPERIMENT))
#error Combined canonical ownership requires scoped rendering, canonical storage, future versions and retained GPU writers
#endif
#if defined(VIM_BACKING_SCATTER_EXPERIMENT) && !defined(VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT)
#error Scatter materialization requires qualified canonical future ownership
#endif
#if defined(VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT) && !defined(VIM_BACKING_SCATTER_EXPERIMENT)
#error CPU-current initialization requires qualified scatter ownership
#endif
#if defined(VIM_BACKING_COALESCING_EXPERIMENT) && !defined(VIM_BACKING_SCATTER_EXPERIMENT)
#error CPU materialization coalescing requires retained scatter ownership
#endif
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLIOSurface.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include "iosurface-view-pitch-host.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <pthread.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <math.h>
#include "passthrough-opengl-shader-compile.h"
#include "passthrough-opengl-mipmap-host.h"
#include "passthrough-opengl-automatic-mipmap-host.h"
#include "pixel-format-host.h"
#include "passthrough-opengl-endian.h"
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
#include <mach/mach_time.h>
// Research-only attribution. Entirely compiled out of ordinary executors;
// metadata only, bounded process-wide, and never an animation-FPS probe.
static int dg_command_timing;
static mach_timebase_info_data_t dg_command_timebase;
static unsigned dg_command_timing_records;
static uint64_t DGCommandTimingBegin(void)
{return dg_command_timing ? mach_absolute_time() : 0;}
static unsigned DGCommandTimingEnd(const char *kind,uint32_t op,uint32_t context,
                               uint32_t bytes,uint64_t began)
{
    if(!began)return 0;
    uint64_t ended=mach_absolute_time();
    double us=(double)(ended-began)*dg_command_timebase.numer/dg_command_timebase.denom/1000.0;
    if(us<2000.0 || __atomic_load_n(&dg_command_timing_records,__ATOMIC_RELAXED)>=2048)return 0;
    unsigned serial=__sync_fetch_and_add(&dg_command_timing_records,1);
    if(serial>=2048)return 0;
    uint64_t tid=0;pthread_threadid_np(NULL,&tid);
    fprintf(stderr,"DG_COMMAND_SLOW serial=%u kind=%s op=%#x context=%u bytes=%u tid=%llu start_ticks=%llu end_ticks=%llu elapsed_us=%.0f\n",
        serial,kind,op,context,bytes,(unsigned long long)tid,
        (unsigned long long)began,(unsigned long long)ended,us);
    return serial+1;
}
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS
#include "../vendor/qemu/include/hw/display/vim-passthrough-opengl-inprocess.h"
#include "passthrough-opengl-memory-stream.h"
static __thread struct DGMemoryStream *dg_memory_host_stream;
struct DGInlineStream;
static __thread struct DGInlineStream *dg_inline_host_stream;
static int DGInlineHostTransfer(void *,size_t,int);
static ssize_t DGInlineHostWritev(const struct iovec *,int);
static int DGMemoryHostTransfer(void *buffer,size_t bytes,int writing)
{
    if(dg_inline_host_stream)return DGInlineHostTransfer(buffer,bytes,writing);
    unsigned char *p=buffer;if(!dg_memory_host_stream)return -1;
    while(bytes){ssize_t n=writing?DGMemoryServerWrite(dg_memory_host_stream,p,bytes):
        DGMemoryServerRead(dg_memory_host_stream,p,bytes);
        if(n<=0)return -1;p+=n;bytes-=(size_t)n;}
    return 0;
}
#define DG_MEMORY_HOST_TRANSFER DGMemoryHostTransfer
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_MACH_EXECUTOR
#include "passthrough-opengl-mach-stream.h"
static __thread struct DGMachStream *dg_mach_host_stream;
static int DGMachHostTransfer(void *buffer,size_t bytes,int writing)
{
    unsigned char *p=buffer;
    while(bytes){ssize_t n=writing?DGMachWrite(dg_mach_host_stream,p,bytes):DGMachRead(dg_mach_host_stream,p,bytes);
        if(n<=0)return -1;p+=n;bytes-=(size_t)n;}
    return 0;
}
#define DG_MEMORY_HOST_TRANSFER DGMachHostTransfer
#endif
#include "passthrough-opengl-wire.h"
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
#include "direct-command-profile-host.c"
#endif
#include "passthrough-opengl-surface-regions.h"
#include "direct-layout.h"
_Static_assert(sizeof(struct DGHelloFeatures)<=sizeof(DG_LAYOUT),"HELLO features fit in the layout span");
#include "direct-opcodes.h"
#include "private-textures-host.c"
/* These aliases apply only to renderer internals. Public GL commands resolve
 * their native functions separately and cannot delete our private objects. */
#define glDeleteTextures DGPrivateDeleteTextures
#define glDeleteFramebuffersEXT DGPrivateDeleteFramebuffers
#define glFramebufferTexture2DEXT DGPrivateAttachTexture
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
#include "direct-backing-scatter-host.h"
static uint64_t dg_scatter_stats[4]; // plans, completed copies, copied bytes, scoped admissions
static volatile int dg_backing_scatter=1; // Local same-process oracle only; eligible production semantics unchanged.
#ifdef VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT
static uint64_t dg_gather_stats[4]; // initialized resources, input bytes, pending refusals, native failures
#endif
#endif
struct DGHostContext {
    CGLContextObj cgl,stipple_query_context;GLuint framebuffer,color,back,depth;
    struct DGPrivateTextureWatch color_watch,back_watch,spare_watches[4];
    IOSurfaceRef color_storage,back_storage;
    GLint depth_bits,stencil_bits,double_buffer,client_storage;GLenum error,read_mode,draw_mode;
    unsigned profile,client_features;
    // Executor-owned native array enables, never speculative guest GL state.
    // VAO switches/deletes and client-stack restores invalidate this witness.
    uint64_t array_enable_mask,element_vbo_mask;
    unsigned array_enable_valid,primitive_restart;
    GLenum list_mode; /* Native witness refreshed only at NewList/EndList. */
    uint32_t element_vbo_limit;
    /* PPC ArrayElement consumes packed raw components captured before Begin.
     * No native buffer remains mapped and Intel arrays keep the native path. */
    struct { struct DGVertexArray array;unsigned char *data; } element_snapshots[DG_VERTEX_SLOTS];
    unsigned element_snapshot_count;
    uint32_t element_snapshot_first,element_snapshot_vertices;
    GLenum element_snapshot_error;
    struct { void *data;uint32_t words; } render_buffers[2];
    GLenum render_mode;
    const void *render_staged;
    uint32_t render_staged_words,render_stage_next;
    GLint native_screen_count,native_hardware_screen;
    GLint native_deferred_draw,native_deferred_read;
    unsigned native_drawable_deferred;
    unsigned pbo_writes_pending;
    unsigned char *texture_read_stage;
    size_t texture_read_bytes,texture_read_offset,texture_read_reserved_bytes;
    unsigned buffer_transfer_mode;GLenum buffer_transfer_target,buffer_transfer_usage;
    GLintptr buffer_transfer_offset;
    GLint max_draw_buffers,weight_units;
    unsigned weight_array_seen,point_array_seen;
    int point_sizes,purgeability;
    uint64_t surface_read_count,surface_read_fallbacks;
    unsigned inside_begin,diagnostic_errors,draw_debug_count,texture_debug_count,frame_debug_count,track_damage;
    unsigned indexed_scissor;
    unsigned program_debug_count,alias_debug_count,surface_debug_count,read_debug_count;uint64_t traced_program_states[256];
    GLint traced_immediate_program;unsigned immediate_vertex_budget;
    struct DGFenceName *fences;
    struct DGHostDrawable *drawables,*drawable;
    uint64_t surface_namespace[2];uint32_t next_surface_lease;
    struct DGSurfaceLease *surface_leases;
    struct DGSharedSurface *shared_surface;
    struct DGAliasGroup *alias_group;
    struct DGHostContext *alias_member_next;
    GLuint *sampled_storage,*sampled_textures,*rectangle_bindings;GLint texture_units;
    uint64_t alias_generation;int alias_bindings_dirty,alias_mappings_dirty;
    GLuint alias_framebuffer;
    // Context-local copy bindings only. Attachments are detached after EVERY
    // import, so caching these names retains no texture/snapshot lifetime.
    GLuint snapshot_read_framebuffer,snapshot_draw_framebuffer;
    GLuint iosurface_draw_textures[32];unsigned iosurface_draw_count;
    uint64_t iosurface_draw_generation;
    unsigned iosurface_draw_dirty,iosurface_mark_all;
    IOSurfaceRef spare_storage[4];size_t spare_bytes;
    GLuint spare_textures[4];int spare_widths[4],spare_heights[4];
    uint64_t storage_allocations,storage_reuses,storage_registrations,storage_texture_reuses;
    struct DGVRAMDrawableBacking *vram;
    // Idle registrations only: exact object/incarnation/layout identity is
    // still required before reuse. No framebuffer contents are cached here.
    struct DGVRAMDrawableBacking *vram_spares[3];uint64_t vram_spare_bytes;
    uint64_t vram_backing_hits,vram_backing_creates;
    // Private copy state belongs to the native context, not the currently
    // mapped guest backing. Alternating auxiliary surfaces must not recreate
    // a CGL context on every import. Its FBO is detached after each copy.
    CGLContextObj vram_copy_context;GLuint vram_copy_framebuffer;
    int vram_copy_width,vram_copy_height;
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    // Explicit auxiliary render transaction; not enabled by an ordinary
    // shared import. The guest must retain its CPU exclusion until End.
    struct DGHostDrawable *vram_render_drawable;
    GLuint vram_render_framebuffer;unsigned vram_render_materialized;
    uint64_t vram_render_serial,vram_render_last_serial;
    uint64_t vram_materialized_group_serial,vram_materialized_drawable_serial;
#endif
    struct DGCompletionQueue *completion_queue;
    struct DGSnapshotPool *snapshot_pool;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    struct DGBackingTicket *scatter_predecessor;
#endif
};
// Explicit research-oracle selection. Zero keeps normal host GPU selection.
static GLint dg_reference_renderer;
struct DGHostDrawable {
    uint32_t name;int width,height;GLuint framebuffer,color,back,depth;
    struct DGPrivateTextureWatch color_watch,back_watch;
    IOSurfaceRef color_storage,back_storage;
    struct DGSharedSurface *shared_surface;
    struct DGSurfaceRegistration *surface_registration;
    int prepared; // Explicit private-attach reservation; never ordinary MakeCurrent.
    struct DGDamage damage;
    int alias_dirty;
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
    int scope_canonical;
#endif
    uint64_t write_serial;
    struct DGHostDrawable *next;
};
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
#ifndef VIM_VRAM_RENDER_TARGET_EXPERIMENT
#error Canonical backing requires explicit scoped-render ownership
#endif
static uint64_t dg_scope_canonical_stats[4]; // attach, reuse, self publication, stale rejection
#endif
static void DGVRAMDelete(struct DGHostContext *);
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
static int DGVRAMRenderEnd(struct DGHostContext *);
#endif
struct DGCoreAPI {
    void (*bind)(GLuint);void (*generate)(GLsizei,GLuint *);
    void (*remove)(GLsizei,const GLuint *);GLboolean (*is_array)(GLuint);
    void (*divisor)(GLuint,GLuint);
    void (*vertex_attrib_lpointer)(GLuint,GLint,GLenum,GLsizei,const void *);
    void (*arrays_instanced)(GLenum,GLint,GLsizei,GLsizei);
    void (*elements_instanced)(GLenum,GLsizei,GLenum,const void *,GLsizei);
    void (*elements_base)(GLenum,GLsizei,GLenum,const void *,GLint);
    void (*range_elements_base)(GLenum,GLuint,GLuint,GLsizei,GLenum,const void *,GLint);
    void (*instanced_elements_base)(GLenum,GLsizei,GLenum,const void *,GLsizei,GLint);
    void (*multi_arrays)(GLenum,const GLint *,const GLsizei *,GLsizei);
    void (*draw_feedback)(GLenum,GLuint);
    void (*draw_feedback_stream)(GLenum,GLuint,GLuint);
    void (*draw_arrays_indirect)(GLenum,const void *);
    void (*draw_elements_indirect)(GLenum,GLenum,const void *);
    void (*multi_elements)(GLenum,const GLsizei *,GLenum,const void *const *,GLsizei);
    void (*multi_base)(GLenum,const GLsizei *,GLenum,const void *const *,GLsizei,const GLint *);
    void (*buffer_parameter64)(GLenum,GLenum,GLint64 *);
    void (*copy_buffer)(GLenum,GLenum,GLintptr,GLintptr,GLsizeiptr);
    void *(*map_range)(GLenum,GLintptr,GLsizeiptr,GLbitfield);
    void (*flush_range)(GLenum,GLintptr,GLsizeiptr);
    void (*signed_result)(GLuint,GLenum,GLint64EXT *);
    void (*unsigned_result)(GLuint,GLenum,GLuint64EXT *);
    void (*signed_result_ext)(GLuint,GLenum,GLint64EXT *);
    void (*unsigned_result_ext)(GLuint,GLenum,GLuint64EXT *);
    void (*query_counter)(GLuint,GLenum);
    void (*uniform_indices)(GLuint,GLsizei,const GLchar *const *,GLuint *);
    void (*uniform_block_iv)(GLuint,GLuint,GLenum,GLint *);
    void (*active_subroutine_iv)(GLuint,GLenum,GLuint,GLenum,GLint *);
    void (*uniform_double)(GLuint,GLint,GLdouble *);
    void (*enable_indexed)(GLenum,GLuint),(*disable_indexed)(GLenum,GLuint);
    int gl4_profile;
    int samplers;
};
static struct DGCoreAPI dg_core_api;
static pthread_once_t dg_core_once=PTHREAD_ONCE_INIT;
static void DGLoadCoreAPI(void)
{
    dg_core_api.bind=dlsym(RTLD_DEFAULT,"glBindVertexArray");
    dg_core_api.generate=dlsym(RTLD_DEFAULT,"glGenVertexArrays");
    dg_core_api.remove=dlsym(RTLD_DEFAULT,"glDeleteVertexArrays");
    dg_core_api.is_array=dlsym(RTLD_DEFAULT,"glIsVertexArray");
    dg_core_api.divisor=dlsym(RTLD_DEFAULT,"glVertexAttribDivisor");
    dg_core_api.vertex_attrib_lpointer=dlsym(RTLD_DEFAULT,"glVertexAttribLPointer");
    dg_core_api.arrays_instanced=dlsym(RTLD_DEFAULT,"glDrawArraysInstanced");
    dg_core_api.elements_instanced=dlsym(RTLD_DEFAULT,"glDrawElementsInstanced");
    dg_core_api.elements_base=dlsym(RTLD_DEFAULT,"glDrawElementsBaseVertex");
    dg_core_api.range_elements_base=dlsym(RTLD_DEFAULT,"glDrawRangeElementsBaseVertex");
    dg_core_api.instanced_elements_base=dlsym(RTLD_DEFAULT,"glDrawElementsInstancedBaseVertex");
    dg_core_api.multi_arrays=dlsym(RTLD_DEFAULT,"glMultiDrawArrays");
    dg_core_api.draw_feedback=dlsym(RTLD_DEFAULT,"glDrawTransformFeedback");
    dg_core_api.draw_feedback_stream=dlsym(RTLD_DEFAULT,"glDrawTransformFeedbackStream");
    dg_core_api.draw_arrays_indirect=dlsym(RTLD_DEFAULT,"glDrawArraysIndirect");
    dg_core_api.draw_elements_indirect=dlsym(RTLD_DEFAULT,"glDrawElementsIndirect");
    dg_core_api.multi_elements=dlsym(RTLD_DEFAULT,"glMultiDrawElements");
    dg_core_api.multi_base=dlsym(RTLD_DEFAULT,"glMultiDrawElementsBaseVertex");
    dg_core_api.buffer_parameter64=dlsym(RTLD_DEFAULT,"glGetBufferParameteri64v");
    dg_core_api.copy_buffer=dlsym(RTLD_DEFAULT,"glCopyBufferSubData");
    dg_core_api.map_range=dlsym(RTLD_DEFAULT,"glMapBufferRange");
    dg_core_api.flush_range=dlsym(RTLD_DEFAULT,"glFlushMappedBufferRange");
    dg_core_api.signed_result=dlsym(RTLD_DEFAULT,"glGetQueryObjecti64v");
    dg_core_api.unsigned_result=dlsym(RTLD_DEFAULT,"glGetQueryObjectui64v");
    dg_core_api.signed_result_ext=dlsym(RTLD_DEFAULT,"glGetQueryObjecti64vEXT");
    dg_core_api.unsigned_result_ext=dlsym(RTLD_DEFAULT,"glGetQueryObjectui64vEXT");
    dg_core_api.query_counter=dlsym(RTLD_DEFAULT,"glQueryCounter");
    dg_core_api.uniform_indices=dlsym(RTLD_DEFAULT,"glGetUniformIndices");
    dg_core_api.uniform_block_iv=dlsym(RTLD_DEFAULT,"glGetActiveUniformBlockiv");
    dg_core_api.active_subroutine_iv=dlsym(RTLD_DEFAULT,"glGetActiveSubroutineUniformiv");
    dg_core_api.uniform_double=dlsym(RTLD_DEFAULT,"glGetUniformdv");
    dg_core_api.enable_indexed=dlsym(RTLD_DEFAULT,"glEnablei");
    dg_core_api.disable_indexed=dlsym(RTLD_DEFAULT,"glDisablei");
    static const char *const sampler_symbols[]={
        "glGenSamplers","glDeleteSamplers","glIsSampler","glBindSampler",
        "glSamplerParameteri","glSamplerParameteriv","glSamplerParameterf","glSamplerParameterfv",
        "glSamplerParameterIiv","glSamplerParameterIuiv","glGetSamplerParameteriv","glGetSamplerParameterfv",
        "glGetSamplerParameterIiv","glGetSamplerParameterIuiv",
    };
    dg_core_api.samplers=1;
    for(unsigned i=0;i<sizeof(sampler_symbols)/sizeof(sampler_symbols[0]);++i)
        if(!dlsym(RTLD_DEFAULT,sampler_symbols[i])){dg_core_api.samplers=0;break;}
    // A distinct selector, not a claim based on GL_VERSION in a 3.2 context.
    CGLPixelFormatAttribute attrs[8]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)DG_CGL_PROFILE_GL4,0};
    if(dg_reference_renderer){attrs[4]=kCGLPFARendererID;attrs[5]=(CGLPixelFormatAttribute)dg_reference_renderer;
        attrs[6]=kCGLPFASupportsAutomaticGraphicsSwitching;}
    CGLPixelFormatObj format=NULL;GLint count=0;
    CGLError result=CGLChoosePixelFormat(attrs,&format,&count);
    dg_core_api.gl4_profile=!result && format && count>0;
    if(format)CGLDestroyPixelFormat(format);
}
/* Clear and blit use only viewport zero's scissor. The unindexed API
 * toggles every viewport, which would destroy independently set enables. */
static void DGSetBlitScissor(struct DGHostContext *c,GLboolean enable)
{
    if(c->indexed_scissor){
        void (*indexed)(GLenum,GLuint)=enable?dg_core_api.enable_indexed:dg_core_api.disable_indexed;
        if(indexed && c->profile!=kCGLOGLPVersion_Legacy){indexed(GL_SCISSOR_TEST,0);return;}
    }
    if(enable)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
}
static void DGBindHostVertexArray(struct DGHostContext *c,GLuint name)
{ c->array_enable_valid=0;if(c->profile==kCGLOGLPVersion_Legacy)glBindVertexArrayAPPLE(name);else dg_core_api.bind(name); }
static void DGGenHostVertexArrays(struct DGHostContext *c,GLsizei n,GLuint *names)
{ if(c->profile==kCGLOGLPVersion_Legacy)glGenVertexArraysAPPLE(n,names);else dg_core_api.generate(n,names); }
static void DGDeleteHostVertexArrays(struct DGHostContext *c,GLsizei n,const GLuint *names)
{ c->array_enable_valid=0;if(c->profile==kCGLOGLPVersion_Legacy)glDeleteVertexArraysAPPLE(n,names);else dg_core_api.remove(n,names); }
static GLboolean DGIsHostVertexArray(struct DGHostContext *c,GLuint name)
{ return c->profile==kCGLOGLPVersion_Legacy ? glIsVertexArrayAPPLE(name) : dg_core_api.is_array(name); }
static volatile sig_atomic_t dg_diagnostics;
static volatile sig_atomic_t dg_transfer_trace;
static volatile sig_atomic_t dg_iosurface_readback;
static int dg_surface_read_stats;
static int dg_storage_reuse;
static volatile int dg_drawable_texture_reuse=1;
static uint64_t dg_drawable_registration_stats[4]; // native registrations, exact-view hits, cached textures, evictions
static volatile sig_atomic_t dg_surface_import;
#include "passthrough-opengl-completion-host.c"
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
#include "direct-backing-completion-host.c"
#endif
struct DGColorTransfer { GLfloat values[8];GLint map_color;int compatibility; };
static const GLenum dg_color_transfer_keys[]={GL_RED_SCALE,GL_GREEN_SCALE,GL_BLUE_SCALE,GL_ALPHA_SCALE,
    GL_RED_BIAS,GL_GREEN_BIAS,GL_BLUE_BIAS,GL_ALPHA_BIAS};
static void DGCanonicalColorTransfer(struct DGHostContext *c,struct DGColorTransfer *saved)
{
    saved->compatibility=c->profile==kCGLOGLPVersion_Legacy;
    if(!saved->compatibility)return; // Removed pixel-transfer state is illegal in core GL.
    // Private backing transfers are raw copies, not application pixel
    // operations. Public glReadPixels/TexImage still retain native transfer
    // behavior. In particular a caller's alpha scale/bias must never change
    // the CPU shadow while reading back a completed compositor batch.
    for(unsigned i=0;i<8;++i) {
        GLfloat identity=i<4?1:0;glGetFloatv(dg_color_transfer_keys[i],&saved->values[i]);
        if(saved->values[i]!=identity)glPixelTransferf(dg_color_transfer_keys[i],identity);
    }
    glGetIntegerv(GL_MAP_COLOR,&saved->map_color);if(saved->map_color)glPixelTransferi(GL_MAP_COLOR,GL_FALSE);
}
static void DGRestoreColorTransfer(const struct DGColorTransfer *saved)
{
    if(!saved->compatibility)return;
    for(unsigned i=0;i<8;++i)if(saved->values[i]!=(i<4?1:0))
        glPixelTransferf(dg_color_transfer_keys[i],saved->values[i]);
    if(saved->map_color)glPixelTransferi(GL_MAP_COLOR,saved->map_color);
}
static void DGDisableDiagnostics(int signal_number)
{ (void)signal_number;dg_diagnostics=0;dg_transfer_trace=0; }
static void DGToggleSurfaceRead(int signal_number)
{ (void)signal_number;dg_iosurface_readback=!dg_iosurface_readback; }
static void DGToggleSurfaceImport(int signal_number)
{ (void)signal_number;dg_surface_import=!dg_surface_import; }
struct DGFenceName { GLuint name;int set;struct DGFenceName *next; };
static struct DGFenceName *DGFence(struct DGHostContext *c,GLuint name,int create)
{
    for(struct DGFenceName *f=c->fences;f;f=f->next) if(f->name==name) return f;
    if(!create) return NULL;
    struct DGFenceName *f=calloc(1,sizeof(*f));
    if(f) { f->name=name;f->next=c->fences;c->fences=f; }else c->error=GL_OUT_OF_MEMORY;
    return f;
}
static void DGForgetFence(struct DGHostContext *c,GLuint name)
{
    struct DGFenceName **p=&c->fences;
    while(*p) { if((*p)->name==name) { struct DGFenceName *f=*p;*p=f->next;free(f);return; }p=&(*p)->next; }
}
static void DGDestroyFences(struct DGHostContext *c)
{ while(c->fences) DGForgetFence(c,c->fences->name); }
static void *DGHostPointer(struct DGMessage *,int,size_t);
#include "passthrough-opengl-program-host.c"
static void DGSurfacePut(struct DGSharedSurface *);
static void DGSurfaceRegistrationPut(struct DGSurfaceRegistration *);
static uint64_t dg_prepared_drawable_stats[4]; // reserved, canonical resolves, private resolves, unused retirements
static int DGDrawableWritable(struct DGHostContext *);
struct DGAliasGroup;
static void DGDestroySyncs(struct DGAliasGroup *);
static void DGDestroyMappings(struct DGAliasGroup *);
static void DGRefreshFlippedAliases(struct DGHostContext *,int);
#include "passthrough-opengl-alias-host.c"
#include "passthrough-opengl-purgeable-host.c"
#include "direct-uniform-query-host.c"
#include "direct-iosurface-texture-host.c"
#include "direct-iosurface-writes-host.c"
#include "passthrough-opengl-sync-host.c"
static void DGPBOWritten(struct DGHostContext *c) { c->pbo_writes_pending=1; }
static void DGHostTexture3D(struct DGHostContext *,int,GLenum,GLint,GLint,GLint,GLint,GLint,
    GLsizei,GLsizei,GLsizei,GLint,GLenum,GLenum,const void *,int);
#include "passthrough-opengl-buffers-host.c"
#include "passthrough-opengl-mapping-host.c"
static void DGMarkDrawableDamage(struct DGHostContext *c)
{
    DGMarkIOSurfaceWrites(c);
    // Conservative without a new native query on ordinary draw paths.
    // An unrelated FBO write can only cause an extra asynchronous flush.
    if(c->drawable && !c->inside_begin){c->drawable->alias_dirty=1;++c->drawable->write_serial;}
    if(!c->track_damage || !c->drawable || c->inside_begin) return;
    GLint framebuffer=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&framebuffer);
    if((GLuint)framebuffer!=c->framebuffer) return;
    GLint box[4]={0,0,c->drawable->width,c->drawable->height};
    // A geometry shader can send draws outside viewport zero. Once the
    // app uses indexed scissors, keep damage conservative without queries.
    if(!c->indexed_scissor && glIsEnabled(GL_SCISSOR_TEST)) glGetIntegerv(GL_SCISSOR_BOX,box);
    int64_t right=(int64_t)box[0]+box[2],top=(int64_t)box[1]+box[3];
    int left=box[0]>0 ? box[0] : 0,bottom=box[1]>0 ? box[1] : 0;
    if(right>c->drawable->width) right=c->drawable->width;
    if(top>c->drawable->height) top=c->drawable->height;
    if(right<=left || top<=bottom) return;
    struct DGDamage *d=&c->drawable->damage;
    if(d->width && d->height) {
        if(d->x<left) left=d->x;if(d->y<bottom) bottom=d->y;
        if(d->x+d->width>right) right=d->x+d->width;
        if(d->y+d->height>top) top=d->y+d->height;
    }
    *d=(struct DGDamage){left,bottom,(int32_t)(right-left),(int32_t)(top-bottom)};
}
#include "passthrough-opengl-diagnostics.c"
#ifdef VIM_GL_DRAW_WITNESS
#include "passthrough-opengl-draw-witness.c"
#endif
#include "passthrough-opengl-arrays-host.c"
static GLenum DGHostBitmapPBO(struct DGHostContext *,GLint,GLint,GLsizei,GLsizei,
    GLenum,uint64_t,GLuint,const struct DGPixelState *);
#include "passthrough-opengl-pbo-host.c"
#include "passthrough-opengl-retained-render-host.c"
/* Only retired, unshared allocations may enter this bounded cache. Guest
 * texture aliases retain their original contents after drawable deletion;
 * an alias in ANY context of this share group therefore vetoes reuse. */
static void DGRemoveStorageSpare(struct DGHostContext *c,unsigned index)
{
    for(unsigned i=index;i<3;++i){
        c->spare_storage[i]=c->spare_storage[i+1];c->spare_textures[i]=c->spare_textures[i+1];
        c->spare_widths[i]=c->spare_widths[i+1];c->spare_heights[i]=c->spare_heights[i+1];
        DGPrivateTextureWatchName(&c->spare_watches[i],&c->spare_textures[i]);
    }
    DGPrivateTextureWatchForget(&c->spare_watches[3]);
    c->spare_storage[3]=NULL;c->spare_textures[3]=0;c->spare_widths[3]=c->spare_heights[3]=0;
}
static void DGRetireStorageTexture(struct DGHostContext *c,IOSurfaceRef surface,GLuint texture,
    int width,int height,int cache)
{
    if(!surface){if(texture)glDeleteTextures(1,&texture);return;}
    size_t bytes=IOSurfaceGetAllocSize(surface);
    if(cache && dg_storage_reuse && !c->shared_surface && bytes<=32*1024*1024) {
        GLuint identity=IOSurfaceGetID(surface);
        for(struct DGAlias *a=c->alias_group?c->alias_group->aliases:NULL;a;a=a->next)
            if(a->storage==identity || a->source_storage==identity) {
                if(texture)glDeleteTextures(1,&texture);CFRelease(surface);return;
            }
        while(c->spare_storage[3] || c->spare_bytes+bytes>32*1024*1024) {
            IOSurfaceRef oldest=c->spare_storage[0];
            if(!oldest)break;
            c->spare_bytes-=IOSurfaceGetAllocSize(oldest);
            if(c->spare_textures[0])glDeleteTextures(1,&c->spare_textures[0]);CFRelease(oldest);
            DGRemoveStorageSpare(c,0);
            __atomic_fetch_add(&dg_drawable_registration_stats[3],1,__ATOMIC_RELAXED);
        }
        for(unsigned i=0;i<4;++i)if(!c->spare_storage[i]) {
            c->spare_storage[i]=surface;c->spare_bytes+=bytes;
            if(texture && dg_drawable_texture_reuse && width>0 && height>0){
                c->spare_textures[i]=texture;c->spare_widths[i]=width;c->spare_heights[i]=height;
                DGPrivateTextureWatchName(&c->spare_watches[i],&c->spare_textures[i]);
                __atomic_fetch_add(&dg_drawable_registration_stats[2],1,__ATOMIC_RELAXED);
            }else if(texture)glDeleteTextures(1,&texture);
            return;
        }
    }
    if(texture)glDeleteTextures(1,&texture);CFRelease(surface);
}
/* A few laboratory callers own only storage, not its native texture. */
static void DGRetireStorage(struct DGHostContext *c,IOSurfaceRef surface,int cache)
{DGRetireStorageTexture(c,surface,0,0,0,cache);}
static void DGDeleteDrawableStorage(struct DGHostContext *c,int cache)
{
    if(c->drawable && c->drawable->prepared){
        c->drawable->prepared=0;
        __atomic_fetch_add(&dg_prepared_drawable_stats[3],1,__ATOMIC_RELAXED);
    }
    if(c->framebuffer) glDeleteFramebuffersEXT(1,&c->framebuffer);
    if(c->depth) glDeleteRenderbuffersEXT(1,&c->depth);
    int width=c->drawable?c->drawable->width:0,height=c->drawable?c->drawable->height:0;
    if(c->drawable && c->drawable->surface_registration){
        DGSurfaceRegistrationPut(c->drawable->surface_registration);
        c->drawable->surface_registration=NULL;
        if(c->color_storage)CFRelease(c->color_storage);
    }else DGRetireStorageTexture(c,c->color_storage,c->color,width,height,cache);
    DGRetireStorageTexture(c,c->back_storage,c->back,width,height,cache);
    if(c->shared_surface) DGSurfacePut(c->shared_surface);
    c->shared_surface=NULL;
    c->color_storage=c->back_storage=NULL;
    c->framebuffer=c->color=c->back=c->depth=0;
    c->iosurface_draw_dirty=1;
}
static void DGDeleteDrawable(struct DGHostContext *c) { DGDeleteDrawableStorage(c,0); }
/* Keep initialization ordered on the GPU. CPU-locking recycled storage here
 * forced completion of its previous drawing before every display-view resize. */
static int DGClearDrawableTexture(struct DGHostContext *c,GLuint texture)
{
    GLint draw=0;GLfloat color[4];GLboolean mask[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
    glGetBooleanIndexedvEXT(GL_COLOR_WRITEMASK,0,mask);
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);
    const char *extensions=c->profile==kCGLOGLPVersion_Legacy?(const char *)glGetString(GL_EXTENSIONS):NULL;
    int has_discard=c->profile!=kCGLOGLPVersion_Legacy ||
        (extensions && strstr(extensions,"GL_EXT_transform_feedback"));
    GLboolean discard=has_discard?glIsEnabled(GL_RASTERIZER_DISCARD_EXT):GL_FALSE;
    GLuint framebuffer=0;glGenFramebuffersEXT(1,&framebuffer);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB,texture,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    int good=framebuffer && glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    DGSetBlitScissor(c,GL_FALSE);glColorMaskIndexedEXT(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    if(discard)glDisable(GL_RASTERIZER_DISCARD_EXT);
    glClearColor(0,0,0,0);if(good)glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(color[0],color[1],color[2],color[3]);
    glColorMaskIndexedEXT(0,mask[0],mask[1],mask[2],mask[3]);
    if(scissor)DGSetBlitScissor(c,GL_TRUE);
    if(discard)glEnable(GL_RASTERIZER_DISCARD_EXT);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    if(framebuffer)glDeleteFramebuffersEXT(1,&framebuffer);
    return good;
}
/* An actual GPU-renderable allocation can also be imported by a private
 * drawable texture. A copy into a new texture would lose native live-alias
 * semantics. These references belong to the drawable, not guest GL names. */
// GPU surfaces are not wire payloads. Keep the per-message limit unchanged.
#define DG_DISPLAY_MAX_DIMENSION 8192u
#define DG_DISPLAY_MAX_BYTES ((size_t)DG_MAX_DISPLAY_PIXELS*4)
#define DG_DISPLAY_BUFFER_BUDGET (4*DG_DISPLAY_MAX_BYTES)
static IOSurfaceRef DGDrawableStorage(struct DGHostContext *c,int width,int height,GLuint *texture)
{
    if(width<1 || height<1 || (unsigned)width>DG_DISPLAY_MAX_DIMENSION ||
       (unsigned)height>DG_DISPLAY_MAX_DIMENSION)return NULL;
    IOSurfaceRef surface=NULL;unsigned best=4;size_t best_bytes=SIZE_MAX;int best_exact=0,reused_texture=0;
    // The drawable texture and its aliases are logical-size views of this
    // storage. Where the driver assumes its own view pitch, only storage with
    // that exact bytesPerRow (or the exact width) may serve this drawable.
    size_t view_pitch=dg_storage_reuse?DGIOSurfaceViewPitch(c->cgl):0;
    for(unsigned i=0;dg_storage_reuse && i<4;++i) {
        IOSurfaceRef spare=c->spare_storage[i];if(!spare)continue;
        size_t bytes=IOSurfaceGetAllocSize(spare);
        int exact=dg_drawable_texture_reuse && c->spare_textures[i] &&
            c->spare_widths[i]==width && c->spare_heights[i]==height;
        if(IOSurfaceGetWidth(spare)>=(size_t)width && IOSurfaceGetHeight(spare)>=(size_t)height &&
           DGIOSurfaceViewPitchMatches(spare,(size_t)width,view_pitch) &&
           (best==4 || exact>best_exact || (exact==best_exact && bytes<best_bytes)))
            {best=i;best_bytes=bytes;best_exact=exact;}
    }
    if(best<4) {
        surface=c->spare_storage[best];c->spare_bytes-=best_bytes;
        GLuint old_texture=c->spare_textures[best];
        if(best_exact){*texture=old_texture;reused_texture=1;}
        else if(old_texture)glDeleteTextures(1,&old_texture);
        DGRemoveStorageSpare(c,best);
        glFlush(); // Submit the previous producer before rebinding its IOSurface.
        ++c->storage_reuses;
    }
    size_t capacity_width=(size_t)width,capacity_height=(size_t)height;
    if(dg_storage_reuse) {
        size_t w=(capacity_width+127)&~(size_t)127,h=(capacity_height+127)&~(size_t)127;
        w=DGIOSurfaceViewCapacityWidth(capacity_width,w,4,view_pitch);
        if(w*h*4<=DG_DISPLAY_MAX_BYTES) { capacity_width=w;capacity_height=h; }
    }
    size_t row=(capacity_width*4+63)&~(size_t)63,bytes=row*capacity_height;
    if(bytes>DG_DISPLAY_MAX_BYTES) return NULL;
    if(!surface) {
    CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!properties) return NULL;
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int64_t values[]={(int64_t)capacity_width,(int64_t)capacity_height,4,(int64_t)row,(int64_t)bytes,0x42475241};
    int good=1;
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i) {
        CFNumberRef value=CFNumberCreate(NULL,kCFNumberSInt64Type,&values[i]);
        if(!value) { good=0;break; }
        CFDictionarySetValue(properties,keys[i],value);CFRelease(value);
    }
    surface=good?IOSurfaceCreate(properties):NULL;CFRelease(properties);
    if(surface)++c->storage_allocations;
    }
    if(!surface) return NULL;
    if(IOSurfaceGetAllocSize(surface)>DG_DISPLAY_MAX_BYTES) {
        CFRelease(surface);return NULL;
    }
    if(!reused_texture)glGenTextures(1,texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,*texture);
    if(!*texture || (!reused_texture && DGPrivateTextureImport(c->cgl,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,width,height,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0))) { CFRelease(surface);return NULL; }
    if(reused_texture){
        ++c->storage_texture_reuses;
        __atomic_fetch_add(&dg_drawable_registration_stats[1],1,__ATOMIC_RELAXED);
    }else{
        ++c->storage_registrations;
        __atomic_fetch_add(&dg_drawable_registration_stats[0],1,__ATOMIC_RELAXED);
    }
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    if(!DGClearDrawableTexture(c,*texture)) { CFRelease(surface);return NULL; }
    return surface;
}
static void DGSetBuffer(struct DGHostContext *c,GLenum mode,int reading)
{
    if(!reading)c->iosurface_draw_dirty=1;
    GLint binding=0;glGetIntegerv(reading ? GL_READ_FRAMEBUFFER_BINDING_EXT : GL_DRAW_FRAMEBUFFER_BINDING_EXT,&binding);
    if((GLuint)binding!=c->framebuffer) { if(reading) glReadBuffer(mode);else glDrawBuffer(mode);return; }
    GLenum attachment=GL_NONE;
    if(mode==GL_FRONT || mode==GL_FRONT_LEFT) attachment=GL_COLOR_ATTACHMENT0_EXT;
    else if((mode==GL_BACK || mode==GL_BACK_LEFT) && c->double_buffer) attachment=GL_COLOR_ATTACHMENT1_EXT;
    else if(mode==GL_FRONT_AND_BACK && !reading) {
        GLenum buffers[]={GL_COLOR_ATTACHMENT0_EXT,GL_COLOR_ATTACHMENT1_EXT};
        glDrawBuffers(c->double_buffer ? 2 : 1,buffers);c->draw_mode=mode;return;
    } else if(mode!=GL_NONE) { c->error=GL_INVALID_OPERATION;return; }
    if(reading) { glReadBuffer(attachment);c->read_mode=mode; }
    else { glDrawBuffer(attachment);c->draw_mode=mode; }
}
static void DGSetDrawBuffers(struct DGHostContext *c,GLsizei n,const GLenum *buffers)
{
    if(c->inside_begin){c->error=GL_INVALID_OPERATION;return;}
    if(n<0){c->error=GL_INVALID_VALUE;return;}
    GLint binding=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&binding);
    if((GLuint)binding!=c->framebuffer){glDrawBuffers(n,buffers);c->iosurface_draw_dirty=1;return;}
    if(!c->max_draw_buffers)glGetIntegerv(GL_MAX_DRAW_BUFFERS_ARB,&c->max_draw_buffers);
    if(n>c->max_draw_buffers){c->error=GL_INVALID_VALUE;return;}
    GLenum local[16],*mapped=n<=16?local:malloc((size_t)n*sizeof(*mapped));
    if(!mapped){c->error=GL_OUT_OF_MEMORY;return;}
    GLenum error=GL_NO_ERROR;
    for(GLsizei i=0;i<n&&!error;++i){
        GLenum mode=buffers[i];
        if(mode==GL_NONE)mapped[i]=GL_NONE;
        else if(mode==GL_FRONT_LEFT)mapped[i]=GL_COLOR_ATTACHMENT0_EXT;
        else if(mode==GL_BACK_LEFT&&c->double_buffer)mapped[i]=GL_COLOR_ATTACHMENT1_EXT;
        else if(mode==GL_BACK&&c->profile!=kCGLOGLPVersion_Legacy&&n==1&&c->double_buffer)
            mapped[i]=GL_COLOR_ATTACHMENT1_EXT;
        else if(mode==GL_FRONT_RIGHT||mode==GL_BACK_RIGHT||mode==GL_BACK_LEFT||
                mode==GL_FRONT||mode==GL_BACK||mode==GL_LEFT||mode==GL_RIGHT||mode==GL_FRONT_AND_BACK||
                (mode>=GL_AUX0&&mode<=GL_AUX3)||
                (mode>=GL_COLOR_ATTACHMENT0_EXT&&mode<=GL_COLOR_ATTACHMENT15_EXT))error=GL_INVALID_OPERATION;
        else error=GL_INVALID_ENUM;
        for(GLsizei j=0;j<i&&!error;++j)if(mode!=GL_NONE&&buffers[j]==mode)error=GL_INVALID_OPERATION;
    }
    if(error)c->error=error;
    else {glDrawBuffers(n,mapped);c->draw_mode=n?buffers[0]:GL_NONE;c->iosurface_draw_dirty=1;}
    if(mapped!=local)free(mapped);
}
static int DGVirtualInteger(struct DGHostContext *c,GLenum key,GLint *value)
{
    GLint binding=0;
    if(key==GL_DOUBLEBUFFER) { *value=c->double_buffer;return 1; }
    if(key==GL_UNPACK_CLIENT_STORAGE_APPLE) { *value=c->client_storage;return 1; }
    if(key>=GL_DRAW_BUFFER0_ARB&&key<=GL_DRAW_BUFFER15_ARB){
        if(c->native_drawable_deferred){*value=key==GL_DRAW_BUFFER0_ARB?c->draw_mode:GL_NONE;return 1;}
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&binding);
        if((GLuint)binding==c->framebuffer){
            if(key==GL_DRAW_BUFFER0_ARB){*value=c->draw_mode;return 1;}
            glGetIntegerv(key,value);
            if(*value==GL_COLOR_ATTACHMENT0_EXT)*value=GL_FRONT_LEFT;
            else if(*value==GL_COLOR_ATTACHMENT1_EXT)*value=GL_BACK_LEFT;
            return 1;
        }
    }
    if(key==GL_READ_BUFFER || key==GL_DRAW_BUFFER) {
        if(c->native_drawable_deferred){*value=key==GL_READ_BUFFER?c->read_mode:c->draw_mode;return 1;}
        glGetIntegerv(key==GL_READ_BUFFER ? GL_READ_FRAMEBUFFER_BINDING_EXT : GL_DRAW_FRAMEBUFFER_BINDING_EXT,&binding);
        if((GLuint)binding==c->framebuffer) { *value=key==GL_READ_BUFFER ? c->read_mode : c->draw_mode;return 1; }
    }
    if(key==GL_READ_FRAMEBUFFER_BINDING_EXT || key==GL_DRAW_FRAMEBUFFER_BINDING_EXT) {
        glGetIntegerv(key,&binding);if((GLuint)binding==c->framebuffer) { *value=0;return 1; }
    }
    return 0;
}
/* The GLI must distinguish a window flush from an offscreen-FBO flush.
 * Query at the same ordered native flush, rather than a second guest/host
 * round trip. Preserve prior GL errors; do not infer state from guest calls. */
static int DGFlushDrawableState(struct DGHostContext *c,const struct DGMessage *m,struct DGReply *reply)
{
    if(m->bytes || m->args[0]>1){reply->error=GL_INVALID_VALUE;return 0;}
    if(c->inside_begin){reply->error=GL_INVALID_OPERATION;return 0;}
    /* A private flipped import is live across shared contexts, not merely
     * when the producer samples itself. Publish changed mirrors before the
     * producer flush/finish so another context's bind sees the new pixels.
     * Serial checks skip unchanged views; no CPU read or additional wait. */
    DGRefreshFlippedAliases(c,0);
    if(m->args[0])glFinish();else glFlush();
    DGSubmitIOSurfaceWrites(c);
    GLint binding=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&binding);
    if((GLuint)binding==c->framebuffer)binding=0;
    reply->result=(uint32_t)binding;return 0;
}
static int DGFlushIOSurfaceState(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    unsigned capacity=(unsigned)m->args[2];
    if(m->args[0]>1 || m->args[2]!=capacity || !capacity || capacity>DG_IOSURFACE_WRITE_LIMIT ||
       (capacity>DG_IOSURFACE_INLINE_WRITES&&!(c->client_features&DG_FEATURE_LARGE_IOSURFACE_WRITES)) ||
       m->flags!=(DG_REPLY|DG_OUTPUT_ONLY) || m->bytes!=capacity*sizeof(struct DGIosurfaceWrite))return -1;
    struct DGIosurfaceWrite *out=DGHostPointer(m,1,m->bytes);if(!out)return -1;
    struct DGMessage flush={0};flush.args[0]=m->args[0];
    int error=DGFlushDrawableState(c,&flush,reply);if(error || reply->error)return error;
    unsigned count=0;
    for(struct DGAlias *p=c->alias_group->aliases;p;p=p->next)
      if(p->iosurface_import&&p->iosurface_write_serial&&p->iosurface_submitted_serial==p->iosurface_write_serial){
        if(count==capacity){reply->error=GL_OUT_OF_MEMORY;return 0;}
        out[count++]=(struct DGIosurfaceWrite){p->texture,0,p->iosurface_write_serial};
    }
    reply->result|=(uint64_t)count<<32;return 0;
}
#include "direct-native-renderers-host.c"
/* All values are captured in this ordered context, with exact public GL
 * types and the same virtual-default-FBO translation as individual queries. */
static void DGFillCommonState(struct DGHostContext *c,struct DGCommonState *out)
{
    glGetIntegerv(GL_VIEWPORT,out->viewport);
    if(!DGVirtualInteger(c,GL_DRAW_FRAMEBUFFER_BINDING_EXT,&out->draw_framebuffer))
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&out->draw_framebuffer);
    if(!DGVirtualInteger(c,GL_DRAW_BUFFER,&out->draw_buffer))glGetIntegerv(GL_DRAW_BUFFER,&out->draw_buffer);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&out->rectangle_binding);
    if(c->profile==kCGLOGLPVersion_Legacy){
        glGetFloatv(GL_MODELVIEW_MATRIX,out->modelview);glGetFloatv(GL_PROJECTION_MATRIX,out->projection);
    }
}
static int DGCommonStateShape(struct DGMessage *m,struct DGCommonState **out)
{
    *out=DGHostPointer(m,0,sizeof(**out));
    if(!*out || m->bytes!=sizeof(**out) || m->flags!=(DG_REPLY|DG_OUTPUT_ONLY) || m->args[0]!=1)return 0;
    for(unsigned i=1;i<sizeof(m->args)/sizeof(m->args[0]);++i)if(m->args[i])return 0;
    return 1;
}
static int DGReadCommonState(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    struct DGCommonState *out=NULL;if(!DGCommonStateShape(m,&out))return -1;
    if(c->inside_begin){reply->error=GL_INVALID_OPERATION;return 0;}
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    DGFillCommonState(c,out);
    reply->error=glGetError();if(!reply->error)out->version=1;return 0;
}
/* An ordered public error observation plus opportunistic actual native state,
 * not a guessed clean-error flag or setter shadow. Queries are valid in every
 * supported compatibility context outside Begin/End and change none of these fields.
 * If another native error is pending, keep it for the next error observation
 * and reject the speculative snapshot instead of suppressing/duplicating it.
 */
static int DGReadErrorState(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    struct DGCommonState *out=NULL;if(!DGCommonStateShape(m,&out))return -1;
    reply->result=c->error ? c->error : glGetError();c->error=0;
    if(c->inside_begin)return 0;
    DGFillCommonState(c,out);
    GLenum error=glGetError();
    if(error)c->error=error;else out->version=1;
    return 0;
}
static int DGResize(struct DGHostContext *c,int width,int height)
{
    /* Compatibility CGL pbuffers are not usable on every Metal-backed host. A real
     * host FBO supplies the virtual default framebuffer instead. Save caller
     * bindings; only guest framebuffer zero maps to this private drawable. */
    GLint read_framebuffer,draw_framebuffer,texture,renderbuffer;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read_framebuffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw_framebuffer);glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    glGetIntegerv(GL_RENDERBUFFER_BINDING_EXT,&renderbuffer);
    GLuint old=c->framebuffer;
    DGDeleteDrawable(c);
    c->color_storage=DGDrawableStorage(c,width,height,&c->color);
    glGenFramebuffersEXT(1,&c->framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c->framebuffer);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,c->color,0);
    if(c->double_buffer) {
        c->back_storage=DGDrawableStorage(c,width,height,&c->back);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT1_EXT,GL_TEXTURE_RECTANGLE_ARB,c->back,0);
    }
    if(c->depth_bits || c->stencil_bits) {
        glGenRenderbuffersEXT(1,&c->depth);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,c->depth);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,c->stencil_bits ? GL_DEPTH24_STENCIL8_EXT : GL_DEPTH_COMPONENT24,width,height);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_RENDERBUFFER_EXT,c->depth);
        if(c->stencil_bits) glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_STENCIL_ATTACHMENT_EXT,GL_RENDERBUFFER_EXT,c->depth);
    }
    DGSetBuffer(c,c->draw_mode,0);DGSetBuffer(c,c->read_mode,1);
    int good=c->color_storage && (!c->double_buffer || c->back_storage) &&
        glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read_framebuffer && (GLuint)read_framebuffer!=old ? (GLuint)read_framebuffer : c->framebuffer);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw_framebuffer && (GLuint)draw_framebuffer!=old ? (GLuint)draw_framebuffer : c->framebuffer);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,(GLuint)renderbuffer);
    if(!good) fprintf(stderr,"PassthroughOpenGL default framebuffer incomplete, GL error=%x\n",glGetError());
    return good;
}
/* Reserve only during our explicit internal attach transaction. A generic
 * GL command resolves real private storage BEFORE execution; no public
 * framebuffer query, draw, export or read can observe a missing attachment.
 * The GLI completes its real backing import before exposing CGL success. */
static int DGResolvePreparedDrawable(struct DGHostContext *c)
{
    struct DGHostDrawable *d=c->drawable;if(!d || !d->prepared)return 1;
    if(c->inside_begin || c->double_buffer || c->depth_bits || c->stencil_bits)return 0;
    GLint read=0,draw=0,texture=0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    GLuint color=0;IOSurfaceRef storage=DGDrawableStorage(c,d->width,d->height,&color);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,d->framebuffer);
    if(storage)glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,color,0);
    int good=storage && glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    if(good){
        c->color=d->color=color;c->color_storage=d->color_storage=storage;d->prepared=0;
        __atomic_fetch_add(&dg_prepared_drawable_stats[2],1,__ATOMIC_RELAXED);
    }else {
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
        if(color)glDeleteTextures(1,&color);if(storage)CFRelease(storage);
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);
    return good;
}
static int DGPreparedNeedsStorage(unsigned op)
{
    return op!=DG_PREPARE_DRAWABLE && op!=DG_RESIZE && op!=DG_DROP_DRAWABLE && op!=DG_DESTROY &&
        op!=DG_BACKING_RENDER_BEGIN && op!=DG_BACKING_RENDER_CONTINUE && op!=DG_SURFACE_DRAWABLE &&
        op!=DG_ERROR && op!=DG_ERROR_STATE && op!=DG_COMMON_STATE && op!=DG_STRING && op!=DG_VRAM_STATS;
}
/* Same wire reply, directly from the locked host IOSurface. No image-sized
 * reply allocation or intermediate memcpy. A short write or unlock failure
 * ends this stream; it must never trigger a second reply for the same read.
 * CGLIOSurface.h requires producer flush before CPU lock; never AvoidSync.
 * No public pixel-pack/transfer, framebuffer or texture state is changed. */
static int DGSendDrawableSurface(struct DGHostContext *c,const struct DGMessage *m,int fd)
{
    size_t width=(size_t)m->args[0],height=(size_t)m->args[1];
    unsigned format=m->op==DG_READ_DRAWABLE?(unsigned)m->args[2]:VIM_GL_PIXEL_BGRA8;
    unsigned element=VIMGLPixelBytes(format);
    if(m->op==DG_READ_DRAWABLE && m->args[2]>VIM_GL_PIXEL_RGBA16)return -1;
    uint64_t x=m->op==DG_READ_DRAWABLE_RECT?m->args[2]:0;
    uint64_t y=m->op==DG_READ_DRAWABLE_RECT?m->args[3]:0;
    if(!c->drawable || c->inside_begin || !width || !height ||
       width>(size_t)c->drawable->width || height>(size_t)c->drawable->height ||
       x>(uint64_t)c->drawable->width-width || y>(uint64_t)c->drawable->height-height ||
       !element || width*height*element!=m->bytes || (m->op==DG_READ_DRAWABLE &&
       (width!=(size_t)c->drawable->width || height!=(size_t)c->drawable->height)))return -1;
    IOSurfaceRef surface=c->color_storage;
    if(!surface || IOSurfaceGetPlaneCount(surface) || IOSurfaceGetBytesPerElement(surface)!=element ||
       IOSurfaceGetPixelFormat(surface)!=DGSurfaceNativeFormat(format) ||
       IOSurfaceGetWidth(surface)<(size_t)c->drawable->width ||
       IOSurfaceGetHeight(surface)<(size_t)c->drawable->height)return 0;
    size_t stride=IOSurfaceGetBytesPerRow(surface),allocation=IOSurfaceGetAllocSize(surface),row_bytes=width*element;
    if(stride<(size_t)c->drawable->width*element || allocation>DG_DISPLAY_MAX_BYTES || stride>DG_MAX_BYTES ||
       (y+height-1)*stride+(x+width)*element>allocation)return 0;
    glFlush();
    if(IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL)!=kIOReturnSuccess)return 0;
    const unsigned char *base=IOSurfaceGetBaseAddress(surface);
    struct DGReply reply={.bytes=m->bytes};
    int failed=!base || DGTransfer(fd,&reply,sizeof(reply),1);
    size_t position=0;
    if(!failed && stride==row_bytes) {
        failed=DGTransfer(fd,(void *)(base+y*stride+x*element),m->bytes,1);
        position=failed?0:m->bytes;
    }
    while(!failed && position<m->bytes) {
        struct iovec vectors[64];unsigned count=0;
        size_t row=position/row_bytes,within=position%row_bytes;
        for(;row<height && count<64;++row,within=0,++count) {
            vectors[count].iov_base=(void *)(base+(y+row)*stride+x*element+within);
            vectors[count].iov_len=row_bytes-within;
        }
        ssize_t written;
        do {
#if defined(VIM_PASSTHROUGH_OPENGL_MACH_EXECUTOR)
            written=fd<0?DGMachWritev(dg_mach_host_stream,vectors,(int)count):writev(fd,vectors,(int)count);
#elif defined(VIM_PASSTHROUGH_OPENGL_INPROCESS)
            // The outer loop already handles partial vector writes. Keep
            // IOSurface locking and exact row order identical to socket I/O.
            written=fd<0?(dg_inline_host_stream?DGInlineHostWritev(vectors,(int)count):
                DGMemoryServerWritev(dg_memory_host_stream,vectors,(int)count)):
                writev(fd,vectors,(int)count);
#else
            written=writev(fd,vectors,(int)count);
#endif
        } while(written<0 && errno==EINTR);
        if(written<=0)failed=1;else position+=(size_t)written;
    }
    kern_return_t unlocked=IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
    if(failed || unlocked!=kIOReturnSuccess)return -1;
    ++c->surface_read_count;return 1;
}
/* Private bridge transactions preserve host state locally, not with a dozen
 * synchronous guest glGet calls. Pixels stay in the existing bottom-up order. */
static int DGImportDrawableSurface(struct DGHostContext *c,const void *pixels,
    size_t stride,size_t x,size_t y,size_t width,size_t height,int reverse)
{
    IOSurfaceRef surface=c->color_storage;
    if(!surface || IOSurfaceGetPlaneCount(surface) || IOSurfaceGetBytesPerElement(surface)!=4 ||
       IOSurfaceGetPixelFormat(surface)!=0x42475241 ||
       x+width>IOSurfaceGetWidth(surface) || y+height>IOSurfaceGetHeight(surface))return 0;
    size_t pitch=IOSurfaceGetBytesPerRow(surface),bytes=IOSurfaceGetAllocSize(surface);
    if(pitch>DG_MAX_BYTES || bytes>DG_MAX_BYTES || pitch<(x+width)*4 ||
       (y+height-1)*pitch+(x+width)*4>bytes)return 0;
    // Complete prior GPU ownership before CPU writes; never use AvoidSync.
    glFlush();
    if(IOSurfaceLock(surface,0,NULL)!=kIOReturnSuccess)return 0;
    unsigned char *base=IOSurfaceGetBaseAddress(surface);
    if(base)for(size_t row=0;row<height;++row)
        memcpy(base+(y+row)*pitch+x*4,(const unsigned char *)pixels+
            (reverse?height-1-row:row)*stride,width*4);
    kern_return_t unlocked=IOSurfaceUnlock(surface,0,NULL);
    if(!base || unlocked!=kIOReturnSuccess)c->error=GL_INVALID_OPERATION;
    c->drawable->alias_dirty=1;++c->drawable->write_serial;
    return 1;
}
static int DGDrawablePixels(struct DGHostContext *c,struct DGMessage *m,int reading)
{
    unsigned format=reading && m->op==DG_READ_DRAWABLE?(unsigned)m->args[2]:VIM_GL_PIXEL_BGRA8;
    unsigned element=VIMGLPixelBytes(format);
    if(!element || (reading && m->op==DG_READ_DRAWABLE && m->args[2]>VIM_GL_PIXEL_RGBA16))return -1;
    int patch=m->op==DG_IMPORT_DRAWABLE_PATCH || m->op==DG_READ_DRAWABLE_RECT;
    int view=m->op==DG_IMPORT_DRAWABLE_VIEW;
    uint64_t width=m->args[0],height=m->args[1],x=patch?m->args[2]:0,y=patch?m->args[3]:0;
    if(!c->drawable || !width || !height || width>(uint64_t)c->drawable->width ||
       height>(uint64_t)c->drawable->height || x>(uint64_t)c->drawable->width-width ||
       y>(uint64_t)c->drawable->height-height || c->inside_begin ||
       (!patch && (width!=(uint64_t)c->drawable->width || height!=(uint64_t)c->drawable->height))) return -1;
    uint64_t stride=view?m->args[2]:width*element;
    if(stride<width*element || stride>DG_MAX_BYTES || (view && (reading || m->args[3]>1)) ||
       (height-1)*stride+width*element!=m->bytes)return -1;
    const unsigned char *pixels=(const void *)(m+1);unsigned char *converted=NULL;
    if(!reading && dg_surface_import &&
       DGImportDrawableSurface(c,pixels,(size_t)stride,(size_t)x,(size_t)y,
           (size_t)width,(size_t)height,view && m->args[3]))return 0;
    if(view && (m->args[3] || stride!=width*4)) {
        converted=malloc((size_t)(width*height*4));
        if(!converted) { c->error=GL_OUT_OF_MEMORY;return 0; }
        for(uint64_t row=0;row<height;++row)memcpy(converted+row*width*4,
            pixels+(m->args[3]?height-1-row:row)*stride,(size_t)(width*4));
        pixels=converted;
    }
    if(reading && dg_transfer_trace && c->read_debug_count++<16)
        fprintf(stderr,"direct readback context=%p op=%x drawable=%dx%d rect=%llu,%llu %llux%llu bytes=%u\n",
            (void *)c,m->op,c->drawable->width,c->drawable->height,x,y,width,height,m->bytes);
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    GLint buffer,row,alignment,skip_rows,skip_pixels,row_bytes,swap,lsb;
    GLenum target=reading ? GL_PIXEL_PACK_BUFFER : GL_PIXEL_UNPACK_BUFFER;
    glGetIntegerv(reading ? GL_PIXEL_PACK_BUFFER_BINDING : GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
    glGetIntegerv(reading ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,&row);
    glGetIntegerv(reading ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,&alignment);
    glGetIntegerv(reading ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS,&skip_rows);
    glGetIntegerv(reading ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS,&skip_pixels);
    glGetIntegerv(reading ? GL_PACK_ROW_BYTES_APPLE : GL_UNPACK_ROW_BYTES_APPLE,&row_bytes);
    glGetIntegerv(reading ? GL_PACK_SWAP_BYTES : GL_UNPACK_SWAP_BYTES,&swap);
    glGetIntegerv(reading ? GL_PACK_LSB_FIRST : GL_UNPACK_LSB_FIRST,&lsb);
    glBindBuffer(target,0);
    glPixelStorei(reading ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(reading ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,4);
    glPixelStorei(reading ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS,0);
    glPixelStorei(reading ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS,0);
    glPixelStorei(reading ? GL_PACK_ROW_BYTES_APPLE : GL_UNPACK_ROW_BYTES_APPLE,0);
    glPixelStorei(reading ? GL_PACK_SWAP_BYTES : GL_UNPACK_SWAP_BYTES,GL_FALSE);
    glPixelStorei(reading ? GL_PACK_LSB_FIRST : GL_UNPACK_LSB_FIRST,GL_FALSE);
    if(reading) {
        GLint fb,read;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&fb);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c->framebuffer);
        glGetIntegerv(GL_READ_BUFFER,&read);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glReadPixels((GLint)x,(GLint)y,(GLsizei)width,(GLsizei)height,DGSurfaceGLFormat(format),DGSurfaceGLType(format),m+1);
        glReadBuffer((GLenum)read);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)fb);
    } else {
        GLint texture;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,c->color);
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,(GLint)x,(GLint)y,(GLsizei)width,(GLsizei)height,
            GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,pixels);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);
        c->drawable->alias_dirty=1;++c->drawable->write_serial;
    }
    free(converted);glBindBuffer(target,(GLuint)buffer);
    glPixelStorei(reading ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,row);
    glPixelStorei(reading ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,alignment);
    glPixelStorei(reading ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS,skip_rows);
    glPixelStorei(reading ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS,skip_pixels);
    glPixelStorei(reading ? GL_PACK_ROW_BYTES_APPLE : GL_UNPACK_ROW_BYTES_APPLE,row_bytes);
    glPixelStorei(reading ? GL_PACK_SWAP_BYTES : GL_UNPACK_SWAP_BYTES,swap);
    glPixelStorei(reading ? GL_PACK_LSB_FIRST : GL_UNPACK_LSB_FIRST,lsb);
    DGRestoreColorTransfer(&transfer);
    return 0;
}
/* Quartz samples its just-rendered accelerator surface while constructing
 * menu/blur effects. Its CPU shadow need not have been published yet. Copy
 * from the ordered GPU drawable, never round-trip those fresh pixels through
 * guest memory. The bound destination texture is the caller's texture. */
static int DGSnapshotDrawableTexture(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    GLenum target=(GLenum)m->args[0];GLint internal=(GLint)m->args[1];
    uint64_t width=m->args[2],height=m->args[3];
    if(!c->drawable || c->inside_begin || m->bytes ||
       (target!=GL_TEXTURE_RECTANGLE_ARB && target!=GL_TEXTURE_2D) ||
       (internal!=GL_RGBA && internal!=GL_RGBA8 && internal!=GL_RGB && internal!=GL_RGB8) ||
       !width || !height || width>(uint64_t)c->drawable->width || height>(uint64_t)c->drawable->height || m->args[4]>1) {
        reply->error=GL_INVALID_VALUE;return 0;
    }
    GLint framebuffer,read;
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&framebuffer);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c->framebuffer);
    glGetIntegerv(GL_READ_BUFFER,&read);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
#ifdef VIM_ALIAS_REBIND_EXPERIMENT
    DGInvalidateAliasStorage(c);
#endif
    glCopyTexImage2D(target,0,internal,0,0,(GLsizei)width,(GLsizei)height,0);
    glReadBuffer((GLenum)read);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)framebuffer);
    if(m->args[4]) glGenerateMipmapEXT(target);
    DGRestoreColorTransfer(&transfer);
    return 0;
}
#include "passthrough-opengl-flipped-alias-host.c"
#ifdef VIM_ALIAS_REBIND_EXPERIMENT
static volatile int dg_drawable_alias_reuse=1; // Owned-process comparison only.
static volatile uint64_t dg_drawable_alias_reuses,dg_drawable_alias_imports;
#endif
static int DGBindDrawableTexture(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    GLenum internal=(GLenum)m->args[0];uint64_t width=m->args[1],height=m->args[2];
    int back=!!(m->args[3]&2);
    IOSurfaceRef storage=back?c->back_storage:c->color_storage;
    unsigned format=storage && IOSurfaceGetPixelFormat(storage)==DGSurfaceNativeFormat(VIM_GL_PIXEL_RGBA16)?
        VIM_GL_PIXEL_RGBA16:VIM_GL_PIXEL_BGRA8;
    GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    if(!c->drawable || !storage || (back && !c->double_buffer) || c->inside_begin || m->bytes || !texture || m->args[3]>3 ||
       !width || !height || width>(uint64_t)c->drawable->width || height>(uint64_t)c->drawable->height ||
       !DGSurfaceInternalMatches(format,internal) || (format && (m->args[3]&1))) {
        reply->error=GL_INVALID_VALUE;return 0;
    }
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    if(m->args[3]&1)return DGBindFlippedAlias(c,(GLuint)texture,internal,(int)width,(int)height,back,reply);
    if(dg_diagnostics && c->alias_debug_count++<32)
        fprintf(stderr,"direct drawable alias context=%p texture=%d size=%llu,%llu drawable=%u framebuffer=%u color=%u storage=%p\n",
            (void *)c,texture,width,height,c->drawable->name,c->framebuffer,c->color,(void *)c->color_storage);
    // Importing the actual render target preserves ordered earlier draws
    // and later live writes; no CPU transfer or GPU snapshot is involved.
    glFlush();
#ifdef VIM_ALIAS_REBIND_EXPERIMENT
    struct DGAlias *alias=c->alias_group?c->alias_group->aliases:NULL;
    for(;alias;alias=alias->next)if(alias->texture==(GLuint)texture)break;
    /* The native storage is already this exact live IOSurface, not a retained
     * pixel version. Texture writes do not change its storage identity; public
     * redefinitions/deletion retire metadata, opaque list/private definitions
     * invalidate its generation. A new backing, format or view must import.
     * Even on a hit retain producer submission and consumer bind ordering.
     * Flipped copies deliberately do NOT use this path.
     */
    if(dg_drawable_alias_reuse && alias && !alias->owned_storage && !alias->source_storage &&
       alias->storage==IOSurfaceGetID(storage) && alias->width==(int)width &&
       alias->height==(int)height && alias->internal==internal &&
       c->alias_group->binding_generation!=UINT64_MAX &&
       alias->binding_generation==c->alias_group->binding_generation){
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);
        GLenum error=glGetError();
        if(error)reply->error=error;else ++dg_drawable_alias_reuses;
        return 0;
    }
#endif
    if(CGLTexImageIOSurface2D(c->cgl,GL_TEXTURE_RECTANGLE_ARB,internal,(GLsizei)width,(GLsizei)height,
        DGSurfaceGLFormat(format),DGSurfaceGLType(format),storage,0)) reply->error=GL_INVALID_OPERATION;
    GLenum error=glGetError();if(error && !reply->error)reply->error=error;
#ifdef VIM_ALIAS_REBIND_EXPERIMENT
    if(!reply->error){
        if(!DGRegisterAlias(c,(GLuint)texture,storage))reply->error=GL_OUT_OF_MEMORY;
        else {
            for(alias=c->alias_group->aliases;alias->texture!=(GLuint)texture;alias=alias->next){}
            alias->width=(int)width;alias->height=(int)height;alias->internal=internal;
            alias->binding_generation=c->alias_group->binding_generation;++dg_drawable_alias_imports;
        }
    }
#else
    if(!reply->error && !DGRegisterAlias(c,(GLuint)texture,storage))reply->error=GL_OUT_OF_MEMORY;
#endif
    return 0;
}
static void DGLoadDrawable(struct DGHostContext *c,const struct DGHostDrawable *d)
{
    c->iosurface_draw_dirty=1;
    c->framebuffer=d ? d->framebuffer : 0;c->color=d ? d->color : 0;
    c->back=d ? d->back : 0;c->depth=d ? d->depth : 0;c->drawable=(struct DGHostDrawable *)d;
    DGPrivateTextureWatchName(&c->color_watch,&c->color);
    DGPrivateTextureWatchName(&c->back_watch,&c->back);
    c->color_storage=d?d->color_storage:NULL;c->back_storage=d?d->back_storage:NULL;
    c->shared_surface=d?d->shared_surface:NULL;
}
static int DGSelectDrawableMode(struct DGHostContext *c,uint32_t name,int width,int height,int prepared)
{
    if(!name) return 0;
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    if(c->vram_render_drawable){
        if(c->drawable && c->drawable->name==name && c->drawable->width==width && c->drawable->height==height)return 1;
        if(c->vram_render_serial)return 0; // only explicit completed End releases a wire scope
        if(!DGVRAMRenderEnd(c))return 0;
    }
#endif
    struct DGHostDrawable *d=c->drawables;
    for(;d;d=d->next) if(d->name==name) break;
    if(d && (d->width!=width || d->height!=height)) return 0;
    GLint read_fb,draw_fb;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read_fb);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw_fb);GLuint old=c->framebuffer;
    if(!d) {
        d=calloc(1,sizeof(*d));if(!d) return 0;
        struct DGHostDrawable *previous=c->drawable;DGLoadDrawable(c,NULL);
        int reserved=0;
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
        if(prepared && c->profile==kCGLOGLPVersion_Legacy && !c->double_buffer && !c->depth_bits && !c->stencil_bits){
            glGenFramebuffersEXT(1,&c->framebuffer);
            if(c->framebuffer){
                glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c->framebuffer);
                DGSetBuffer(c,c->draw_mode,0);DGSetBuffer(c,c->read_mode,1);reserved=1;
            }
        }
#else
        (void)prepared;
#endif
        if(!reserved && !DGResize(c,width,height)) { DGDeleteDrawable(c);DGLoadDrawable(c,previous);free(d);return 0; }
        d->name=name;d->width=width;d->height=height;
        d->prepared=reserved;
        if(reserved)__atomic_fetch_add(&dg_prepared_drawable_stats[0],1,__ATOMIC_RELAXED);
        d->framebuffer=c->framebuffer;d->color=c->color;d->back=c->back;d->depth=c->depth;
        DGPrivateTextureWatchName(&d->color_watch,&d->color);
        DGPrivateTextureWatchName(&d->back_watch,&d->back);
        d->color_storage=c->color_storage;d->back_storage=c->back_storage;
        d->shared_surface=c->shared_surface;
        d->next=c->drawables;c->drawables=d;
    }
    DGLoadDrawable(c,d);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read_fb==old ? c->framebuffer : (GLuint)read_fb);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw_fb==old ? c->framebuffer : (GLuint)draw_fb);
    if((GLuint)read_fb==old) DGSetBuffer(c,c->read_mode,1);
    if((GLuint)draw_fb==old) DGSetBuffer(c,c->draw_mode,0);
    return 1;
}
static int DGSelectDrawable(struct DGHostContext *c,uint32_t name,int width,int height)
{return DGSelectDrawableMode(c,name,width,height,0);}
static void DGDropDrawable(struct DGHostContext *c,uint32_t name)
{
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    if(c->vram_render_drawable && c->vram_render_drawable->name==name){
        if(c->inside_begin || c->vram_render_serial){c->error=GL_INVALID_OPERATION;return;}
        if(!DGVRAMRenderEnd(c))c->error=GL_INVALID_OPERATION;
    }
#endif
    struct DGHostDrawable **link=&c->drawables;
    while(*link) {
        struct DGHostDrawable *d=*link;if(d->name!=name) { link=&d->next;continue; }
        struct DGHostDrawable *previous=c->drawable;
        DGLoadDrawable(c,d);DGDeleteDrawableStorage(c,1);
        DGLoadDrawable(c,previous==d ? NULL : previous);
        *link=d->next;DGPrivateTextureWatchForget(&d->color_watch);
        DGPrivateTextureWatchForget(&d->back_watch);free(d);return;
    }
}
static void DGDestroyDrawables(struct DGHostContext *c)
{
    if(getenv("VIM_PASSTHROUGH_OPENGL_RETIRE_TRACE"))
        fprintf(stderr,"DG_RETIRE context=%p renderer=%s deferred=%d double=%d framebuffer=%u color=%u back=%u spare=%u,%u,%u,%u\n",
            (void *)c,glGetString(GL_RENDERER),c->native_drawable_deferred,c->double_buffer,c->framebuffer,c->color,c->back,
            c->spare_textures[0],c->spare_textures[1],c->spare_textures[2],c->spare_textures[3]);
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    if(c->vram_render_drawable){
        // This is final context retirement, not a public End or Drop while
        // inside Begin. Complete an abandoned primitive before unmapping.
        if(c->inside_begin){glEnd();c->inside_begin=0;}
        if(!DGVRAMRenderEnd(c))c->error=GL_INVALID_OPERATION;
    }
#endif
    while(c->drawables) DGDropDrawable(c,c->drawables->name);
    DGVRAMDelete(c);
    if(c->vram_copy_context)CGLDestroyContext(c->vram_copy_context);
    c->vram_copy_context=NULL;c->vram_copy_framebuffer=0;
    if(c->stipple_query_context)CGLDestroyContext(c->stipple_query_context);
    c->stipple_query_context=NULL;
    c->vram_copy_width=c->vram_copy_height=0;
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    if(c->vram_render_framebuffer)glDeleteFramebuffersEXT(1,&c->vram_render_framebuffer);
    c->vram_render_framebuffer=0;
#endif
    for(unsigned i=0;i<4;++i){
        if(c->spare_textures[i])glDeleteTextures(1,&c->spare_textures[i]);c->spare_textures[i]=0;
        if(c->spare_storage[i])CFRelease(c->spare_storage[i]);c->spare_storage[i]=NULL;
        c->spare_widths[i]=c->spare_heights[i]=0;
    }
    c->spare_bytes=0;
}
#include "passthrough-opengl-surfaces-host.c"
#include "direct-vram-drawable-host.c"
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
#include "direct-backing-future-host.c"
#endif
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
#include "direct-backing-coalescing-host.c"
#endif
#ifdef VIM_BACKING_WRITER_EXPERIMENT
#ifndef VIM_BACKING_FUTURE_EXPERIMENT
#error GPU writer continuation requires independent future completion
#endif
#include "direct-backing-writer-host.c"
#endif
static void *DGHostPointer(struct DGMessage *m,int index,size_t bytes)
{
    uint64_t offset=m->args[index];
    if(!offset || bytes==SIZE_MAX || !bytes || offset-1>m->bytes || bytes>m->bytes-(offset-1)) return NULL;
    return (unsigned char *)(m+1)+offset-1;
}
/* Compressed uploads specify their byte span explicitly. Keep client storage
 * distinct from a PBO offset (including offset zero); native GL still owns
 * compressed-format, mip/block alignment and destination validation. */
static int DGHostCompressedInput(struct DGHostContext *c,struct DGMessage *m,
    GLenum target,GLsizei imageSize,int index,const GLvoid **data)
{
    if(m->args[15]>1)return -1;
    if(c->inside_begin){c->error=GL_INVALID_OPERATION;return 0;}
    if(imageSize<0){c->error=GL_INVALID_VALUE;return 0;}
    GLint binding=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);
    if(!!binding!=!!m->args[15]){c->error=GL_INVALID_OPERATION;return 0;}
    if(DGCompressedProxy(target)){
        if(binding && m->bytes)return -1;
        *data=binding?(const void *)(uintptr_t)m->args[index]:NULL;return 1;
    }
    if(!binding){
        *data=DGHostPointer(m,index,imageSize?(size_t)imageSize:1);
        return m->args[index]&&!*data?-1:1;
    }
    if(m->bytes)return -1;
    GLint mapped=0,size32=0;GLint64 size=0;
    glGetBufferParameteriv(GL_PIXEL_UNPACK_BUFFER,GL_BUFFER_MAPPED,&mapped);
    if(c->profile==kCGLOGLPVersion_Legacy){glGetBufferParameteriv(GL_PIXEL_UNPACK_BUFFER,GL_BUFFER_SIZE,&size32);size=size32;}
    else dg_core_api.buffer_parameter64(GL_PIXEL_UNPACK_BUFFER,GL_BUFFER_SIZE,&size);
    if(mapped||size<0||m->args[index]>(uint64_t)size||(uint64_t)imageSize>(uint64_t)size-m->args[index]){
        c->error=GL_INVALID_OPERATION;return 0;
    }
#if defined(__aarch64__) || defined(__arm64__)
    if(c->pbo_writes_pending){glFinish();c->pbo_writes_pending=0;}
#endif
    *data=(const void *)(uintptr_t)m->args[index];return 1;
}
struct DGImagingPixelScope { GLint saved[5];int pack,active; };
static const GLenum dg_imaging_store_keys[2][5]={
    {GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_ROW_BYTES_APPLE},
    {GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_PACK_ROW_BYTES_APPLE}};
static void DGHostImagingEnd(struct DGHostContext *c,struct DGImagingPixelScope *scope)
{
    if(!scope->active)return;
    GLenum error=glGetError();if(error&&!c->error)c->error=error;
    const GLint tight[]={1,0,0,0,0};
    for(unsigned i=0;i<5;++i)if(scope->saved[i]!=tight[i])
        glPixelStorei(dg_imaging_store_keys[scope->pack][i],scope->saved[i]);
    error=glGetError();if(error&&!c->error)c->error=error;
    scope->active=0;
}
static int DGHostImagingTransfer(struct DGHostContext *c,struct DGMessage *m,
    enum DGImagingTransfer op,GLenum target,GLint width,GLint height,GLint start,
    GLenum format,GLenum type,int pack,const int indices[2],void *pixels[2],struct DGImagingPixelScope *scope)
{
    GLenum error=GL_NO_ERROR;
    size_t bits=DGImagingPixelBits(format,type);
    if(c->inside_begin || c->profile!=kCGLOGLPVersion_Legacy)error=GL_INVALID_OPERATION;
    else if(!DGImagingTargetValid(op,target) || !bits)error=GL_INVALID_ENUM;
    else if(width<0 || height<0 || start<0 ||
            (op==DG_IMAGING_TABLE && (width & (width-1))))error=GL_INVALID_VALUE;
    if(error){if(!c->error)c->error=error;return 0;}
    if(op==DG_IMAGING_TABLE && DGImagingProxy(target))
        return (!m->bytes && !m->args[15] && !m->args[indices[0]])?1:-1;

    int compact=m->args[15]==2;
    if(compact){GLenum prior=glGetError();if(prior&&!c->error)c->error=prior;}
    GLint binding=0;
    glGetIntegerv(pack?GL_PIXEL_PACK_BUFFER_BINDING:GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);
    if(m->args[15]>2)return -1;
    if(!!binding!=(m->args[15]==1)){
        if(!c->error)c->error=GL_INVALID_OPERATION;return 0;
    }
    if(binding){
        if(m->bytes)return -1;
        for(int i=0;i<2 && indices[i]>=0;++i){
            uint64_t offset=m->args[indices[i]];
            if(offset>INTPTR_MAX){if(!c->error)c->error=GL_INVALID_OPERATION;return 0;}
            pixels[i]=(void *)(uintptr_t)offset;
        }
        /* Native PBO validation accounts for mapped buffers, alignment and
         * conservative bounds that can differ from the CPU bytes touched. */
#if defined(__aarch64__) || defined(__arm64__)
        if(!pack && c->pbo_writes_pending){glFinish();c->pbo_writes_pending=0;}
#endif
        return 1;
    }
    if(pack && !DGImagingDimensions(op,target,&width,&height))return 0;
    if(op==DG_IMAGING_SUBTABLE){
        GLint extent=0,unused;
        if(!DGImagingDimensions(op,target,&extent,&unused))return 0;
        if((int64_t)start+width>extent){if(!c->error)c->error=GL_INVALID_VALUE;return 0;}
    }
    struct DGPixelState state={0};DGNativePixelState(&state);
    uint64_t spans[2];
    DGImagingSpans(op,width,height,bits,compact?0:state.row_length[pack],compact?1:state.alignment[pack],
        compact?0:state.skip_rows[pack],compact?0:state.skip_pixels[pack],compact?0:state.row_bytes[pack],spans);
    for(int i=0;i<2 && indices[i]>=0;++i){
        if(!spans[i]){if(m->args[indices[i]])return -1;continue;}
        if(spans[i]>DG_MAX_BYTES)return -1;
        pixels[i]=DGHostPointer(m,indices[i],(size_t)spans[i]);
        if(!pixels[i])return -1;
    }
    if(compact){
        scope->pack=pack;scope->saved[0]=state.alignment[pack];scope->saved[1]=state.row_length[pack];
        scope->saved[2]=state.skip_rows[pack];scope->saved[3]=state.skip_pixels[pack];scope->saved[4]=state.row_bytes[pack];
        error=glGetError();if(error){if(!c->error)c->error=error;return 0;}
        scope->active=1;const GLint tight[]={1,0,0,0,0};
        for(unsigned i=0;i<5;++i)if(scope->saved[i]!=tight[i])glPixelStorei(dg_imaging_store_keys[pack][i],tight[i]);
        error=glGetError();if(error){if(!c->error)c->error=error;DGHostImagingEnd(c,scope);return 0;}
    }
    return 1;
}
static int DGHostCompatPixelPointer(struct DGHostContext *c,struct DGMessage *m,
    uint64_t span,size_t alignment,int index,int pack,GLint binding,void **pointer)
{
    if(m->args[15]>1)return -1;
    if(!!binding!=!!m->args[15])goto invalid_operation;
    if(!binding){
        if(!span)return (!m->args[index] && !m->bytes)?1:-1;
        if(m->args[index]!=1 || m->bytes!=span)return -1;
        *pointer=DGHostPointer(m,index,(size_t)span);return *pointer?1:-1;
    }
    if(m->bytes)return -1;
    GLenum target=pack?GL_PIXEL_PACK_BUFFER:GL_PIXEL_UNPACK_BUFFER;
    GLint size=0,mapped=0;
    glGetBufferParameteriv(target,GL_BUFFER_SIZE,&size);
    glGetBufferParameteriv(target,GL_BUFFER_MAPPED,&mapped);
    uint64_t offset=m->args[index];
    if(mapped || size<0 || offset%alignment || offset>(uint64_t)size ||
       span>(uint64_t)size-offset)goto invalid_operation;
#if defined(__aarch64__) || defined(__arm64__)
    if(!pack && c->pbo_writes_pending){glFinish();c->pbo_writes_pending=0;}
#endif
    *pointer=(void *)(uintptr_t)offset;
    return 1;
invalid_operation:
    if(!c->error)c->error=GL_INVALID_OPERATION;
    return 0;
}
/* Pixel maps ignore pixel storage; the native table owns their extent. */
static int DGHostPixelMapPointer(struct DGHostContext *c,struct DGMessage *m,
    GLenum map,GLsizei count,size_t element,int index,int pack,void **pointer)
{
    if(c->inside_begin || c->profile!=kCGLOGLPVersion_Legacy){
        if(!c->error)c->error=GL_INVALID_OPERATION;return 0;
    }
    GLenum key=DGPixelMapSizeKey(map);
    if(!key){if(!c->error)c->error=GL_INVALID_ENUM;return 0;}
    GLint extent=32;
    if(pack || count>extent)glGetIntegerv(pack?key:GL_MAX_PIXEL_MAP_TABLE,&extent);
    if(pack)count=extent;
    else if(!DGPixelMapCountValid(map,count,extent)){if(!c->error)c->error=GL_INVALID_VALUE;return 0;}
    size_t span=DGArrayBytes(count,element);
    if(!span || span==SIZE_MAX)return -1;
    GLint binding=0;
    glGetIntegerv(pack?GL_PIXEL_PACK_BUFFER_BINDING:GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);
    return DGHostCompatPixelPointer(c,m,span,element,index,pack,binding,pointer);
}
static int DGHostCompatRasterPointer(struct DGHostContext *c,struct DGMessage *m,
    GLsizei width,GLsizei height,GLenum format,GLenum type,int index,int pack,void **pointer)
{
    GLenum error=GL_NO_ERROR;
    size_t bits=DGRasterPixelBits(format,type);
    if(c->inside_begin || c->profile!=kCGLOGLPVersion_Legacy)error=GL_INVALID_OPERATION;
    else if(m->op==DG_OP_draw_pixels && DGDrawPixelsNoInput(width,height,format,type)){
        /* A rejected command carries no CPU pixels, or just its original
         * PBO offset. Let native GL validate/record it at the right time. */
        GLint buffer=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
        if(m->args[15]>1 || !!buffer!=!!m->args[15] || m->bytes ||
           (!buffer && m->args[index]))return -1;
        *pointer=buffer?(void *)(uintptr_t)m->args[index]:NULL;return 1;
    }
    else if(width<0 || height<0)error=GL_INVALID_VALUE;
    else if(!bits)error=GL_INVALID_ENUM;
    if(error){if(!c->error)c->error=error;return 0;}
    struct DGPixelState state={0};DGNativePixelState(&state);
    uint64_t span=DGPixelLayoutSpan(width,height,bits,state.row_length[pack],
        state.alignment[pack],state.skip_rows[pack],state.skip_pixels[pack],state.row_bytes[pack]);
    /* Alignment is one datum, not the entire pixel (e.g. three shorts). */
    size_t datum=type==GL_BITMAP?1:DGPixelSize(type==0x85ba || type==0x85bb?format:GL_RED,type);
    return DGHostCompatPixelPointer(c,m,span,datum,index,pack,state.buffers[pack],pointer);
}
static const char *DGHostString(struct DGMessage *m,int index)
{
    uint64_t offset=m->args[index];
    if(!offset || offset-1>=m->bytes) return NULL;
    const char *s=(const char *)(m+1)+offset-1;
    return memchr(s,0,m->bytes-(offset-1)) ? s : NULL;
}
static size_t DGPixelBytes(struct DGHostContext *context,int w,int h,GLenum format,GLenum type,int pack)
{
    (void)context;GLint buffer=0,row=0,alignment=0,skiprows=0,skippixels=0,rowbytes=0;
    glGetIntegerv(pack ? GL_PIXEL_PACK_BUFFER_BINDING : GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
    if(buffer) return SIZE_MAX;
    glGetIntegerv(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,&row);
    glGetIntegerv(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,&alignment);
    glGetIntegerv(pack ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS,&skiprows);
    glGetIntegerv(pack ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS,&skippixels);
    glGetIntegerv(pack ? GL_PACK_ROW_BYTES_APPLE : GL_UNPACK_ROW_BYTES_APPLE,&rowbytes);
    return DGPixelSpan(w,h,format,type,row,alignment,skiprows,skippixels,rowbytes);
}
static void DGTraceRead(struct DGHostContext *,int,int,GLenum,GLenum,const void *);
static int DGHostReadPixels(struct DGHostContext *c,struct DGMessage *m,GLint x,GLint y,
    GLsizei width,GLsizei height,GLenum format,GLenum type)
{
    static const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,
        GL_PACK_SKIP_PIXELS,GL_PACK_ROW_BYTES_APPLE};
    GLint saved[5]={0};
    for(unsigned i=0;i<5;++i)glGetIntegerv(keys[i],&saved[i]);
    size_t span=DGPixelSpan(width,height,format,type,saved[1],saved[0],saved[2],saved[3],saved[4]);
    unsigned char *pixels=DGHostPointer(m,6,span?span:1);
    if(!pixels)return -1;
#if defined(__arm64__)
    /* Apple Silicon OpenGL honors the row pitch but ignores pack skips on
     * our IOSurface color attachment (also reproduced in a native CGL probe).
     * Offset the destination without staging or changing native conversion. */
    if(span && c->color_storage && type!=GL_BITMAP && format!=GL_DEPTH_COMPONENT && format!=GL_STENCIL_INDEX &&
       format!=GL_DEPTH_STENCIL && (saved[2] || saved[3])){
        GLint framebuffer=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&framebuffer);
        if((GLuint)framebuffer==c->framebuffer){
            size_t pixel=DGPixelSize(format,type);
            size_t stride=saved[4]?saved[4]:
                (((size_t)(saved[1]?saved[1]:width)*pixel+saved[0]-1)/saved[0])*saved[0];
            size_t offset=(size_t)saved[2]*stride+(size_t)saved[3]*pixel;
            for(unsigned i=2;i<4;++i)if(saved[i])glPixelStorei(keys[i],0);
            glReadPixels(x,y,width,height,format,type,pixels+offset);
            for(unsigned i=2;i<4;++i)if(saved[i])glPixelStorei(keys[i],saved[i]);
            return 0;
        }
    }
#endif
    glReadPixels(x,y,width,height,format,type,pixels);
    DGTraceRead(c,width,height,format,type,pixels);
    return 0;
}
static size_t DGPixelBytes3D(struct DGHostContext *context,int w,int h,int d,GLenum format,GLenum type,int pack)
{
    (void)context;struct DGPixelState state={0};DGNativePixelState(&state);
    GLint imageheight=0,skipimages=0;
    if(state.buffers[pack])return SIZE_MAX;
    glGetIntegerv(pack?GL_PACK_IMAGE_HEIGHT:GL_UNPACK_IMAGE_HEIGHT,&imageheight);
    glGetIntegerv(pack?GL_PACK_SKIP_IMAGES:GL_UNPACK_SKIP_IMAGES,&skipimages);
    return DGPixelSpan3D(w,h,d,format,type,state.row_length[pack],state.alignment[pack],
        state.skip_rows[pack],state.skip_pixels[pack],state.row_bytes[pack],imageheight,skipimages);
}
#include "passthrough-opengl-compact-texture-host.h"
static int DGTextureReadDimensions(GLenum target)
{
    if(target>=GL_TEXTURE_CUBE_MAP_POSITIVE_X && target<=GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)return 2;
    switch(target){
    case GL_TEXTURE_1D:return 1;
    case GL_TEXTURE_2D:case GL_TEXTURE_RECTANGLE_ARB:case 0x8c18:return 2; /* 1D array */
    case GL_TEXTURE_3D:case 0x8c1a:case 0x9009:return 3; /* 2D and cube arrays */
    default:return 0;
    }
}
/* Bound retained native readbacks across connections, not by wire packet
 * size. No staging GL object is visible in the guest's shared namespace. */
#define DG_TEXTURE_READ_BUDGET (UINT64_C(1)<<30)
static uint64_t dg_texture_read_reserved;
static void DGReleaseTextureRead(struct DGHostContext *c)
{
    if(!c->texture_read_stage)return;
    free(c->texture_read_stage);
    __atomic_fetch_sub(&dg_texture_read_reserved,c->texture_read_reserved_bytes,__ATOMIC_RELAXED);
    c->texture_read_stage=NULL;c->texture_read_bytes=0;c->texture_read_offset=0;c->texture_read_reserved_bytes=0;
    c->buffer_transfer_mode=0;
}
static int DGReserveTextureRead(struct DGHostContext *c,size_t bytes)
{
    DGReleaseTextureRead(c);
    uint64_t used=__atomic_load_n(&dg_texture_read_reserved,__ATOMIC_RELAXED);
    do {
        if(bytes>DG_TEXTURE_READ_BUDGET-used)return 0;
    } while(!__atomic_compare_exchange_n(&dg_texture_read_reserved,&used,used+bytes,
                                          1,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    c->texture_read_stage=calloc(1,bytes);
    if(!c->texture_read_stage){
        __atomic_fetch_sub(&dg_texture_read_reserved,bytes,__ATOMIC_RELAXED);
        return 0;
    }
    c->texture_read_bytes=c->texture_read_reserved_bytes=bytes;return 1;
}
#include "passthrough-opengl-buffer-transfer-host.c"
#include "passthrough-opengl-raster-read-host.c"
static int DGHostTextureRead(struct DGHostContext *c,struct DGMessage *m,struct DGReply *r)
{
    unsigned mode=(unsigned)m->args[0],compressed=(unsigned)m->args[1];
    GLenum target=(GLenum)m->args[2],format=(GLenum)m->args[4],type=(GLenum)m->args[5];
    GLint level=(GLint)m->args[3];
    if(m->args[0]>DG_TEXTURE_READ_CHUNK || m->args[1]>1 || m->args[2]!=target ||
        m->args[3]!=(uint64_t)(int64_t)level || m->args[4]!=format || m->args[5]!=type ||
        m->args[7]>INT_MAX || (compressed && (format || type || m->args[7])))return -1;
    GLint row_override=(GLint)m->args[7];
    for(unsigned i=9;i<16;++i)if(m->args[i])return -1;
    if(mode==DG_TEXTURE_READ_CHUNK){
        if(m->flags!=(DG_REPLY|DG_OUTPUT_ONLY) || m->args[6]!=1 ||
            compressed || target || level || format || type || row_override ||
            !m->bytes || m->bytes>DG_TEXTURE_READ_CHUNK_BYTES)return -1;
        if(c->buffer_transfer_mode || !c->texture_read_stage || m->args[8]!=c->texture_read_offset ||
            m->bytes>c->texture_read_bytes-c->texture_read_offset){
            DGReleaseTextureRead(c);r->error=GL_INVALID_OPERATION;return 0;
        }
        memcpy(m+1,c->texture_read_stage+c->texture_read_offset,m->bytes);
        c->texture_read_offset+=m->bytes;
        if(c->texture_read_offset==c->texture_read_bytes)DGReleaseTextureRead(c);
        r->result=m->bytes;return 0;
    }
    if(mode==DG_TEXTURE_READ_PBO){
        if(m->flags || m->bytes || m->args[8])return -1;
    }else if(mode==DG_TEXTURE_READ_STAGE){
        if(m->flags!=DG_REPLY || m->bytes || m->args[6] || m->args[8]<=DG_MAX_BYTES)return -1;
        DGReleaseTextureRead(c);
    }else if(m->flags!=(DG_REPLY|DG_OUTPUT_ONLY) || m->args[6]!=1 ||
        (mode==DG_TEXTURE_READ_LAYOUT ? (m->bytes!=sizeof(struct DGTextureReadLayout) || m->args[8]>1)
                                     : m->args[8]!=0))return -1;
    if(c->inside_begin){
        if(mode!=DG_TEXTURE_READ_PBO)r->error=GL_INVALID_OPERATION;
        else if(!c->error)c->error=GL_INVALID_OPERATION;
        return 0;
    }
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint binding=0;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);
    if(mode==DG_TEXTURE_READ_PBO){
        /* A checked nonzero host binding makes this an offset, never a
         * process pointer. Native GL validates mapping, range and alignment. */
        if(binding && m->args[6]<=INTPTR_MAX){
            void *offset=(void *)(uintptr_t)m->args[6];
            GLint saved=0;
            if(row_override){
                glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&saved);
                glPixelStorei(GL_PACK_ROW_BYTES_APPLE,row_override);
            }
            if(compressed)glGetCompressedTexImage(target,level,offset);
            else glGetTexImage(target,level,format,type,offset);
            GLenum error=glGetError();
            if(row_override)glPixelStorei(GL_PACK_ROW_BYTES_APPLE,saved);
            if(error){if(!c->error)c->error=error;}
            else DGPBOWritten(c);
        }else if(!c->error)c->error=GL_INVALID_OPERATION;
        return 0;
    }
    if(binding){r->error=GL_INVALID_OPERATION;return 0;}
    int dimensions=DGTextureReadDimensions(target);
    if(!dimensions){r->error=GL_INVALID_ENUM;return 0;}
    struct DGTextureReadLayout layout={0};
    GLint values[7]={0};
    static const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,
        GL_PACK_SKIP_PIXELS,GL_PACK_ROW_BYTES_APPLE,GL_PACK_IMAGE_HEIGHT,GL_PACK_SKIP_IMAGES};
    const unsigned count=dimensions==3?7:5;
    if(compressed){
        GLint is_compressed=0,bytes=0;
        glGetTexLevelParameteriv(target,level,GL_TEXTURE_COMPRESSED,&is_compressed);
        r->error=glGetError();if(r->error)return 0;
        if(!is_compressed){r->error=GL_INVALID_OPERATION;return 0;}
        glGetTexLevelParameteriv(target,level,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,&bytes);
        r->error=glGetError();if(r->error)return 0;
        if(bytes>0)layout=(struct DGTextureReadLayout){.row_bytes=bytes,.rows=1,.images=1};
    }else{
        size_t pixel=DGPixelSize(format,type);
        if(!pixel){r->error=GL_INVALID_ENUM;return 0;}
        GLint width=0,height=1,depth=1;
        glGetTexLevelParameteriv(target,level,GL_TEXTURE_WIDTH,&width);
        if(dimensions>=2)glGetTexLevelParameteriv(target,level,GL_TEXTURE_HEIGHT,&height);
        if(dimensions==3)glGetTexLevelParameteriv(target,level,GL_TEXTURE_DEPTH,&depth);
        r->error=glGetError();if(r->error)return 0;
        if(width>0 && height>0 && depth>0){
            for(unsigned i=0;i<count;++i)glGetIntegerv(keys[i],&values[i]);
            r->error=glGetError();if(r->error)return 0;
            unsigned alignment=values[0];
            uint64_t row=(uint64_t)(values[1]?values[1]:width)*pixel;
            GLint row_bytes=row_override?row_override:values[4];
            layout.row_stride=row_bytes?(uint64_t)row_bytes:(row+alignment-1)&~(uint64_t)(alignment-1);
            uint64_t image_rows=values[5]?values[5]:height;
            if(layout.row_stride>UINT64_MAX/image_rows ||
                (uint64_t)width*pixel>UINT32_MAX){r->error=GL_OUT_OF_MEMORY;return 0;}
            layout.image_stride=image_rows*layout.row_stride;
            uint64_t skip=(uint64_t)values[3]*pixel;
            if(values[2] && layout.row_stride>(UINT64_MAX-skip)/(uint64_t)values[2]){
                r->error=GL_OUT_OF_MEMORY;return 0;
            }
            skip+=(uint64_t)values[2]*layout.row_stride;
            if(values[6] && layout.image_stride>(UINT64_MAX-skip)/(uint64_t)values[6]){
                r->error=GL_OUT_OF_MEMORY;return 0;
            }
            layout.offset=skip+(uint64_t)values[6]*layout.image_stride;
            layout.row_bytes=(uint32_t)(width*pixel);layout.rows=height;layout.images=depth;
        }
    }
    uint64_t end=0;size_t bytes=DGTextureReadSize(&layout,&end);
    if(bytes==SIZE_MAX){r->error=GL_OUT_OF_MEMORY;return 0;}
    if(mode==DG_TEXTURE_READ_LAYOUT){
        if(bytes>DG_MAX_BYTES && !m->args[8]){r->error=GL_OUT_OF_MEMORY;return 0;}
        memcpy(m+1,&layout,sizeof(layout));r->result=sizeof(layout);return 0;
    }
    void *output=m+1;
    if(mode==DG_TEXTURE_READ_STAGE){
        if(bytes!=m->args[8]){r->error=GL_INVALID_OPERATION;return 0;}
        if(!DGReserveTextureRead(c,bytes)){r->error=GL_OUT_OF_MEMORY;return 0;}
        output=c->texture_read_stage;
    }else if(!bytes || m->bytes!=bytes)return -1;
    if(compressed)glGetCompressedTexImage(target,level,output);
    else {
        /* Send only touched image bytes, keeping byte-swap/format conversion
         * native. Pack state is restored before any following guest command. */
        for(unsigned i=0;i<count;++i)if(values[i]!=(i==0))glPixelStorei(keys[i],i==0);
        glGetTexImage(target,level,format,type,output);
        r->error=glGetError();
        for(unsigned i=0;i<count;++i)if(values[i]!=(i==0))glPixelStorei(keys[i],values[i]);
    }
    if(!r->error)r->error=glGetError();
    if(r->error && mode==DG_TEXTURE_READ_STAGE)DGReleaseTextureRead(c);
    if(!r->error)r->result=bytes;
    return 0;
}
/* Apple's native accelerated volume fast path (also reproduced on physical
 * Mavericks Intel) applies IMAGE_HEIGHT to the initial skip but advances
 * subsequent slices by the upload height. The generic GLEngine storage
 * helper computes the correct image stride. Submit one native slice at a
 * time only for this divergent case; ordinary packed volumes remain one
 * call. No CPU conversion/readback or app/shader-specific exception. */
static void DGHostTexture3D(struct DGHostContext *c,int subimage,GLenum target,GLint level,GLint internal,
    GLint x,GLint y,GLint z,GLsizei width,GLsizei height,GLsizei depth,GLint border,
    GLenum format,GLenum type,const void *pixels,int pbo)
{
    GLint imageheight=0;glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT,&imageheight);
    if(!imageheight || imageheight==height || !width || !height || !depth || (!pixels && !pbo) ||
       (!subimage && target==GL_PROXY_TEXTURE_3D)) {
        if(subimage)glTexSubImage3D(target,level,x,y,z,width,height,depth,format,type,pixels);
        else glTexImage3D(target,level,internal,width,height,depth,border,format,type,pixels);
        return;
    }
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    if(subimage) {
        /* Validate the entire destination before splitting. Otherwise an
         * invalid multi-slice request could mutate its in-range slices
         * before a later slice finally raised INVALID_VALUE. */
        GLint bounds[4]={0};const GLenum keys[]={GL_TEXTURE_WIDTH,GL_TEXTURE_HEIGHT,GL_TEXTURE_DEPTH,GL_TEXTURE_BORDER};
        for(unsigned i=0;i<3;++i)glGetTexLevelParameteriv(target,level,keys[i],bounds+i);
        if(c->profile==kCGLOGLPVersion_Legacy)glGetTexLevelParameteriv(target,level,GL_TEXTURE_BORDER,bounds+3);
        GLenum error=glGetError();if(error) { if(!c->error)c->error=error;return; }
        if((int64_t)x < -(int64_t)bounds[3] || (int64_t)y < -(int64_t)bounds[3] || (int64_t)z < -(int64_t)bounds[3] ||
           (int64_t)x+width>(int64_t)bounds[0]-bounds[3] ||
           (int64_t)y+height>(int64_t)bounds[1]-bounds[3] ||
           (int64_t)z+depth>(int64_t)bounds[2]-bounds[3]) { c->error=GL_INVALID_VALUE;return; }
    }
    struct DGPixelState state={0};DGNativePixelState(&state);GLint skips=0;
    glGetIntegerv(GL_UNPACK_SKIP_IMAGES,&skips);
    uint64_t span=DGPixelSpan3DWide(width,height,depth,format,type,state.row_length[0],state.alignment[0],
        state.skip_rows[0],state.skip_pixels[0],state.row_bytes[0],imageheight,skips);
    if(span==UINT64_MAX || (!pbo&&span>DG_MAX_BYTES)) { c->error=GL_INVALID_OPERATION;return; }
    uint64_t bits=DGRasterPixelBits(format,type),stride=state.row_bytes[0] ? (uint64_t)state.row_bytes[0] :
        (((((uint64_t)(state.row_length[0]?state.row_length[0]:width)*bits+7)/8)+state.alignment[0]-1)/state.alignment[0])*state.alignment[0];
    /* Rebinding a deleted-but-retained name must not create a new PBO. */
    if(!subimage && pbo && !glIsBuffer(state.buffers[0])) { c->error=GL_INVALID_OPERATION;return; }
    if(!subimage) {
        if(pbo)glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        glTexImage3D(target,level,internal,width,height,depth,border,format,type,NULL);
        GLenum error=glGetError();if(pbo)glBindBuffer(GL_PIXEL_UNPACK_BUFFER,state.buffers[0]);
        if(error) { if(!c->error)c->error=error;return; }
        x=y=z=-border;
    }
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,0);glPixelStorei(GL_UNPACK_SKIP_IMAGES,0);
    for(GLsizei i=0;i<depth;++i) {
        const void *slice=(const void *)((uintptr_t)pixels+((uint64_t)skips+i)*imageheight*stride);
        glTexSubImage3D(target,level,x,y,z+i,width,height,1,format,type,slice);
    }
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,imageheight);glPixelStorei(GL_UNPACK_SKIP_IMAGES,skips);
}
static void DGTraceTexture(struct DGHostContext *c,int w,int h,GLenum format,GLenum type,const void *pixels)
{
    if(!dg_diagnostics || c->texture_debug_count++>=32) return;
    size_t bytes=DGPixelBytes(c,w,h,format,type,0),nonzero=0,samples=0;
    if(pixels && bytes!=SIZE_MAX) {
        size_t stride=bytes/4096 ? bytes/4096 : 1;
        for(size_t i=0;i<bytes;i+=stride) { ++samples;if(((const unsigned char *)pixels)[i]) ++nonzero; }
    }
    fprintf(stderr,"direct texture upload=%dx%d format=%x type=%x bytes=%zu nonzero=%zu/%zu\n",w,h,format,type,bytes,nonzero,samples);
    if(pixels && w==256 && h==256) {
        GLint pitch=0,row=0;glGetIntegerv(GL_UNPACK_ROW_BYTES_APPLE,&pitch);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
        fprintf(stderr,"direct texture pixelstore row_bytes=%d row_length=%d input_rgba:",pitch,row);
        for(unsigned y=0;y<256;y+=16) {
            const unsigned char *p=(const unsigned char *)pixels+y*1024;
            fprintf(stderr," %u:%u,%u,%u,%u",y,p[0],p[1],p[2],p[3]);
        }
        fputc('\n',stderr);
    }
}
static void DGTraceRead(struct DGHostContext *c,int w,int h,GLenum format,GLenum type,const void *pixels)
{
    if(!dg_diagnostics || !pixels || w<2000 || h<1000 || c->frame_debug_count++>=16 ||
       (format!=GL_BGRA && format!=GL_RGBA) || type!=GL_UNSIGNED_INT_8_8_8_8_REV) return;
    GLint row=0,alignment=0,skiprows=0,skippixels=0,framebuffer=0,rowbytes=0;
    glGetIntegerv(GL_PACK_ROW_LENGTH,&row);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glGetIntegerv(GL_PACK_SKIP_ROWS,&skiprows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skippixels);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&framebuffer);
    glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&rowbytes);
    if(skiprows || skippixels || (row && row!=w) || alignment>4 || (rowbytes && rowbytes!=w*4)) return;
    fprintf(stderr,"direct frame read=%dx%d framebuffer=%d default=%u drawable=%u row_rgb:",w,h,framebuffer,c->framebuffer,c->drawable ? c->drawable->name : 0);
    int previous=-1,begin=0,transitions=0;
    for(int y=0;y<=h;++y) {
        int visible=0;
        if(y<h) for(int x=0;x<w;x+=32) {
            const unsigned char *p=(const unsigned char *)pixels+((size_t)y*w+x)*4;
            if(p[0] || p[1] || p[2]) { visible=1;break; }
        }
        if(visible!=previous || y==h) {
            if(previous>=0 && transitions++<32) fprintf(stderr," %d-%d:%d",begin,y-1,previous);
            begin=y;previous=visible;
        }
    }
    fputc('\n',stderr);
}
static int DGHostShaderSource(struct DGHostContext *context,struct DGMessage *m,struct DGReply *reply)
{
    (void)context;(void)reply;
    if(m->args[1]>DG_MAX_BYTES) return -1;
    const char *s=DGHostPointer(m,2,(size_t)m->args[1]+1);if(!s) return -1;
    GLint length=(GLint)m->args[1];glShaderSource((GLuint)m->args[0],1,&s,&length);return 0;
}
static void DGHostCompileShader(struct DGHostContext *context,GLuint shader)
{
    GLenum error=DGCompileAdaptedShader(shader,context->inside_begin);
    if(error&&!context->error)context->error=error;
}
static void DGHostGenerateMipmap(struct DGHostContext *context,GLenum target)
{
    /* Internal correction commands must not be recorded in a caller's
     * native list. Leave generation there to the native list executor. */
    if(context->list_mode){glGenerateMipmapEXT(target);return;}
    GLenum error=DGGenerateSrgbMipmap(target,context->profile==kCGLOGLPVersion_Legacy,context->inside_begin);
    if(error&&!context->error)context->error=error;
}
static const GLchar *DGHostIncludeString(struct DGMessage *m,int index,GLint length)
{
    return length<0?DGHostString(m,index):DGHostPointer(m,index,length?(size_t)length:1);
}
static int DGHostCompileShaderInclude(struct DGHostContext *c,struct DGMessage *m)
{
    if(m->flags || m->args[0]>UINT32_MAX || m->args[1]>DG_FEEDBACK_NAME_LIMIT)return -1;
    for(unsigned i=2;i<16;++i)if(m->args[i])return -1;
    GLsizei count=(GLsizei)m->args[1];
    size_t header=(size_t)count*sizeof(GLint);
    if(header+(size_t)count>m->bytes)return -1;
    typedef void (*CompileProc)(GLuint,GLsizei,const GLchar *const *,const GLint *);
    static CompileProc compile;
    if(!compile)compile=(CompileProc)dlsym(RTLD_DEFAULT,"glCompileShaderIncludeARB");
    if(!compile || c->inside_begin){if(!c->error)c->error=GL_INVALID_OPERATION;return 0;}
    const GLchar *local[32],**paths=count<=32?local:malloc((size_t)count*sizeof(*paths));
    if(!paths){if(!c->error)c->error=GL_OUT_OF_MEMORY;return 0;}
    const GLint *sizes=(const GLint *)(m+1);
    const char *next=(const char *)sizes+header,*end=(const char *)sizes+m->bytes;
    int valid=1;
    for(GLsizei i=0;i<count;++i){
        GLint length=sizes[i];
        if(length<0 || length>=end-next || next[length]){valid=0;break;}
        paths[i]=next;next+=(size_t)length+1;
    }
    valid=valid && next==end;
    if(valid)compile((GLuint)m->args[0],count,count?paths:NULL,count?sizes:NULL);
    if(paths!=local)free(paths);
    return valid?0:-1;
}
static int DGHostFeedbackVaryings(struct DGHostContext *c,struct DGMessage *m)
{
    if(m->flags || m->args[0]>UINT32_MAX || m->args[1]>DG_FEEDBACK_NAME_LIMIT ||
        m->args[2]>UINT32_MAX || m->args[1]>m->bytes)return -1;
    for(unsigned i=3;i<16;++i)if(m->args[i])return -1;
    unsigned count=(unsigned)m->args[1];
    const GLchar *local[32],**names=count<=32?local:malloc(count*sizeof(*names));
    if(!names){if(!c->error)c->error=GL_OUT_OF_MEMORY;return 0;}
    const char *next=(const char *)(m+1),*end=next+m->bytes;
    for(unsigned i=0;i<count && next<end;++i){
        const char *nul=memchr(next,0,end-next);
        if(!nul){next=NULL;break;}
        names[i]=next;next=nul+1;
        if(next==end && i+1!=count){next=NULL;break;}
    }
    int valid=next==end;
    if(valid)glTransformFeedbackVaryingsEXT((GLuint)m->args[0],(GLsizei)count,count?names:NULL,(GLenum)m->args[2]);
    if(names!=local)free(names);
    return valid?0:-1;
}
static int DGHostCreateShaderProgram(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    if((m->flags&(DG_REPLY|DG_INPUT_ONLY))!=(DG_REPLY|DG_INPUT_ONLY) ||
       (m->flags&~(DG_REPLY|DG_INPUT_ONLY|DG_OBSERVE_ERROR_EMPTY)) ||
       m->args[0]>UINT32_MAX || m->args[1]>UINT32_MAX)return -1;
    for(unsigned i=2;i<16;++i)if(m->args[i])return -1;
    GLsizei count=(int32_t)(uint32_t)m->args[1];
    if(count>0 && ((unsigned)count>DG_FEEDBACK_NAME_LIMIT || (unsigned)count>m->bytes))return -1;
    typedef GLuint (*CreateProc)(GLenum,GLsizei,const GLchar *const *);
    CreateProc create=(CreateProc)dlsym(RTLD_DEFAULT,"glCreateShaderProgramv");
    if(!create || c->inside_begin){c->error=GL_INVALID_OPERATION;return 0;}
    const GLchar *local[32],**strings=count<=32?local:malloc((size_t)count*sizeof(*strings));
    if(!strings){c->error=GL_OUT_OF_MEMORY;return 0;}
    const char *next=(const char *)(m+1),*end=next+m->bytes;
    for(GLsizei i=0;i<count;++i){
        const char *nul=memchr(next,0,end-next);
        if(!nul){next=NULL;break;}
        strings[i]=next;next=nul+1;
    }
    int valid=next==end;
    if(valid){
        GLenum compile_error=GL_NO_ERROR;
        reply->result=DGCreateAdaptedShaderProgram(create,(GLenum)m->args[0],count,count>0?strings:NULL,&compile_error);
        if(compile_error&&!c->error)c->error=compile_error;
    }
    if(strings!=local)free(strings);
    return valid?0:-1;
}
static int DGHostUniformIndices(struct DGHostContext *c,struct DGMessage *m)
{
    if(!(m->flags&DG_REPLY) || (m->flags&~(DG_REPLY|DG_OBSERVE_ERROR_EMPTY)) ||
       m->args[0]>UINT32_MAX || m->args[1]>DG_FEEDBACK_NAME_LIMIT)return -1;
    for(unsigned i=3;i<16;++i)if(m->args[i])return -1;
    unsigned count=(unsigned)m->args[1];
    size_t output=(size_t)count*sizeof(GLuint);
    if(m->args[2]!=(count?1u:0u) || output+count>m->bytes)return -1;
    if(c->inside_begin || !dg_core_api.uniform_indices){c->error=GL_INVALID_OPERATION;return 0;}
    const GLchar *local[32],**names=count<=32?local:malloc(count*sizeof(*names));
    if(!names){c->error=GL_OUT_OF_MEMORY;return 0;}
    char *begin=(char *)(m+1);
    const char *next=begin+output,*end=begin+m->bytes;
    for(unsigned i=0;i<count;++i){
        const char *nul=memchr(next,0,end-next);
        if(!nul){next=NULL;break;}
        names[i]=next;next=nul+1;
    }
    int valid=next==end;
    if(valid)dg_core_api.uniform_indices((GLuint)m->args[0],(GLsizei)count,
        count?names:NULL,count?(GLuint *)begin:NULL);
    if(names!=local)free(names);
    return valid?0:-1;
}
static int DGHostUniformBlockCapacity(struct DGHostContext *c,GLuint program,GLuint block,GLint capacity)
{
    if(c->inside_begin || !dg_core_api.uniform_block_iv){c->error=GL_INVALID_OPERATION;return 0;}
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint current=-1;
    dg_core_api.uniform_block_iv(program,block,0x8a42,&current); /* ACTIVE_UNIFORMS */
    GLenum error=glGetError();
    if(error || current!=capacity){
        if(!c->error)c->error=error?error:GL_INVALID_OPERATION;
        return 0;
    }
    return 1;
}
static int DGHostSubroutineCapacity(struct DGHostContext *c,GLuint program,GLenum stage,
                                   GLuint index,GLint capacity)
{
    if(c->inside_begin || !dg_core_api.active_subroutine_iv){
        if(!c->error)c->error=GL_INVALID_OPERATION;return 0;
    }
    /* A shared program may have been relinked since the guest measured the
     * result. Never let native reflection write past the reserved reply. */
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint current=-1;
    dg_core_api.active_subroutine_iv(program,stage,index,0x8e4a,&current);
    GLenum error=glGetError();
    if(error || current!=capacity){
        if(!c->error)c->error=error?error:GL_INVALID_OPERATION;return 0;
    }
    return 1;
}
static int DGHostStateArrayCapacity(struct DGHostContext *c,GLenum count_key,GLint capacity)
{
    if(c->inside_begin){if(!c->error)c->error=GL_INVALID_OPERATION;return 0;}
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint current=-1;
    if(count_key==GL_MAX_VERTEX_UNITS_ARB)current=DGHostWeightUnits(c);
    else glGetIntegerv(count_key,&current);
    GLenum error=glGetError();
    if(error || current!=capacity){
        if(!c->error)c->error=error?error:GL_INVALID_OPERATION;return 0;
    }
    return 1;
}
static int DGTightTexSubImage2D(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    (void)reply;
    if(m->flags || m->args[8]!=1)return -1;
    for(unsigned i=0;i<8;++i)if(m->args[i]>UINT32_MAX)return -1;
    for(unsigned i=9;i<16;++i)if(m->args[i])return -1;
    GLsizei width=(int32_t)m->args[4],height=(int32_t)m->args[5];
    size_t bytes=DGPixelSpan(width,height,(GLenum)m->args[6],(GLenum)m->args[7],0,1,0,0,0);
    if(!bytes || bytes==SIZE_MAX || bytes!=m->bytes)return -1;
    if(c->inside_begin){if(!c->error)c->error=GL_INVALID_OPERATION;return 0;}
    GLint buffer=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
    if(buffer){if(!c->error)c->error=GL_INVALID_OPERATION;return 0;}
    // Pixel-store calls are not compiled into display lists. Capture/restore
    // their real current values around the native upload; swap/LSB remain
    // untouched, as do texture selection and validation/error semantics.
    const GLenum keys[]={GL_UNPACK_ROW_LENGTH,GL_UNPACK_ALIGNMENT,GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS,GL_UNPACK_ROW_BYTES_APPLE};
    const GLint tight[]={0,1,0,0,0};GLint saved[5];
    if(!DGPrepareScopedTextureCPUWrite(c,(GLenum)m->args[0]))return 0;
    for(unsigned i=0;i<5;++i){glGetIntegerv(keys[i],saved+i);glPixelStorei(keys[i],tight[i]);}
    struct DGAutomaticMipmapScope automatic=DGBeginAutomaticMipmap((GLenum)m->args[0],(int32_t)m->args[1],
        c->profile==kCGLOGLPVersion_Legacy,c->inside_begin,c->list_mode,
        &c->error,0,DGNonzeroMipmapBasePossible(c));
    glTexSubImage2D((GLenum)m->args[0],(int32_t)m->args[1],(int32_t)m->args[2],(int32_t)m->args[3],
        width,height,(GLenum)m->args[6],(GLenum)m->args[7],m+1);
    for(unsigned i=0;i<5;++i)glPixelStorei(keys[i],saved[i]);
    DGEndAutomaticMipmap(&automatic,NULL,&c->error);
    return 0;
}
/* A data-free preflight lets GL reject invalid requests without reading a
 * guest pointer. Validate the upload again: the executor does not trust
 * that a guest ran a preflight or packed the points correctly. */
static int DGHostEvaluatorInput(struct DGHostContext *c,struct DGMessage *m,
    struct DGReply *reply,GLuint target,unsigned width,GLint ustride,GLint uorder,GLint vstride,
    GLint vorder,int domain_valid,size_t element,int dimensions,int index,int apple)
{
    uint64_t preflight=m->args[15];
    if(preflight>1 || (preflight?(m->flags!=DG_REPLY || m->bytes || m->args[index]):
                               (m->flags || m->args[index]!=1)))return -1;
    GLenum error=GL_NO_ERROR;
    if(c->inside_begin || c->profile!=kCGLOGLPVersion_Legacy)error=GL_INVALID_OPERATION;
    else {
        GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
        GLint max_order=0,max_index=0;
        glGetIntegerv(GL_MAX_EVAL_ORDER,&max_order);
        if(apple)glGetIntegerv(GL_MAX_VERTEX_ATTRIBS_ARB,&max_index);
        // Intel 10.13 and Apple Silicon both accept maps on nonzero
        // active units and update the same evaluator map as unit zero.
        if(!apple && DGMapDimensions(target)!=(unsigned)dimensions)error=GL_INVALID_ENUM;
        else if((apple && (max_index<=0 || target>=(GLuint)max_index || width<1 || width>4)) ||
            !domain_valid || uorder<1 || uorder>max_order || ustride<(GLint)width ||
            (dimensions==2 && (vorder<1 || vorder>max_order || vstride<(GLint)width)))
            error=GL_INVALID_VALUE;
    }
    if(error){if(!c->error)c->error=error;return 0;}
    if(preflight){reply->result=1;return 0;}
    size_t bytes=DGEvaluatorCoefficientBytes(width,uorder,vorder,element);
    if(m->bytes!=bytes || ustride!=vorder*(GLint)width ||
       (dimensions==2 && vstride!=(GLint)width))return -1;
    return 1;
}
static int DGHostAppleEvaluatorQuery(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply,unsigned type)
{
    GLuint index=(GLuint)m->args[0];GLenum pname=(GLenum)m->args[1];
    size_t element=type==0?sizeof(GLint):type==1?sizeof(GLfloat):sizeof(GLdouble);
    unsigned kind=(pname-0x8a02)&3,dimensions=pname>=0x8a06?2:1;
    int preflight=m->args[3]==1;
    if(m->args[3]>1 || (preflight?(kind!=1 || m->flags!=DG_REPLY || m->bytes || m->args[2]):
        (m->flags!=(DG_REPLY|DG_OUTPUT_ONLY) || m->args[2]!=1)))return -1;
    if(c->inside_begin || c->profile!=kCGLOGLPVersion_Legacy){reply->error=GL_INVALID_OPERATION;return 0;}
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    size_t bytes=(kind==3?2*dimensions:kind==2?dimensions:1)*element;
    if(kind==1){
        GLint width=0,order[2]={0,1};
        glGetVertexAttribivARB(index,pname-1,&width);
        glGetVertexAttribivARB(index,pname+1,order);
        reply->error=glGetError();if(reply->error)return 0;
        bytes=DGEvaluatorCoefficientBytes((unsigned)width,order[0],order[1],element);
        if(bytes==SIZE_MAX){reply->error=GL_INVALID_OPERATION;return 0;}
    }
    if(preflight){reply->result=bytes;return 0;}
    if(m->bytes!=bytes)return -1;
    void *out=m+1;
    if(type==0)glGetVertexAttribivARB(index,pname,out);
    else if(type==1)glGetVertexAttribfvARB(index,pname,out);
    else glGetVertexAttribdvARB(index,pname,out);
    reply->error=glGetError();
    if(!reply->error)reply->result=bytes;
    return 0;
}
static int DGExecute(struct DGHostContext *context,struct DGMessage *dg_wire_message,struct DGReply *reply)
{
    switch(dg_wire_message->op) {
/* Adapt our own typed dispatch call, not an import in Apple's framework. */
#define glCompileShader(shader) DGHostCompileShader(context,(shader))
#include "direct-host-generated.inc"
#undef glCompileShader
    default:context->error=GL_INVALID_OPERATION;return 0;
    }
}
static int DGRendererMemory(struct DGHostContext *context,struct DGReply *reply)
{
    GLint screen=0,id=0,n=0,video=0,texture=0;CGLRendererInfoObj info=NULL;
    CGLError error=CGLGetVirtualScreen(context->cgl,&screen);
    if(!error) error=CGLDescribePixelFormat(CGLGetPixelFormat(context->cgl),screen,kCGLPFARendererID,&id);
    if(!error) error=CGLQueryRendererInfo(~0U,&info,&n);
    int matched=0;
    for(GLint i=0;!error && i<n;++i) {
        GLint candidate=0;error=CGLDescribeRenderer(info,i,kCGLRPRendererID,&candidate);
        if(error || (candidate&0x00fe7f00)!=(id&0x00fe7f00)) continue;
        error=CGLDescribeRenderer(info,i,kCGLRPVideoMemoryMegabytes,&video);
        if(!error) error=CGLDescribeRenderer(info,i,kCGLRPTextureMemoryMegabytes,&texture);
        matched=!error;break;
    }
    if(info) CGLDestroyRendererInfo(info);
    if(!matched || video<0 || texture<0) { reply->error=GL_INVALID_OPERATION;return 0; }
    reply->result=(uint32_t)video|((uint64_t)(uint32_t)texture<<32);return 0;
}
/* Persistent interpreter state is independent of transport and native thread.
 * A connection must serialize calls and bind/unbind its CGL contexts when
 * changing threads. Socket/memory-worker semantics remain unchanged. */
struct DGInterpreter {
    struct DGHostContext contexts[256];
    unsigned next,client_features;
    uint32_t core_extension_candidates;
    int hello,service_only;
    uint64_t surface_namespace[2];
    struct DGSurfaceHold *surface_holds;
    uint32_t next_surface_hold;
#ifdef VIM_PASSTHROUGH_OPENGL_PACKET_JOURNAL
    int journal_fd,journal_depth;
    uint64_t journal_bytes;
#endif
};
#ifdef VIM_PASSTHROUGH_OPENGL_PACKET_JOURNAL
#include "../probes/opengl-packet-journal.inc"
#endif
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
#include "direct-backing-read-only.inc"
static int DGBackingPacketMayWrite(uint32_t operation)
{
    if(operation<974)return !dg_backing_read_only[operation];
    switch(operation){
    case DG_BACKING_RENDER_BEGIN:case DG_BACKING_RENDER_CONTINUE:case DG_BACKING_RENDER_END:case DG_BACKING_RENDER_MATERIALIZE:
    case DG_BACKING_RENDER_DEFER:case DG_BACKING_COMPLETION_WAIT:case DG_BACKING_COMPLETION_RELEASE:case DG_BACKING_COMPLETION_CHECK:
    case DG_FLUSH_DRAWABLE_STATE:case DG_FLUSH_IOSURFACE_STATE:case DG_BIND_DRAWABLE_TEXTURE:
    case DG_ERROR:case DG_ERROR_STATE:case DG_STRING:case DG_COMMON_STATE:case DG_VRAM_STATS:
    case DG_DRAWABLE_DAMAGE:case DG_UNIFORM_QUERY:case DG_TEXTURE_READ:return 0;
    default:return !(operation>=DG_SYNC_FENCE && operation<=DG_SYNC_GET);
    }
}
#endif
static int DGHeaderValid(const struct DGMessage *header)
{
    if(header->op==DG_COMPACT_SCALARS && (header->context || header->flags ||
       header->bytes<2*sizeof(struct DGCompactScalar) || header->bytes>DG_BATCH_BYTES-sizeof(*header)))return 0;
    return !(header->bytes>DG_MAX_BYTES || (header->flags&~(DG_REPLY|DG_OUTPUT_ONLY|DG_INPUT_ONLY|DG_OBSERVE_ERROR_EMPTY)) ||
           ((header->flags&DG_OBSERVE_ERROR_EMPTY) && (!(header->flags&DG_REPLY)||!header->context)) ||
           ((header->flags&DG_INPUT_ONLY) && ((header->op!=DG_BUFFER_TRANSFER && header->op!=DG_SURFACE_UPLOAD && header->op!=DG_PBO_DELETE && header->op!=DG_SURFACE_PUBLISH_REGIONS && header->op!=DG_SURFACE_COPY_REGIONS && header->op!=DG_SURFACE_SEALED_REGIONS && header->op!=DG_VRAM_PUBLISH_DRAWABLE && header->op!=DG_VRAM_IMPORT_DRAWABLE && header->op!=DG_VRAM_PATCH_DRAWABLE && header->op!=DG_IOSURFACE_TEXTURE && header->op!=DG_IOSURFACE_TEXTURE_PACKED && header->op!=DG_BACKING_RENDER_BEGIN && header->op!=DG_BACKING_RENDER_CONTINUE && header->op!=DG_OP_create_shader_programv) || !(header->flags&DG_REPLY) || (header->flags&DG_OUTPUT_ONLY))) ||
           ((header->flags&DG_OUTPUT_ONLY) &&
            ((header->op!=DG_UNPACK_SNAPSHOT && header->op!=DG_READ_DRAWABLE && header->op!=DG_READ_DRAWABLE_RECT && header->op!=DG_SYNC_GET &&
              header->op!=DG_BUFFER_QUERY && header->op!=DG_BUFFER_READ && header->op!=DG_BUFFER_TRANSFER && header->op!=DG_MAP_BEGIN &&
              header->op!=DG_MAP_INFO && header->op!=DG_MAP_READ && header->op!=DG_PBO_CLIENT_STACK &&
              header->op!=DG_SURFACE_HOLD_READ && header->op!=DG_VRAM_STATS && header->op!=DG_PROGRAM_STRING && header->op!=DG_COMMON_STATE && header->op!=DG_ERROR_STATE && header->op!=DG_FLUSH_IOSURFACE_STATE && header->op!=DG_READ_IOSURFACE_TEXTURE && header->op!=DG_UNIFORM_QUERY &&
              header->op!=DG_OP_get_mapdv && header->op!=DG_OP_get_mapfv && header->op!=DG_OP_get_mapiv &&
              header->op!=DG_OP_get_vertex_attribiv_ARB && header->op!=DG_OP_get_vertex_attribfv_ARB && header->op!=DG_OP_get_vertex_attribdv_ARB &&
              header->op!=DG_TEXTURE_READ && header->op!=DG_STIPPLE_READ && header->op!=DG_OP_render_mode && header->op!=DG_RETAINED_RENDER_READ
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
              && header->op!=DG_COMMAND_PROFILE
#endif
              ) || !(header->flags&DG_REPLY))));
}
static volatile int dg_error_empty_replies=1; /* Same-boot owned-renderer A/B. */
static int DGInterpreterFastReadCommand(struct DGInterpreter *s,int fd,struct DGMessage *header)
{
    struct DGHostContext *contexts=s->contexts;
    if(s->hello && dg_iosurface_readback && (header->flags&DG_OUTPUT_ONLY) &&
       (header->op==DG_READ_DRAWABLE || header->op==DG_READ_DRAWABLE_RECT) &&
       header->context && header->context<s->next && contexts[header->context].cgl) {
        struct DGHostContext *c=&contexts[header->context];
        if(c->drawable && c->drawable->prepared)return 0; // full interpreter resolves real storage first
        if(CGLGetCurrentContext()!=c->cgl && CGLSetCurrentContext(c->cgl))return -1;
        int sent=DGSendDrawableSurface(c,header,fd);
        if(sent<0)return -1;
        if(sent)return 1;
        ++c->surface_read_fallbacks;
    }
    return 0;
}
static int DGInterpreterFastRead(struct DGInterpreter *s,int fd,struct DGMessage *header)
{
#ifdef VIM_PASSTHROUGH_OPENGL_PACKET_JOURNAL
    /* The diagnostic must record the real command before any fast reply. */
    if(getenv("VIM_GL_PACKET_JOURNAL"))return 0;
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    struct DGCommandProfileStamp profile={0};
    if(header->op==DG_READ_DRAWABLE || header->op==DG_READ_DRAWABLE_RECT)profile=DGCommandProfileBegin();
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    uint64_t began=DGCommandTimingBegin();
#endif
    int result=DGInterpreterFastReadCommand(s,fd,header);
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    if(result)DGCommandProfileEnd(profile,header->op,header->bytes,2);
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    DGCommandTimingEnd("fast-read",header->op,header->context,header->bytes,began);
#endif
    return result;
}
static int DGInterpret(struct DGInterpreter *,int,struct DGMessage *);
#include "passthrough-opengl-compact-host.c"
static int DGInterpretCommand(struct DGInterpreter *s,int fd,struct DGMessage *m)
{
    uint32_t original_flags=m->flags;
    int observe_errors=!!(original_flags&DG_OBSERVE_ERROR_EMPTY);
    m->flags&=~DG_OBSERVE_ERROR_EMPTY;
    // The envelope has no native context/state of its own. Each decoded
    // scalar traverses the SAME prepared/scoped/error/validation path below.
    if(m->op==DG_COMPACT_SCALARS)return DGInterpretCompact(s,fd,m);
    struct DGHostContext *contexts=s->contexts;
    struct DGReply reply={.bytes=(m->flags&DG_INPUT_ONLY)?0:m->bytes};int error=0;
    struct DGHostContext *context=m->context && m->context<s->next ? &contexts[m->context] : NULL;
    if(context && (context->element_vbo_limit || context->element_snapshot_count || context->element_snapshot_error) && !context->inside_begin &&
       m->op!=DG_ARRAY_ELEMENT_STATE && m->op!=DG_OP_begin && m->op!=DG_OP_array_element)
        DGHostReleaseArrayElements(context);
    if(context && context->cgl && context->drawable && context->drawable->prepared && DGPreparedNeedsStorage(m->op)){
        if(CGLGetCurrentContext()!=context->cgl && CGLSetCurrentContext(context->cgl))return -1;
        if(!DGResolvePreparedDrawable(context)){
            if(m->flags&DG_REPLY)reply.error=GL_OUT_OF_MEMORY;else context->error=GL_OUT_OF_MEMORY;
            goto respond;
        }
    }
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    // Private drawable export/publication must observe the current scoped
    // contents, not the old private IOSurface. Generated GL calls keep using
    // the real shared FBO. Copy once on first private operation; End publishes
    // subsequent private writes before returning CPU ownership.
    if(context && context->cgl && context->vram_render_serial &&
       m->op>=DG_CREATE && m->op!=DG_BACKING_RENDER_BEGIN &&
       m->op!=DG_BACKING_RENDER_END && m->op!=DG_BACKING_RENDER_MATERIALIZE &&
       m->op!=DG_BACKING_RENDER_CONTINUE &&
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
       m->op!=DG_BACKING_RENDER_DEFER &&
#endif
       m->op!=DG_RESIZE && m->op!=DG_DROP_DRAWABLE && m->op!=DG_ERROR &&
       m->op!=DG_ERROR_STATE && m->op!=DG_STRING && m->op!=DG_COMMON_STATE &&
       m->op!=DG_FLUSH_DRAWABLE_STATE && m->op!=DG_FLUSH_IOSURFACE_STATE &&
       // These private wire packets are actual GL draws into the CURRENT
       // FBO, not exports of the private drawable. Materializing here makes
       // every compositor array draw abandon its scoped shared target.
       m->op!=DG_DRAW && m->op!=DG_DRAW_INSTANCED_ARRAYS && m->op!=DG_ARRAY_ELEMENT_STATE &&
       m->op!=DG_VRAM_STATS && !(m->op>=DG_SYNC_FENCE && m->op<=DG_SYNC_GET)) {
        if((CGLGetCurrentContext()!=context->cgl && CGLSetCurrentContext(context->cgl)) ||
           !DGVRAMRenderMaterialize(context))return -1;
    }
#endif
    if(!s->hello && m->op!=DG_HELLO && m->op!=VIM_GL_SERVICE_HELLO) error=-1;
    else if(s->service_only && m->op!=DG_SURFACE_HOLD && m->op!=DG_SURFACE_UNHOLD && m->op!=DG_SURFACE_HOLD_READ &&
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
        m->op!=DG_BACKING_COMPLETION_WAIT && m->op!=DG_BACKING_COMPLETION_RELEASE && m->op!=DG_BACKING_COMPLETION_CHECK &&
#endif
        m->op!=DG_SURFACE_HOLD_PRESENT && m->op!=DG_SURFACE_PRESENT_READY)error=-1;
    else if(m->op==VIM_GL_SERVICE_HELLO) {
        if(s->hello || m->context || m->bytes || m->flags!=DG_REPLY ||
           m->args[0]!=VIM_GL_SERVICE_MAGIC || m->args[1]!=VIM_GL_SERVICE_VERSION || !(m->args[3]|m->args[4]))error=-1;
        else { s->hello=s->service_only=1;memcpy(s->surface_namespace,&m->args[3],sizeof(s->surface_namespace));
            reply.result=DG_CAP_SURFACE_HOLDS|DG_CAP_SURFACE_HOLD_READ|DG_CAP_SURFACE_VERSIONS;
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
            reply.result|=DG_CAP_BACKING_COMPLETION;
#endif
            if(getenv("VIM_OPENGL_DISPLAY_ENDPOINT"))reply.result|=VIM_GL_SERVICE_CAP_PRESENT; }
    }
    else if(m->op==DG_HELLO) {
        const char *layout=DGHostString(m,2);
        if(s->hello || m->context || m->args[0]!=DG_MAGIC || m->args[1]!=DG_VERSION || !layout || strcmp(layout,DG_LAYOUT)) error=-1;
        else { s->hello=1;memcpy(s->surface_namespace,&m->args[3],sizeof(s->surface_namespace));
            if(m->args[5]==DG_HELLO_FEATURES_MAGIC)s->client_features=(unsigned)m->args[6]&DG_FEATURE_LARGE_IOSURFACE_WRITES;
            reply.result=DG_CAP_RETAINED_SURFACES|DG_CAP_SURFACE_EPOCHS|DG_CAP_SURFACE_HOLDS|DG_CAP_SURFACE_RETIREMENT|DG_CAP_SURFACE_VIEWS|DG_CAP_SURFACE_DRAWABLES|DG_CAP_DRAWABLE_ALIASES|DG_CAP_DRAWABLE_PATCHES|DG_CAP_DRAWABLE_READ_RECTS|DG_CAP_CORE_PROFILE|DG_CAP_SYNC_OBJECTS;
            pthread_once(&dg_core_once,DGLoadCoreAPI);
            reply.result|=DG_CAP_BUFFER_QUERIES;
            reply.result|=DG_CAP_PIXEL_BUFFERS;
            reply.result|=DG_CAP_STRIDED_IMPORT;
            reply.result|=DG_CAP_SURFACE_REGIONS;
            reply.result|=DG_CAP_DRAWABLE_BACK_ALIASES;
            /* Publish/copy and seal under one resource lock and one native
             * submission. Old clients ignore this independently negotiated
             * bit; new clients retain their separate-call fallback on old
             * hosts. All immutable copies and completion fences remain. */
            reply.result|=DG_CAP_SEALED_SURFACE_REGIONS;
            reply.result|=DG_CAP_SURFACE_DRAWABLE_PUBLICATION;
            reply.result|=DG_CAP_SURFACE_HOLD_READ;
            reply.result|=DG_CAP_UNPACK_SNAPSHOT;
            reply.result|=DG_CAP_IOSURFACE_TEXTURE;
            reply.result|=DG_CAP_IOSURFACE_FLOAT|DG_CAP_IOSURFACE_ROWS;
            reply.result|=DG_CAP_IOSURFACE_LUMINANCE8;
            reply.result|=DG_CAP_IOSURFACE_LUMINANCE_ALPHA8;
            reply.result|=DG_CAP_APPLE_VIDEO_TEXTURES;
            reply.result|=DG_CAP_TYPED_SURFACES;
            reply.result|=DG_CAP_BIG_ENDIAN_ARRAYS;
            reply.result|=DG_CAP_IOSURFACE_WRITES;
            reply.result|=DG_CAP_NATIVE_RENDERERS;
            reply.result|=DG_CAP_UNIFORM_QUERY;
            reply.result|=DG_CAP_CALL_LISTS;
            reply.result|=DG_CAP_QUERY_REPLIES;
            reply.result|=DG_CAP_CLIENT_VERTEX_STACK;
            reply.result|=DG_CAP_ARB_PROGRAMS;
            reply.result|=DG_CAP_FLIPPED_DRAWABLE_ALIASES;
            reply.result|=DG_CAP_CROPPED_FLIPPED_ALIASES;
            reply.result|=DG_CAP_FLUSH_DRAWABLE_STATE;
            reply.result|=DG_CAP_SURFACE_IMPORT_REGION;
            reply.result|=DG_CAP_SURFACE_COPY_REGIONS;
            reply.result|=DG_CAP_SURFACE_PATCH_IMPORT;
            reply.result|=DG_CAP_COMMON_STATE;
            reply.result|=DG_CAP_COMPACT_SCALARS;
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
            reply.result|=DG_CAP_COMMAND_PROFILE|DG_CAP_SNAPSHOT_IMPORT_POLICY|DG_CAP_SNAPSHOT_RECYCLE_POLICY;
#endif
            reply.result|=DG_CAP_VALIDATED_UNIFORM_LOCATIONS;
            reply.result|=DG_CAP_ERROR_STATE;
            reply.result|=DG_CAP_ERROR_EMPTY_REPLY;
            reply.result|=DG_CAP_TIGHT_TEX_SUB_IMAGE_2D;
            reply.result|=DG_CAP_TEXTURE_1D;
            reply.result|=DG_CAP_TEXTURE_3D;
            reply.result|=DG_CAP_SURFACE_VERSIONS;
            reply.result|=DG_CAP_SURFACE_VERSION_TEXTURE;
            if(getenv("VIM_OPENGL_DISPLAY_ENDPOINT"))reply.result|=DG_CAP_SURFACE_PRESENT_VERSION;
            if(DGVRAMAvailable())reply.result|=DG_CAP_VRAM_DRAWABLES|DG_CAP_VRAM_PATCHES;
            if(dg_ram.address)reply.result|=DG_CAP_RAM_DRAWABLES;
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
            if(dg_ram.address)reply.result|=DG_CAP_BACKING_RENDER_SCOPE;
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
            if(dg_ram.address)reply.result|=DG_CAP_PREPARED_DRAWABLE;
#endif
#endif
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
            if(dg_ram.address)reply.result|=DG_CAP_BACKING_COMPLETION;
#endif
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
            if(dg_ram.address)reply.result|=DG_CAP_BACKING_FUTURE_VERSION;
#endif
#ifdef VIM_BACKING_WRITER_EXPERIMENT
            if(dg_ram.address)reply.result|=DG_CAP_BACKING_WRITER_CONTINUATION;
#endif
            if(dg_core_api.map_range && dg_core_api.flush_range && dg_core_api.buffer_parameter64)
                reply.result|=DG_CAP_BUFFER_MAPPING;
            if(dg_core_api.divisor && dg_core_api.arrays_instanced && dg_core_api.elements_instanced)
                reply.result|=DG_CAP_INSTANCED_ARRAYS;
            if(m->args[5]==DG_HELLO_FEATURES_MAGIC){
                struct DGHelloFeatures features={DG_HELLO_FEATURES_MAGIC,
                    DG_FEATURE_IOSURFACE_OUTPUT_READ|DG_FEATURE_CORE_COMMON_STATE|DG_FEATURE_IOSURFACE_RED8|
                    DG_FEATURE_COMPRESSED_TEXTURE_UPLOAD|DG_FEATURE_LARGE_IOSURFACE_WRITES|DG_FEATURE_IOSURFACE_PACKED_422|
                    DG_FEATURE_TEXTURE_IMAGE_READBACK|
                    DG_FEATURE_EVALUATOR_QUERIES|DG_FEATURE_SURFACE_UPLOAD_ROWS|
                    DG_FEATURE_EVALUATOR_UPLOADS|DG_FEATURE_INTEGER_PARAMETERS|
                    DG_FEATURE_TEXTURE_READ_LAYOUT|DG_FEATURE_TEXTURE_READ_CHUNKS|
                    DG_FEATURE_TRANSFORM_FEEDBACK_NAMES|DG_FEATURE_INDEXED_STATE|DG_FEATURE_INTEGER_VERTEX_ARRAYS,
                    DG_HELLO_FEATURES2_MAGIC,0,DG_HELLO_FEATURES3_MAGIC,DG_FEATURE3_STIPPLE_READ|DG_FEATURE3_COMPACT_IMAGING|DG_FEATURE3_COMPACT_TEXTURE|DG_FEATURE3_BIG_ENDIAN_ARRAY_ELEMENT};
                if(dlsym(RTLD_DEFAULT,"glClearBufferiv") && dlsym(RTLD_DEFAULT,"glClearBufferuiv") &&
                   dlsym(RTLD_DEFAULT,"glClearBufferfv") && dlsym(RTLD_DEFAULT,"glClearBufferfi"))
                    features.features|=DG_FEATURE_CLEAR_BUFFER;
                if(dg_core_api.signed_result && dg_core_api.unsigned_result &&
                   dg_core_api.signed_result_ext && dg_core_api.unsigned_result_ext && dg_core_api.query_counter)
                    features.features|=DG_FEATURE_TIMER_QUERY;
                if(dg_core_api.uniform_indices && dg_core_api.uniform_block_iv &&
                   dlsym(RTLD_DEFAULT,"glGetActiveUniformsiv") && dlsym(RTLD_DEFAULT,"glGetActiveUniformName") &&
                   dlsym(RTLD_DEFAULT,"glGetUniformBlockIndex") && dlsym(RTLD_DEFAULT,"glGetActiveUniformBlockName") &&
                   dlsym(RTLD_DEFAULT,"glUniformBlockBinding"))
                    features.features|=DG_FEATURE_UNIFORM_BLOCKS;
                if(dlsym(RTLD_DEFAULT,"glGetInteger64i_v"))features.features|=DG_FEATURE_INDEXED_INT64;
                if(dg_core_api.samplers)features.features|=DG_FEATURE_SAMPLERS;
                if(dg_core_api.samplers)features.features2|=DG_FEATURE2_SRGB_DECODE;
                features.features2|=DG_FEATURE2_ADDITIONAL_STATE;
                features.features2|=DG_FEATURE2_LARGE_TEXTURE_INPUT;
                features.features2|=DG_FEATURE2_LARGE_BUFFER_TRANSFER;
                features.features2|=DG_FEATURE2_RASTER_STREAM;
                if(dlsym(RTLD_DEFAULT,"glTexBuffer"))features.features|=DG_FEATURE_TEXTURE_BUFFER;
                if(dlsym(RTLD_DEFAULT,"glTexImage2DMultisample") && dlsym(RTLD_DEFAULT,"glTexImage3DMultisample") &&
                   dlsym(RTLD_DEFAULT,"glGetMultisamplefv") && dlsym(RTLD_DEFAULT,"glSampleMaski"))
                    features.features|=DG_FEATURE_MULTISAMPLE_TEXTURE;
                if(dlsym(RTLD_DEFAULT,"glBindFragDataLocationIndexed") && dlsym(RTLD_DEFAULT,"glGetFragDataIndex"))
                    features.features|=DG_FEATURE_DUAL_SOURCE_BLEND;
                if(dlsym(RTLD_DEFAULT,"glBlendFunci") && dlsym(RTLD_DEFAULT,"glBlendFuncSeparatei") &&
                   dlsym(RTLD_DEFAULT,"glBlendEquationi") && dlsym(RTLD_DEFAULT,"glBlendEquationSeparatei"))
                    features.features|=DG_FEATURE_INDEXED_BLEND;
                if(dlsym(RTLD_DEFAULT,"glCreateShaderProgramv") && dlsym(RTLD_DEFAULT,"glUseProgramStages") &&
                   dlsym(RTLD_DEFAULT,"glGetProgramPipelineInfoLog") && dlsym(RTLD_DEFAULT,"glProgramParameteri") &&
                   dlsym(RTLD_DEFAULT,"glProgramUniformMatrix4x3dv"))
                    features.features|=DG_FEATURE_PROGRAM_PIPELINE;
                if(dg_core_api.uniform_double && dlsym(RTLD_DEFAULT,"glUniform1d") &&
                   dlsym(RTLD_DEFAULT,"glUniformMatrix4x3dv"))
                    features.features|=DG_FEATURE_FP64_UNIFORM;
                if(dg_core_api.enable_indexed && dg_core_api.disable_indexed &&
                   dlsym(RTLD_DEFAULT,"glViewportArrayv") && dlsym(RTLD_DEFAULT,"glScissorArrayv") &&
                   dlsym(RTLD_DEFAULT,"glDepthRangeArrayv") && dlsym(RTLD_DEFAULT,"glGetFloati_v") &&
                   dlsym(RTLD_DEFAULT,"glGetDoublei_v"))
                    features.features|=DG_FEATURE_VIEWPORT_ARRAY;
                if(dg_core_api.gl4_profile)features.features|=DG_FEATURE_GL4_PROFILE;
                if(dg_core_api.gl4_profile && dlsym(RTLD_DEFAULT,"glPatchParameteri") &&
                   dlsym(RTLD_DEFAULT,"glPatchParameterfv"))features.features2|=DG_FEATURE2_TESSELLATION;
                if(dg_core_api.gl4_profile && dg_core_api.divisor && dg_core_api.draw_feedback &&
                   dg_core_api.draw_feedback_stream && dlsym(RTLD_DEFAULT,"glGenTransformFeedbacks") &&
                   dlsym(RTLD_DEFAULT,"glDeleteTransformFeedbacks") && dlsym(RTLD_DEFAULT,"glBindTransformFeedback") &&
                   dlsym(RTLD_DEFAULT,"glIsTransformFeedback") && dlsym(RTLD_DEFAULT,"glPauseTransformFeedback") &&
                   dlsym(RTLD_DEFAULT,"glResumeTransformFeedback") && dlsym(RTLD_DEFAULT,"glBeginQueryIndexed") &&
                   dlsym(RTLD_DEFAULT,"glEndQueryIndexed") && dlsym(RTLD_DEFAULT,"glGetQueryIndexediv"))
                    features.features2|=DG_FEATURE2_FEEDBACK_OBJECTS;
                if(dlsym(RTLD_DEFAULT,"glGetStringi"))features.features2|=DG_FEATURE2_CORE_EXTENSIONS;
#if defined(__x86_64__)
                /* ARM Metal's software-stage subroutine path can fault in
                 * native GL. Do not advertise that optional family with the
                 * old private renderer rebinding removed. Intel stays native. */
                if(dg_core_api.gl4_profile && dg_core_api.active_subroutine_iv &&
                   dlsym(RTLD_DEFAULT,"glGetSubroutineUniformLocation") && dlsym(RTLD_DEFAULT,"glGetSubroutineIndex") &&
                   dlsym(RTLD_DEFAULT,"glGetActiveSubroutineUniformName") && dlsym(RTLD_DEFAULT,"glGetActiveSubroutineName") &&
                   dlsym(RTLD_DEFAULT,"glUniformSubroutinesuiv") && dlsym(RTLD_DEFAULT,"glGetUniformSubroutineuiv") &&
                   dlsym(RTLD_DEFAULT,"glGetProgramStageiv"))
                    features.features2|=DG_FEATURE2_SUBROUTINES;
#endif
                if(dlsym(RTLD_DEFAULT,"glReleaseShaderCompiler") && dlsym(RTLD_DEFAULT,"glShaderBinary") &&
                   dlsym(RTLD_DEFAULT,"glGetShaderPrecisionFormat") &&
                   dlsym(RTLD_DEFAULT,"glDepthRangef") && dlsym(RTLD_DEFAULT,"glClearDepthf"))
                    features.features2|=DG_FEATURE2_ES2_COMPAT;
                if(dlsym(RTLD_DEFAULT,"glVertexAttribP1ui") && dlsym(RTLD_DEFAULT,"glVertexAttribP2ui") &&
                   dlsym(RTLD_DEFAULT,"glVertexAttribP3ui") && dlsym(RTLD_DEFAULT,"glVertexAttribP4ui") &&
                   dlsym(RTLD_DEFAULT,"glVertexAttribP1uiv") && dlsym(RTLD_DEFAULT,"glVertexAttribP2uiv") &&
                   dlsym(RTLD_DEFAULT,"glVertexAttribP3uiv") && dlsym(RTLD_DEFAULT,"glVertexAttribP4uiv"))
                    features.features2|=DG_FEATURE2_PACKED_ATTRIBS;
                if(dlsym(RTLD_DEFAULT,"glGetProgramBinary") && dlsym(RTLD_DEFAULT,"glProgramBinary") &&
                   dlsym(RTLD_DEFAULT,"glProgramParameteri"))features.features2|=DG_FEATURE2_PROGRAM_BINARY;
                if(dlsym(RTLD_DEFAULT,"glMinSampleShading"))features.features2|=DG_FEATURE2_SAMPLE_SHADING;
                if(dlsym(RTLD_DEFAULT,"glGetInternalformativ"))features.features2|=DG_FEATURE2_INTERNAL_FORMAT;
                if(dlsym(RTLD_DEFAULT,"glPixelMapfv") && dlsym(RTLD_DEFAULT,"glPixelMapuiv") &&
                   dlsym(RTLD_DEFAULT,"glPixelMapusv") && dlsym(RTLD_DEFAULT,"glGetPixelMapfv") &&
                   dlsym(RTLD_DEFAULT,"glGetPixelMapuiv") && dlsym(RTLD_DEFAULT,"glGetPixelMapusv"))
                    features.features2|=DG_FEATURE2_PIXEL_MAPS;
                if(dlsym(RTLD_DEFAULT,"glBitmap") && dlsym(RTLD_DEFAULT,"glDrawPixels") &&
                   dlsym(RTLD_DEFAULT,"glPolygonStipple") && dlsym(RTLD_DEFAULT,"glGetPolygonStipple"))
                    features.features2|=DG_FEATURE2_COMPAT_RASTER;
                if(dlsym(RTLD_DEFAULT,"glEdgeFlagPointer") && dlsym(RTLD_DEFAULT,"glIndexPointer"))
                    features.features2|=DG_FEATURE2_AUX_VERTEX_ARRAYS;
                if(dlsym(RTLD_DEFAULT,"glArrayElement"))features.features2|=DG_FEATURE2_ARRAY_ELEMENT;
                if(dlsym(RTLD_DEFAULT,"glLabelObjectEXT") && dlsym(RTLD_DEFAULT,"glGetObjectLabelEXT"))
                    features.features2|=DG_FEATURE2_DEBUG_LABEL;
                if(dlsym(RTLD_DEFAULT,"glNamedStringARB") && dlsym(RTLD_DEFAULT,"glDeleteNamedStringARB") &&
                   dlsym(RTLD_DEFAULT,"glCompileShaderIncludeARB") && dlsym(RTLD_DEFAULT,"glIsNamedStringARB") &&
                   dlsym(RTLD_DEFAULT,"glGetNamedStringARB") && dlsym(RTLD_DEFAULT,"glGetNamedStringivARB"))
                    features.features2|=DG_FEATURE2_SHADER_INCLUDE;
                if(dlsym(RTLD_DEFAULT,"glEnableVertexAttribAPPLE") && dlsym(RTLD_DEFAULT,"glDisableVertexAttribAPPLE") &&
                   dlsym(RTLD_DEFAULT,"glIsVertexAttribEnabledAPPLE") && dlsym(RTLD_DEFAULT,"glMapVertexAttrib1dAPPLE") &&
                   dlsym(RTLD_DEFAULT,"glMapVertexAttrib1fAPPLE") && dlsym(RTLD_DEFAULT,"glMapVertexAttrib2dAPPLE") &&
                   dlsym(RTLD_DEFAULT,"glMapVertexAttrib2fAPPLE"))
                    features.features2|=DG_FEATURE2_APPLE_EVALUATORS;
                if(dlsym(RTLD_DEFAULT,"glWeightPointerARB") && dlsym(RTLD_DEFAULT,"glVertexBlendARB"))
                    features.features2|=DG_FEATURE2_VERTEX_BLEND;
                if(dlsym(RTLD_DEFAULT,"glPointSizePointerAPPLE") && dlsym(RTLD_DEFAULT,"glVertexPointSizefAPPLE"))
                    features.features2|=DG_FEATURE2_POINT_SIZE_ARRAY;
                if(dlsym(RTLD_DEFAULT,"glObjectPurgeableAPPLE") && dlsym(RTLD_DEFAULT,"glObjectUnpurgeableAPPLE") &&
                   dlsym(RTLD_DEFAULT,"glGetObjectParameterivAPPLE"))
                    features.features2|=DG_FEATURE2_OBJECT_PURGEABLE;
                /* Wire capabilities are separate from profile advertisement. */
                if(dlsym(RTLD_DEFAULT,"glColorTableParameterfv") && dlsym(RTLD_DEFAULT,"glColorTableParameteriv") &&
                   dlsym(RTLD_DEFAULT,"glGetColorTableParameterfv") && dlsym(RTLD_DEFAULT,"glGetColorTableParameteriv") &&
                   dlsym(RTLD_DEFAULT,"glConvolutionParameterfv") && dlsym(RTLD_DEFAULT,"glConvolutionParameteriv") &&
                   dlsym(RTLD_DEFAULT,"glGetConvolutionParameterfv") && dlsym(RTLD_DEFAULT,"glGetConvolutionParameteriv") &&
                   dlsym(RTLD_DEFAULT,"glGetHistogramParameterfv") && dlsym(RTLD_DEFAULT,"glGetHistogramParameteriv") &&
                   dlsym(RTLD_DEFAULT,"glGetMinmaxParameterfv") && dlsym(RTLD_DEFAULT,"glGetMinmaxParameteriv"))
                    features.features2|=DG_FEATURE2_IMAGING_PARAMETERS;
                if((features.features2&DG_FEATURE2_IMAGING_PARAMETERS) &&
                   dlsym(RTLD_DEFAULT,"glColorTable") && dlsym(RTLD_DEFAULT,"glColorSubTable") &&
                   dlsym(RTLD_DEFAULT,"glGetColorTable") && dlsym(RTLD_DEFAULT,"glConvolutionFilter1D") &&
                   dlsym(RTLD_DEFAULT,"glConvolutionFilter2D") && dlsym(RTLD_DEFAULT,"glGetConvolutionFilter") &&
                   dlsym(RTLD_DEFAULT,"glSeparableFilter2D") && dlsym(RTLD_DEFAULT,"glGetSeparableFilter") &&
                   dlsym(RTLD_DEFAULT,"glGetHistogram") && dlsym(RTLD_DEFAULT,"glGetMinmax"))
                    features.features2|=DG_FEATURE2_IMAGING_TRANSFERS;
                /* Retained entry points are transport capabilities. The
                 * observed legacy combiner exports are not an advertised
                 * rendering extension on the tested hardware. */
                if(dlsym(RTLD_DEFAULT,"glGetCombinerInputParameterfvNV") &&
                   dlsym(RTLD_DEFAULT,"glGetCombinerInputParameterivNV") &&
                   dlsym(RTLD_DEFAULT,"glGetCombinerOutputParameterfvNV") &&
                   dlsym(RTLD_DEFAULT,"glGetCombinerOutputParameterivNV") &&
                   dlsym(RTLD_DEFAULT,"glGetFinalCombinerInputParameterfvNV") &&
                   dlsym(RTLD_DEFAULT,"glGetFinalCombinerInputParameterivNV") &&
                   dlsym(RTLD_DEFAULT,"glCombinerStageParameterfvNV") &&
                   dlsym(RTLD_DEFAULT,"glGetCombinerStageParameterfvNV"))
                    features.features2|=DG_FEATURE2_LEGACY_COMBINERS;
                if(dlsym(RTLD_DEFAULT,"glSamplePass"))
                    features.features2|=DG_FEATURE2_LEGACY_SAMPLE_PASS;
                if(dlsym(RTLD_DEFAULT,"glSelectBuffer") && dlsym(RTLD_DEFAULT,"glFeedbackBuffer") &&
                   dlsym(RTLD_DEFAULT,"glRenderMode"))features.features2|=DG_FEATURE2_RETAINED_RENDER;
                if(dg_core_api.vertex_attrib_lpointer && dlsym(RTLD_DEFAULT,"glGetVertexAttribLdv") &&
                   dlsym(RTLD_DEFAULT,"glVertexAttribL1d") && dlsym(RTLD_DEFAULT,"glVertexAttribL1dv") &&
                   dlsym(RTLD_DEFAULT,"glVertexAttribL2d") && dlsym(RTLD_DEFAULT,"glVertexAttribL2dv") &&
                   dlsym(RTLD_DEFAULT,"glVertexAttribL3d") && dlsym(RTLD_DEFAULT,"glVertexAttribL3dv") &&
                   dlsym(RTLD_DEFAULT,"glVertexAttribL4d") && dlsym(RTLD_DEFAULT,"glVertexAttribL4dv"))
                    features.features2|=DG_FEATURE2_FP64_VERTEX;
                if(dg_core_api.divisor && dg_core_api.arrays_instanced && dg_core_api.elements_instanced &&
                   dg_core_api.buffer_parameter64 &&
                   dg_core_api.draw_arrays_indirect && dg_core_api.draw_elements_indirect)
                    features.features2|=DG_FEATURE2_DRAW_INDIRECT;
                if(dg_core_api.elements_base && dg_core_api.range_elements_base && dg_core_api.instanced_elements_base)
                    features.features|=DG_FEATURE_BASE_VERTEX;
                if(dg_core_api.multi_arrays && dg_core_api.multi_elements && dg_core_api.multi_base)
                    features.features|=DG_FEATURE_MULTI_DRAW;
                if((features.features&(DG_FEATURE_BASE_VERTEX|DG_FEATURE_MULTI_DRAW))==
                   (DG_FEATURE_BASE_VERTEX|DG_FEATURE_MULTI_DRAW))
                    features.features2|=DG_FEATURE2_APPLE_ARRAYS;
                if(features.features2&DG_FEATURE2_CORE_EXTENSIONS)
                    s->core_extension_candidates=DGHostCoreExtensionCandidates(features.features,features.features2);
                memcpy((void *)layout,&features,sizeof(features));
            }
        }
    }
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    else if(m->op==DG_COMMAND_PROFILE)error=DGCommandProfileControl(m,&reply);
#endif
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
    else if(m->op==DG_BACKING_COMPLETION_WAIT || m->op==DG_BACKING_COMPLETION_RELEASE || m->op==DG_BACKING_COMPLETION_CHECK){
        error=DGBackingTicketCommand(s->surface_namespace,m,&reply);
    }
#endif
    else if(m->op==DG_SURFACE_HOLD || m->op==DG_SURFACE_UNHOLD || m->op==DG_SURFACE_HOLD_READ ||
        m->op==DG_SURFACE_HOLD_PRESENT || m->op==DG_SURFACE_PRESENT_READY) {
        error=DGSurfaceHoldCommand(s->surface_namespace,&s->surface_holds,&s->next_surface_hold,m,&reply);
    } else if(m->op==DG_CREATE) {
        uint64_t share=m->args[0];
        unsigned profile=m->args[5]?(unsigned)m->args[5]:kCGLOGLPVersion_Legacy;
        if(s->next>=256 || share>=s->next || (share && !contexts[share].cgl) || m->args[1]>24 || m->args[2]>8 || m->args[3]>1 || m->args[4]>1 ||
            (m->args[5] && m->args[5]!=kCGLOGLPVersion_Legacy &&
             m->args[5]!=kCGLOGLPVersion_3_2_Core && m->args[5]!=DG_CGL_PROFILE_GL4)) error=-1;
        else if(profile==DG_CGL_PROFILE_GL4 && !dg_core_api.gl4_profile)reply.error=GL_INVALID_OPERATION;
        else if(share && contexts[share].profile!=profile)reply.error=GL_INVALID_OPERATION;
        else {
            pthread_once(&dg_core_once,DGLoadCoreAPI);
            if(profile!=kCGLOGLPVersion_Legacy &&
               (!dg_core_api.bind || !dg_core_api.generate || !dg_core_api.remove || !dg_core_api.is_array)) {
                return -1;
            }
            CGLPixelFormatAttribute attrs[12]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,
                kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,kCGLPFADepthSize,(CGLPixelFormatAttribute)m->args[1],
                kCGLPFAStencilSize,(CGLPixelFormatAttribute)m->args[2],0};
            if(dg_reference_renderer){attrs[8]=kCGLPFARendererID;attrs[9]=(CGLPixelFormatAttribute)dg_reference_renderer;
                attrs[10]=kCGLPFASupportsAutomaticGraphicsSwitching;}
            CGLPixelFormatObj pf=NULL;GLint count=0;
            CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
            if(!e) e=CGLCreateContext(pf,share ? contexts[share].cgl : NULL,&contexts[s->next].cgl);
            if(pf) CGLDestroyPixelFormat(pf);
            if(e || !contexts[s->next].cgl) {
                fprintf(stderr,"PassthroughOpenGL context creation failed error=%d formats=%d explicit_renderer=0x%x\n",e,count,dg_reference_renderer);
                reply.error=GL_OUT_OF_MEMORY;
            }
            else { contexts[s->next].depth_bits=(GLint)m->args[1];contexts[s->next].stencil_bits=(GLint)m->args[2];
                contexts[s->next].profile=profile;
                contexts[s->next].client_features=s->client_features;
                contexts[s->next].native_screen_count=count;contexts[s->next].native_hardware_screen=-1;
                memcpy(contexts[s->next].surface_namespace,s->surface_namespace,sizeof(s->surface_namespace));
                contexts[s->next].double_buffer=(GLint)m->args[3];
                contexts[s->next].track_damage=(unsigned)m->args[4];
                contexts[s->next].read_mode=contexts[s->next].draw_mode=m->args[3] ? GL_BACK : GL_FRONT;
                CGLSetCurrentContext(contexts[s->next].cgl);
                if(!DGCreateAliases(&contexts[s->next],share?&contexts[share]:NULL)) {
                    CGLSetCurrentContext(NULL);CGLDestroyContext(contexts[s->next].cgl);contexts[s->next].cgl=NULL;
                    reply.error=GL_OUT_OF_MEMORY;
                } else {
                    reply.result=s->next++;
                    fprintf(stderr,"PassthroughOpenGL context=%llu renderer=%s version=%s\n",reply.result,glGetString(GL_RENDERER),glGetString(GL_VERSION));
                } }
        }
    } else if(!context || !context->cgl ||
        (CGLGetCurrentContext()!=context->cgl && CGLSetCurrentContext(context->cgl)) ||
        (context->native_drawable_deferred && !DGNativeRendererPrepareCommand(context,m))) error=-1;
    else if(m->op==DG_DESTROY) {
        if(dg_surface_read_stats)fprintf(stderr,"IOSurface private reads=%llu fallbacks=%llu\n",
            context->surface_read_count,context->surface_read_fallbacks);
        // A destroyed context disappears from the later endpoint-wide drain.
        // Its last producer use must therefore finish HERE, not just submit.
        if(context->inside_begin){glEnd();context->inside_begin=0;}
        DGHostReleaseArrayElements(context);
        DGReleaseTextureRead(context);
        DGSubmitIOSurfaceWrites(context);glFinish();
        DGDisplaySenderStop(context);DGDestroySurfaceLeases(context);DGDestroyDrawables(context);DGDestroyFences(context);DGDestroyAliases(context);CGLSetCurrentContext(NULL);CGLDestroyContext(context->cgl);
        DGHostFreeRenderBuffers(context);
        memset(context,0,sizeof(*context));
    } else if(m->op==DG_RESIZE
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
        || m->op==DG_PREPARE_DRAWABLE
#endif
    ) {
        if(!m->args[0] || !m->args[1] || m->args[0]>8192 || m->args[1]>8192) error=-1;
        else {
            int shape=m->args[2] && m->args[2]<=UINT32_MAX && !m->bytes && m->flags==DG_REPLY;
            for(unsigned i=3;i<16;++i)if(m->args[i])shape=0;
            if(m->op==DG_PREPARE_DRAWABLE && !shape)error=-1;
            else if(m->args[2]>UINT32_MAX || !DGSelectDrawableMode(context,(uint32_t)m->args[2],
                (int)m->args[0],(int)m->args[1],m->op==DG_PREPARE_DRAWABLE))reply.error=GL_INVALID_OPERATION;
        }
    } else if(m->op==DG_DROP_DRAWABLE) {
        if(m->args[0]>UINT32_MAX) error=-1;else DGDropDrawable(context,(uint32_t)m->args[0]);
    } else if(m->op==DG_ERROR) {
        reply.result=context->error ? context->error : glGetError();context->error=0;
    } else if(m->op==DG_ERROR_STATE) {
        error=DGReadErrorState(context,m,&reply);
    } else if(m->op==DG_STRING) {
        GLenum key=(GLenum)m->args[0];
        char *out=DGHostPointer(m,1,4096);
        if(m->args[0]==0x821d){ /* GL_NUM_EXTENSIONS */
            if(m->bytes || m->flags!=DG_REPLY || context->profile==kCGLOGLPVersion_Legacy)error=-1;
            else {
                for(unsigned i=1;i<16;++i)if(m->args[i])error=-1;
                if(!error)reply.result=DGHostCoreExtensionMask(s->core_extension_candidates);
            }
        }else if(!out || (key!=GL_VENDOR && key!=GL_RENDERER && key!=GL_VERSION && key!=GL_SHADING_LANGUAGE_VERSION &&
            key!=GL_PROGRAM_ERROR_STRING_ARB && key!=GL_EXTENSIONS)) error=-1;
        else if(key==GL_EXTENSIONS){
            const char *ext=context->profile==kCGLOGLPVersion_Legacy?(const char *)glGetString(GL_EXTENSIONS):NULL;
            // Native 10.8 CGXGLDisplayDeviceAccelerator requires FPRG and
            // NPOT for its filtered menu-bar backing. Ordinary 2D NPOT
            // allocation, repeat sampling and mipmaps use the same real GL
            // operations as power-of-two textures; qualify the host's actual
            // extension rather than silently forcing the native filter gate.
            /* The APPLE storage entry points use indirect-renderer semantics:
             * guest pointers never escape into native GL. Packed YCbCr is
             * decoded by the actual native renderer, not a CPU conversion.
             * Require the new guest's explicit span-decoder acknowledgement
             * as well as native format support before exposing this path. */
            int video=(m->args[2]&DG_CAP_APPLE_VIDEO_TEXTURES) &&
                DGHostHasExtension(ext,"GL_APPLE_ycbcr_422") &&
                (DGHostHasExtension(ext,"GL_EXT_texture_rectangle") ||
                 DGHostHasExtension(ext,"GL_ARB_texture_rectangle"));
            // Snow Core Image also selects its real assembly vertex path
            // from this string. The existing typed program operations handle
            // both ARB targets; the native/forwarded qualification exercises
            // vertex outputs, parameter/query/source contracts and GPU pixels
            // in both guest widths. Admit only what this native context has.
            /* ARB_depth_texture adds no entry points: DEPTH_COMPONENT
             * uploads, copies and readback, DEPTH_TEXTURE_MODE, compare
             * state and TEXTURE_DEPTH_SIZE already reach native GL through
             * the generic texture codecs, for old clients as well. Apps such
             * as Pro Apps and games gate shadow and depth effects on the
             * string, not on the GL 1.4 core version. */
            snprintf(out,4096,"%s%s%s%s%s%s",DG_COMPATIBILITY_EXTENSIONS,
                DGHostHasExtension(ext,"GL_ARB_depth_texture")?" GL_ARB_depth_texture":"",
                DGHostHasExtension(ext,"GL_ARB_fragment_program")?" GL_ARB_fragment_program":"",
                DGHostHasExtension(ext,"GL_ARB_vertex_program")?" GL_ARB_vertex_program":"",
                DGHostHasExtension(ext,"GL_ARB_texture_non_power_of_two")?" GL_ARB_texture_non_power_of_two":"",
                video?" GL_EXT_texture_rectangle GL_APPLE_ycbcr_422 GL_APPLE_client_storage GL_APPLE_texture_range":"");
            if((m->args[2]&DG_CAP_PIXEL_BUFFERS) && DGHostHasExtension(ext,"GL_ARB_pixel_buffer_object")){
                size_t used=strlen(out);snprintf(out+used,4096-used," GL_ARB_pixel_buffer_object");
            }
            if(m->args[3]>=1){
                /* Complete interface families, not a copy of every native
                 * extension. Native availability AND a new guest decoder
                 * are required; no added query, copy or polling per draw. */
                static const char *const completed[]={"GL_ARB_occlusion_query",
                    "GL_ARB_point_parameters","GL_EXT_fog_coord","GL_EXT_secondary_color",
                    "GL_ARB_transpose_matrix","GL_ARB_window_pos"};
                for(unsigned i=0;i<sizeof(completed)/sizeof(completed[0]);++i)
                    if(DGHostHasExtension(ext,completed[i])){
                        size_t used=strlen(out);snprintf(out+used,4096-used," %s",completed[i]);
                    }
            }
            DGAppendIndexedExtensions(out,4096,ext,m->args[3],m->args[4]);
            DGAppendTextureExtensions(out,4096,ext,m->args[3],m->args[4],m->args[5]);
            /* Old clients echo the executor's entire features2 word,
             * including bits they cannot implement. Require the client
             * dispatch revision before advertising these families. */
            if(m->args[3]>=3 && DGHostHasExtension(ext,"GL_EXT_bindable_uniform") &&
               dlsym(RTLD_DEFAULT,"glGetUniformOffsetEXT") && dlsym(RTLD_DEFAULT,"glUniformBufferEXT") &&
               dlsym(RTLD_DEFAULT,"glGetUniformBufferSizeEXT")){
                size_t used=strlen(out);snprintf(out+used,4096-used," GL_EXT_bindable_uniform");
            }
            if(m->args[3]>=3 && (m->args[5]&DG_FEATURE2_DEBUG_LABEL) && DGHostHasExtension(ext,"GL_EXT_debug_label")){
                size_t used=strlen(out);snprintf(out+used,4096-used," GL_EXT_debug_label");
            }
            if(m->args[3]>=3 && (m->args[5]&DG_FEATURE2_SHADER_INCLUDE) && DGHostHasExtension(ext,"GL_ARB_shading_language_include")){
                size_t used=strlen(out);snprintf(out+used,4096-used," GL_ARB_shading_language_include");
            }
            if(m->args[3]>=3 && (m->args[5]&DG_FEATURE2_APPLE_EVALUATORS) && DGHostHasExtension(ext,"GL_ARB_vertex_program") &&
               DGHostHasExtension(ext,"GL_APPLE_vertex_program_evaluators")){
                size_t used=strlen(out);snprintf(out+used,4096-used," GL_APPLE_vertex_program_evaluators");
            }
            if(m->args[3]>=3 && (m->args[5]&DG_FEATURE2_VERTEX_BLEND) && DGHostWeightUnits(context)){
                size_t used=strlen(out);snprintf(out+used,4096-used," GL_ARB_vertex_blend");
            }
            if(m->args[3]>=4 && (m->args[5]&DG_FEATURE2_POINT_SIZE_ARRAY) && DGHostPointSizes(context)){
                size_t used=strlen(out);snprintf(out+used,4096-used," GL_APPLE_vertex_point_size");
            }
            if(m->args[3]>=5 && (m->args[5]&DG_FEATURE2_OBJECT_PURGEABLE) &&
               DGHostHasExtension(ext,"GL_APPLE_object_purgeable")){
                size_t used=strlen(out);snprintf(out+used,4096-used," GL_APPLE_object_purgeable");
            }
            const uint32_t imaging=DG_FEATURE2_IMAGING_TRANSFERS|DG_FEATURE2_IMAGING_PARAMETERS|
                DG_FEATURE2_COMPAT_RASTER;
            if(m->args[3]>=6 && (m->args[5]&imaging)==imaging && DGHostHasExtension(ext,"GL_ARB_imaging")){
                size_t used=strlen(out);snprintf(out+used,4096-used," GL_ARB_imaging");
            }
            if(m->args[3]>=3 && (m->args[5]&DG_FEATURE2_APPLE_ARRAYS) && ext){
                size_t used=strlen(out);
                snprintf(out+used,4096-used," GL_APPLE_vertex_array_range%s",
                    DGHostHasExtension(ext,"GL_APPLE_element_array")?" GL_APPLE_element_array":"");
            }
            if((m->args[4]&DG_FEATURE_MULTI_DRAW) && dg_core_api.multi_arrays &&
               dg_core_api.multi_elements && dg_core_api.multi_base){
                if(DGHostHasExtension(ext,"GL_EXT_multi_draw_arrays")){
                    size_t used=strlen(out);snprintf(out+used,4096-used," GL_EXT_multi_draw_arrays");
                }
                if((m->args[4]&DG_FEATURE_BASE_VERTEX) && dg_core_api.elements_base &&
                   dg_core_api.range_elements_base && dg_core_api.instanced_elements_base &&
                   DGHostHasExtension(ext,"GL_ARB_draw_elements_base_vertex")){
                    size_t used=strlen(out);snprintf(out+used,4096-used," GL_ARB_draw_elements_base_vertex");
                }
            }
        }else { const GLubyte *s=glGetString(key);snprintf(out,4096,"%s",s ? (const char *)s : ""); }
    } else if(m->op==DG_PROGRAM_STRING){
        error=DGProgramString(context,m,&reply);
    } else if(m->op==DG_UNPACK_SNAPSHOT) {
        GLint *out=DGHostPointer(m,0,7*sizeof(GLint));
        if(!out || m->bytes!=7*sizeof(GLint) || m->flags!=(DG_REPLY|DG_OUTPUT_ONLY))error=-1;
        else {
            const GLenum keys[]={GL_UNPACK_ROW_LENGTH,GL_UNPACK_ALIGNMENT,GL_PIXEL_UNPACK_BUFFER_BINDING,
                GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};
            for(unsigned i=0;i<7;++i)glGetIntegerv(keys[i],out+i);
        }
    } else if(m->op==DG_NATIVE_RENDERERS || m->op==DG_SELECT_NATIVE_RENDERER) {
        error=DGNativeRendererCommand(context,m,&reply);
    } else if(m->op==DG_UNIFORM_QUERY) {
        error=DGUniformQuery(context,m,&reply);
    } else if(m->op==DG_RENDERER_MEMORY) error=DGRendererMemory(context,&reply);
    else if(m->op==DG_DRAWABLE_DAMAGE) {
        struct DGDamage *out=DGHostPointer(m,0,sizeof(*out));
        if(!out || !context->drawable || m->args[1]>1) error=-1;
        else { *out=context->drawable->damage;if(m->args[1]) memset(&context->drawable->damage,0,sizeof(*out)); }
    }
    else if(m->op==DG_READ_DRAWABLE || m->op==DG_READ_DRAWABLE_RECT ||
            m->op==DG_IMPORT_DRAWABLE || m->op==DG_IMPORT_DRAWABLE_PATCH || m->op==DG_IMPORT_DRAWABLE_VIEW) {
        int reading=m->op==DG_READ_DRAWABLE || m->op==DG_READ_DRAWABLE_RECT;
        if(!reading && !DGDrawableWritable(context))reply.error=GL_INVALID_OPERATION;
        else error=DGDrawablePixels(context,m,reading);
    }
    else if(m->op==DG_SNAPSHOT_DRAWABLE_TEXTURE)
        error=DGSnapshotDrawableTexture(context,m,&reply);
    else if(m->op==DG_BIND_DRAWABLE_TEXTURE)
        error=DGBindDrawableTexture(context,m,&reply);
    else if(m->op==DG_FLUSH_DRAWABLE_STATE)
        error=DGFlushDrawableState(context,m,&reply);
    else if(m->op==DG_FLUSH_IOSURFACE_STATE)
        error=DGFlushIOSurfaceState(context,m,&reply);
    else if(m->op==DG_READ_IOSURFACE_TEXTURE)
        error=DGReadIOSurfaceTexture(context,m,&reply);
    else if(m->op==DG_COMMON_STATE)
        error=DGReadCommonState(context,m,&reply);
    else if(m->op==DG_TIGHT_TEX_SUB_IMAGE_2D)
        error=DGTightTexSubImage2D(context,m,&reply);
    else if(m->op==DG_IOSURFACE_TEXTURE||m->op==DG_IOSURFACE_TEXTURE_PACKED)
        error=DGIOSurfaceTexture(context,m,&reply);
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
    else if(m->op==DG_BACKING_FUTURE_TEXTURE)
        error=DGBackingFutureTexture(context,m,&reply);
#endif
#ifdef VIM_BACKING_WRITER_EXPERIMENT
    else if(m->op==DG_BACKING_RENDER_CONTINUE)
        error=DGBackingWriterContinue(context,m,&reply);
#endif
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    else if(m->op>=DG_BACKING_RENDER_BEGIN && (m->op<=DG_BACKING_RENDER_MATERIALIZE
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
        || m->op==DG_BACKING_RENDER_DEFER
#endif
    ))
        error=DGVRAMRenderCommand(context,m,&reply);
#endif
    else if(m->op==DG_VRAM_IMPORT_DRAWABLE || m->op==DG_VRAM_PUBLISH_DRAWABLE)
        error=DGVRAMTransfer(context,m,&reply);
    else if(m->op==DG_VRAM_PATCH_DRAWABLE) {
        if(!DGDrawableWritable(context))reply.error=GL_INVALID_OPERATION;
        else error=DGVRAMTransfer(context,m,&reply);
    }
    else if(m->op==DG_VRAM_STATS) {
        if(m->bytes!=sizeof(dg_vram_stats) || m->flags!=(DG_REPLY|DG_OUTPUT_ONLY))error=-1;
        else if(!m->args[0])for(unsigned i=0;i<4;++i)
            ((uint64_t *)(m+1))[i]=__atomic_load_n(&dg_vram_stats[i],__ATOMIC_RELAXED);
        else if(m->args[0]==DG_VRAM_STATS_DRAWABLE_STORAGE)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=
                __atomic_load_n(&dg_drawable_registration_stats[i],__ATOMIC_RELAXED);
        else if(m->args[0]==DG_VRAM_STATS_SURFACE_REGISTRATION)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=
                __atomic_load_n(&dg_surface_registration_stats[i],__ATOMIC_RELAXED);
        else if(m->args[0]==DG_VRAM_STATS_PREPARED_DRAWABLE)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=
                __atomic_load_n(&dg_prepared_drawable_stats[i],__ATOMIC_RELAXED);
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
        else if(m->args[0]==DG_VRAM_STATS_SCATTER_BACKING)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=
                __atomic_load_n(&dg_scatter_stats[i],__ATOMIC_RELAXED);
#endif
#ifdef VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT
        else if(m->args[0]==DG_VRAM_STATS_GATHER_INITIALIZE)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=
                __atomic_load_n(&dg_gather_stats[i],__ATOMIC_RELAXED);
#endif
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
        else if(m->args[0]==DG_VRAM_STATS_CPU_COALESCING)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=
                __atomic_load_n(&dg_cpu_coalescing_stats[i],__ATOMIC_RELAXED);
#endif
#ifdef VIM_BACKING_WRITER_EXPERIMENT
        else if(m->args[0]==DG_VRAM_STATS_WRITER_ERRORS || m->args[0]==DG_VRAM_STATS_WRITER_ERROR_FIELDS)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=__atomic_load_n(
                m->args[0]==DG_VRAM_STATS_WRITER_ERRORS?&dg_writer_errors[i]:&dg_writer_error_fields[i],__ATOMIC_RELAXED);
        else if(m->args[0]==DG_VRAM_STATS_WRITER_REFUSALS || m->args[0]==DG_VRAM_STATS_WRITER_ADMISSION)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=__atomic_load_n(
                m->args[0]==DG_VRAM_STATS_WRITER_REFUSALS?&dg_writer_refusals[i]:&dg_writer_admission_refusals[i],__ATOMIC_RELAXED);
#endif
#ifdef VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT
        else if(m->args[0]==DG_VRAM_STATS_CANONICAL_WRITER)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=
                __atomic_load_n(&dg_canonical_writer_stats[i],__ATOMIC_RELAXED);
        else if(m->args[0]==DG_VRAM_STATS_CPU_CANONICAL)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=
                __atomic_load_n(&dg_cpu_canonical_stats[i],__ATOMIC_RELAXED);
#endif
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
        else if(m->args[0]==DG_VRAM_STATS_BACKING_SCOPE){
            uint64_t *out=(uint64_t *)(m+1);
            out[0]=__atomic_load_n(&dg_vram_render_begins,__ATOMIC_RELAXED);
            out[1]=__atomic_load_n(&dg_vram_render_materializations,__ATOMIC_RELAXED);
            out[2]=__atomic_load_n(&dg_vram_render_read_only_ends,__ATOMIC_RELAXED);
            out[3]=__atomic_load_n(&dg_vram_render_republished_ends,__ATOMIC_RELAXED);
        }
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
        else if(m->args[0]==DG_VRAM_STATS_CANONICAL_SCOPE)
            for(unsigned i=0;i<4;++i)((uint64_t *)(m+1))[i]=
                __atomic_load_n(&dg_scope_canonical_stats[i],__ATOMIC_RELAXED);
#endif
#endif
        else error=-1;
    }
    else if((m->op>=DG_SURFACE_ACQUIRE && m->op<=DG_SURFACE_UPLOAD) || m->op==DG_SURFACE_RETIRE || m->op==DG_SURFACE_DRAWABLE || m->op==DG_SURFACE_PUBLISH_REGIONS || m->op==DG_SURFACE_SEALED_REGIONS || m->op==DG_SURFACE_SEAL_VERSION || m->op==DG_SURFACE_VERSION_TEXTURE || m->op==DG_SURFACE_PRESENT_VERSION || m->op==DG_SURFACE_IMPORT_REGION || m->op==DG_SURFACE_COPY_REGIONS)
        error=DGSurfaceCommand(context,m,&reply);
    else if(m->op>=DG_SYNC_FENCE && m->op<=DG_SYNC_GET)error=DGHostSyncCommand(context,m,&reply);
    else if(m->op>=DG_BUFFER_QUERY && m->op<=DG_BUFFER_COPY)error=DGHostBufferAccess(context,m,&reply);
    else if(m->op>=DG_MAP_BEGIN && m->op<=DG_MAP_READY)error=DGHostMappingCommand(context,m,&reply);
    else if(m->op==DG_TEXTURE_READ)error=DGHostTextureRead(context,m,&reply);
    else if(m->op==DG_BUFFER_TRANSFER)error=DGHostBufferTransfer(context,m,&reply);
    else if(m->op==DG_RASTER_READ_STAGE)error=DGHostRasterRead(context,m,&reply);
    else if(m->op==DG_STIPPLE_READ)error=DGHostStippleRead(context,m,&reply);
    else if(m->op==DG_PBO_PIXEL_2D || m->op==DG_PBO_PIXEL_3D)error=DGHostPBOPixels(context,m);
    else if(m->op>=DG_PBO_CLIENT_STACK && m->op<=DG_PBO_BIND)error=DGHostPBOState(context,m,&reply);
    else if(m->op==DG_ARRAY_ELEMENT_STATE)error=DGHostPrepareArrayElement(context,m);
    else if(m->op==DG_RETAINED_RENDER_READ)error=DGHostRenderRead(context,m);
    else if(m->op==DG_ARRAY_DIVISOR) {
        if(m->bytes || m->args[0]>UINT32_MAX || m->args[1]>UINT32_MAX)error=-1;
        else if(!dg_core_api.divisor)reply.error=GL_INVALID_OPERATION;
        else {
            GLenum prior=glGetError();if(prior && !context->error)context->error=prior;
            dg_core_api.divisor((GLuint)m->args[0],(GLuint)m->args[1]);reply.error=glGetError();
        }
    }
    else if(m->op==DG_DRAW || m->op==DG_DRAW_INSTANCED_ARRAYS) error=DGHostDraw(context,m);
    else error=DGExecute(context,m,&reply);
#ifdef VIM_GL_DRAW_WITNESS
    if(!error)DGTraceDrawWitness(context,m);
#endif
    if(dg_diagnostics && context && context->cgl && !context->inside_begin &&
       (m->op<974 || m->op==DG_DRAW || m->op==DG_DRAW_INSTANCED_ARRAYS)) {
        // Canonical SDK's End opcode; no extra test on the normal hot path.
        if(m->op==74)DGTraceImmediateResult(context);
        GLenum native_error=glGetError();
        if(native_error) {
            if(!context->error) context->error=native_error;
            if(context->diagnostic_errors++<64) fprintf(stderr,"direct native GL error=%x op=%u args=%llx,%llx,%llx\n",native_error,m->op,m->args[0],m->args[1],m->args[2]);
            if(native_error==GL_INVALID_FRAMEBUFFER_OPERATION_EXT && context->diagnostic_errors<65){
                GLint fbo=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&fbo);
                fprintf(stderr,"direct incomplete framebuffer context=%p fbo=%d status=%x",(void *)context,fbo,
                    glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT));
                const GLenum attachments[]={GL_COLOR_ATTACHMENT0_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_STENCIL_ATTACHMENT_EXT};
                for(unsigned i=0;i<3;++i){
                    GLint type=0,name=0,level=0;
                    glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,attachments[i],GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,&type);
                    if(type){
                        glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,attachments[i],GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&name);
                        if(type==GL_TEXTURE)glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,attachments[i],GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT,&level);
                    }
                    fprintf(stderr," attachment=%x:%x:%d:%d",attachments[i],type,name,level);
                }
                fprintf(stderr,"\n");glGetError();
            }
        }
    }
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
    // AFTER execution: a command can first materialize then write private
    // storage in that same call. All sharing contexts participate, including
    // writes through a texture alias which do not touch the owner's drawable
    // damage serial. Unknown/failed operations invalidate conservatively.
    if(context && context->alias_group && DGBackingPacketMayWrite(m->op) &&
       context->alias_group->backing_write_serial!=UINT64_MAX)++context->alias_group->backing_write_serial;
#endif
respond:
    if(error) { fprintf(stderr,"PassthroughOpenGL rejected op=%u context=%u bytes=%u\n",m->op,m->context,m->bytes);return -1; }
    /* An ordered native observation, not a prediction from a setter. A
     * query's ordinary result remains valid even with an older GL error;
     * retain that error for GetError rather than failing/zeroing the query.
     * Never query inside Begin/End or on a foreign/current-less context. */
    if(observe_errors && dg_error_empty_replies && !reply.error && context && context->cgl &&
       !context->inside_begin && !context->error && CGLGetCurrentContext()==context->cgl){
        GLenum pending=glGetError();
        if(pending)context->error=pending;
        else reply.error|=DG_REPLY_ERROR_EMPTY;
    }
    m->flags=original_flags;
    if(m->flags&DG_REPLY) error=DGTransfer(fd,&reply,sizeof(reply),1) || DGTransfer(fd,m+1,reply.bytes,1);
    return error?-1:0;
}
static int DGInterpret(struct DGInterpreter *s,int fd,struct DGMessage *m)
{
#ifdef VIM_PASSTHROUGH_OPENGL_PACKET_JOURNAL
    if(!s->journal_depth++)DGJournalPacket(s,m);
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    // Nested decoded commands are the actual GL operations. The outer inline
    // batch already covers codec time; do not double-count the envelope.
    struct DGCommandProfileStamp profile=m->op==DG_COMPACT_SCALARS?(struct DGCommandProfileStamp){0}:DGCommandProfileBegin();
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    uint64_t began=DGCommandTimingBegin();
#endif
    int result=DGInterpretCommand(s,fd,m);
#ifdef VIM_PASSTHROUGH_OPENGL_PACKET_JOURNAL
    --s->journal_depth;
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    DGCommandProfileEnd(profile,m->op,m->bytes,0);
#endif
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    unsigned record=DGCommandTimingEnd("interpret",m->op,m->context,m->bytes,began);
    if(record && m->op==DG_BACKING_RENDER_BEGIN && m->args[13]==1 &&
       m->args[14] && m->args[14]<=16384 && m->bytes==m->args[14]*8){
        const uint64_t *pages=(const void *)(m+1);unsigned runs=1,max_run=1,run=1;
        for(unsigned i=1;i<m->args[14];++i){
            if(pages[i-1]<=UINT64_MAX-4096 && pages[i]==pages[i-1]+4096){
                ++run;if(run>max_run)max_run=run;
            }else{++runs;run=1;}
        }
        fprintf(stderr,"DG_COMMAND_BACKING serial=%u pages=%llu gpa_runs=%u maximum_run_pages=%u object=%llu incarnation=%llu epoch=%llu\n",
            record-1,(unsigned long long)m->args[14],runs,max_run,
            (unsigned long long)m->args[10],(unsigned long long)m->args[11],(unsigned long long)m->args[12]);
    }
#endif
    return result;
}
static void DGInterpreterDestroy(struct DGInterpreter *s)
{
    struct DGHostContext *contexts=s->contexts;
    CGLSetCurrentContext(NULL);
    for(unsigned i=1;i<s->next;++i)if(contexts[i].cgl){
        CGLSetCurrentContext(contexts[i].cgl);
        if(contexts[i].inside_begin){glEnd();contexts[i].inside_begin=0;}
        DGHostReleaseArrayElements(&contexts[i]);
        // Drain ALL shared producers before any context can retire a backing
        // used by another one. A half-open primitive must end before Finish.
        glFinish();
    }
    for(unsigned i=1;i<s->next;++i) {
        if(contexts[i].cgl) {
            DGReleaseTextureRead(&contexts[i]);
            CGLSetCurrentContext(contexts[i].cgl);
            DGDisplaySenderStop(&contexts[i]);DGDestroySurfaceLeases(&contexts[i]);
            DGDestroyDrawables(&contexts[i]);DGDestroyFences(&contexts[i]);DGDestroyAliases(&contexts[i]);
            CGLSetCurrentContext(NULL);CGLDestroyContext(contexts[i].cgl);
            DGHostFreeRenderBuffers(&contexts[i]);
        }
    }
    DGDisplaySenderStop(&s->surface_holds);DGDestroySurfaceHolds(&s->surface_holds);
#ifdef VIM_PASSTHROUGH_OPENGL_PACKET_JOURNAL
    DGJournalClose(s);
#endif
    free(s);
}
static void *DGServe(void *opaque)
{
    int fd=(int)(intptr_t)opaque;
    struct DGInterpreter *s=calloc(1,sizeof(*s));
    if(!s){if(fd>=0)close(fd);return NULL;}s->next=1;
    struct DGMessage *storage=NULL;size_t capacity=0;
    const size_t retain_limit=16u*1024u*1024u;
    for(;;) {
        struct DGMessage header;
        if(DGTransfer(fd,&header,sizeof(header),0) || !DGHeaderValid(&header))break;
        int sent=DGInterpreterFastRead(s,fd,&header);
        if(sent<0)break;if(sent)continue;
        size_t needed=sizeof(struct DGMessage)+header.bytes;
        struct DGMessage *m=NULL;
        if(needed<=retain_limit) {
            if(needed>capacity) {
                size_t grown=capacity?capacity:4096;
                while(grown<needed)grown*=2;
                void *replacement=realloc(storage,grown);
                if(!replacement)break;
                storage=replacement;capacity=grown;
            }
            m=storage;
        } else m=malloc(needed);
        if(!m) break;*m=header;
        // Input payloads are completely overwritten by receive. Output-only
        // replies must retain calloc's initialized bytes, including errors.
        if(m->flags&DG_OUTPUT_ONLY)memset(m+1,0,m->bytes);
        else if(DGTransfer(fd,m+1,m->bytes,0)) { if(m!=storage)free(m);break; }
        int error=DGInterpret(s,fd,m);
        if(m!=storage)free(m);if(error)break;
    }
    // EOF or cancellation is not GPU completion. Drain every shared producer
    // before any context/alias retirement, as the inline interpreter does.
    free(storage);DGInterpreterDestroy(s);
    if(fd>=0)close(fd);return NULL;
}
static int DGConfigureHost(void)
{
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING
    if(getenv("VIM_PASSTHROUGH_OPENGL_COMMAND_TIMING")){
        if(mach_timebase_info(&dg_command_timebase)!=KERN_SUCCESS || !dg_command_timebase.denom)return 2;
        dg_command_timing=1;
        fprintf(stderr,"DG_COMMAND_TIMING enabled threshold_us=2000 maximum_records=2048 timebase_numer=%u timebase_denom=%u\n",
            dg_command_timebase.numer,dg_command_timebase.denom);
    }
#endif
    const char *renderer=getenv("VIM_PASSTHROUGH_OPENGL_RENDERER_ID");
    if(renderer){char *end=NULL;unsigned long id=strtoul(renderer,&end,0);
        if(!*renderer || !end || *end || !id || id>INT_MAX){fprintf(stderr,"Invalid explicit research renderer ID\n");return 2;}
        dg_reference_renderer=(GLint)id;}
    // Qualified on native NVIDIA and M4, plus the live shared-memory route.
    // Keep the old GL readback as an explicit diagnostic override/fallback.
    const char *surface_read=getenv("VIM_PASSTHROUGH_OPENGL_IOSURFACE_READBACK");
    dg_iosurface_readback=!surface_read || strcmp(surface_read,"0");
    dg_surface_read_stats=getenv("VIM_PASSTHROUGH_OPENGL_SURFACE_READ_STATS")!=NULL;
    // Bounded unshared backing reuse preserves exact logical texture/FBO
    // dimensions and excludes live aliases. Keep the qualified allocation
    // saving on by default; "0" is an explicit diagnostic comparison only.
    const char *storage_reuse=getenv("VIM_PASSTHROUGH_OPENGL_STORAGE_REUSE");
    dg_storage_reuse=!storage_reuse || strcmp(storage_reuse,"0");
    dg_surface_import=getenv("VIM_PASSTHROUGH_OPENGL_SURFACE_IMPORT")!=NULL;
    dg_diagnostics=getenv("VIM_PASSTHROUGH_OPENGL_DIAGNOSTICS")!=NULL;
    dg_transfer_trace=getenv("VIM_PASSTHROUGH_OPENGL_TRANSFER_TRACE")!=NULL;
    return 0;
}
#if defined(VIM_PASSTHROUGH_OPENGL_MACH_EXECUTOR)
static void DGMachExecute(struct DGMachStream *stream)
{dg_mach_host_stream=stream;DGServe((void *)(intptr_t)-1);dg_mach_host_stream=NULL;}
int main(void)
{return DGConfigureHost()?2:DGMachServe(DGMachExecute);}
#elif !defined(VIM_PASSTHROUGH_OPENGL_INPROCESS)
int main(void)
{
    if(DGConfigureHost())return 2;
    // End a bounded capture without disconnecting/restarting the desktop.
    signal(SIGUSR1,DGDisableDiagnostics);
    // Explicit research A/B only: switch next commands without resetting
    // the guest or abandoning active surfaces/contexts. No signal-time I/O.
    if(getenv("VIM_PASSTHROUGH_OPENGL_AB_CONTROL"))signal(SIGUSR2,DGToggleSurfaceRead);
    if(getenv("VIM_PASSTHROUGH_OPENGL_AB_SURFACE_IMPORT"))signal(SIGUSR2,DGToggleSurfaceImport);
    signal(SIGPIPE,SIG_IGN);int fd=socket(AF_INET,SOCK_STREAM,0),yes=1;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(DGPort()),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    if(bind(fd,(void *)&address,sizeof(address)) || listen(fd,16)) { perror("PassthroughOpenGL listener");return 1; }
    fprintf(stderr,"Direct desktop GL proof listening on 127.0.0.1:%u; SDK %s\n",DGPort(),DG_LAYOUT);
    for(;;) {
        int client=accept(fd,NULL,NULL);if(client<0) { if(errno==EINTR) continue;return 1; }
        setsockopt(client,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
        pthread_t thread;
        if(pthread_create(&thread,NULL,DGServe,(void *)(intptr_t)client)) close(client);
        else pthread_detach(thread);
    }
}
#else
#include "passthrough-opengl-memory-stream.c"
#include "passthrough-opengl-inline-stream.c"
static pthread_once_t dg_inprocess_config_once=PTHREAD_ONCE_INIT;
static int dg_inprocess_config_error;
static int dg_inprocess_inline;
static void DGInprocessConfigure(void)
{dg_inprocess_config_error=DGConfigureHost();dg_inprocess_inline=getenv("VIM_PASSTHROUGH_OPENGL_INLINE") && !strcmp(getenv("VIM_PASSTHROUGH_OPENGL_INLINE"),"1");}
static void DGInprocessServe(struct DGMemoryStream *stream)
{dg_memory_host_stream=stream;DGServe((void *)(intptr_t)-1);dg_memory_host_stream=NULL;}
static void *DGInprocessOpen(void) {return DGMemoryStreamOpen(DGInprocessServe);}
static ssize_t DGInprocessRead(void *s,void *p,size_t n) {return DGMemoryClientRead(s,p,n);}
static ssize_t DGInprocessWrite(void *s,const void *p,size_t n) {return DGMemoryClientWrite(s,p,n);}
static ssize_t DGInprocessReadv(void *s,const struct iovec *v,int n) {return DGMemoryClientReadv(s,v,n);}
static ssize_t DGInprocessWritev(void *s,const struct iovec *v,int n) {return DGMemoryClientWritev(s,v,n);}
static int DGInprocessShutdown(void *s) {return DGMemoryStreamShutdown(s);}
static int DGInprocessClose(void *s) {return DGMemoryStreamCloseAndWait(s);}
__attribute__((visibility("default")))
const struct VIMGLInprocessAPI *VIMPassthroughOpenGLInprocessGetAPI(uint32_t version,uint32_t bytes)
{
    static const struct VIMGLInprocessAPI api={VIM_GL_INPROCESS_ABI,sizeof(api),
        DGInprocessOpen,DGInprocessRead,DGInprocessWrite,DGInprocessReadv,DGInprocessWritev,
        DGInprocessShutdown,DGInprocessClose,DGMemoryStreamLiveCount};
    static const struct VIMGLInprocessAPI inline_api={VIM_GL_INPROCESS_ABI,sizeof(inline_api),
        DGInlineOpen,DGInlineRead,DGInlineWrite,DGInlineReadv,DGInlineWritev,
        DGInlineShutdown,DGInlineClose,DGInlineLiveCount};
    if(version!=VIM_GL_INPROCESS_ABI || bytes!=sizeof(api))return NULL;
    pthread_once(&dg_inprocess_config_once,DGInprocessConfigure);
    return dg_inprocess_config_error?NULL:dg_inprocess_inline?&inline_api:&api;
}
#endif
