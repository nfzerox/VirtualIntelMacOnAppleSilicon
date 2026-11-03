/* Native-host GL client. The default transport uses the framebuffer's shared
 * VRAM/RAM path; a guest-local socket relay remains a diagnostic fallback. Scalar
 * void calls are batched; queries drain the batch once and return real data.
 * This is deliberately separate from supported-guest production payloads. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <stdio.h>
#include "period-stderr.h"
#include <stdlib.h>
#include <limits.h>
#include <syslog.h>
#include <sys/sysctl.h>
#include <uuid/uuid.h>
#include <mach/mach_time.h>
#include <IOKit/IOKitLib.h>
#if defined(__i386__)
/* Apple's Rosetta1 IOKit bridge has the struct method but no trap wrapper.
 * Negotiate the existing method path before submitting anything; never
 * retry an already consumed command. Native Intel keeps its fast trap. */
extern IOReturn IOConnectTrap4(io_connect_t,unsigned int,uintptr_t,uintptr_t,
    uintptr_t,uintptr_t) __attribute__((weak_import));
#endif
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
#include "passthrough-opengl-wire.h"
#include "iosurface-element-layout.h"
#include "direct-layout.h"
#include "passthrough-opengl-surfaces.h"
#include "passthrough-opengl-program-locations.h"
#include "guest-resource-namespace.h"
#include "passthrough-opengl-endian.h"
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 && !defined(VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT)
/* The product's native Tiger driver uses shared RAM/VRAM exclusively.
 * Rosetta's native GL loader deliberately lacks BSD socket interposers;
 * merely linking the old opt-in relay fallback prevents PPC GL from loading.
 * Keep those research transports in later/host builds, not in this payload. */
#define VIM_GL_SHARED_ONLY 1
#else
#define VIM_GL_SHARED_ONLY 0
#endif
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ <= 1060
#include <sys/sysctl.h>
static pthread_once_t dg_rosetta_once=PTHREAD_ONCE_INIT;
static int dg_rosetta_arrays;
static void DGQueryRosettaArrays(void)
{
    int native=1;size_t bytes=sizeof(native);
    dg_rosetta_arrays=!sysctlbyname("sysctl.proc_native",&native,&bytes,NULL,0) &&
        bytes==sizeof(native) && native==0;
}
#endif
static size_t DGBoundedStringLength(const char *string,size_t capacity)
{
    const char *end=memchr(string,0,capacity);
    return end?(size_t)(end-string):capacity;
}
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT
// Host-only qualification adapter for the same interpreter/library ABI that
// QEMU will use. Not compiled into a guest client; no guest dylib dependency.
#include "../vendor/qemu/include/hw/display/vim-passthrough-opengl-inprocess.h"
#include <dlfcn.h>
static const struct VIMGLInprocessAPI *dg_inprocess_api;
static pthread_once_t dg_inprocess_once=PTHREAD_ONCE_INIT;
static void DGLoadInprocessClient(void)
{
    const char *path=getenv("VIM_PASSTHROUGH_OPENGL_INPROCESS_LIBRARY");if(!path || !*path)return;
    void *library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    VIMGLGetInprocessAPI get=library?(VIMGLGetInprocessAPI)dlsym(library,"VIMPassthroughOpenGLInprocessGetAPI"):NULL;
    dg_inprocess_api=get?get(VIM_GL_INPROCESS_ABI,sizeof(struct VIMGLInprocessAPI)):NULL;
    if(!dg_inprocess_api)VIMGLStderr("In-process executor initialization failed: %s\n",library?"incompatible API":dlerror());
    // Retain the library for process lifetime: workers retire asynchronously.
}
#endif

struct DGConnection { int fd,dead;io_connect_t device;unsigned refs,features;uint64_t capabilities;
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT
    void *inprocess;
#endif
    unsigned scatter_supported,ram_scatter_supported,masked_scatter_supported,row_upload_supported,row_upload_transaction_supported,exchange_supported,exchange_trap_supported,drained_close_supported,large_exchange_supported;
    struct DGContext *backing_owner;pthread_mutex_t backing_lock;
    uint64_t direct_read_bytes,staged_read_bytes,direct_write_bytes,staged_write_bytes;
    uint64_t row_upload_bytes,row_upload_source_bytes;
    uint64_t borrowed_packet_bytes,promoted_packet_bytes;
    _Alignas(8) uint64_t borrowed_import_bytes,copied_import_bytes;
    struct DGProgramLocations program_locations;
    struct DGClientSync *syncs;
    struct DGClientMapping *maps;size_t map_bytes;pthread_mutex_t map_lock;
    pthread_mutex_t lock;size_t used;unsigned char batch[DG_BATCH_BYTES];
    size_t scalar_start;unsigned scalar_compact;
    uint64_t compact_commands,compact_runs,compact_saved_bytes;
    _Alignas(8) unsigned char exchange_send[VIM_GL_LARGE_EXCHANGE_LIMIT],exchange_receive[VIM_GL_EXCHANGE_LIMIT]; };
struct DGClientDrawable { void *pixels;GLsizei width,height;uint32_t id;struct DGClientDrawable *next; };
struct DGClientSurface { uint64_t object,incarnation;int width,height;uint32_t lease,refs,format;struct DGClientSurface *next; };
struct DGContext {
    struct DGConnection *connection;uint32_t id;GLenum error;
    int width,height,row,alignment[2],row_length[2],skip_rows[2],skip_pixels[2],row_bytes[2];
    int image_height[2],skip_images[2];
    unsigned profile,big_endian_arrays;
    /* CGL can migrate one native context between real hardware/software
     * screens. Retain both sets so callers' old string pointers stay valid. */
    char *strings[2][6];unsigned native_renderer_kind;
    GLuint pixel_buffer[2];void *pixels;uint64_t unsupported[16];
    GLuint array_buffer;int client_texture;
    struct DGVertexState *vertex_states,*vertex;
    struct DGClientAttribFrame *client_attrib_stack;
    unsigned char *draw_scratch;size_t draw_scratch_capacity;
    uint64_t shape_failures[64];unsigned shape_failure_count;
    struct DGClientDrawable *drawables;uint32_t next_drawable,current_drawable;
    unsigned draw_failures;
    struct DGClientSurface *surfaces;
    struct DGCommonState common_state;
    int common_state_valid,common_state_blocked,error_observed_empty;
    struct VIMSurfaceBackingLoan backing_loan;
    int backing_deferred,backing_future,backing_writer;
};
struct DGPacket { struct DGMessage message;unsigned char *data;size_t capacity;int failed,valid_reply,borrowed,promoted,oom; };
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#include "direct-client-profile.c"
#endif
#include "passthrough-opengl-program-locations-client.c"
#include "period-thread-storage.h"
#if VIM_PERIOD_PTHREAD_TLS
typedef struct DGContext *DGThreadContext;
VIM_DEFINE_PERIOD_STORAGE(DGThreadContext,dg_thread)
#define dg_current (*dg_thread_storage())
#else
static __thread struct DGContext *dg_current;
#endif
// A debugger can change this process-local integer between research workloads
// without calling malloc/setenv in a paused WindowServer. -1 uses the normal
// environment/default; 0/1 force copied/borrowed packets. Never persisted.
static volatile int dg_packet_borrow_override=-1;
// Same-boot transport comparison only; a debugger writes this integer without
// invoking guest allocation or modifying the persisted launch environment.
static volatile int dg_exchange_override=-1;
// Same-boot diagnostic comparison; negotiation, not failure/replay, selects
// the path. Both paths preserve the exact same ordered native exchange.
static volatile int dg_exchange_trap_override=1;
// Same ordered exchange and replies, different negotiated REQUEST bound only.
static volatile int dg_large_exchange_override=1;
const char *DGTransportName(void)
{
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT
    if(dg_current && dg_current->connection->inprocess)return "in-process native executor";
#endif
    return !dg_current ? "none" : dg_current->connection->device ? "shared VRAM device" : "socket";
}
static void DGUnsupported(struct DGContext *,unsigned);
static void DGFreeClientSyncs(struct DGConnection *);
static void DGFreeClientMaps(struct DGConnection *);
static int DGImportDrawableRows(const void *,int,int,size_t,int);
static int DGImportDrawablePatchRows(const void *,int,int,int,int,size_t,int);
static int DGTightTexSubImage2D(struct DGContext *,GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void *);

#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#define DGConnectionTransfer DGConnectionTransferProfileBody
#endif
static int DGConnectionTransfer(struct DGConnection *c,void *buffer,size_t bytes,int writing)
{
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT
    if(c->inprocess){
        unsigned char *p=buffer;
        while(bytes){ssize_t n=writing?dg_inprocess_api->write(c->inprocess,p,bytes):
            dg_inprocess_api->read(c->inprocess,p,bytes);
            if(n<=0)return -1;p+=n;bytes-=(size_t)n;}
        return 0;
    }
#endif
    if(!c->device) {
#if VIM_GL_SHARED_ONLY
        return -1;
#else
        return DGTransfer(c->fd,buffer,bytes,writing);
#endif
    }
    unsigned char *p=buffer;
    while(bytes) {
        struct VIMGLTransfer request={writing ? 2 : 3,bytes>VIM_GL_CHUNK ? VIM_GL_CHUNK : (uint32_t)bytes,(uintptr_t)p};
        struct VIMGLResult result={0};size_t size=sizeof(result);
        IOReturn error=IOConnectCallStructMethod(c->device,kIOVM2DDirectGLTransfer,&request,sizeof(request),&result,&size);
        if(error || size!=sizeof(result) || result.count<=0 || (uint32_t)result.count>request.bytes) {
            if(getenv("VIM_PASSTHROUGH_OPENGL_TRANSPORT_DIAGNOSTICS"))
                VIMGLStderr("PassthroughOpenGL device transfer action=%u bytes=%u error=%x size=%lu result=%d\n",
                    request.action,request.bytes,error,(unsigned long)size,result.count);
            return -1;
        }
        if(!writing) {
            if(result.reserved&VIM_GL_RESULT_DIRECT_READ)c->direct_read_bytes+=result.count;
            else c->staged_read_bytes+=result.count;
        } else {
            if(result.reserved&VIM_GL_RESULT_DIRECT_WRITE)c->direct_write_bytes+=result.count;
            else c->staged_write_bytes+=result.count;
        }
        p+=result.count;bytes-=result.count;
    }
    return 0;
}
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#undef DGConnectionTransfer
static int DGConnectionTransfer(struct DGConnection *c,void *buffer,size_t bytes,int writing)
{
    struct DGClientProfileStamp s=DGClientProfileBegin(2,!!writing,bytes);
    int result=DGConnectionTransferProfileBody(c,buffer,bytes,writing);DGClientProfileEnd(s);return result;
}
#endif
static __attribute__((noinline,cold)) int DGDiscardReply(struct DGConnection *c,size_t bytes)
{
    // Keep this error-only scratch buffer out of DGSubmit's hot stack frame
    // (and avoid a stack probe on every ordinary batched graphics command).
    unsigned char discard[4096];
    while(bytes) {
        size_t n=bytes>sizeof(discard)?sizeof(discard):bytes;
        if(DGConnectionTransfer(c,discard,n,0))return -1;bytes-=n;
    }
    return 0;
}
int DGReadTransferStats(uint64_t *direct_bytes,uint64_t *staged_bytes)
{
    if(!dg_current || !direct_bytes || !staged_bytes)return 0;
    struct DGConnection *c=dg_current->connection;
    pthread_mutex_lock(&c->lock);*direct_bytes=c->direct_read_bytes;*staged_bytes=c->staged_read_bytes;
    pthread_mutex_unlock(&c->lock);return c->device!=0;
}
int DGImportTransferStats(uint64_t *borrowed_bytes,uint64_t *copied_bytes)
{
    if(!dg_current || !borrowed_bytes || !copied_bytes)return 0;
    struct DGConnection *c=dg_current->connection;
    *borrowed_bytes=__sync_fetch_and_add(&c->borrowed_import_bytes,0);
    *copied_bytes=__sync_fetch_and_add(&c->copied_import_bytes,0);return 1;
}
int DGWriteTransferStats(uint64_t *direct_bytes,uint64_t *staged_bytes)
{
    if(!dg_current || !direct_bytes || !staged_bytes)return 0;
    struct DGConnection *c=dg_current->connection;
    pthread_mutex_lock(&c->lock);*direct_bytes=c->direct_write_bytes;*staged_bytes=c->staged_write_bytes;
    pthread_mutex_unlock(&c->lock);return c->device!=0;
}
int DGRowUploadStats(uint64_t *payload_bytes,uint64_t *source_bytes)
{
    if(!dg_current || !payload_bytes || !source_bytes)return 0;
    struct DGConnection *c=dg_current->connection;pthread_mutex_lock(&c->lock);
    *payload_bytes=c->row_upload_bytes;*source_bytes=c->row_upload_source_bytes;
    pthread_mutex_unlock(&c->lock);return c->device && c->row_upload_supported;
}
int DGBorrowedPacketStats(uint64_t *borrowed_bytes,uint64_t *promoted_bytes)
{
    if(!dg_current || !borrowed_bytes || !promoted_bytes)return 0;
    struct DGConnection *c=dg_current->connection;pthread_mutex_lock(&c->lock);
    *borrowed_bytes=c->borrowed_packet_bytes;*promoted_bytes=c->promoted_packet_bytes;
    pthread_mutex_unlock(&c->lock);return 1;
}
static void DGConnectionClose(struct DGConnection *c)
{
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT
    if(c->inprocess){dg_inprocess_api->close(c->inprocess);c->inprocess=NULL;}
#endif
    if(c->device) { IOServiceClose(c->device);c->device=0; }
#if !VIM_GL_SHARED_ONLY
    if(c->fd>=0) { close(c->fd);c->fd=-1; }
#endif
}
static void DGConnectionRelease(struct DGConnection *c)
{ if(!__sync_sub_and_fetch(&c->refs,1)) { DGConnectionClose(c);DGFreeClientSyncs(c);DGFreeClientMaps(c);
    pthread_mutex_destroy(&c->backing_lock);pthread_mutex_destroy(&c->map_lock);pthread_mutex_destroy(&c->lock);free(c); } }
static int DGConnect(struct DGConnection *c)
{
    const char *server=getenv("VIM_PASSTHROUGH_OPENGL_HOST");
    c->fd=-1;
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT
    if(getenv("VIM_PASSTHROUGH_OPENGL_INPROCESS_LIBRARY")){
        pthread_once(&dg_inprocess_once,DGLoadInprocessClient);
        if(!dg_inprocess_api)return -1;
        c->inprocess=dg_inprocess_api->open();return c->inprocess?0:-1;
    }
#endif
    if(!server && !getenv("VIM_PASSTHROUGH_OPENGL_SOCKET_ONLY")) {
        io_service_t service=VIMIOServiceForClass("VMsvga2Accel");
        if(service) {
            IOReturn error=IOServiceOpen(service,mach_task_self(),2,&c->device);IOObjectRelease(service);
            struct VIMGLTransfer request={1,0,0};struct VIMGLResult result={0};size_t size=sizeof(result);
            if(!error) error=IOConnectCallStructMethod(c->device,kIOVM2DDirectGLTransfer,&request,sizeof(request),&result,&size);
            if(!error && size==sizeof(result) && result.count==0) {
                c->scatter_supported=!!(result.reserved&VIM_GL_RESULT_CAP_SCATTER);
                c->ram_scatter_supported=!!(result.reserved&VIM_GL_RESULT_CAP_RAM_SCATTER);
                c->masked_scatter_supported=!!(result.reserved&VIM_GL_RESULT_CAP_MASKED_SCATTER);
                c->row_upload_supported=!!(result.reserved&VIM_GL_RESULT_CAP_ROW_UPLOAD);
                c->row_upload_transaction_supported=c->row_upload_supported && !!(result.reserved&VIM_GL_RESULT_CAP_ROW_UPLOAD_TRANSACTION);
                c->exchange_supported=!!(result.reserved&VIM_GL_RESULT_CAP_EXCHANGE) && !getenv("VIM_PASSTHROUGH_OPENGL_DISABLE_EXCHANGE");
                c->exchange_trap_supported=!!(result.reserved&VIM_GL_RESULT_CAP_EXCHANGE_TRAP);
#if defined(__LP64__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
                /* Native Darwin9 forwards only each argument's low word to
                 * IOTrap's 32-bit void* callback (9J3050 +147..199). Select
                 * the existing user_addr_t-safe struct method BEFORE sending
                 * commands. Keep batching/one-call exchange, and keep native
                 * i386's fast trap. Never replay a failed, possibly consumed
                 * trap. No unexported kernel KPI or newer guest API needed. */
                c->exchange_trap_supported=0;
#endif
#if defined(__i386__)
                c->exchange_trap_supported=c->exchange_trap_supported && IOConnectTrap4!=NULL;
#endif
                c->large_exchange_supported=c->exchange_trap_supported && !!(result.reserved&VIM_GL_RESULT_CAP_LARGE_EXCHANGE);
                c->drained_close_supported=!!(result.reserved&VIM_GL_RESULT_CAP_DRAINED_CLOSE);
                return 0;
            }
            if(getenv("VIM_PASSTHROUGH_OPENGL_TRANSPORT_DIAGNOSTICS"))
                VIMGLStderr("PassthroughOpenGL device unavailable error=%x size=%lu result=%d\n",error,(unsigned long)size,result.count);
            if(c->device) { IOServiceClose(c->device);c->device=0; }
        }
    }
#if VIM_GL_SHARED_ONLY
    return -1;
#else
    int fd=socket(server ? AF_INET : AF_UNIX,SOCK_STREAM,0),yes=1;
    if(fd<0) return -1;
    setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
    int result;
    if(server) {
        /* Explicit diagnostic TCP override only. Sandboxed system clients
         * use the normal local graphics service, not network exceptions. */
        struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(DGPort())};
        setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
        result=inet_pton(AF_INET,server,&address.sin_addr)==1 ? connect(fd,(void *)&address,sizeof(address)) : -1;
    } else {
        struct sockaddr_un address={.sun_family=AF_UNIX};
        strlcpy(address.sun_path,"/private/var/run/vim-passthrough-opengl.sock",sizeof(address.sun_path));
        result=connect(fd,(void *)&address,sizeof(address));
    }
    if(result) { close(fd);return -1; }c->fd=fd;return 0;
#endif
}

static int DGDrain(struct DGConnection *c)
{
    if(c->dead) return -1;
    if(c->used && DGConnectionTransfer(c,c->batch,c->used,1)) { c->dead=1;return -1; }
    c->used=0;c->scalar_start=0;c->scalar_compact=0;return 0;
}
#include "passthrough-opengl-compact-client.c"
static void DGFlushTransport(struct DGContext *context)
{
    if(!context) return;struct DGConnection *c=context->connection;
    pthread_mutex_lock(&c->lock);
    if(DGDrain(c)) context->error=GL_INVALID_OPERATION;
    pthread_mutex_unlock(&c->lock);
}
enum { DG_COMMON_VIEWPORT=1u, DG_COMMON_FRAMEBUFFER=2u, DG_COMMON_DRAW_BUFFER=4u,
    DG_COMMON_RECTANGLE=8u, DG_COMMON_MODELVIEW=16u, DG_COMMON_PROJECTION=32u,
    DG_COMMON_CORE=15u,DG_COMMON_ALL=63u };
#include "direct-common-neutral-ops.inc"
static volatile int dg_common_neutral_reuse=1; // Owned-process A/B only.
static struct DGPacket DGPacketNew(struct DGContext *c,uint32_t op)
{
    // Retain actual native values only across explicitly audited unrelated
    // operations. An error check still executes normally and clears only GL
    // error flags, not matrices, viewport or bindings. Unknown/private calls
    // and all actual field mutations retain conservative invalidation.
    // Captured-array draws preserve all six fields too: the native executor
    // restores temporary buffer/client-unit state and alias/FBO helpers do
    // not change the logical viewport, matrices or public bindings.
    unsigned retained=dg_common_neutral_reuse ?
        (op==DG_ERROR||op==DG_UNIFORM_QUERY || op==DG_DRAW || op==DG_DRAW_INSTANCED_ARRAYS ||
         op==DG_FLUSH_DRAWABLE_STATE || op==DG_FLUSH_IOSURFACE_STATE || op==DG_SURFACE_EPOCH ||
         op==DG_SURFACE_PUBLISH || op==DG_SURFACE_PUBLISH_REGIONS || op==DG_SURFACE_COPY_REGIONS ||
         op==DG_SURFACE_SEALED_REGIONS || op==DG_SURFACE_SEAL_VERSION || op==DG_SURFACE_IMPORT_REGION ?
            DG_COMMON_ALL : op<974 ? dg_common_neutral_ops[op] : 0) : 0;
    // The compound Flush queries the native FBO and submits work; its alias
    // refresh restores caller bindings and never changes matrices/viewport.
    // IOSurface write enumeration and resource-epoch observation likewise
    // mutate none of the six public fields. Region publication, sealing and
    // import restore the caller's bindings after their private GPU blits;
    // pixel/version changes are not viewport/matrix/binding mutations.
    // Drawable retargeting and unknown service calls still invalidate all.
    if(c){c->common_state_valid&=retained;c->error_observed_empty=0;}
    struct DGPacket p={0};p.message.op=op;p.message.context=c ? c->id : 0;return p;
}
static int DGPacketOwn(struct DGPacket *p,size_t capacity)
{
    if(capacity<p->message.bytes || capacity>DG_MAX_BYTES) { p->failed=1;return 0; }
    if(!p->borrowed) {
        if(capacity<=p->capacity)return 1;
        unsigned char *data=realloc(p->data,capacity?capacity:1);
        if(!data) { p->failed=p->oom=1;return 0; }
        p->data=data;p->capacity=capacity;return 1;
    }
    unsigned char *data=malloc(capacity?capacity:1);
    if(!data) { p->failed=p->oom=1;return 0; }
    if(p->message.bytes)memcpy(data,p->data,p->message.bytes);
    p->data=data;p->capacity=capacity;p->borrowed=0;p->promoted=1;return 1;
}
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#define DGPacketPointer DGPacketPointerProfileBody
#endif
static void DGPacketPointer(struct DGPacket *p,int index,const void *pointer,size_t bytes,int output)
{
    if(bytes==SIZE_MAX || (pointer && !bytes) || bytes>DG_MAX_BYTES || p->message.bytes>DG_MAX_BYTES-bytes) { p->failed=1;return; }
    if(!pointer) return;
    size_t offset=((size_t)p->message.bytes+7)&~(size_t)7,end=offset+bytes;
    if(end>DG_MAX_BYTES) { p->failed=1;return; }
    // The generated wrapper submits before returning to its caller. For a
    // single large input span, borrow until DGSubmit snapshots its small batch
    // or synchronously consumes the large transfer. No host wire change or
    // guest pointer escapes. Multiple spans/output replies promote to owned
    // storage before any mutation. Small stack/name packets keep the old path.
    if(!output && !p->data && !p->message.bytes && bytes>=64u*1024u &&
       (dg_packet_borrow_override>0 || (dg_packet_borrow_override<0 && !getenv("VIM_GL_DISABLE_PACKET_BORROW")))) {
        p->data=(unsigned char *)pointer;p->capacity=bytes;p->borrowed=1;
        p->message.args[index]=1;p->message.bytes=(uint32_t)bytes;return;
    }
    if(p->borrowed && !DGPacketOwn(p,end))return;
    if(end>p->capacity) {
        unsigned char *data=realloc(p->data,end ? end : 1);
        if(!data) { p->failed=p->oom=1;return; }p->data=data;p->capacity=end;
    }
    if(offset>p->message.bytes) memset(p->data+p->message.bytes,0,offset-p->message.bytes);
    // Mode 2 seeds an output without borrowing it or doing a redundant clear.
    if(bytes) { if(output==1) memset(p->data+offset,0,bytes);else memcpy(p->data+offset,pointer,bytes); }
    p->message.args[index]=offset+1;p->message.bytes=(uint32_t)end;
}
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#undef DGPacketPointer
static void DGPacketPointer(struct DGPacket *p,int index,const void *pointer,size_t bytes,int output)
{
    struct DGClientProfileStamp s=DGClientProfileBegin(3,(unsigned)output,bytes);
    DGPacketPointerProfileBody(p,index,pointer,bytes,output);DGClientProfileEnd(s);
}
#endif
static void DGPacketOutput(struct DGPacket *p,struct DGContext *context,int index,void *pointer,size_t bytes)
{
    if(!bytes){
        if(!context || !(context->connection->capabilities&DG_CAP_QUERY_REPLIES)) { p->failed=1;return; }
        /* Native zero-capacity calls may still write scalar metadata. Give
         * the decoder a real private zero-length address, without reading
         * or writing the unused caller pointer (which may be NULL/guarded). */
        unsigned char unused=0;DGPacketPointer(p,index,&unused,1,0);return;
    }
    DGPacketPointer(p,index,pointer,bytes,2);
}
static void DGPacketQueryInput(struct DGPacket *p,int index,const void *pointer,size_t bytes)
{
    if(!bytes){unsigned char unused=0;DGPacketPointer(p,index,&unused,1,0);return;}
    DGPacketPointer(p,index,pointer,bytes,0);
}
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#define DGSubmit DGSubmitProfileBody
#endif
static struct DGReply DGSubmit(struct DGContext *context,struct DGPacket *p,int sync)
{
    struct DGReply reply={0};
    p->valid_reply=0; // a reused packet must never retain a previous request's acknowledgement
    // A compatibility synchronous reply echoes the payload unless INPUT_ONLY. Never
    // receive into borrowed read-only input (e.g. an input-only query name),
    // or bypass ReadPixels' seeded-padding/error-preservation semantics.
    if(!p->failed && p->borrowed && sync && !(p->message.flags&DG_INPUT_ONLY))
        DGPacketOwn(p,p->message.bytes);
    if(p->oom) { if(context)context->error=GL_OUT_OF_MEMORY;return reply; }
    if(!context || p->failed) {
        if(context) {
            if(p->message.op<974) {
                unsigned op=p->message.op;
                uint64_t key=((uint64_t)op<<32)|(uint32_t)p->message.args[0];unsigned i=0;
                for(;i<context->shape_failure_count;++i) if(context->shape_failures[i]==key) break;
                if(i==context->shape_failure_count && i<64)
                {
                    context->shape_failures[context->shape_failure_count++]=key;
                    VIMGLStderr("VIM PassthroughOpenGL codec shape failed slot=%u args=%llx,%llx,%llx\n",op,
                        p->message.args[0],p->message.args[1],p->message.args[2]);
                    syslog(LOG_NOTICE,"VIM PassthroughOpenGL codec shape failed slot=%u args=%llx,%llx,%llx",op,
                        p->message.args[0],p->message.args[1],p->message.args[2]);
                }
                DGUnsupported(context,op);
            } else context->error=GL_INVALID_OPERATION;
        }
        return reply;
    }
    struct DGConnection *c=context->connection;
    p->message.flags|=sync ? DG_REPLY : 0;
    /* Drawable ownership exchanges are already ordered after queued drawing.
     * Observe the native error state there too, rather than requiring another
     * trap immediately afterwards. The executor must still be on this exact
     * context, outside Begin/End, and actually observe NO_ERROR. Completion
     * and CPU exclusion are not weakened by this reply-only optimization. */
    int observe_errors=sync && p->message.context &&
        (c->capabilities&DG_CAP_ERROR_EMPTY_REPLY);
    if(observe_errors)p->message.flags|=DG_OBSERVE_ERROR_EMPTY;
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
    struct DGClientProfileStamp lock_profile=DGClientProfileBegin(5,0,0);
#endif
    pthread_mutex_lock(&c->lock);
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
    DGClientProfileEnd(lock_profile);
#endif
    unsigned location_operation=DGProgramLocationOperation(p->message.op);
    if(!c->dead && location_operation && location_operation!=DG_LOCATION_CREATE)
        DGLocationMutation(&c->program_locations,location_operation,(uint32_t)p->message.args[0]);
    if(p->borrowed)c->borrowed_packet_bytes+=p->message.bytes;
    if(p->promoted)c->promoted_packet_bytes+=p->message.bytes;
    size_t bytes=sizeof(p->message)+p->message.bytes;
    int failed=c->dead;
    size_t sendBytes=sizeof(p->message)+((p->message.flags&DG_OUTPUT_ONLY)?0:p->message.bytes);
    size_t receiveBytes=sizeof(reply)+((p->message.flags&DG_INPUT_ONLY)?0:p->message.bytes);
    size_t exchange_limit=c->large_exchange_supported && dg_large_exchange_override && dg_exchange_trap_override ?
        VIM_GL_LARGE_EXCHANGE_LIMIT:VIM_GL_EXCHANGE_LIMIT;
    int exchange=sync && c->device && c->exchange_supported && dg_exchange_override!=0 &&
        sendBytes<=exchange_limit-sizeof(struct VIMGLExchange) && receiveBytes<=VIM_GL_EXCHANGE_LIMIT;
    if(!failed && exchange) {
        size_t limit=exchange_limit-sizeof(struct VIMGLExchange);
        if(c->used>limit-sendBytes)failed=DGDrain(c);
        if(!failed){
            struct VIMGLExchange header={1,(uint32_t)(c->used+sendBytes),(uint32_t)receiveBytes,0};
            memcpy(c->exchange_send,&header,sizeof(header));
            unsigned char *data=c->exchange_send+sizeof(header);
            if(c->used)memcpy(data,c->batch,c->used);data+=c->used;
            memcpy(data,&p->message,sizeof(p->message));
            if(!(p->message.flags&DG_OUTPUT_ONLY) && p->message.bytes)
                memcpy(data+sizeof(p->message),p->data,p->message.bytes);
            c->used=0;c->scalar_start=0;c->scalar_compact=0;size_t received=receiveBytes;
            // A failed trap may already have consumed the stream request.
            // Never replay it through the method fallback. Old providers do
            // not advertise the trap and keep their existing method path.
            IOReturn error=c->exchange_trap_supported && dg_exchange_trap_override ?
                IOConnectTrap4(c->device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)c->exchange_send,
                    sizeof(header)+header.send_bytes,(uintptr_t)c->exchange_receive,received) :
                IOConnectCallStructMethod(c->device,kIOVM2DDirectGLExchange,c->exchange_send,
                    sizeof(header)+header.send_bytes,c->exchange_receive,&received);
            failed=error || received!=receiveBytes;
            if(!failed){
                memcpy(&reply,c->exchange_receive,sizeof(reply));
                if(observe_errors && (reply.error&DG_REPLY_ERROR_EMPTY)){
                    context->error_observed_empty=1;reply.error&=~DG_REPLY_ERROR_EMPTY;
                }
                failed=reply.bytes!=receiveBytes-sizeof(reply);
                if(!failed && !(reply.error && (p->message.flags&DG_OUTPUT_ONLY)) && reply.bytes)
                    memcpy(p->data,c->exchange_receive+sizeof(reply),reply.bytes);
                if(!failed && !reply.error)p->valid_reply=1;
            }
        }
    } else if(!failed && !sync && bytes<=sizeof(c->batch)) {
        int packed=DGCompactAppend(c,&p->message);
        if(packed<0)failed=1;
        else if(!packed){
            c->scalar_start=0;c->scalar_compact=0;
            if(c->used+bytes>sizeof(c->batch)) failed=DGDrain(c);
        }
        if(!failed && !packed) {
            memcpy(c->batch+c->used,&p->message,sizeof(p->message));c->used+=sizeof(p->message);
            if(p->message.bytes) memcpy(c->batch+c->used,p->data,p->message.bytes);
            c->used+=p->message.bytes;
        }
    } else if(!failed) {
        failed=DGDrain(c) || DGConnectionTransfer(c,&p->message,sizeof(p->message),1) ||
            (!(p->message.flags&DG_OUTPUT_ONLY) && DGConnectionTransfer(c,p->data,p->message.bytes,1));
    }
    if(!failed && sync && !exchange) {
        failed=DGConnectionTransfer(c,&reply,sizeof(reply),0) || reply.bytes!=((p->message.flags&DG_INPUT_ONLY)?0:p->message.bytes);
        if(!failed && observe_errors && (reply.error&DG_REPLY_ERROR_EMPTY)){
            context->error_observed_empty=1;reply.error&=~DG_REPLY_ERROR_EMPTY;
        }
        if(!failed && reply.error && (p->message.flags&DG_OUTPUT_ONLY)) {
            // Consume the failed response, but do not overwrite borrowed
            // caller memory with an executor's unused/zero output span.
            if(reply.bytes)failed=DGDiscardReply(c,reply.bytes);
        } else if(!failed)failed=DGConnectionTransfer(c,p->data,reply.bytes,0);
        if(!failed && !reply.error) p->valid_reply=1;
    }
    if(failed) { c->dead=1;context->error=GL_INVALID_OPERATION;context->error_observed_empty=0; }
    else if(reply.error) context->error=reply.error;
    if(!failed && p->valid_reply && location_operation==DG_LOCATION_CREATE)
        DGLocationBorn(&c->program_locations,(uint32_t)reply.result);
    pthread_mutex_unlock(&c->lock);
    return reply;
}
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#undef DGSubmit
static struct DGReply DGSubmit(struct DGContext *context,struct DGPacket *p,int sync)
{
    struct DGClientProfileStamp s=DGClientProfileBegin(!!sync,p->message.op,p->message.bytes);
    struct DGReply result=DGSubmitProfileBody(context,p,sync);DGClientProfileEnd(s);return result;
}
#endif
static void DGPacketCopy(struct DGPacket *p,int index,void *pointer,size_t bytes)
{
    uint64_t offset=p->message.args[index];
    if(p->valid_reply && bytes && pointer && offset && offset-1<=p->message.bytes && bytes<=p->message.bytes-(offset-1))
        memcpy(pointer,p->data+offset-1,bytes);
}
static void DGPacketFree(struct DGPacket *p) { if(!p->borrowed)free(p->data); }
#include "passthrough-opengl-sync-client.c"
#include "passthrough-opengl-buffers-client.c"
#include "passthrough-opengl-mapping-client.c"
#include "passthrough-opengl-arrays-client.c"
#include "passthrough-opengl-pbo-client.c"
static void DGUnsupported(struct DGContext *c,unsigned slot)
{
    if(!c) return;c->error=GL_INVALID_OPERATION;
    if(!(c->unsupported[slot/64] & (UINT64_C(1)<<(slot%64)))) {
        c->unsupported[slot/64]|=UINT64_C(1)<<(slot%64);
        VIMGLStderr("VIM PassthroughOpenGL: unsupported codec at GLI slot %u\n",slot);
        syslog(LOG_NOTICE,"VIM PassthroughOpenGL: unsupported codec at GLI slot %u",slot);
    }
}
static size_t DGStringBytes(const char *s)
{
    if(!s)return 0;
    // strnlen is absent from the actual 10J3250 libSystem, despite the later
    // SDK exporting it. Preserve the same 1-MiB bound with native memchr;
    // never replace this validation with an unbounded string scan.
    size_t length=DGBoundedStringLength(s,1024*1024);
    return length==1024*1024?SIZE_MAX:length+1;
}
static size_t DGPixelBytes(struct DGContext *c,int w,int h,GLenum format,GLenum type,int pack)
{
    if(!c || c->pixel_buffer[pack]) return SIZE_MAX; /* No host pointer from a guest PBO offset. */
    return DGPixelSpan(w,h,format,type,c->row_length[pack],c->alignment[pack],c->skip_rows[pack],c->skip_pixels[pack],c->row_bytes[pack]);
}
static size_t DGPixelBytes3D(struct DGContext *c,int w,int h,int d,GLenum format,GLenum type,int pack)
{
    if(!c || c->pixel_buffer[pack])return SIZE_MAX;
    return DGPixelSpan3D(w,h,d,format,type,c->row_length[pack],c->alignment[pack],
        c->skip_rows[pack],c->skip_pixels[pack],c->row_bytes[pack],c->image_height[pack],c->skip_images[pack]);
}
static void DGRecordPixelStore(struct DGContext *c,GLenum key,GLint value)
{
    if(!c || c->common_state_blocked) return;
    switch(key) {
    case GL_UNPACK_ROW_BYTES_APPLE:case GL_PACK_ROW_BYTES_APPLE:
        if(value>=0) c->row_bytes[key==GL_PACK_ROW_BYTES_APPLE]=value;break;
    case GL_UNPACK_ALIGNMENT:case GL_PACK_ALIGNMENT:
        if(value==1 || value==2 || value==4 || value==8) c->alignment[key==GL_PACK_ALIGNMENT]=value;break;
    case GL_UNPACK_ROW_LENGTH:case GL_PACK_ROW_LENGTH:
        if(value>=0) c->row_length[key==GL_PACK_ROW_LENGTH]=value;break;
    case GL_UNPACK_SKIP_ROWS:case GL_PACK_SKIP_ROWS:
        if(value>=0) c->skip_rows[key==GL_PACK_SKIP_ROWS]=value;break;
    case GL_UNPACK_SKIP_PIXELS:case GL_PACK_SKIP_PIXELS:
        if(value>=0) c->skip_pixels[key==GL_PACK_SKIP_PIXELS]=value;break;
    case GL_UNPACK_IMAGE_HEIGHT:case GL_PACK_IMAGE_HEIGHT:
        if(value>=0)c->image_height[key==GL_PACK_IMAGE_HEIGHT]=value;break;
    case GL_UNPACK_SKIP_IMAGES:case GL_PACK_SKIP_IMAGES:
        if(value>=0)c->skip_images[key==GL_PACK_SKIP_IMAGES]=value;break;
    }
}
static void DGRecordPixelStoreFloat(struct DGContext *c,GLenum key,GLfloat value)
{
    if(!c || c->common_state_blocked || !(value>=0))return;
    /* Native Mavericks glPixelStoref_Exec adds 0.5f before CVTTSS2SI for
     * nonnegative integer fields; it does NOT truncate. Alignment requires
     * an exact 1/2/4/8 even when a nearby fraction would round to one.
     * APPLE row-byte enums are accepted by PixelStorei, not PixelStoref. */
    if(key==GL_UNPACK_ROW_BYTES_APPLE || key==GL_PACK_ROW_BYTES_APPLE)return;
    if((key==GL_UNPACK_ALIGNMENT || key==GL_PACK_ALIGNMENT) &&
       value!=1 && value!=2 && value!=4 && value!=8)return;
    GLfloat rounded=value+.5f;
    if((double)rounded>INT_MAX)return; // Never perform an undefined C float-to-int conversion.
    DGRecordPixelStore(c,key,(GLint)rounded);
}
static int DGTextureTarget(GLenum target)
{
    return target==GL_TEXTURE_1D || target==GL_TEXTURE_2D || target==GL_TEXTURE_3D ||
        target==GL_TEXTURE_RECTANGLE_ARB || target==GL_TEXTURE_CUBE_MAP;
}
static void DGTextureRange(struct DGContext *c,GLenum target,GLsizei length,const void *pointer)
{
    (void)pointer;
    if(!c) return;
    if(!DGTextureTarget(target)) c->error=GL_INVALID_ENUM;
    else if(length<0) c->error=GL_INVALID_VALUE;
    /* Indirect rendering: ignore the optional mapping hint, as specified. */
}
static void DGTextureRangePointer(struct DGContext *c,GLenum target,GLenum pname,void **params)
{
    if(!c) return;
    if(!DGTextureTarget(target) || pname!=GL_TEXTURE_RANGE_POINTER_APPLE) c->error=GL_INVALID_ENUM;
    else if(params) *params=NULL;
}
static void DGRecordBuffer(struct DGContext *c,GLenum target,GLuint buffer)
{
    if(c && (target==GL_PIXEL_PACK_BUFFER || target==GL_PIXEL_UNPACK_BUFFER))
        c->pixel_buffer[target==GL_PIXEL_PACK_BUFFER]=buffer;
    if(c && target==GL_ARRAY_BUFFER) c->array_buffer=buffer;
    if(c && target==GL_ELEMENT_ARRAY_BUFFER) {
        if(!c->vertex) c->vertex=DGVertexState(c,0);
        if(c->vertex) c->vertex->element_buffer=buffer;
    }
}
static void DGShaderSource(struct DGContext *c,GLuint shader,GLsizei count,const GLchar *const *strings,const GLint *lengths)
{
    if(!c) return;
    if(count<0 || count>65536 || (count && !strings)) { c->error=GL_INVALID_VALUE;return; }
    size_t total=0;
    for(int i=0;i<count;++i) {
        if(!strings[i]) { c->error=GL_INVALID_VALUE;return; }
        size_t n=lengths && lengths[i]>=0 ? (size_t)lengths[i] : DGStringBytes(strings[i]);
        if(!(lengths && lengths[i]>=0) && n!=SIZE_MAX) --n;
        if(n>DG_MAX_BYTES || total>DG_MAX_BYTES-n) { c->error=GL_INVALID_VALUE;return; }total+=n;
    }
    char *joined=malloc(total+1);if(!joined) { c->error=GL_OUT_OF_MEMORY;return; }
    size_t offset=0;
    for(int i=0;i<count;++i) {
        size_t n=lengths && lengths[i]>=0 ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(joined+offset,strings[i],n);offset+=n;
    }
    joined[total]=0;
    struct DGPacket packet=DGPacketNew(c,0);
    extern unsigned DGShaderSourceSlot(void);packet.message.op=DGShaderSourceSlot();
    packet.message.args[0]=shader;packet.message.args[1]=total;
    DGPacketPointer(&packet,2,joined,total+1,0);DGSubmit(c,&packet,0);DGPacketFree(&packet);free(joined);
}
#include "direct-declarations.inc"
#include "passthrough-opengl-common-state-client.c"
#include "direct-client-generated.inc"

/* The public API has no buffer length. Size our private reply from the real
 * object, then let the executor recheck it before writing. A concurrent
 * relink/redefinition cannot overflow the bounded reply or caller's query. */
void glGetProgramStringARB(GLenum target,GLenum pname,void *string)
{
    struct DGContext *c=dg_current;if(!c)return;
    if(!(c->connection->capabilities&DG_CAP_ARB_PROGRAMS)){c->error=GL_INVALID_OPERATION;return;}
    if(pname!=GL_PROGRAM_STRING_ARB){c->error=GL_INVALID_ENUM;return;}
    GLint length=0;glGetProgramivARB(target,GL_PROGRAM_LENGTH_ARB,&length);
    if(length<0 || (uint32_t)length>DG_MAX_BYTES){c->error=GL_OUT_OF_MEMORY;return;}
    if(length && !string){c->error=GL_INVALID_VALUE;return;}
    struct DGPacket p=DGPacketNew(c,DG_PROGRAM_STRING);
    p.message.flags=DG_OUTPUT_ONLY;p.message.args[0]=target;p.message.args[1]=pname;
    if(length)DGPacketPointer(&p,2,string,(size_t)length,1);
    struct DGReply reply=DGSubmit(c,&p,1);
    if(p.valid_reply){
        if(reply.result>(uint32_t)length)c->error=GL_INVALID_OPERATION;
        else if(reply.result)memcpy(string,p.data,(size_t)reply.result);
    }
    DGPacketFree(&p);
}

/* The SDK's GLI shader slots use ARB handles even for public core GL calls.
 * These are numeric wire shader/program names, never native host pointers. */
GLuint glCreateShader(GLenum type) { return (GLuint)(uintptr_t)glCreateShaderObjectARB(type); }
GLuint glCreateProgram(void) { return (GLuint)(uintptr_t)glCreateProgramObjectARB(); }
void glCompileShader(GLuint s) { glCompileShaderARB((GLhandleARB)(uintptr_t)s); }
void glAttachShader(GLuint p,GLuint s) { glAttachObjectARB((GLhandleARB)(uintptr_t)p,(GLhandleARB)(uintptr_t)s); }
void glLinkProgram(GLuint p) { glLinkProgramARB((GLhandleARB)(uintptr_t)p); }
void glUseProgram(GLuint p) { glUseProgramObjectARB((GLhandleARB)(uintptr_t)p); }
void glDeleteShader(GLuint s) { glDeleteObjectARB((GLhandleARB)(uintptr_t)s); }
void glDeleteProgram(GLuint p) { glDeleteObjectARB((GLhandleARB)(uintptr_t)p); }
void glShaderSource(GLuint s,GLsizei n,const GLchar *const *strings,const GLint *lengths)
{ DGShaderSource(dg_current,s,n,strings,lengths); }
static void DGReadUniform(GLuint program,GLint location,void *params,int integer)
{
    struct DGContext *c=dg_current;if(!c)return;
    if(!(c->connection->capabilities&DG_CAP_UNIFORM_QUERY)){c->error=GL_INVALID_OPERATION;return;}
    if(!params){c->error=GL_INVALID_VALUE;return;}
    union {GLfloat floating[16];GLint integer[16];} value={0};
    struct DGPacket packet=DGPacketNew(c,DG_UNIFORM_QUERY);packet.message.flags=DG_OUTPUT_ONLY;
    packet.message.args[0]=program;packet.message.args[1]=(uint64_t)(int64_t)location;packet.message.args[2]=integer;
    DGPacketPointer(&packet,3,&value,sizeof(value),1);struct DGReply reply=DGSubmit(c,&packet,1);
    if(packet.valid_reply&&!reply.error){
        if(!reply.result||reply.result>16){if(!c->error)c->error=GL_INVALID_OPERATION;}
        else {size_t bytes=(size_t)reply.result*sizeof(GLfloat);DGPacketCopy(&packet,3,&value,bytes);memcpy(params,&value,bytes);}
    }
    DGPacketFree(&packet);
}
void glGetUniformfvARB(GLhandleARB programObj,GLint location,GLfloat *params)
{DGReadUniform((GLuint)(uintptr_t)programObj,location,params,0);}
void glGetUniformivARB(GLhandleARB programObj,GLint location,GLint *params)
{DGReadUniform((GLuint)(uintptr_t)programObj,location,params,1);}
void glGetUniformfv(GLuint program,GLint location,GLfloat *params)
{DGReadUniform(program,location,params,0);}
void glGetUniformiv(GLuint program,GLint location,GLint *params)
{DGReadUniform(program,location,params,1);}

GLenum glGetError(void)
{
    struct DGContext *c=dg_current;if(!c) return GL_NO_ERROR;
    if(c->error) { GLenum error=c->error;c->error=0;return error; }
    /* Valid only until the next packet/queued operation. The host observed
     * its native error flags after the preceding synchronous command. */
    if(c->error_observed_empty && !c->connection->dead)return GL_NO_ERROR;
    if(dg_error_state_prefetch && dg_common_state_reuse && dg_common_neutral_reuse &&
       (c->profile==0x1000 || (c->connection->features&DG_FEATURE_CORE_COMMON_STATE)) && !c->common_state_blocked &&
       c->common_state_valid!=(c->profile==0x1000?DG_COMMON_ALL:DG_COMMON_CORE) &&
       (c->connection->capabilities&DG_CAP_ERROR_STATE)) {
        struct DGPacket packet=DGPacketNew(c,DG_ERROR_STATE);packet.message.flags=DG_OUTPUT_ONLY;
        DGPacketPointer(&packet,0,&c->common_state,sizeof(c->common_state),1);
        struct DGReply reply=DGSubmit(c,&packet,1);
        if(packet.valid_reply){
            DGPacketCopy(&packet,0,&c->common_state,sizeof(c->common_state));
            if(c->common_state.version==1){c->common_state_valid=c->profile==0x1000?DG_COMMON_ALL:DG_COMMON_CORE;
                __sync_fetch_and_add(&dg_common_state_prefetches,1);}
        }
        DGPacketFree(&packet);return (GLenum)reply.result;
    }
    struct DGPacket packet=DGPacketNew(c,DG_ERROR);
    struct DGReply reply=DGSubmit(c,&packet,1);return (GLenum)reply.result;
}
static int DGClientNativeRendererCommand(int select,unsigned software,struct DGNativeRendererInfo *info)
{
    struct DGContext *c=dg_current;
    if(!c||!(c->connection->capabilities&DG_CAP_NATIVE_RENDERERS))return -1;
    if(!info||software>1)return 0;
    struct DGPacket packet=DGPacketNew(c,select?DG_SELECT_NATIVE_RENDERER:DG_NATIVE_RENDERERS);
    if(select)packet.message.args[0]=software;
    struct DGReply reply=DGSubmit(c,&packet,1);
    int valid=packet.valid_reply&&!reply.error;DGPacketFree(&packet);
    if(!valid||!reply.result)return 0;
    struct DGNativeRendererInfo observed={(uint32_t)(reply.result>>32),(uint32_t)reply.result};
    unsigned kind=!!(observed.flags&DG_NATIVE_CURRENT_SOFTWARE);
    if(!observed.renderer||(observed.flags&~7u)||
       !(observed.flags&(kind?DG_NATIVE_SOFTWARE:DG_NATIVE_HARDWARE))||(select&&kind!=software)){
        if(!c->error)c->error=GL_INVALID_OPERATION;return 0;
    }
    if(c->native_renderer_kind!=kind)c->common_state_valid=0;
    c->native_renderer_kind=kind;*info=observed;return 1;
}
/* Unsupported peers return -1; native unavailable/rejected transitions return
 * 0. Never replay a failed transition or invent a software renderer flag. */
int DGQueryNativeRenderer(struct DGNativeRendererInfo *info)
{return DGClientNativeRendererCommand(0,0,info);}
int DGSelectNativeRenderer(unsigned software,struct DGNativeRendererInfo *info)
{return DGClientNativeRendererCommand(1,software,info);}
const GLubyte *glGetString(GLenum key)
{
    int index=key==GL_VENDOR ? 0 : key==GL_RENDERER ? 1 : key==GL_VERSION ? 2 : key==GL_EXTENSIONS ? 3 : key==GL_SHADING_LANGUAGE_VERSION ? 4 : key==GL_PROGRAM_ERROR_STRING_ARB ? 5 : -1;
    if(!dg_current)return NULL;
    if(index<0) { dg_current->error=GL_INVALID_ENUM;return NULL; }
    /* Implemented compositor capability set, not the host's entire extension
     * list. Each rendering path below has real GPU pixel/completion probes;
     * missing individual codecs still fail and are logged, never no-op. */
    if(key==GL_EXTENSIONS && dg_current->profile!=0x1000) { dg_current->error=GL_INVALID_ENUM;return NULL; }
    if(key==GL_EXTENSIONS && !(dg_current->connection->capabilities&DG_CAP_ARB_PROGRAMS))
        return (const GLubyte *)DG_COMPATIBILITY_EXTENSIONS;
    if(index==5 && (dg_current->profile!=0x1000 || !(dg_current->connection->capabilities&DG_CAP_ARB_PROGRAMS))){
        dg_current->error=GL_INVALID_ENUM;return NULL;
    }
    // Compiler diagnostics are mutable after ProgramStringARB, unlike the
    // context's vendor/version/qualified extension strings. Reuse the storage,
    // never cache an obsolete success/error message across compilations.
    char **strings=dg_current->strings[dg_current->native_renderer_kind];
    if(!strings[index] || index==5) {
        char *value=strings[index];int allocated=!value;
        if(!value)value=calloc(4096,1);
        if(!value) { dg_current->error=GL_OUT_OF_MEMORY;return NULL; }
        struct DGPacket packet=DGPacketNew(dg_current,DG_STRING);packet.message.args[0]=key;
        if(key==GL_EXTENSIONS)packet.message.args[3]=DG_COMPAT_DISPATCH_REVISION;
        if(key==GL_EXTENSIONS && (dg_current->connection->capabilities&DG_CAP_APPLE_VIDEO_TEXTURES))
            packet.message.args[2]=DG_CAP_APPLE_VIDEO_TEXTURES;
        DGPacketPointer(&packet,1,value,4096,1);
        DGSubmit(dg_current,&packet,1);DGPacketCopy(&packet,1,value,4096);
        int good=packet.valid_reply;DGPacketFree(&packet);
        if(!good) { if(allocated)free(value);return NULL; }
        /* This is a virtual Intel GPU, not a physical host GPU exposed to
         * the guest. Period Core Image's renderer-family tables cannot know
         * Apple Silicon: Tiger classifies "Apple M4" as speed zero, selects
         * its CPU filter JIT and crashes instead of using supported ARB GL.
         * Give the accelerated virtual renderer a period Intel identity.
         * Preserve the actual backing model in the label for diagnostics;
         * extensions, limits, formats and execution still come from the host.
         * Never apply this identity to a selected native software renderer.
         * Strings are initialized once per context, not on the draw path. */
        if(!dg_current->native_renderer_kind && key==GL_VENDOR)
            snprintf(value,4096,"Intel Inc.");
        if(!dg_current->native_renderer_kind && key==GL_RENDERER) {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ <= 1040
            const char *model="Intel GMA 950";
#elif __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050
            const char *model="Intel GMA X3100";
#elif __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ <= 1070
            const char *model="Intel HD Graphics 3000";
#elif __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ <= 1090
            const char *model="Intel HD Graphics 4000";
#elif __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ <= 101100
            const char *model="Intel Iris Pro";
#else
            const char *model="Intel HD Graphics 630";
#endif
            char backing[4096];memcpy(backing,value,sizeof(backing));backing[4095]=0;
            snprintf(value,4096,"%s (PassthroughOpenGL; %.3800s)",model,backing);
        }
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050
        /* Leopard Core Animation gates even its ordinary desktop layers on
         * the ARB spelling of texture combine, promoted to core in GL 1.3.
         * These are the real forwarded fixed-function operations (qualified
         * by the dual-width six-mode pixel probe), not a capability override.
         * Cache the alias only when the actual native context is >=1.3;
         * an older peer/context must not gain an unsupported extension. */
        if(key==GL_EXTENSIONS && !strstr(value,"GL_ARB_texture_env_combine")) {
            const char *version=(const char *)glGetString(GL_VERSION);
            unsigned major=0,minor=0;
            if(version && sscanf(version,"%u.%u",&major,&minor)==2 &&
               (major>1 || (major==1 && minor>=3))) {
                static const char alias[]=" GL_ARB_texture_env_combine";
                size_t length=strlen(value);
                if(length+sizeof(alias)<=4096)memcpy(value+length,alias,sizeof(alias));
            }
        }
#endif
        strings[index]=value;
    }
    return (const GLubyte *)strings[index];
}

const GLubyte *glGetStringi(GLenum name,GLuint index)
{
    (void)index;
    if(!dg_current)return NULL;
    /* No optional core extensions are qualified yet. Keep NUM_EXTENSIONS,
     * indexed bounds and the removed compatibility query mutually consistent;
     * never forward a host pointer or imply all host extensions are ported. */
    dg_current->error=dg_current->profile==0x1000 ? GL_INVALID_OPERATION :
        name==GL_EXTENSIONS ? GL_INVALID_VALUE : GL_INVALID_ENUM;
    return NULL;
}
/* Process-local context/drawable backend for the measured CGL/GLI adapter. */
void *DGCreateContext(const int *attrs,void *shared)
{
    struct DGContext *share=shared,*c=calloc(1,sizeof(*c));if(!c) return NULL;
    c->alignment[0]=c->alignment[1]=4;c->profile=0x1000;
    if(share) { c->connection=share->connection;__sync_add_and_fetch(&c->connection->refs,1); }
    else {
        c->connection=calloc(1,sizeof(*c->connection));if(!c->connection) { free(c);return NULL; }
        struct DGConnection *connection=c->connection;connection->refs=1;
        pthread_mutex_init(&connection->lock,NULL);
        pthread_mutex_init(&connection->map_lock,NULL);
        pthread_mutex_init(&connection->backing_lock,NULL);
        if(DGConnect(connection)) {
            pthread_mutex_destroy(&connection->backing_lock);pthread_mutex_destroy(&connection->map_lock);pthread_mutex_destroy(&connection->lock);free(connection);free(c);return NULL;
        }
        struct DGPacket hello=DGPacketNew(c,DG_HELLO);
        hello.message.args[0]=DG_MAGIC;hello.message.args[1]=DG_VERSION;
        hello.message.args[5]=DG_HELLO_FEATURES_MAGIC;
        hello.message.args[6]=DG_FEATURE_LARGE_IOSURFACE_WRITES;
        /* Stable across guest processes, different after a guest reboot.
         * The renderer is one VM per endpoint. This is resource
         * identity, not authentication; never multiplex untrusted VMs on it. */
        uuid_t namespace;
        if(VIMGetResourceNamespace(namespace)) {
            memcpy(&hello.message.args[3],namespace,sizeof(namespace));
        }
        DGPacketPointer(&hello,2,DG_LAYOUT,sizeof(DG_LAYOUT),0);
        struct DGReply hello_reply=DGSubmit(c,&hello,1);
        connection->capabilities=hello_reply.result;
        struct DGHelloFeatures features={0};DGPacketCopy(&hello,2,&features,sizeof(features));
        if(hello.valid_reply&&features.magic==DG_HELLO_FEATURES_MAGIC)connection->features=features.features;
        DGPacketFree(&hello);
        if(c->error) { DGConnectionRelease(connection);free(c);return NULL; }
    }
    struct DGPacket packet=DGPacketNew(c,DG_CREATE);
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ <= 1060
    pthread_once(&dg_rosetta_once,DGQueryRosettaArrays);
    c->big_endian_arrays=dg_rosetta_arrays;
    if(c->big_endian_arrays && !(c->connection->capabilities&DG_CAP_BIG_ENDIAN_ARRAYS)) {
        DGConnectionRelease(c->connection);free(c);return NULL;
    }
#endif
    packet.message.args[0]=share ? share->id : 0;
    packet.message.args[4]=getenv("VIM_GL_DAMAGE_PRESENT")!=NULL;
    for(unsigned i=0;attrs && attrs[i] && i<64;i+=2) {
        if(attrs[i]==0x30) packet.message.args[1]=attrs[i+1];
        if(attrs[i]==0x31) packet.message.args[2]=attrs[i+1];
        if(attrs[i]==0x56494d01) packet.message.args[3]=attrs[i+1];
        if(attrs[i]==0x56494d02)c->profile=attrs[i+1];
    }
    if(c->profile!=0x1000 && (c->profile!=0x3200 || !(c->connection->capabilities&DG_CAP_CORE_PROFILE))) {
        DGConnectionRelease(c->connection);free(c);return NULL;
    }
    if(c->profile!=0x1000)packet.message.args[5]=c->profile;
    struct DGReply reply=DGSubmit(c,&packet,1);c->id=(uint32_t)reply.result;
    if(!c->id) {
        DGConnectionRelease(c->connection);
        free(c);return NULL;
    }
    return c;
}
static GLboolean DGMakeCurrentMode(void *opaque,void *pixels,GLenum type,GLsizei width,GLsizei height,int prepared)
{
    struct DGContext *c=opaque;
    if(!c) { dg_current=NULL;return GL_TRUE; }
    if(type!=GL_UNSIGNED_BYTE || width<1 || height<1 || width>8192 || height>8192) return GL_FALSE;
    struct DGClientDrawable *d=c->drawables;
    for(;d;d=d->next) if(d->pixels==pixels && d->width==width && d->height==height) break;
    int created=0;
    if(!d) {
        d=calloc(1,sizeof(*d));if(!d) { c->error=GL_OUT_OF_MEMORY;return GL_FALSE; }
        if(!++c->next_drawable) { free(d);c->error=GL_OUT_OF_MEMORY;return GL_FALSE; }
        d->pixels=pixels;d->width=width;d->height=height;d->id=c->next_drawable;
        d->next=c->drawables;c->drawables=d;created=1;
    }
    if(c->current_drawable!=d->id) {
        unsigned op=prepared && (c->connection->capabilities&DG_CAP_PREPARED_DRAWABLE)?DG_PREPARE_DRAWABLE:DG_RESIZE;
        struct DGPacket packet=DGPacketNew(c,op);packet.message.args[0]=width;packet.message.args[1]=height;
        packet.message.args[2]=d->id;
        struct DGReply reply=DGSubmit(c,&packet,1);
        if(reply.error || c->connection->dead) {
            if(created) { c->drawables=d->next;free(d); }return GL_FALSE;
        }
        c->width=width;c->height=height;c->current_drawable=d->id;
    }
    c->pixels=pixels;dg_current=c;return GL_TRUE;
}
GLboolean DGMakeCurrent(void *opaque,void *pixels,GLenum type,GLsizei width,GLsizei height)
{return DGMakeCurrentMode(opaque,pixels,type,width,height,0);}
/* Private GLI attach preparation only. Before returning success to CGL the
 * caller MUST complete its actual backing attach/import. Generic operations
 * resolve a prepared target to real private storage before using it. Old
 * hosts and every ordinary/offscreen MakeCurrent keep eager allocation. */
GLboolean DGMakeCurrentPrepared(void *opaque,void *key,GLsizei width,GLsizei height)
{return DGMakeCurrentMode(opaque,key,GL_UNSIGNED_BYTE,width,height,1);}
void DGReleaseBuffer(void *opaque,void *pixels)
{
    struct DGContext *c=opaque;if(!c || !pixels) return;
    if(c->backing_loan.serial && c->pixels==pixels && DGEndBackingRender(c,NULL)!=1)return;
    struct DGClientDrawable **link=&c->drawables;
    while(*link) {
        struct DGClientDrawable *d=*link;
        if(d->pixels!=pixels) { link=&d->next;continue; }
        struct DGPacket packet=DGPacketNew(c,DG_DROP_DRAWABLE);packet.message.args[0]=d->id;
        DGSubmit(c,&packet,1);
        if(c->current_drawable==d->id) { c->current_drawable=0;c->pixels=NULL; }
        *link=d->next;free(d);
    }
}
void DGDestroyContext(void *opaque)
{
    struct DGContext *c=opaque;if(!c) return;
    if(c->backing_loan.serial)DGEndBackingRender(c,NULL);
    struct DGPacket packet=DGPacketNew(c,DG_DESTROY);DGSubmit(c,&packet,1);
    if(dg_current==c) dg_current=NULL;
    struct DGConnection *connection=c->connection;
    DGConnectionRelease(connection);
    DGDestroyArrays(c);
    free(c->draw_scratch);
    while(c->drawables) { struct DGClientDrawable *d=c->drawables;c->drawables=d->next;free(d); }
    while(c->surfaces) { struct DGClientSurface *s=c->surfaces;c->surfaces=s->next;free(s); }
    for(unsigned renderer=0;renderer<2;++renderer)for(unsigned i=0;i<6;++i)free(c->strings[renderer][i]);
    free(c);
}
int DGQueryRendererDetails(GLint *video,GLint *texture,struct DGNativeRendererInfo *native)
{
    if(!video||!texture)return 0;
    /* Enumeration can precede any guest GL context. A temporary real native
     * context selects the same renderer family; no client TLS is changed. */
    struct DGContext *c=DGCreateContext(NULL,NULL);if(!c) return 0;
    struct DGPacket packet=DGPacketNew(c,DG_RENDERER_MEMORY);
    struct DGReply reply=DGSubmit(c,&packet,1);int good=packet.valid_reply && !reply.error;
    if(good) { *video=(GLint)(uint32_t)reply.result;*texture=(GLint)(uint32_t)(reply.result>>32); }
    DGPacketFree(&packet);
    if(native){
        *native=(struct DGNativeRendererInfo){0};
        if(good&&(c->connection->capabilities&DG_CAP_NATIVE_RENDERERS)){
            packet=DGPacketNew(c,DG_NATIVE_RENDERERS);reply=DGSubmit(c,&packet,1);
            if(packet.valid_reply&&!reply.error&&reply.result){
                struct DGNativeRendererInfo observed={(uint32_t)(reply.result>>32),(uint32_t)reply.result};
                if(observed.renderer&&!(observed.flags&~7u))*native=observed;
            }
            DGPacketFree(&packet);
        }
    }
    DGDestroyContext(c);return good;
}
int DGQueryRendererMemory(GLint *video,GLint *texture)
{return DGQueryRendererDetails(video,texture,NULL);}
int DGDrawableDamage(int32_t bounds[4],int reset)
{
    struct DGContext *c=dg_current;if(!c) return 0;
    struct DGPacket packet=DGPacketNew(c,DG_DRAWABLE_DAMAGE);
    struct DGDamage damage={0};
    DGPacketPointer(&packet,0,&damage,sizeof(damage),1);packet.message.args[1]=!!reset;
    struct DGReply reply=DGSubmit(c,&packet,1);
    int good=packet.valid_reply && !reply.error;
    if(good) { DGPacketCopy(&packet,0,&damage,sizeof(damage));memcpy(bounds,&damage,sizeof(damage)); }
    DGPacketFree(&packet);return good;
}
int DGFlushDrawableState(int finish,uint32_t *draw_framebuffer)
{
    struct DGContext *c=dg_current;
    if(!c || !(c->connection->capabilities&DG_CAP_FLUSH_DRAWABLE_STATE))return -1;
    if(!draw_framebuffer || (finish!=0 && finish!=1))return 0;
    struct DGPacket packet=DGPacketNew(c,DG_FLUSH_DRAWABLE_STATE);packet.message.args[0]=finish;
    struct DGReply reply=DGSubmit(c,&packet,1);
    int good=packet.valid_reply && !reply.error && reply.result<=UINT32_MAX;
    if(good)*draw_framebuffer=(uint32_t)reply.result;
    else if(!c->error)c->error=GL_INVALID_OPERATION;
    DGPacketFree(&packet);return good;
}
int DGFlushIOSurfaceState(int finish,uint32_t *draw_framebuffer,struct DGIosurfaceWrite *writes,
    unsigned capacity,unsigned *count)
{
    struct DGContext *c=dg_current;
    if(!c || !(c->connection->capabilities&DG_CAP_IOSURFACE_WRITES))return -1;
    if(!draw_framebuffer||!writes||!count||!capacity||capacity>DG_IOSURFACE_WRITE_LIMIT||(finish!=0&&finish!=1)){
        if(!c->error)c->error=GL_INVALID_VALUE;return 0;
    }
    if(capacity>DG_IOSURFACE_INLINE_WRITES&&!(c->connection->features&DG_FEATURE_LARGE_IOSURFACE_WRITES))return -1;
    struct DGPacket packet=DGPacketNew(c,DG_FLUSH_IOSURFACE_STATE);
    packet.message.flags=DG_OUTPUT_ONLY;packet.message.args[0]=finish;packet.message.args[2]=capacity;
    DGPacketPointer(&packet,1,writes,capacity*sizeof(*writes),1);
    struct DGReply reply=DGSubmit(c,&packet,1);unsigned n=(unsigned)(reply.result>>32);
    int good=packet.valid_reply&&!reply.error&&n<=capacity;
    if(good){*draw_framebuffer=(uint32_t)reply.result;*count=n;DGPacketCopy(&packet,1,writes,n*sizeof(*writes));
        for(unsigned i=0;i<n;++i)if(!writes[i].texture||writes[i].reserved||!writes[i].serial)good=0;
    }
    if(!good&&!c->error)c->error=GL_INVALID_OPERATION;
    DGPacketFree(&packet);return good;
}
int DGReadIOSurfaceTextureElements(GLuint texture,uint64_t serial,unsigned width,unsigned height,void *pixels,size_t stride,unsigned element_bytes)
{
    struct DGContext *c=dg_current;
    if(!c||!(c->connection->capabilities&DG_CAP_IOSURFACE_WRITES)||!texture||!serial||!pixels||
       (element_bytes!=4 && !(element_bytes==1&&(c->connection->capabilities&DG_CAP_IOSURFACE_LUMINANCE8)) &&
                           !(element_bytes==2&&(c->connection->capabilities&DG_CAP_IOSURFACE_LUMINANCE_ALPHA8)) &&
                           !((element_bytes==8||element_bytes==16)&&(c->connection->capabilities&DG_CAP_IOSURFACE_FLOAT)))||
       !width||!height||width>8192||height>8192||stride<(size_t)width*element_bytes||stride>DG_MAX_BYTES||
       (uint64_t)(height-1)*stride+(uint64_t)width*element_bytes>DG_IOSURFACE_MAX_BYTES)return 0;
    unsigned chunked=(uint64_t)(height-1)*stride+(uint64_t)width*element_bytes>DG_MAX_BYTES;
    if(chunked && !(c->connection->capabilities&DG_CAP_IOSURFACE_ROWS))return 0;
    for(unsigned y=0;y<height;){
        unsigned rows=height-y;if(rows>DG_MAX_BYTES/stride)rows=(unsigned)(DG_MAX_BYTES/stride);
        size_t bytes=(rows-1)*stride+(size_t)width*element_bytes;
        unsigned char *out=(unsigned char *)pixels+y*stride;
        struct DGPacket p=DGPacketNew(c,DG_READ_IOSURFACE_TEXTURE);
        p.message.args[0]=texture;p.message.args[1]=serial;p.message.args[2]=width;p.message.args[3]=rows;p.message.args[4]=stride;
        p.message.args[6]=element_bytes==4?0:element_bytes;
        if(chunked){p.message.args[7]=height;p.message.args[8]=y;}
        int output_only=(c->connection->features&DG_FEATURE_IOSURFACE_OUTPUT_READ)!=0;
        if(output_only){
            p.message.flags=DG_OUTPUT_ONLY;p.message.args[5]=1;p.message.bytes=(uint32_t)bytes;
            if(!DGPacketOwn(&p,bytes)){DGPacketFree(&p);return 0;}
        }else DGPacketPointer(&p,5,out,bytes,0);
        struct DGReply reply=DGSubmit(c,&p,1);
        int good=p.valid_reply&&!reply.error&&reply.result==1;
        if(good){
            if(output_only&&stride!=(size_t)width*element_bytes){
                // Padding was never uploaded and must never be overwritten.
                for(unsigned row=0;row<rows;row++)memcpy(out+row*stride,p.data+row*stride,(size_t)width*element_bytes);
            }else DGPacketCopy(&p,5,out,bytes);
        }
        DGPacketFree(&p);if(!good)return 0;y+=rows;
    }
    return 1;
}
int DGReadIOSurfaceTexture(GLuint texture,uint64_t serial,unsigned width,unsigned height,void *pixels,size_t stride)
{return DGReadIOSurfaceTextureElements(texture,serial,width,height,pixels,stride,4);}
int DGSnapshotDrawableTexture(GLenum target,GLint internal,int width,int height,int mipmaps)
{
    struct DGContext *c=dg_current;if(!c) return 0;
    struct DGPacket packet=DGPacketNew(c,DG_SNAPSHOT_DRAWABLE_TEXTURE);
    packet.message.args[0]=target;packet.message.args[1]=(uint32_t)internal;
    packet.message.args[2]=(uint32_t)width;packet.message.args[3]=(uint32_t)height;packet.message.args[4]=!!mipmaps;
    struct DGReply reply=DGSubmit(c,&packet,1);
    int good=packet.valid_reply && !reply.error;
    DGPacketFree(&packet);return good;
}
uint32_t DGAcquireSurface(uint64_t resource,uint64_t incarnation,int width,int height)
{return DGAcquireTypedSurface(resource,incarnation,width,height,VIM_GL_PIXEL_BGRA8);}
int DGSupportsTypedSurfaces(void)
{return dg_current && (dg_current->connection->capabilities&DG_CAP_TYPED_SURFACES)!=0;}
uint32_t DGAcquireTypedSurface(uint64_t resource,uint64_t incarnation,int width,int height,unsigned format)
{
    struct DGContext *c=dg_current;if(!c || !resource || !incarnation || width<=0 || height<=0) return 0;
    if(!VIMGLPixelBytes(format) || (format && !DGSupportsTypedSurfaces()))return 0;
    if(!(c->connection->capabilities&DG_CAP_RETAINED_SURFACES)) { c->error=GL_INVALID_OPERATION;return 0; }
    for(struct DGClientSurface *s=c->surfaces;s;s=s->next) if(s->object==resource && s->incarnation==incarnation) {
        if(s->width!=width || s->height!=height || s->format!=format || s->refs==UINT32_MAX) { c->error=GL_INVALID_OPERATION;return 0; }
        ++s->refs;return s->lease;
    }
    struct DGClientSurface *s=calloc(1,sizeof(*s));if(!s) { c->error=GL_OUT_OF_MEMORY;return 0; }
    struct DGPacket p=DGPacketNew(c,DG_SURFACE_ACQUIRE);
    p.message.args[0]=resource;p.message.args[1]=incarnation;
    p.message.args[2]=(uint32_t)width;p.message.args[3]=(uint32_t)height;
    p.message.args[4]=format;
    struct DGReply r=DGSubmit(c,&p,1);
    uint32_t lease=p.valid_reply && !r.error && r.result<=UINT32_MAX ? (uint32_t)r.result : 0;
    DGPacketFree(&p);
    if(lease) { s->object=resource;s->incarnation=incarnation;s->width=width;s->height=height;
        s->lease=lease;s->refs=1;s->format=format;s->next=c->surfaces;c->surfaces=s; }else free(s);
    return lease;
}
static int DGSurfaceOperation(uint32_t operation,uint32_t lease,uint64_t a,uint64_t b)
{
    struct DGContext *c=dg_current;if(!c || !lease) return 0;
    if(!(c->connection->capabilities&DG_CAP_RETAINED_SURFACES)) { c->error=GL_INVALID_OPERATION;return 0; }
    struct DGPacket p=DGPacketNew(c,operation);p.message.args[0]=lease;
    p.message.args[1]=a;p.message.args[2]=b;
    struct DGReply r=DGSubmit(c,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGReleaseSurfaceInContext(void *opaque,uint32_t lease)
{
    struct DGContext *c=opaque;if(!c || !lease) return 0;
    struct DGClientSurface **link=&c->surfaces;
    while(*link && (*link)->lease!=lease) link=&(*link)->next;
    if(!*link) return 0;
    struct DGClientSurface *s=*link;if(--s->refs) return 1;
    struct DGPacket p=DGPacketNew(c,DG_SURFACE_RELEASE);p.message.args[0]=lease;
    struct DGReply r=DGSubmit(c,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);*link=s->next;free(s);return good;
}
int DGReleaseSurface(uint32_t lease) { return DGReleaseSurfaceInContext(dg_current,lease); }
int DGPublishSurface(uint32_t lease,int flip,int back)
{ return DGSurfaceOperation(DG_SURFACE_PUBLISH,lease,!!flip,!!back); }
int DGBindSurface(uint32_t lease,unsigned internal)
{ return DGSurfaceOperation(DG_SURFACE_BIND,lease,internal,0); }
int DGBindSurfaceView(uint32_t lease,unsigned internal,int width,int height)
{
    if(!DGSupportsSurfaceViews() || !lease || width<1 || height<1 || width>8192 || height>8192) return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_BIND);
    p.message.args[0]=lease;p.message.args[1]=internal;p.message.args[2]=width;p.message.args[3]=height;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGRetireSurface(uint32_t lease)
{
    if(!dg_current || !(dg_current->connection->capabilities&DG_CAP_SURFACE_RETIREMENT)) return 0;
    return DGSurfaceOperation(DG_SURFACE_RETIRE,lease,0,0);
}
int DGSupportsSurfaceEpochs(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_EPOCHS)!=0; }
int DGSupportsSurfaceHolds(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_HOLDS)!=0; }
int DGSupportsSurfaceViews(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_VIEWS)!=0; }
int DGSupportsDrawableAliases(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_DRAWABLE_ALIASES)!=0; }
int DGSupportsSurfaceDrawables(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_DRAWABLES)!=0; }
int DGSupportsSurfaceDrawablePublication(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_DRAWABLE_PUBLICATION)!=0; }
int DGAttachSurfaceDrawable(uint32_t lease)
{ return DGSupportsSurfaceDrawables() && DGSurfaceOperation(DG_SURFACE_DRAWABLE,lease,0,0); }
int DGSupportsSurfacePatchImport(void)
{return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_PATCH_IMPORT)!=0;}
int DGImportSurfacePatch(uint32_t lease,uint64_t epoch,int x,int y,int width,int height,
    int destination_x,int destination_y,int flip)
{
    if(!DGSupportsSurfacePatchImport() || !lease || !epoch || x<0 || y<0 ||
       width<1 || height<1 || destination_x<0 || destination_y<0)return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_IMPORT_REGION);
    p.message.args[0]=lease;p.message.args[1]=epoch;p.message.args[2]=x;p.message.args[3]=y;
    p.message.args[4]=width;p.message.args[5]=height;p.message.args[6]=!!flip;
    p.message.args[7]=destination_x;p.message.args[8]=destination_y;p.message.args[9]=1;
    struct DGReply r=DGSubmit(dg_current,&p,1);return p.valid_reply && !r.error;
}
int DGSupportsSurfaceRegionImport(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_IMPORT_REGION)!=0; }
int DGImportSurfaceRegion(uint32_t lease,uint64_t epoch,int x,int y,int width,int height,int flip)
{
    if(!DGSupportsSurfaceRegionImport() || !lease || !epoch || x<0 || y<0 ||
       width<1 || height<1 || width>8192 || height>8192)return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_IMPORT_REGION);
    p.message.args[0]=lease;p.message.args[1]=epoch;p.message.args[2]=x;p.message.args[3]=y;
    p.message.args[4]=width;p.message.args[5]=height;p.message.args[6]=!!flip;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGBindDrawableTexture(unsigned internal,int width,int height)
{
    if(!DGSupportsDrawableAliases() || width<1 || height<1 || width>8192 || height>8192) return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_BIND_DRAWABLE_TEXTURE);
    p.message.args[0]=internal;p.message.args[1]=width;p.message.args[2]=height;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGSupportsFlippedDrawableAliases(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_FLIPPED_DRAWABLE_ALIASES)!=0; }
int DGSupportsCroppedFlippedDrawableAliases(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_CROPPED_FLIPPED_ALIASES)!=0; }
int DGBindFlippedDrawableTexture(unsigned internal,int width,int height)
{
    if(!DGSupportsFlippedDrawableAliases() || width<1 || height<1 || width>8192 || height>8192)return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_BIND_DRAWABLE_TEXTURE);
    p.message.args[0]=internal;p.message.args[1]=width;p.message.args[2]=height;p.message.args[3]=1;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGSupportsBackDrawableAliases(void)
{return dg_current && (dg_current->connection->capabilities&DG_CAP_DRAWABLE_BACK_ALIASES)!=0;}
int DGBindBackDrawableTexture(unsigned internal,int width,int height,int flip)
{
    if(!DGSupportsBackDrawableAliases() || width<1 || height<1 || width>8192 || height>8192)return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_BIND_DRAWABLE_TEXTURE);
    p.message.args[0]=internal;p.message.args[1]=width;p.message.args[2]=height;p.message.args[3]=2|!!flip;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
struct DGClientHold { struct DGConnection *connection;uint32_t name; };
void *DGCreateSurfaceReadHold(uint64_t object,uint64_t incarnation,int width,int height)
{
    if(!object || !incarnation || width<1 || height<1 || width>8192 || height>8192)return NULL;
    uuid_t namespace;if(!VIMGetResourceNamespace(namespace))return NULL;
    struct DGConnection *connection=calloc(1,sizeof(*connection));if(!connection)return NULL;
    connection->refs=1;connection->fd=-1;
    pthread_mutex_init(&connection->lock,NULL);pthread_mutex_init(&connection->map_lock,NULL);
    pthread_mutex_init(&connection->backing_lock,NULL);
    if(DGConnect(connection)) { DGConnectionRelease(connection);return NULL; }
    struct DGContext service={.connection=connection};
    struct DGPacket hello=DGPacketNew(&service,VIM_GL_SERVICE_HELLO);
    hello.message.args[0]=VIM_GL_SERVICE_MAGIC;hello.message.args[1]=VIM_GL_SERVICE_VERSION;
    memcpy(&hello.message.args[3],namespace,sizeof(namespace));
    struct DGReply reply=DGSubmit(&service,&hello,1);
    int good=hello.valid_reply && !reply.error && (reply.result&DG_CAP_SURFACE_HOLD_READ);
    connection->capabilities=reply.result;DGPacketFree(&hello);
    void *hold=good?DGCreateSurfaceHold(&service,object,incarnation,width,height):NULL;
    DGConnectionRelease(connection);return hold;
}
void *DGCreateSurfaceHold(void *opaque,uint64_t object,uint64_t incarnation,int width,int height)
{ return DGCreateSurfaceVersionHold(opaque,object,incarnation,width,height,0); }
int DGSealSurfaceVersion(uint32_t lease,uint64_t epoch)
{
    if(!dg_current || !epoch || !(dg_current->connection->capabilities&DG_CAP_SURFACE_VERSIONS))return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_SEAL_VERSION);
    p.message.args[0]=lease;p.message.args[1]=epoch;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGSupportsSurfaceVersionTextures(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_VERSION_TEXTURE)!=0; }
int DGSupportsBackingFutureTextures(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_BACKING_FUTURE_VERSION)!=0; }
int DGCopyBackingFutureTexture(uint64_t ticket,const struct VIMSurfaceState *state,unsigned internal,int width,int height)
{
    if(!DGSupportsBackingFutureTextures() || !ticket || !state ||
       state->version!=VIM_SURFACE_STATE_VERSION || !state->object || !state->incarnation || !state->content_epoch ||
       width<1 || height<1 || width>8192 || height>8192)return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_BACKING_FUTURE_TEXTURE);
    uint64_t *a=p.message.args;a[0]=ticket;a[1]=state->object;a[2]=state->incarnation;
    a[3]=state->content_epoch;a[4]=internal;a[5]=(uint32_t)width;a[6]=(uint32_t)height;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
uint64_t DGPresentSurfaceVersion(uint32_t lease,uint64_t epoch)
{
    if(!dg_current || !epoch || !(dg_current->connection->capabilities&DG_CAP_SURFACE_PRESENT_VERSION))return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_PRESENT_VERSION);
    p.message.args[0]=lease;p.message.args[1]=epoch;
    struct DGReply r=DGSubmit(dg_current,&p,1);
    uint64_t ticket=p.valid_reply && !r.error?r.result:0;
    DGPacketFree(&p);return ticket;
}
int DGCopySurfaceVersionTexture(uint32_t lease,uint64_t epoch,unsigned internal,int width,int height)
{
    if(!dg_current || !epoch || width<1 || height<1 || width>8192 || height>8192 ||
       !(dg_current->connection->capabilities&DG_CAP_SURFACE_VERSION_TEXTURE))return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_VERSION_TEXTURE);
    p.message.args[0]=lease;p.message.args[1]=epoch;p.message.args[2]=internal;
    p.message.args[3]=width;p.message.args[4]=height;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
void *DGCreateSurfaceVersionHold(void *opaque,uint64_t object,uint64_t incarnation,int width,int height,uint64_t epoch)
{
    struct DGContext *c=opaque;
    if(!c || !(c->connection->capabilities&DG_CAP_SURFACE_HOLDS)) return NULL;
    if(epoch && !(c->connection->capabilities&DG_CAP_SURFACE_VERSIONS))return NULL;
    struct DGClientHold *hold=calloc(1,sizeof(*hold));if(!hold) return NULL;
    struct DGPacket p=DGPacketNew(c,DG_SURFACE_HOLD);p.message.context=0;
    p.message.args[0]=object;p.message.args[1]=incarnation;p.message.args[2]=(uint32_t)width;p.message.args[3]=(uint32_t)height;
    p.message.args[4]=epoch;
    struct DGReply r=DGSubmit(c,&p,1);int good=p.valid_reply && !r.error && r.result && r.result<=UINT32_MAX;
    if(good) { hold->connection=c->connection;hold->name=(uint32_t)r.result;__sync_add_and_fetch(&hold->connection->refs,1); }
    else { free(hold);hold=NULL; }
    DGPacketFree(&p);return hold;
}
void DGReleaseSurfaceHold(void *opaque)
{
    struct DGClientHold *hold=opaque;if(!hold) return;
    struct DGContext service={.connection=hold->connection};
    struct DGPacket p=DGPacketNew(&service,DG_SURFACE_UNHOLD);p.message.args[0]=hold->name;
    DGSubmit(&service,&p,1);DGPacketFree(&p);DGConnectionRelease(hold->connection);free(hold);
}
int DGReadSurfaceHoldRect(void *opaque,uint64_t epoch,int x,int y,int width,int height,void *pixels,size_t stride)
{
    struct DGClientHold *hold=opaque;
    if(!hold || !(hold->connection->capabilities&DG_CAP_SURFACE_HOLD_READ) || !epoch || !pixels ||
       x<0 || y<0 || width<1 || height<1 || width>8192 || height>8192 ||
       (size_t)width*4>stride || (uint64_t)width*height*4>DG_MAX_BYTES ||
       (size_t)(height-1)>(SIZE_MAX-(size_t)width*4)/stride)return 0;
    struct DGContext service={.connection=hold->connection};
    struct DGPacket p=DGPacketNew(&service,DG_SURFACE_HOLD_READ);
    p.message.flags=DG_OUTPUT_ONLY;p.message.args[0]=hold->name;p.message.args[1]=epoch;
    p.message.args[2]=x;p.message.args[3]=y;p.message.args[4]=width;p.message.args[5]=height;
    p.message.bytes=(uint32_t)width*height*4;
    p.data=calloc(1,p.message.bytes);p.capacity=p.message.bytes;
    if(!p.data)return 0;
    struct DGReply r=DGSubmit(&service,&p,1);int good=p.valid_reply && !r.error;
    if(good)for(int row=0;row<height;++row)
        memcpy((unsigned char *)pixels+(size_t)row*stride,p.data+(size_t)row*width*4,(size_t)width*4);
    DGPacketFree(&p);return good;
}
int DGSurfaceEpoch(uint32_t lease,uint64_t *epoch)
{
    if(!DGSupportsSurfaceEpochs() || !lease || !epoch) return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_EPOCH);p.message.args[0]=lease;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    if(good) *epoch=r.result;DGPacketFree(&p);return good;
}
int DGPublishSurfaceEpoch(uint32_t lease,int flip,int back,uint64_t epoch)
{
    if(!DGSupportsSurfaceEpochs() || !lease || !epoch) return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_PUBLISH);
    p.message.args[0]=lease;p.message.args[1]=!!flip;p.message.args[2]=!!back;p.message.args[3]=epoch;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGSupportsSurfaceRegions(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_REGIONS)!=0; }
int DGGetUnpackSnapshot(int32_t values[7])
{
    if(!values || !dg_current || !(dg_current->connection->capabilities&DG_CAP_UNPACK_SNAPSHOT))return 0;
    int32_t result[7]={0};struct DGPacket p=DGPacketNew(dg_current,DG_UNPACK_SNAPSHOT);
    p.message.flags=DG_OUTPUT_ONLY;DGPacketPointer(&p,0,result,sizeof(result),1);
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    if(good) { DGPacketCopy(&p,0,result,sizeof(result));memcpy(values,result,sizeof(result)); }
    DGPacketFree(&p);return good;
}
int DGPublishSurfaceRegions(uint32_t lease,const struct DGSurfaceRegion *regions,unsigned count,
    int flip,int back,uint64_t expected_epoch,uint64_t published_epoch)
{
    if(!DGSupportsSurfaceRegions() || !lease || !regions || !count || count>DG_SURFACE_REGION_LIMIT ||
       !expected_epoch || published_epoch<=expected_epoch)return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_PUBLISH_REGIONS);
    p.message.flags=DG_INPUT_ONLY;p.message.args[0]=lease;p.message.args[1]=expected_epoch;
    p.message.args[2]=published_epoch;p.message.args[3]=count;p.message.args[4]=!!flip;p.message.args[5]=!!back;
    DGPacketPointer(&p,6,regions,count*sizeof(*regions),0);
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGSupportsSurfaceCopies(void)
{return dg_current && (dg_current->connection->capabilities&DG_CAP_SURFACE_COPY_REGIONS)!=0;}
int DGPublishSealedSurfaceRegions(uint32_t lease,const struct DGSurfaceRegion *regions,unsigned count,
    int flip,int back,uint64_t expected_epoch,uint64_t published_epoch,int copy)
{
    if(!DGSupportsSurfaceRegions() || !lease || !regions || !count || count>DG_SURFACE_REGION_LIMIT ||
       !expected_epoch || published_epoch<=expected_epoch || (copy && (flip || back || !DGSupportsSurfaceCopies())))return 0;
    if(!(dg_current->connection->capabilities&DG_CAP_SEALED_SURFACE_REGIONS))
        return (copy?DGCopySurfaceRegions(lease,regions,count,expected_epoch,published_epoch):
            DGPublishSurfaceRegions(lease,regions,count,flip,back,expected_epoch,published_epoch)) &&
            DGSealSurfaceVersion(lease,published_epoch);
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_SEALED_REGIONS);
    p.message.flags=DG_INPUT_ONLY;p.message.args[0]=lease;p.message.args[1]=expected_epoch;
    p.message.args[2]=published_epoch;p.message.args[3]=count;p.message.args[4]=!!flip;p.message.args[5]=!!back;
    DGPacketPointer(&p,6,regions,count*sizeof(*regions),0);p.message.args[7]=!!copy;
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGCopySurfaceRegions(uint32_t lease,const struct DGSurfaceRegion *regions,unsigned count,
    uint64_t expected_epoch,uint64_t published_epoch)
{
    if(!DGSupportsSurfaceCopies() || !lease || !regions || !count || count>DG_SURFACE_REGION_LIMIT ||
       !expected_epoch || published_epoch<=expected_epoch)return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_COPY_REGIONS);
    p.message.flags=DG_INPUT_ONLY;p.message.args[0]=lease;p.message.args[1]=expected_epoch;
    p.message.args[2]=published_epoch;p.message.args[3]=count;
    DGPacketPointer(&p,6,regions,count*sizeof(*regions),0);
    struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
    DGPacketFree(&p);return good;
}
int DGUploadSurface(uint32_t lease,const void *pixels,int width,int height,unsigned stride,uint64_t epoch)
{
    struct DGClientSurface *s=dg_current?dg_current->surfaces:NULL;
    for(;s && s->lease!=lease;s=s->next){}
    unsigned element=s?VIMGLPixelBytes(s->format):0;
    if(!DGSupportsSurfaceEpochs() || !lease || !epoch || !pixels || width<1 || height<1 ||
        !element || width>8192 || height>8192 || (uint64_t)width*element>stride) return 0;
    size_t row=(size_t)width*element;
    if((size_t)(height-1)>(SIZE_MAX-row)/stride)return 0;
    int split=row*(size_t)height>DG_MAX_BYTES;
    if(split && (s->format!=VIM_GL_PIXEL_BGRA8 ||
        !(dg_current->connection->features&DG_FEATURE_SURFACE_UPLOAD_ROWS)))return 0;
    int limit=split?(int)((16u*1024u*1024u)/row):height;
    for(int first=0;first<height;first+=limit) {
        int rows=height-first<limit?height-first:limit;
        const unsigned char *source=(const unsigned char *)pixels+(size_t)first*stride;
        struct DGPacket p=DGPacketNew(dg_current,DG_SURFACE_UPLOAD);
        p.message.flags=DG_INPUT_ONLY;
        p.message.args[0]=lease;p.message.args[1]=epoch;p.message.args[2]=width;p.message.args[3]=height;
        if(split){p.message.args[5]=first;p.message.args[6]=rows;}
        if(stride==row) {
            // Borrow the caller's mapping only through the synchronous reply.
            DGPacketPointer(&p,4,source,row*rows,0);p.message.args[4]=0;
        } else {
            p.message.bytes=(uint32_t)(row*rows);p.data=malloc(p.message.bytes);p.capacity=p.message.bytes;
            if(!p.data) { dg_current->error=GL_OUT_OF_MEMORY;return 0; }
            for(int y=0;y<rows;++y)memcpy(p.data+(size_t)y*row,source+(size_t)y*stride,row);
        }
        struct DGReply r=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !r.error;
        DGPacketFree(&p);if(!good)return 0;
    }
    return 1;
}
int DGSupportsDrawableReadRects(void)
{ return dg_current && (dg_current->connection->capabilities&DG_CAP_DRAWABLE_READ_RECTS)!=0; }
int DGReadDrawableRect(void *pixels,int x,int y,int width,int height)
{
    struct DGContext *c=dg_current;
    if(!DGSupportsDrawableReadRects() || !pixels || x<0 || y<0 || width<1 || height<1 ||
       (int64_t)x+width>c->width || (int64_t)y+height>c->height) return 0;
    size_t row=(size_t)width*4;
    if((uint64_t)row*height>DG_MAX_BYTES) {
        int limit=(int)((16u*1024u*1024u)/row);
        for(int first=0;first<height;first+=limit) {
            int rows=height-first<limit?height-first:limit;
            if(!DGReadDrawableRect((unsigned char *)pixels+(size_t)first*row,x,y+first,width,rows))return 0;
        }
        return 1;
    }
    struct DGPacket packet=DGPacketNew(c,DG_READ_DRAWABLE_RECT);
    packet.message.flags=DG_OUTPUT_ONLY;
    packet.message.args[0]=width;packet.message.args[1]=height;
    packet.message.args[2]=x;packet.message.args[3]=y;
    packet.message.bytes=(uint32_t)width*height*4;
    packet.data=pixels;packet.capacity=packet.message.bytes;
    struct DGReply reply=DGSubmit(c,&packet,1);
    return packet.valid_reply && !reply.error;
}
// Owned-process A/B only. Preserve real CPU reads, but avoid bouncing larger
// replies through per-chunk guest descriptors and emulated row copies when
// the existing negotiated prepared-RAM receive can write the caller's buffer.
static volatile int dg_prepared_full_read=1;
#define DG_PREPARED_FULL_READ_MIN_BYTES (1024u*1024u)
int DGReadDrawable(void *pixels,int width,int height)
{
    struct DGContext *c=dg_current;
    if(!c || !pixels || width!=c->width || height!=c->height) return 0;
    size_t bytes=(size_t)width*height*4;
    if(bytes>DG_MAX_BYTES) return DGReadDrawableRect(pixels,0,0,width,height);
    if(bytes>=DG_PREPARED_FULL_READ_MIN_BYTES && dg_prepared_full_read &&
       !c->connection->dead && DGSupportsRAMScatterReads()) {
        // Match the old private packet's conservative state invalidation.
        // -1 is pre-submit unsupported/preparation exhaustion ONLY. Once a
        // read was submitted (including failure), never replay the command.
        c->common_state_valid=0;
        int read=DGReadDrawableRectRows(pixels,(size_t)width*4,0,0,0,width,height);
        if(read>=0)return read;
    }
    struct DGPacket packet=DGPacketNew(c,DG_READ_DRAWABLE);
    packet.message.flags=DG_OUTPUT_ONLY;
    packet.message.args[0]=width;packet.message.args[1]=height;
    /* The destination has no preserved padding and is completely replaced.
     * No need to allocate, zero, or upload a second frame-sized packet. */
    packet.message.bytes=(uint32_t)bytes;packet.data=pixels;packet.capacity=bytes;
    struct DGReply reply=DGSubmit(c,&packet,1);
    return packet.valid_reply && !reply.error;
}
int DGReadTypedDrawable(void *pixels,int width,int height,unsigned format)
{
    struct DGContext *c=dg_current;unsigned element=VIMGLPixelBytes(format);
    if(!c || !pixels || !element || !DGSupportsTypedSurfaces() || width!=c->width || height!=c->height ||
       width<1 || height<1 || (uint64_t)width*height*element>DG_MAX_BYTES)return 0;
    struct DGPacket p=DGPacketNew(c,DG_READ_DRAWABLE);p.message.flags=DG_OUTPUT_ONLY;
    p.message.args[0]=width;p.message.args[1]=height;p.message.args[2]=format;
    p.message.bytes=(uint32_t)width*height*element;p.data=pixels;p.capacity=p.message.bytes;
    struct DGReply r=DGSubmit(c,&p,1);
    return p.valid_reply && !r.error;
}
int DGSupportsVRAMDrawables(void)
{return dg_current && dg_current->connection->device && (dg_current->connection->capabilities&DG_CAP_VRAM_DRAWABLES);}
int DGSupportsRAMDrawables(void)
{return dg_current && dg_current->connection->device && (dg_current->connection->capabilities&DG_CAP_RAM_DRAWABLES);}
int DGVRAMStats(uint64_t stats[4])
{
    if(!DGSupportsVRAMDrawables() || !stats)return 0;
    struct DGPacket p=DGPacketNew(dg_current,DG_VRAM_STATS);p.message.flags=DG_OUTPUT_ONLY;
    p.message.bytes=4*sizeof(uint64_t);p.data=(void *)stats;p.capacity=p.message.bytes;p.borrowed=1;
    struct DGReply reply=DGSubmit(dg_current,&p,1);int good=p.valid_reply && !reply.error;
    // Synchronous output promotes borrowed storage to owned reply memory.
    // Copy it back explicitly; otherwise callers observe their seeded zeros.
    if(good)memcpy(stats,p.data,4*sizeof(uint64_t));DGPacketFree(&p);return good;
}
static int DGVRAMTransaction(const struct VIMSurfaceVRAM *b,int x,int y,int width,int height,int flip,
    const struct DGVRAMRect *rects,unsigned count,int action,const uint64_t *pages,unsigned pageCount)
{
    if(!DGSupportsVRAMDrawables())return -1;
    struct DGContext *c=dg_current;
    if(action==2 && (!(c->connection->capabilities&DG_CAP_VRAM_PATCHES) || !count))return -1;
    if(!b || (b->version!=VIM_SURFACE_VRAM_VERSION && b->version!=VIM_SURFACE_RAM_VERSION) ||
       !VIMGLSharedRowsSupported(b->width,b->height,b->row_bytes) ||
       (b->version==VIM_SURFACE_RAM_VERSION && !DGSupportsRAMDrawables()) ||
       pageCount>16384 || (pageCount && !pages) || x<0 || y<0 || width!=c->width || height!=c->height ||
       (uint64_t)x+width>b->width || (uint64_t)y+height>b->height || count>510 || (count && !rects) ||
       !b->state.object || !b->state.incarnation || !b->state.content_epoch ||
       !(b->state.flags&VIM_SURFACE_STATE_CONTEXT_LOCKED) ||
       (b->state.flags&(VIM_SURFACE_STATE_CPU_WRITER|VIM_SURFACE_STATE_GPU_NEWER)))return -1;
    for(unsigned i=0;i<count;++i)if(rects[i].x<0 || rects[i].y<0 || rects[i].width<1 || rects[i].height<1 ||
        (uint64_t)rects[i].x+rects[i].width>(uint64_t)width ||
        (uint64_t)rects[i].y+rects[i].height>(uint64_t)height)return -1;
    struct DGPacket packet=DGPacketNew(c,action==2?DG_VRAM_PATCH_DRAWABLE:action?DG_VRAM_PUBLISH_DRAWABLE:DG_VRAM_IMPORT_DRAWABLE);
    uint64_t *a=packet.message.args;
    a[0]=b->offset;a[1]=b->length;a[2]=b->width;a[3]=b->height;a[4]=b->row_bytes;
    a[5]=x;a[6]=y;a[7]=width;a[8]=height;a[9]=!!flip;
    a[10]=b->state.object;a[11]=b->state.incarnation;a[12]=b->state.content_epoch;
    a[13]=b->version==VIM_SURFACE_RAM_VERSION;a[14]=pageCount;
    if(action){
        packet.message.flags=DG_INPUT_ONLY;packet.message.bytes=count*sizeof(*rects);
        packet.data=(void *)rects;packet.capacity=packet.message.bytes;packet.borrowed=1;
    } else if(pageCount){
        packet.message.flags=DG_INPUT_ONLY;packet.message.bytes=pageCount*sizeof(*pages);
        packet.data=(void *)pages;packet.capacity=packet.message.bytes;packet.borrowed=1;
    }
    struct DGReply reply=DGSubmit(c,&packet,1);
    if(!packet.valid_reply || reply.error)return 0;
    return reply.result==1?1:reply.result==2?-2:-1;
}
int DGImportVRAMDrawable(const struct VIMSurfaceVRAM *b,int x,int y,int width,int height,int flip)
{return DGVRAMTransaction(b,x,y,width,height,flip,NULL,0,0,NULL,0);}
int DGImportRAMDrawable(const struct VIMSurfaceVRAM *b,int x,int y,int width,int height,int flip,const uint64_t *pages,unsigned count)
{return DGVRAMTransaction(b,x,y,width,height,flip,NULL,0,0,pages,count);}
int DGPublishVRAMDrawable(const struct VIMSurfaceVRAM *b,int x,int y,int width,int height,int flip,
    const struct DGVRAMRect *rects,unsigned count)
{return DGVRAMTransaction(b,x,y,width,height,flip,rects,count,1,NULL,0);}
#include "passthrough-opengl-backing-scope-client.c"
int DGImportSharedDrawableRects(const struct VIMSurfaceVRAM *b,int x,int y,int width,int height,int flip,
    const struct DGVRAMRect *rects,unsigned count)
{return DGVRAMTransaction(b,x,y,width,height,flip,rects,count,2,NULL,0);}
int DGImportIOSurfaceTextureFormat(unsigned internal,unsigned format,unsigned type,int width,int height,const void *pixels,size_t stride)
{
    struct DGContext *c=dg_current;
    if(!c)return -1;
    int packed=DGIOSurfacePacked422(internal,format,type);
    if(packed&&(!(c->connection->features&DG_FEATURE_IOSURFACE_PACKED_422)||
       !(c->connection->capabilities&DG_CAP_IOSURFACE_TEXTURE))){
        // Existing peers can still upload converted YCbCr through their
        // ordinary packed GL codec. Never send them raw422 as BGRA/luma.
        if(format==0x85b9&&(c->connection->capabilities&DG_CAP_APPLE_VIDEO_TEXTURES))return -1;
        c->error=GL_INVALID_OPERATION;return 0;
    }
    if(!(c->connection->capabilities&DG_CAP_IOSURFACE_TEXTURE))return -1;
    unsigned element_bytes=DGIOSurfaceTransferBytes(internal,format,type);
    // Older executors know single-channel luminance but not red sampling.
    // Keep their ordinary GL upload fallback instead of sending a new format.
    if(internal==0x8229 && !(c->connection->features&DG_FEATURE_IOSURFACE_RED8))return -1;
    if(internal!=0x8229 && element_bytes==1 && !(c->connection->capabilities&DG_CAP_IOSURFACE_LUMINANCE8)){
        c->error=GL_INVALID_OPERATION;return 0;
    }
    if(!packed && element_bytes==2 && !(c->connection->capabilities&DG_CAP_IOSURFACE_LUMINANCE_ALPHA8)){
        c->error=GL_INVALID_OPERATION;return 0;
    }
    if(element_bytes>4 && !(c->connection->capabilities&DG_CAP_IOSURFACE_FLOAT))return -1;
    if(!pixels || width<1 || height<1 || width>8192 || height>8192 ||
       !element_bytes || (packed&&(width&1)) || stride<(uint64_t)width*element_bytes || stride>DG_MAX_BYTES || stride%element_bytes ||
       (uint64_t)(height-1)*stride+(uint64_t)width*element_bytes>DG_IOSURFACE_MAX_BYTES){
        c->error=GL_INVALID_VALUE;return 0;
    }
    unsigned chunked=(uint64_t)(height-1)*stride+(uint64_t)width*element_bytes>DG_MAX_BYTES;
    if(chunked && !(c->connection->capabilities&DG_CAP_IOSURFACE_ROWS)){c->error=GL_OUT_OF_MEMORY;return 0;}
    for(unsigned y=0;y<(unsigned)height;){
        unsigned rows=(unsigned)height-y;if(rows>DG_MAX_BYTES/stride)rows=(unsigned)(DG_MAX_BYTES/stride);
        struct DGPacket p=DGPacketNew(c,packed?DG_IOSURFACE_TEXTURE_PACKED:DG_IOSURFACE_TEXTURE);
        p.message.flags=DG_INPUT_ONLY;p.message.args[0]=internal;p.message.args[1]=width;
        p.message.args[2]=rows;p.message.args[3]=stride;
        if(chunked){p.message.args[4]=height;p.message.args[5]=y;}
        if(packed){p.message.args[6]=format;p.message.args[7]=type;}
        p.message.bytes=(uint32_t)((rows-1)*stride+(size_t)width*element_bytes);
        p.data=(unsigned char *)pixels+y*stride;p.capacity=p.message.bytes;p.borrowed=1;
        struct DGReply r=DGSubmit(c,&p,1);
        if(!p.valid_reply || r.error || r.result!=1)return 0;y+=rows;
    }
    return 1;
}
int DGImportIOSurfaceTexture(unsigned internal,int width,int height,const void *pixels,size_t stride)
{
    return DGImportIOSurfaceTextureFormat(internal,DGIOSurfaceInternalGLFormat(internal),
        DGIOSurfaceGLType(DGIOSurfaceElementBytes(internal)),width,height,pixels,stride);
}
int DGImportDrawable(const void *pixels,int width,int height,size_t stride,int top_down)
{
    struct DGContext *c=dg_current;
    if(!c || !pixels || width!=c->width || height!=c->height || stride<(size_t)width*4) return 0;
    size_t bytes=(size_t)width*height*4;
    if(bytes>DG_MAX_BYTES) return 0;
    struct DGPacket packet=DGPacketNew(c,DG_IMPORT_DRAWABLE);
    packet.message.args[0]=width;packet.message.args[1]=height;
    /* Borrow the already-locked CPU backing through the synchronous stream
     * write, not through GPU execution. Small commands are copied into the
     * connection's owned batch before return; large payloads are consumed
     * by the transport before returning. No borrowed pointer is retained.
     * Move pitch/orientation conversion off the emulated guest CPU. */
    uint64_t span=(uint64_t)(height-1)*stride+(uint64_t)width*4;
    // Research opt-in: native gather reduces bytes, but repeated same-boot
    // latency measurements do not yet justify enabling its prepare overhead
    // generally. Preserve the established borrowed/batched default.
    if(c->connection->row_upload_supported && bytes>=1024u*1024u && stride>=(uint64_t)width*6 &&
       getenv("VIM_GL_ROW_UPLOAD")) {
        int uploaded=DGImportDrawableRows(pixels,width,height,stride,top_down);
        if(uploaded>=0) {
            if(uploaded)__sync_fetch_and_add(&c->connection->borrowed_import_bytes,bytes);
            return uploaded;
        }
    }
    if((c->connection->capabilities&DG_CAP_STRIDED_IMPORT) &&
       stride<=DG_MAX_BYTES && span<=DG_MAX_BYTES) {
        packet.message.op=DG_IMPORT_DRAWABLE_VIEW;packet.message.args[2]=stride;packet.message.args[3]=!!top_down;
        packet.message.bytes=(uint32_t)span;packet.data=(unsigned char *)pixels;packet.capacity=(size_t)span;
        DGSubmit(c,&packet,0);
        if(!c->connection->dead)__sync_fetch_and_add(&c->connection->borrowed_import_bytes,span);return !c->connection->dead;
    }
    if(!top_down && stride==(size_t)width*4) {
        // The original canonical opcode can borrow tight bottom-up pixels
        // too; this optimization does not require a new executor.
        packet.message.bytes=(uint32_t)bytes;packet.data=(unsigned char *)pixels;packet.capacity=bytes;
        DGSubmit(c,&packet,0);
        if(!c->connection->dead)__sync_fetch_and_add(&c->connection->borrowed_import_bytes,bytes);return !c->connection->dead;
    }
    packet.data=malloc(bytes);if(!packet.data) { c->error=GL_OUT_OF_MEMORY;return 0; }
    packet.message.bytes=(uint32_t)bytes;packet.capacity=bytes;
    for(int y=0;y<height;++y) memcpy(packet.data+(size_t)y*width*4,
        (const char *)pixels+(size_t)(top_down ? height-1-y : y)*stride,(size_t)width*4);
    /* Ordered before subsequent drawing on this connection, no reply needed. */
    DGSubmit(c,&packet,0);DGPacketFree(&packet);
    if(!c->connection->dead)__sync_fetch_and_add(&c->connection->copied_import_bytes,bytes);return !c->connection->dead;
}
int DGImportDrawablePatch(const void *pixels,int x,int y,int width,int height,unsigned stride,int top_down)
{
    struct DGContext *c=dg_current;
    if(!c || !(c->connection->capabilities&DG_CAP_DRAWABLE_PATCHES) || !pixels ||
       x<0 || y<0 || width<1 || height<1 || (int64_t)x+width>c->width ||
       (int64_t)y+height>c->height || stride<(uint64_t)width*4 ||
       (uint64_t)width*height*4>DG_MAX_BYTES) return 0;
    size_t bytes=(size_t)width*height*4;
    struct DGPacket packet=DGPacketNew(c,DG_IMPORT_DRAWABLE_PATCH);
    packet.message.args[0]=width;packet.message.args[1]=height;
    packet.message.args[2]=x;packet.message.args[3]=y;
    if(!getenv("VIM_GL_DISABLE_PATCH_GATHER")) {
        if(!top_down && stride==(size_t)width*4) {
            // DGSubmit snapshots small borrowed packets into its owned batch,
            // and consumes larger payloads before returning. No heap packing
            // is needed for an already canonical patch on either transport.
            packet.message.bytes=(uint32_t)bytes;packet.data=(unsigned char *)pixels;packet.capacity=bytes;
            DGSubmit(c,&packet,0);return !c->connection->dead;
        }
        if(c->connection->row_upload_supported && bytes>=64u*1024u) {
            int uploaded=DGImportDrawablePatchRows(pixels,x,y,width,height,stride,top_down);
            if(uploaded>=0)return uploaded;
            // Fallback is allowed only before the header/stream is touched.
        }
    }
    packet.data=malloc(bytes);if(!packet.data) { c->error=GL_OUT_OF_MEMORY;return 0; }
    packet.message.bytes=(uint32_t)bytes;packet.capacity=bytes;
    for(int row=0;row<height;++row) memcpy(packet.data+(size_t)row*width*4,
        (const char *)pixels+(size_t)(top_down ? height-1-row : row)*stride,(size_t)width*4);
    DGSubmit(c,&packet,0);DGPacketFree(&packet);return !c->connection->dead;
}
#include "passthrough-opengl-scatter-client.c"
#include "passthrough-opengl-tight-upload-client.c"
void DGPixelStore(GLint key,GLint value) { if(dg_current && key==0x10) dg_current->row=value; }
static void DGExternalPresent(GLboolean external) { (void)external; /* This backend never implicitly presents. */ }
static void DGPreserveDrawable(void) { /* Host drawable persists until resize/destroy. */ }
void (*DGGetProcAddress(const char *name))(void)
{
    if(!strcmp(name,"glBlitFramebuffer")) return (void (*)(void))glBlitFramebufferEXT;
    if(!strcmp(name,"DGSetExternalPresent")) return (void (*)(void))DGExternalPresent;
    if(!strcmp(name,"DGPreserveCurrentBuffer")) return DGPreserveDrawable;
#include "direct-lookup.inc"
    return NULL;
}
