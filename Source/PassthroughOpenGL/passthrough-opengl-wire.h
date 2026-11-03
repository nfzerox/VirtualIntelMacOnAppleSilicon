/* Isolated desktop GL remoting proof. Fixed-width little-endian wire; never
 * transmit process pointers. Both supported hosts and these Intel guests are
 * little-endian. This TCP proof is loopback-only, not a production service.
 * Design reference: qemu-ios 58e49856, gles-public per-context batching.
 * No iOS renderer, guest trap code, or Mesa implementation is included. */
#ifndef VIM_PASSTHROUGH_OPENGL_WIRE_H
#define VIM_PASSTHROUGH_OPENGL_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "../vendor/source/VMsvga2/vminclude/vim_gl_surface_service.h"
#include "../vendor/source/VMsvga2/vminclude/vim_gl_pixel_formats.h"
#define DG_MAGIC 0x31474c44u
#define DG_VERSION 8u
/* Optional HELLO acknowledgement in the echoed layout-string span. Old
 * executors echo the hexadecimal layout unchanged; old clients never ask.
 * This extends exhausted capability space without another startup round trip. */
#define DG_HELLO_FEATURES_MAGIC UINT32_C(0x584d4956)
#define DG_FEATURE_IOSURFACE_OUTPUT_READ 1u
#define DG_FEATURE_CORE_COMMON_STATE 2u
#define DG_FEATURE_IOSURFACE_RED8 4u
#define DG_FEATURE_COMPRESSED_TEXTURE_UPLOAD 8u
#define DG_FEATURE_LARGE_IOSURFACE_WRITES 16u
#define DG_FEATURE_IOSURFACE_PACKED_422 32u
#define DG_FEATURE_TEXTURE_IMAGE_READBACK 64u
struct DGHelloFeatures { uint32_t magic,features; };
/* Additive HELLO reply capability. Old v8 clients ignore reply.result; old
 * servers return zero. Never send new resource commands without negotiation. */
#define DG_CAP_RETAINED_SURFACES 1u
#define DG_CAP_SURFACE_EPOCHS 2u
#define DG_CAP_SURFACE_HOLDS VIM_GL_SERVICE_CAP_HOLDS
#define DG_CAP_SURFACE_RETIREMENT 8u
#define DG_CAP_SURFACE_VIEWS 16u
#define DG_CAP_DRAWABLE_ALIASES 32u
#define DG_CAP_SURFACE_DRAWABLES 64u
#define DG_CAP_DRAWABLE_PATCHES 128u
#define DG_CAP_DRAWABLE_READ_RECTS 256u
#define DG_CAP_CORE_PROFILE 512u
#define DG_CAP_SYNC_OBJECTS 1024u
#define DG_CAP_INSTANCED_ARRAYS 2048u
#define DG_CAP_BUFFER_QUERIES 4096u
#define DG_CAP_BUFFER_MAPPING 8192u
#define DG_CAP_PIXEL_BUFFERS 16384u
#define DG_CAP_STRIDED_IMPORT 32768u
#define DG_CAP_SURFACE_REGIONS 65536u
#define DG_CAP_SURFACE_HOLD_READ VIM_GL_SERVICE_CAP_READ
#define DG_CAP_UNPACK_SNAPSHOT 262144u
#define DG_CAP_SURFACE_VERSIONS VIM_GL_SERVICE_CAP_VERSIONS
#define DG_CAP_SURFACE_VERSION_TEXTURE 1048576u
#define DG_CAP_SURFACE_PRESENT_VERSION 2097152u
#define DG_CAP_VRAM_DRAWABLES 4194304u
#define DG_CAP_RAM_DRAWABLES 8388608u
#define DG_CAP_VRAM_PATCHES 16777216u
#define DG_CAP_IOSURFACE_TEXTURE 33554432u
#define DG_CAP_ARB_PROGRAMS 67108864u
#define DG_CAP_FLIPPED_DRAWABLE_ALIASES 134217728u
#define DG_CAP_FLUSH_DRAWABLE_STATE 268435456u
#define DG_CAP_SURFACE_IMPORT_REGION 1073741824u
#define DG_CAP_SURFACE_COPY_REGIONS 2147483648u
#define DG_CAP_SURFACE_PATCH_IMPORT (UINT64_C(1)<<32)
#define DG_CAP_COMMON_STATE (UINT64_C(1)<<33)
#define DG_CAP_TIGHT_TEX_SUB_IMAGE_2D (UINT64_C(1)<<34)
#define DG_CAP_TEXTURE_1D (UINT64_C(1)<<35)
#define DG_CAP_TEXTURE_3D (UINT64_C(1)<<36)
#define DG_CAP_CROPPED_FLIPPED_ALIASES (UINT64_C(1)<<37)
#define DG_CAP_VALIDATED_UNIFORM_LOCATIONS (UINT64_C(1)<<38)
#define DG_CAP_ERROR_STATE (UINT64_C(1)<<39)
#define DG_CAP_SURFACE_DRAWABLE_PUBLICATION (UINT64_C(1)<<40)
#define DG_CAP_IOSURFACE_WRITES (UINT64_C(1)<<41)
#define DG_CAP_NATIVE_RENDERERS (UINT64_C(1)<<42)
#define DG_CAP_UNIFORM_QUERY (UINT64_C(1)<<43)
#define DG_CAP_CALL_LISTS (UINT64_C(1)<<44)
#define DG_CAP_QUERY_REPLIES (UINT64_C(1)<<45)
#define DG_CAP_CLIENT_VERTEX_STACK (UINT64_C(1)<<46)
/* Explicit borrowed-render scope; never inferred from a snapshot import.
 * Requires a separately retained guest pin/CPU exclusion and drained close.
 * Only executors implementing those scope operations advertise this. */
#define DG_CAP_BACKING_RENDER_SCOPE (UINT64_C(1)<<47)
#define DG_CAP_BACKING_COMPLETION VIM_GL_SERVICE_CAP_BACKING_TICKETS
#define DG_CAP_BACKING_FUTURE_VERSION (UINT64_C(1)<<49)
/* Explicit writer continuation, never implied by immutable GPU read support.
 * The kernel must transfer its real CPU gate before this can be selected. */
#define DG_CAP_BACKING_WRITER_CONTINUATION (UINT64_C(1)<<50)
/* Internal GLI attachment preparation only, not lazy ordinary GL allocation.
 * Backing attach/import completes before public CGL success. Every generic
 * operation which needs actual pixels first resolves a private target. */
#define DG_CAP_PREPARED_DRAWABLE (UINT64_C(1)<<51)
/* Native RGhA/RGfA IOSurface storage, without quantizing to BGRA8. */
#define DG_CAP_IOSURFACE_FLOAT (UINT64_C(1)<<52)
/* IOSURFACE_TEXTURE args[4:5]=full height/start row; READ args[7:8].
 * Zero retains the original whole-image operation. Each packet remains
 * bounded by DG_MAX_BYTES, even for larger floating-point surfaces. */
#define DG_CAP_IOSURFACE_ROWS (UINT64_C(1)<<53)
/* Lossless async scalar batches only. Old v8 peers keep ordinary messages;
 * native GL still executes/validates every decoded command in order. */
#define DG_CAP_COMPACT_SCALARS (UINT64_C(1)<<55)
#define DG_CAP_IOSURFACE_LUMINANCE8 (UINT64_C(1)<<56)
#define DG_CAP_IOSURFACE_LUMINANCE_ALPHA8 (UINT64_C(1)<<57)
/* Packed Apple video pixels and the qualified indirect texture-storage
 * contract. STRING also requires this bit in args[2], so an older guest
 * cannot be offered formats its pixel-span decoder cannot transport. */
#define DG_CAP_APPLE_VIDEO_TEXTURES (UINT64_C(1)<<58)
/* ACQUIRE args[4] names an explicit canonical format. Zero remains BGRA8.
 * Nonzero formats must never be sent to an older peer. */
#define DG_CAP_TYPED_SURFACES (UINT64_C(1)<<59)
/* DRAW args[13]=1: raw PowerPC vertex/index bytes, typed conversion only.
 * Buffer contents and all scalar GL command arguments remain unchanged. */
#define DG_CAP_BIG_ENDIAN_ARRAYS (UINT64_C(1)<<60)
/* Requested per synchronous reply, never assumed by an old peer. */
#define DG_CAP_ERROR_EMPTY_REPLY (UINT64_C(1)<<61)
#define DG_CAP_SEALED_SURFACE_REGIONS (UINT64_C(1)<<62)
/* BIND_DRAWABLE_TEXTURE args[3]: bit0 opposite rows (existing), bit1 actual
 * double-buffered drawable BACK storage. Send bit1 only with this capability;
 * older hosts reject it, rather than silently importing the wrong buffer. */
#define DG_CAP_DRAWABLE_BACK_ALIASES (UINT64_C(1)<<63)
#define DG_COMPACT_SCALARS 0x10057u
/* PUBLISH_REGIONS layout plus args[7]=0 (drawable publication) or 1
 * (canonical in-place copy). Submit the ordered copy AND immutable seal
 * together, under the resource lock. Remaining arguments zero. Negotiated
 * independently; old clients/servers retain separate publication + seal. */
#define DG_SURFACE_SEALED_REGIONS 0x10058u
/* Shared conservative extension set. Optional additions must be negotiated
 * AND supported by the actual host context, not merely present in headers. */
#define DG_COMPATIBILITY_EXTENSIONS "GL_ARB_texture_rectangle GL_EXT_framebuffer_object GL_ARB_vertex_buffer_object " \
    "GL_ARB_shader_objects GL_ARB_vertex_shader GL_ARB_fragment_shader GL_ARB_shading_language_100 GL_APPLE_fence"
/* DG_STRING.args[3] acknowledges the shared query/vector codecs. Old guests
 * send zero and must not acquire extensions whose entry points they lack. */
#define DG_COMPAT_DISPATCH_REVISION 1u
#define DG_MAX_BYTES (64u*1024u*1024u)
/* A wire packet is not a texture-memory budget. Native CI's ordinary 5K
 * image pyramid already exceeds 256MiB with RGBA16F intermediates. Row
 * transfers retain the packet bound while admitting our advertised 8192²
 * texture extent at up to RGBA32F, with bounded share-group residency. */
#define DG_IOSURFACE_MAX_BYTES (UINT64_C(1)<<30)
#define DG_IOSURFACE_GROUP_BYTES (UINT64_C(2)<<30)
#define DG_BATCH_BYTES (64u*1024u)
#define DG_CREATE 0x10000u
#define DG_DESTROY 0x10001u
#define DG_RESIZE 0x10002u
/* Same width,height,logical-name args as RESIZE, all other args/bytes zero,
 * DG_REPLY. Only explicitly negotiated canonical hosts accept it.
 * Reserves an FBO name, never authorizes GPU writing or grants a CPU loan. */
#define DG_PREPARE_DRAWABLE 0x10055u
#define DG_HELLO 0x10003u
#define DG_STRING 0x10004u
#define DG_ERROR 0x10005u
#define DG_DRAW 0x10006u
#define DG_DROP_DRAWABLE 0x10007u
#define DG_RENDERER_MEMORY 0x10008u
#define DG_DRAWABLE_DAMAGE 0x10009u
#define DG_READ_DRAWABLE 0x1000au
#define DG_IMPORT_DRAWABLE 0x1000bu
#define DG_SNAPSHOT_DRAWABLE_TEXTURE 0x1000cu
#define DG_SURFACE_ACQUIRE 0x1000du
#define DG_SURFACE_RELEASE 0x1000eu
#define DG_SURFACE_PUBLISH 0x1000fu
#define DG_SURFACE_BIND 0x10010u
#define DG_SURFACE_EPOCH 0x10011u
#define DG_SURFACE_UPLOAD 0x10012u
#define DG_SURFACE_HOLD VIM_GL_SERVICE_HOLD
#define DG_SURFACE_UNHOLD VIM_GL_SERVICE_UNHOLD
#define DG_SURFACE_RETIRE 0x10015u
#define DG_BIND_DRAWABLE_TEXTURE 0x10016u
#define DG_SURFACE_DRAWABLE 0x10017u
#define DG_IMPORT_DRAWABLE_PATCH 0x10018u
#define DG_READ_DRAWABLE_RECT 0x10019u
#define DG_SYNC_FENCE 0x1001au
#define DG_SYNC_DELETE 0x1001bu
#define DG_SYNC_IS 0x1001cu
#define DG_SYNC_CLIENT_WAIT 0x1001du
#define DG_SYNC_WAIT 0x1001eu
#define DG_SYNC_GET 0x1001fu
#define DG_DRAW_INSTANCED_ARRAYS 0x10020u
#define DG_ARRAY_DIVISOR 0x10021u
#define DG_BUFFER_QUERY 0x10022u
#define DG_BUFFER_READ 0x10023u
#define DG_BUFFER_COPY 0x10024u
#define DG_MAP_BEGIN 0x10025u
#define DG_MAP_INFO 0x10026u
#define DG_MAP_READ 0x10027u
#define DG_MAP_WRITE 0x10028u
#define DG_MAP_FLUSH 0x10029u
#define DG_MAP_END 0x1002au
#define DG_MAP_READY 0x1002bu
#define DG_PBO_PIXEL_2D 0x1002cu
#define DG_PBO_CLIENT_STACK 0x1002du
#define DG_PBO_DELETE 0x1002eu
#define DG_PBO_BIND 0x1002fu
#define DG_IMPORT_DRAWABLE_VIEW 0x10030u
#define DG_SURFACE_PUBLISH_REGIONS 0x10031u
#define DG_SURFACE_HOLD_READ VIM_GL_SERVICE_READ
#define DG_UNPACK_SNAPSHOT 0x10034u
#define DG_SURFACE_SEAL_VERSION 0x10035u
#define DG_SURFACE_VERSION_TEXTURE 0x10036u
#define DG_SURFACE_PRESENT_VERSION 0x10037u
#define DG_VRAM_IMPORT_DRAWABLE 0x10038u
#define DG_VRAM_PUBLISH_DRAWABLE 0x10039u
#define DG_VRAM_STATS 0x1003au
#define DG_VRAM_PATCH_DRAWABLE 0x1003bu
#define DG_IOSURFACE_TEXTURE 0x1003cu
/* Additive native video import, negotiated by IOSURFACE_PACKED_422. Same
 * row/chunk layout as IOSURFACE_TEXTURE, plus args[6]=format,args[7]=type.
 * Old peers retain their canonical internal-format-only BGRA/luma opcode. */
#define DG_IOSURFACE_TEXTURE_PACKED 0x10059u
#define DG_PROGRAM_STRING 0x1003du
#define DG_FLUSH_DRAWABLE_STATE 0x1003eu
#define DG_SURFACE_HOLD_PRESENT VIM_GL_SERVICE_PRESENT
#define DG_SURFACE_PRESENT_READY VIM_GL_SERVICE_READY
#define DG_SURFACE_IMPORT_REGION 0x10041u
#define DG_SURFACE_COPY_REGIONS 0x10042u
#define DG_COMMON_STATE 0x10043u
#define DG_TIGHT_TEX_SUB_IMAGE_2D 0x10044u
#define DG_PBO_PIXEL_3D 0x10045u
#define DG_ERROR_STATE 0x10046u
#define DG_FLUSH_IOSURFACE_STATE 0x10047u
#define DG_READ_IOSURFACE_TEXTURE 0x10048u
#define DG_NATIVE_RENDERERS 0x10049u
#define DG_SELECT_NATIVE_RENDERER 0x1004au
#define DG_UNIFORM_QUERY 0x1004bu
/* Begin uses the full RAM backing descriptor/page-vector layout of Import,
 * with args[15]=nonzero endpoint loan serial. End/Materialize use args[0]=
 * that exact serial, all other args/bytes zero. End replies only after GPU
 * completion; Materialize does NOT end CPU exclusion or permit backing reuse. */
#define DG_BACKING_RENDER_BEGIN 0x1004cu
#define DG_BACKING_RENDER_END 0x1004du
#define DG_BACKING_RENDER_MATERIALIZE 0x1004eu
/* Returns an independent native completion ticket, NOT CPU ownership. The
 * guest must retain backing preparation/exclusion until service Wait succeeds.
 * No ordinary renderer selects this without an explicit CPU-access gate. */
#define DG_BACKING_RENDER_DEFER 0x1004fu
#define DG_BACKING_FUTURE_TEXTURE 0x10053u
#define DG_BACKING_RENDER_CONTINUE 0x10054u
/* Same descriptor as BEGIN, args[5]=previous completion ticket. Metadata-only,
 * no page vector/bytes; only an EXACT existing RAM registration is eligible.
 * A server GPU wait orders the previous queue's last RAM writer before the
 * new writer. This is NOT a CPU completion or permission to release a pin. */
_Static_assert(DG_BACKING_FUTURE_TEXTURE!=VIM_GL_SERVICE_BACKING_WAIT &&
    DG_BACKING_FUTURE_TEXTURE!=VIM_GL_SERVICE_BACKING_RELEASE &&
    DG_BACKING_FUTURE_TEXTURE!=VIM_GL_SERVICE_BACKING_CHECK,"distinct future consumer opcode");
/* GPU-only read of the ticket's EXACT immutable image, never the resource's
 * latest published epoch. args: ticket, object, incarnation, epoch, internal,
 * width, height; remaining args/bytes zero, DG_REPLY. The caller retains its
 * kernel GPU-read reservation until this submission returns. CPU ownership
 * remains with the independent completion job. */
/* Separately negotiated prototype: DEFER args[1]=1 publishes/seals the exact
 * next backing epoch BEFORE exposing the completion ticket. A GPU consumer
 * may retain that version; it never grants guest CPU access. Ordinary DEFER
 * keeps all remaining args zero and its original behavior on every peer. */
#define DG_BACKING_COMPLETION_WAIT VIM_GL_SERVICE_BACKING_WAIT
#define DG_BACKING_COMPLETION_RELEASE VIM_GL_SERVICE_BACKING_RELEASE
#define DG_BACKING_COMPLETION_CHECK VIM_GL_SERVICE_BACKING_CHECK
/* DG_VRAM_STATS args[0]: zero preserves the original four transfer counters.
 * An explicit scope-capable executor also accepts this read-only
 * selector, returning four uint64 counters: successful Begin, materialization,
 * unchanged-snapshot End, private-writeback End. No polling or enable switch. */
#define DG_VRAM_STATS_BACKING_SCOPE 1u
/* Explicit canonical-scope candidate only: attach/reuse/self-publication/
 * stale-rejection counts. Same four-counter payload; old selectors unchanged. */
#define DG_VRAM_STATS_CANONICAL_SCOPE 2u
/* Renderer-global, read-only drawable registration counters: fresh native
 * imports, exact-view texture hits, texture retentions and spare evictions. */
#define DG_VRAM_STATS_DRAWABLE_STORAGE 3u
/* Explicit writer builds only: attempted continuation/cache miss/foreign
 * group/Begin refusal; then Begin's shape/shared/alias/registration refusals. */
#define DG_VRAM_STATS_WRITER_REFUSALS 4u
#define DG_VRAM_STATS_WRITER_ADMISSION 5u
#define DG_VRAM_STATS_CANONICAL_WRITER 6u
#define DG_VRAM_STATS_WRITER_ERRORS 7u
#define DG_VRAM_STATS_WRITER_ERROR_FIELDS 8u
#define DG_VRAM_STATS_SURFACE_REGISTRATION 9u
#define DG_VRAM_STATS_PREPARED_DRAWABLE 10u
#define DG_VRAM_STATS_CPU_CANONICAL 11u
#define DG_VRAM_STATS_SCATTER_BACKING 12u
#define DG_VRAM_STATS_GATHER_INITIALIZE 13u
#define DG_VRAM_STATS_CPU_COALESCING 14u
/* Logical hardware/software choices, never private host screen indices or
 * pointers. Availability and current class come from actual native CGL. */
#include "direct-native-renderers.h"
#include "direct-iosurface-writes.h"
_Static_assert(DG_IOSURFACE_WRITE_LIMIT*sizeof(struct DGIosurfaceWrite)<=DG_MAX_BYTES,"publication payload bound");
/* Exact typed compatibility GL values, not narrowed double/integer conversions. */
struct DGCommonState {
    uint32_t version;
    int32_t viewport[4],draw_framebuffer,draw_buffer,rectangle_binding;
    float modelview[16],projection[16];
};
_Static_assert(sizeof(struct DGCommonState)==160,"fixed typed state snapshot");
#include "direct-vram-contract.h"
struct DGPixelState {
    int32_t alignment[2],row_length[2],skip_rows[2],skip_pixels[2],row_bytes[2];uint32_t buffers[2];
};
_Static_assert(sizeof(struct DGPixelState)==48,"fixed client pixel state, no process pointers");
/* Additive negotiated state reply; old v8 peers retain the 48-byte reply. */
struct DGPixelState3D {
    struct DGPixelState base;
    int32_t image_height[2],skip_images[2];
};
_Static_assert(sizeof(struct DGPixelState3D)==64,"fixed volume pixel state");
#define DG_MAP_SEEDED 0x100u /* Private: write-only preserve snapshot available. */
#define DG_MAP_BUDGET (4u*DG_MAX_BYTES)
#define DG_MAP_CHUNK (1024u*1024u)
/* Standard values from the target SDK's gl3.h. Its compatibility gl.h/glext.h
 * intentionally omit these declarations although the native API exists. */
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001u
#define GL_MAP_WRITE_BIT 0x0002u
#define GL_MAP_INVALIDATE_RANGE_BIT 0x0004u
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008u
#define GL_MAP_FLUSH_EXPLICIT_BIT 0x0010u
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020u
#endif
struct DGMapInfo { uint64_t size,offset,length;uint32_t lease,access; };
_Static_assert(sizeof(struct DGMapInfo)==32,"fixed buffer-map reply, no process pointer");
struct DGSyncQuery { int32_t length,value; };
_Static_assert(sizeof(struct DGSyncQuery)==8,"fixed sync query reply");
struct DGDamage { int32_t x,y,width,height; };
_Static_assert(sizeof(struct DGDamage)==16,"fixed drawable damage record");
#define DG_VERTEX_SLOTS 32u
/* Slots 0..4 are vertex/normal/color/secondary/fog, 8..15 texture units,
 * 16..31 generic attributes. pointer is a payload offset or VBO byte offset,
 * never an address in either process. */
struct DGVertexArray { uint32_t slot,size,type,stride,normalized,buffer;uint64_t pointer,bytes; };
_Static_assert(sizeof(struct DGVertexArray)==40,"fixed vertex array descriptor");
/* Negotiated stack push: pixel reply prefix plus logical vertex metadata.
 * CPU pointers are always zero; pointer carries only VBO byte offsets. */
struct DGClientStackState {
    struct DGPixelState3D pixel;
    uint32_t vertex_array,array_buffer,element_buffer,client_texture,enabled,reserved;
    struct DGVertexArray arrays[DG_VERTEX_SLOTS];
};
_Static_assert(sizeof(struct DGClientStackState)==1368,"fixed client stack metadata");
#define DG_REPLY 1u
#define DG_OBSERVE_ERROR_EMPTY 8u
#define DG_REPLY_ERROR_EMPTY UINT32_C(0x80000000)
/* Private drawable read only: reserve a reply buffer without uploading old
 * pixels. Public glReadPixels retains its padding-preservation codec. */
#define DG_OUTPUT_ONLY 2u
/* Negotiated surface upload: acknowledge without echoing the input image. */
#define DG_INPUT_ONLY 4u
struct DGMessage { uint32_t op,context,bytes,flags;uint64_t args[16]; };
/* Container: context=flags=0, args[0]=1, args[1]=record count>=2, other
 * args zero, complete header+body<=DG_BATCH_BYTES. Each record is followed
 * by words exact uint64 argument bits; omitted TRAILING words are zero.
 * No replies, pointer body, nested/private opcode or changed GL semantics.
 * Records and their argument spans are 8-byte multiples; enclosing streams
 * need not be aligned, so encoders/decoders must use memcpy, not casts. */
struct DGCompactScalar { uint16_t op,words;uint32_t context; };
_Static_assert(sizeof(struct DGCompactScalar)==8,"fixed compact scalar record in both guest widths");
struct DGReply { uint32_t bytes,error;uint64_t result; };
static unsigned short DGPort(void)
{
    const char *setting=getenv("VIM_PASSTHROUGH_OPENGL_PORT");char *end=NULL;
    unsigned long port=setting ? strtoul(setting,&end,10) : 18091;
    return setting && (!*setting || *end || port<1024 || port>65535) ? 18091 : (unsigned short)port;
}
_Static_assert(sizeof(struct DGMessage)==144,"fixed desktop GL wire header");
_Static_assert(sizeof(struct DGReply)==16,"fixed desktop GL reply header");
static int DGTransfer(int fd,void *buffer,size_t bytes,int writing)
{
#ifdef DG_MEMORY_HOST_TRANSFER
    if(fd<0)return DG_MEMORY_HOST_TRANSFER(buffer,bytes,writing);
#endif
    unsigned char *p=buffer;
    while(bytes) {
        ssize_t n=writing ? write(fd,p,bytes) : read(fd,p,bytes);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) return -1;
        p+=n;bytes-=(size_t)n;
    }
    return 0;
}
static uint64_t DGBits(const void *value,size_t size)
{ uint64_t bits=0;if(size<=sizeof(bits)) memcpy(&bits,value,size);return bits; }
static size_t DGArrayBytes(int64_t count,size_t size)
{ return count<0 || (uint64_t)count>DG_MAX_BYTES/size ? SIZE_MAX : (size_t)count*size; }
static int DGCompressedProxy(uint32_t target)
{return target==0x8063u || target==0x8064u || target==0x8070u || target==0x851bu;}
/* Native query/generator validation owns nonpositive capacity semantics.
 * These calls carry no array bytes until capacity is positive. Negative
 * and zero-capacity shapes are negotiated; they must not look like malformed
 * wire pointers to an older executor that rejects zero-length spans. */
static size_t DGQueryArrayBytes(int64_t count,size_t size)
{return count<=0?0:DGArrayBytes(count,size);}
/* Raw list-index bytes retain native signed/float conversion and the packed
 * big-endian GL_2/3/4_BYTES interpretation. Invalid types and nonpositive
 * counts carry no data: native GL performs its own validation/no-op ordering,
 * which also avoids reading an unused or protected client pointer. */
static size_t DGCallListsBytes(int32_t count,uint32_t type)
{
    if(count<=0)return 0;
    size_t element;
    switch(type){
    case 0x1400:case 0x1401:element=1;break;
    case 0x1402:case 0x1403:case 0x1407:element=2;break;
    case 0x1408:element=3;break;
    case 0x1404:case 0x1405:case 0x1406:case 0x1409:element=4;break;
    default:return 0;
    }
    return DGArrayBytes(count,element);
}
static size_t DGVertexBytes(uint32_t type)
{
    switch(type) {
    case 0x1400:case 0x1401:return 1;
    case 0x1402:case 0x1403:case 0x140b:return 2;
    case 0x1404:case 0x1405:case 0x1406:return 4;
    case 0x140a:return 8;
    default:return 0;
    }
}
static size_t DGVertexSpan(const struct DGVertexArray *a,uint64_t count)
{
    size_t element=DGVertexBytes(a->type)*a->size;
    if(!element || !a->size || a->size>4 || a->stride>DG_MAX_BYTES) return SIZE_MAX;
    if(!count) return 0;
    uint64_t stride=a->stride ? a->stride : element;
    if(count-1>DG_MAX_BYTES/stride) return SIZE_MAX;
    uint64_t bytes=(count-1)*stride+element;
    return bytes>DG_MAX_BYTES ? SIZE_MAX : (size_t)bytes;
}
static size_t DGVertexAttribCount(uint32_t key)
{
    switch(key) {
    case 0x8626:return 4; /* CURRENT_VERTEX_ATTRIB */
    case 0x8622:case 0x8623:case 0x8624:case 0x8625: /* enabled,size,stride,type */
    case 0x886a:case 0x889f:case 0x88fd:case 0x88fe:return 1; /* normalized,binding,integer,divisor */
    default:return 0;
    }
}
static uint64_t DGMaxIndex(const void *indices,uint32_t count,uint32_t type)
{
    uint64_t highest=0;
    for(uint32_t i=0;i<count;++i) {
        uint32_t value=0;
        if(type==0x1401) value=((const uint8_t *)indices)[i];
        else if(type==0x1403) { uint16_t v;memcpy(&v,(const char *)indices+i*2,2);value=v; }
        else if(type==0x1405) memcpy(&value,(const char *)indices+i*4,4);
        else return UINT64_MAX;
        if(value>highest) highest=value;
    }
    return count ? highest+1 : 0;
}
/* Unknown Get pnames must not be guessed scalar: that would overwrite a
 * caller's allocation for vector results. Expand from audited API contracts. */
static size_t DGGetCount(uint32_t key)
{
    switch(key) {
#define DG_SERVER_QUERY(name,count) case name:return count;
#include "server-state-query-counts.inc"
#undef DG_SERVER_QUERY
    case 0x0ba6:case 0x0ba7:case 0x0ba8:case 0x0b89:
    case 0x8641:case 0x88b7:return 16; /* matrices, ARB current/transpose */
    case 0x0ba2:case 0x0c10:case 0x0c22:case 0x0c23:case 0x0b00:
    case 0x0b03:case 0x0b04:case 0x0b53:case 0x0b66:return 4;
    case 0x0b70:case 0x0d3a:case 0x0b12:case 0x0b22:case 0x846d:case 0x846e:return 2;
    case 0x0ba0:case 0x0ba1:case 0x0be2:case 0x0b71:case 0x0b90:case 0x0b44:
    case 0x0c00:case 0x0c01:case 0x0c02:case 0x0c11:case 0x0d05:
    case 0x0cf5:case 0x0cf2:case 0x0cf3:case 0x0cf4:case 0x0d02:case 0x0d03:case 0x0d04:
    case 0x85b2:case 0x8a15:case 0x8a16:
    case 0x0d33:case 0x8069:case 0x8068:case 0x806a:case 0x84f6:case 0x8514:
    case 0x84e0:case 0x84e1:case 0x84e2:case 0x84f8:case 0x8872:case 0x8b4d:case 0x8b8d:
    case 0x8894:case 0x8895:case 0x88ed:case 0x88ef:
    case 0x8ca6:case 0x8caa:case 0x8ca7:case 0x0d56:case 0x0d57:return 1;
    case GL_RED_BITS:case GL_GREEN_BITS:case GL_BLUE_BITS:case GL_ALPHA_BITS:return 1;
    case GL_SUBPIXEL_BITS:return 1;
    case 0x8825:case 0x8826:case 0x8827:case 0x8828:case 0x8829:case 0x882a:case 0x882b:case 0x882c:
    case 0x882d:case 0x882e:case 0x882f:case 0x8830:case 0x8831:case 0x8832:case 0x8833:case 0x8834:return 1;
    /* Scalar depth state saved/restored by native WebKit's WebGL backend.
     * Keep native boolean/double conversion; these are not depth-range pairs. */
    case GL_DEPTH_WRITEMASK:case GL_DEPTH_CLEAR_VALUE:return 1;
    /* Actual iDVD/Quartz Composer state-save queries. Both are scalar enums
     * in OpenGL 2.1; retain native validation/conversion rather than inferring
     * a value from our setter cache or guessing all unknown pnames scalar. */
    case GL_FRONT_FACE:case GL_LIGHT_MODEL_COLOR_CONTROL:return 1;
    /* Native8R4088 CGLSetPBuffer queries this APPLE_float_pixels boolean
     * before attaching the native pbuffer. Forward the actual host query;
     * do not guess a float/non-float value from the requested pixel format. */
    case GL_COLOR_FLOAT_APPLE:return 1;
    case GL_UNPACK_SWAP_BYTES:case GL_UNPACK_LSB_FIRST:
    case GL_PACK_SWAP_BYTES:case GL_PACK_LSB_FIRST:
    /* Scalar since GL 1.2, also queried while saving/restoring Core Image's
     * pixel-store state around 2D effects. Unknown pnames still fail closed;
     * these are not texture dimensions or variable-length vector results. */
    case GL_PACK_IMAGE_HEIGHT:case GL_PACK_SKIP_IMAGES:
    case GL_UNPACK_IMAGE_HEIGHT:case GL_UNPACK_SKIP_IMAGES:
    case GL_RED_SCALE:case GL_RED_BIAS:case GL_GREEN_SCALE:case GL_GREEN_BIAS:
    case GL_BLUE_SCALE:case GL_BLUE_BIAS:case GL_ALPHA_SCALE:case GL_ALPHA_BIAS:
    case GL_DEPTH_SCALE:case GL_DEPTH_BIAS:case GL_MAP_COLOR:case GL_MAP_STENCIL:
    case GL_INDEX_SHIFT:case GL_INDEX_OFFSET:
    case GL_RENDER_MODE: /* Native CGL queries this when switching recovery screens. */
    case GL_LIST_BASE:case GL_LIST_INDEX:case GL_LIST_MODE:
    case GL_ATTRIB_STACK_DEPTH:case GL_MODELVIEW_STACK_DEPTH:
    case 0x8640:case 0x864b:case 0x8804: /* ARB program stack/error/enabled */
    case GL_PROJECTION_STACK_DEPTH:case GL_TEXTURE_STACK_DEPTH:return 1;
    case 0x821b:case 0x821c:case 0x821d:case 0x821e:case 0x9126:return 1; /* core version, extensions, flags/profile */
    case 0x85b5:return 1; /* VERTEX_ARRAY_BINDING(_APPLE) */
    default:break;
    }
    switch(key) {
#include "direct-get-counts.inc"
    default:return 0;
    }
}
static size_t DGLightingCount(unsigned kind,uint32_t key)
{
    if(kind==0) { /* glLight[fi]v / glGetLight[fi]v */
        switch(key) {
        case GL_AMBIENT:case GL_DIFFUSE:case GL_SPECULAR:case GL_POSITION:return 4;
        case GL_SPOT_DIRECTION:return 3;
        case GL_SPOT_EXPONENT:case GL_SPOT_CUTOFF:case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:case GL_QUADRATIC_ATTENUATION:return 1;
        default:return 0;
        }
    }
    if(kind==1) { /* glMaterial[fi]v / glGetMaterial[fi]v */
        switch(key) {
        case GL_AMBIENT:case GL_DIFFUSE:case GL_SPECULAR:case GL_EMISSION:case GL_AMBIENT_AND_DIFFUSE:return 4;
        case GL_COLOR_INDEXES:return 3;
        case GL_SHININESS:return 1;
        default:return 0;
        }
    }
    if(kind==2) { /* glLightModel[fi]v */
        switch(key) {
        case GL_LIGHT_MODEL_AMBIENT:return 4;
        case GL_LIGHT_MODEL_LOCAL_VIEWER:case GL_LIGHT_MODEL_TWO_SIDE:case GL_LIGHT_MODEL_COLOR_CONTROL:return 1;
        default:return 0;
        }
    }
    switch(key) { /* glFog[fi]v */
    case GL_FOG_COLOR:return 4;
    case GL_FOG_MODE:case GL_FOG_DENSITY:case GL_FOG_START:case GL_FOG_END:case GL_FOG_INDEX:case GL_FOG_COORD_SRC:return 1;
    default:return 0;
    }
}
static size_t DGParameterCount(uint32_t key)
{
    switch(key) {
    case 0x1004:case 0x8e46:return 4; /* border color, swizzle RGBA */
    case 0x1000:case 0x1001:case 0x1003:case 0x1005:case 0x8071:
    /* GL_TEXTURE_{RED,GREEN,BLUE,ALPHA,LUMINANCE,INTENSITY}_SIZE. */
    case 0x805c:case 0x805d:case 0x805e:case 0x805f:case 0x8060:case 0x8061:
    case 0x2800:case 0x2801:case 0x2802:case 0x2803:case 0x8072:
    case 0x85b7:case 0x85bc:case 0x8191:case 0x8a4b:
    case 0x813a:case 0x813b:case 0x813c:case 0x813d:
    case 0x912f:case 0x82df: /* TEXTURE_IMMUTABLE_FORMAT / LEVELS: one GLint. */
    case 0x8b80:case 0x8b81:case 0x8b82:case 0x8b83:case 0x8b84:
    case 0x8b85:case 0x8b86:case 0x8b87:case 0x8b88:
    case GL_SHADER_TYPE: /* glGetShaderiv: one GLint, used before shader translation. */
    case GL_ACTIVE_ATTRIBUTES:case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
    case 0x8cd0:case 0x8cd1:case 0x8cd2:case 0x8cd3:
    case 0x8d42:case 0x8d43:case 0x8d44:case 0x8d50:case 0x8d51:case 0x8d52:return 1;
    default:return 0; /* generator rejects zero/unknown before calling host */
    }
}
static size_t DGPixelSize(uint32_t format,uint32_t type)
{
    /* APPLE_ycbcr_422: both 2vuy/yuvs orderings use two bytes per pixel,
     * not two independently stored components. Native GL does conversion. */
    if(type==0x85ba || type==0x85bb)return format==0x85b9 ? 2 : 0;
    switch(type) {
    case 0x8035:case 0x8367:case 0x8036:case 0x8368:case 0x84fa:return 4;
    case 0x8363:case 0x8364:case 0x8033:case 0x8365:case 0x8034:case 0x8366:return 2;
    }
    size_t components=0,bytes=0;
    switch(format) {
    case 0x1902:case 0x1903:case 0x1904:case 0x1905:case 0x1906:case 0x1909:components=1;break;
    case 0x190a:case 0x8227:components=2;break;
    case 0x1907:case 0x80e0:components=3;break;
    case 0x1908:case 0x80e1:components=4;break;
    }
    switch(type) {
    case 0x1400:case 0x1401:bytes=1;break;
    case 0x1402:case 0x1403:case 0x140b:bytes=2;break;
    case 0x1404:case 0x1405:case 0x1406:bytes=4;break;
    }
    return components*bytes;
}
static size_t DGPixelSpan(int width,int height,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes)
{
    size_t pixel=DGPixelSize(format,type);
    if(!pixel || width<0 || height<0 || row<0 || skiprows<0 || skippixels<0 || rowbytes<0 ||
       (alignment!=1 && alignment!=2 && alignment!=4 && alignment!=8)) return SIZE_MAX;
    if(!width || !height) return 0;
    /* APPLE_row_bytes overrides row length/alignment, including padding
     * that is not a whole pixel. Both endpoints must bound the actual span
     * native OpenGL will access, not just the visible rectangle. */
    uint64_t stride=rowbytes ? (uint64_t)rowbytes : (((uint64_t)(row ? row : width)*pixel+alignment-1)/alignment)*alignment;
    uint64_t rows=(uint64_t)skiprows+height-1,tail=((uint64_t)skippixels+width)*pixel;
    if(tail>DG_MAX_BYTES || (rows && stride>(DG_MAX_BYTES-tail)/rows))return SIZE_MAX;
    return (size_t)(rows*stride+tail);
}
/* Last touched byte, not a full padded final image. Check products before
 * multiplying: legal GL integer states can exceed uint64_t in three axes. */
static size_t DGPixelSpan3D(int width,int height,int depth,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes,int imageheight,int skipimages)
{
    if(depth<0 || imageheight<0 || skipimages<0)return SIZE_MAX;
    size_t bytes=DGPixelSpan(width,height,format,type,row,alignment,skiprows,skippixels,rowbytes);
    if(bytes==SIZE_MAX || !width || !height || !depth)return bytes==SIZE_MAX ? SIZE_MAX : 0;
    uint64_t pixel=DGPixelSize(format,type);
    uint64_t stride=rowbytes ? (uint64_t)rowbytes :
        (((uint64_t)(row ? row : width)*pixel+alignment-1)/alignment)*alignment;
    uint64_t rows=(uint64_t)(imageheight ? imageheight : height);
    uint64_t images=(uint64_t)skipimages+(uint64_t)depth-1;
    if(images && (rows>DG_MAX_BYTES/images || stride>DG_MAX_BYTES/(images*rows)))return SIZE_MAX;
    uint64_t prefix=images*rows*stride;
    return prefix>DG_MAX_BYTES-bytes ? SIZE_MAX : (size_t)prefix+bytes;
}
#endif
