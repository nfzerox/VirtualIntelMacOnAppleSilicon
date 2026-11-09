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
/* Preserve the released 2.0 row-upload bit. SURFACE_UPLOAD args[5:6]
 * carry the first row and strip height. */
#define DG_FEATURE_SURFACE_UPLOAD_ROWS 128u
#define DG_FEATURE_EVALUATOR_UPLOADS 256u
#define DG_FEATURE_INTEGER_PARAMETERS 512u
#define DG_FEATURE_TEXTURE_READ_LAYOUT 1024u
#define DG_FEATURE_TEXTURE_READ_CHUNKS 2048u
#define DG_FEATURE_BASE_VERTEX 4096u
#define DG_FEATURE_MULTI_DRAW 8192u
#define DG_FEATURE_TRANSFORM_FEEDBACK_NAMES 16384u
#define DG_FEATURE_INDEXED_STATE 32768u
#define DG_FEATURE_INTEGER_VERTEX_ARRAYS 65536u
#define DG_FEATURE_CLEAR_BUFFER 131072u
#define DG_FEATURE_TIMER_QUERY 262144u
#define DG_FEATURE_UNIFORM_BLOCKS 524288u
#define DG_FEATURE_INDEXED_INT64 1048576u
#define DG_FEATURE_SAMPLERS 2097152u
#define DG_FEATURE_TEXTURE_BUFFER 4194304u
#define DG_FEATURE_MULTISAMPLE_TEXTURE 8388608u
#define DG_FEATURE_DUAL_SOURCE_BLEND 16777216u
#define DG_FEATURE_INDEXED_BLEND 33554432u
#define DG_FEATURE_PROGRAM_PIPELINE 67108864u
#define DG_FEATURE_FP64_UNIFORM 134217728u
#define DG_FEATURE_VIEWPORT_ARRAY 268435456u
#define DG_FEATURE_GL4_PROFILE 536870912u
/* Evaluator queries used bit 7 only in unreleased 2.1 builds. */
#define DG_FEATURE_EVALUATOR_QUERIES UINT32_C(0x40000000)
#define DG_FEEDBACK_NAME_LIMIT 65536u
/* The existing layout echo has room for further words. A distinct marker
 * prevents interpreting a legacy host's remaining layout hex as features. */
#define DG_HELLO_FEATURES2_MAGIC UINT32_C(0x324d4956)
#define DG_FEATURE2_TESSELLATION UINT32_C(1)
#define DG_FEATURE2_FEEDBACK_OBJECTS UINT32_C(2)
#define DG_FEATURE2_CORE_EXTENSIONS UINT32_C(4)
#define DG_FEATURE2_SUBROUTINES UINT32_C(8)
#define DG_FEATURE2_ES2_COMPAT UINT32_C(16)
#define DG_FEATURE2_PACKED_ATTRIBS UINT32_C(32)
#define DG_FEATURE2_PROGRAM_BINARY UINT32_C(64)
#define DG_FEATURE2_SAMPLE_SHADING UINT32_C(128)
#define DG_FEATURE2_INTERNAL_FORMAT UINT32_C(256)
#define DG_FEATURE2_DRAW_INDIRECT UINT32_C(512)
#define DG_FEATURE2_FP64_VERTEX UINT32_C(1024)
#define DG_FEATURE2_PIXEL_MAPS UINT32_C(2048)
#define DG_FEATURE2_COMPAT_RASTER UINT32_C(4096)
#define DG_FEATURE2_AUX_VERTEX_ARRAYS UINT32_C(8192)
#define DG_FEATURE2_ARRAY_ELEMENT UINT32_C(16384)
#define DG_FEATURE2_RETAINED_RENDER UINT32_C(32768)
#define DG_FEATURE2_APPLE_ARRAYS UINT32_C(65536)
#define DG_FEATURE2_DEBUG_LABEL UINT32_C(131072)
#define DG_FEATURE2_SHADER_INCLUDE UINT32_C(262144)
#define DG_FEATURE2_APPLE_EVALUATORS UINT32_C(524288)
#define DG_FEATURE2_VERTEX_BLEND UINT32_C(1048576)
#define DG_FEATURE2_POINT_SIZE_ARRAY UINT32_C(2097152)
#define DG_FEATURE2_OBJECT_PURGEABLE UINT32_C(4194304)
#define DG_FEATURE2_IMAGING_PARAMETERS UINT32_C(8388608)
#define DG_FEATURE2_IMAGING_TRANSFERS UINT32_C(16777216)
#define DG_FEATURE2_LEGACY_COMBINERS UINT32_C(33554432)
#define DG_FEATURE2_LEGACY_SAMPLE_PASS UINT32_C(67108864)
#define DG_FEATURE2_SRGB_DECODE UINT32_C(134217728)
#define DG_FEATURE2_ADDITIONAL_STATE UINT32_C(268435456)
#define DG_FEATURE2_LARGE_TEXTURE_INPUT UINT32_C(536870912)
#define DG_FEATURE2_LARGE_BUFFER_TRANSFER UINT32_C(1073741824)
#define DG_FEATURE2_RASTER_STREAM UINT32_C(2147483648)
#define DG_HELLO_FEATURES3_MAGIC UINT32_C(0x334d4956)
#define DG_FEATURE3_STIPPLE_READ UINT32_C(1)
#define DG_FEATURE3_COMPACT_IMAGING UINT32_C(2)
#define DG_FEATURE3_COMPACT_TEXTURE UINT32_C(4)
#define DG_FEATURE3_BIG_ENDIAN_ARRAY_ELEMENT UINT32_C(8)
/* DG_STRING(GL_NUM_EXTENSIONS) returns a stable bitmask, not the host's
 * extension list. Only add a bit after qualifying its entire wire family. */
#define DG_CORE_EXTENSIONS(X) \
    X(TEXTURE_STORAGE, 0, "GL_ARB_texture_storage", 0, 0) \
    X(SHADER_SUBROUTINE, 1, "GL_ARB_shader_subroutine", 0, DG_FEATURE2_SUBROUTINES) \
    X(ES2_COMPATIBILITY, 2, "GL_ARB_ES2_compatibility", 0, DG_FEATURE2_ES2_COMPAT) \
    X(BLEND_FUNC_EXTENDED, 3, "GL_ARB_blend_func_extended", DG_FEATURE_DUAL_SOURCE_BLEND, 0) \
    X(DRAW_BUFFERS_BLEND, 4, "GL_ARB_draw_buffers_blend", DG_FEATURE_INDEXED_BLEND, 0) \
    X(SAMPLER_OBJECTS, 5, "GL_ARB_sampler_objects", DG_FEATURE_SAMPLERS, 0) \
    X(SEPARATE_SHADER_OBJECTS, 6, "GL_ARB_separate_shader_objects", DG_FEATURE_PROGRAM_PIPELINE, 0) \
    X(TESSELLATION_SHADER, 7, "GL_ARB_tessellation_shader", 0, DG_FEATURE2_TESSELLATION) \
    X(TRANSFORM_FEEDBACK2, 8, "GL_ARB_transform_feedback2", 0, DG_FEATURE2_FEEDBACK_OBJECTS) \
    X(TRANSFORM_FEEDBACK3, 9, "GL_ARB_transform_feedback3", 0, DG_FEATURE2_FEEDBACK_OBJECTS) \
    X(VIEWPORT_ARRAY, 10, "GL_ARB_viewport_array", DG_FEATURE_VIEWPORT_ARRAY, 0) \
    X(TIMER_QUERY, 11, "GL_ARB_timer_query", DG_FEATURE_TIMER_QUERY, 0) \
    X(INTERNAL_FORMAT, 12, "GL_ARB_internalformat_query", 0, DG_FEATURE2_INTERNAL_FORMAT) \
    X(DRAW_INDIRECT, 13, "GL_ARB_draw_indirect", 0, DG_FEATURE2_DRAW_INDIRECT) \
    X(FP64_VERTEX, 14, "GL_ARB_vertex_attrib_64bit", DG_FEATURE_FP64_UNIFORM, DG_FEATURE2_FP64_VERTEX) \
    X(DEBUG_LABEL, 15, "GL_EXT_debug_label", 0, DG_FEATURE2_DEBUG_LABEL) \
    X(SHADER_INCLUDE, 16, "GL_ARB_shading_language_include", 0, DG_FEATURE2_SHADER_INCLUDE) \
    X(OBJECT_PURGEABLE, 17, "GL_APPLE_object_purgeable", 0, DG_FEATURE2_OBJECT_PURGEABLE) \
    X(GPU_SHADER_FP64, 18, "GL_ARB_gpu_shader_fp64", DG_FEATURE_FP64_UNIFORM|DG_FEATURE_GL4_PROFILE, 0) \
    X(TEXTURE_COMPRESSION_S3TC, 19, "GL_EXT_texture_compression_s3tc", DG_FEATURE_COMPRESSED_TEXTURE_UPLOAD|DG_FEATURE_TEXTURE_IMAGE_READBACK, 0) \
    X(TEXTURE_FILTER_ANISOTROPIC, 20, "GL_EXT_texture_filter_anisotropic", 0, 0) \
    X(TEXTURE_SWIZZLE, 21, "GL_ARB_texture_swizzle", 0, 0) \
    X(TEXTURE_RGB10_A2UI, 22, "GL_ARB_texture_rgb10_a2ui", 0, 0) \
    X(TEXTURE_BUFFER_RGB32, 23, "GL_ARB_texture_buffer_object_rgb32", DG_FEATURE_TEXTURE_BUFFER, 0) \
    X(TEXTURE_CUBE_MAP_ARRAY, 24, "GL_ARB_texture_cube_map_array", 0, 0) \
    X(TEXTURE_SRGB_DECODE, 25, "GL_EXT_texture_sRGB_decode", DG_FEATURE_SAMPLERS, DG_FEATURE2_SRGB_DECODE)
#define DG_CORE_EXT_BIT(symbol, bit, name, required1, required2) DG_CORE_EXT_##symbol = 1u << bit,
enum { DG_CORE_EXTENSIONS(DG_CORE_EXT_BIT) };
#undef DG_CORE_EXT_BIT
#define DG_CORE_EXT_OR(symbol, bit, name, required1, required2) | DG_CORE_EXT_##symbol
enum { DG_CORE_EXT_KNOWN = 0 DG_CORE_EXTENSIONS(DG_CORE_EXT_OR) };
#undef DG_CORE_EXT_OR
struct DGHelloFeatures { uint32_t magic,features,magic2,features2,magic3,features3; };
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
/* DG_STRING.args[3] acknowledges guest-side codec coverage: 1 for the
 * query/vector family, 2 for indexed draw buffers and transform feedback,
 * 3 for the subsequent negotiated compatibility families,
 * 4 for the extended point-size array descriptor, 5 for object purgeability,
 * 7 for qualified compressed-texture and anisotropy discovery,
 * 8 for texture/sampler sRGB-decode state.
 * The echoed host feature bits alone cannot prove what the client supports. */
#define DG_COMPAT_DISPATCH_REVISION 8u
#define DG_MAX_BYTES (64u*1024u*1024u)
#define DG_MAX_DISPLAY_PIXELS (8192u*8192u)
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
/* Extended draw args[14]=variant, [15]=sign-extended base, [7]=end:start.
 * Feedback replay uses [7]=stream:name. Indirect draw uses [15]=native
 * command-buffer byte offset. Neither operation has a CPU vertex count. */
enum { DG_BASE_ELEMENTS=1, DG_BASE_RANGE, DG_BASE_INSTANCED,
       DG_MULTI_ARRAYS, DG_MULTI_ELEMENTS, DG_MULTI_BASE,
       DG_DRAW_FEEDBACK, DG_DRAW_FEEDBACK_STREAM, DG_DRAW_ARRAYS_INDIRECT, DG_DRAW_ELEMENTS_INDIRECT };
/* Multi-draw args[2]=list length, [15]=body offset of fixed-width records.
 * An index is either a bounded body span or a native EBO byte offset. */
struct DGDrawListItem { uint64_t index; int32_t count,first_or_base; };
_Static_assert(sizeof(struct DGDrawListItem)==16,"fixed draw list layout");
#define DG_DRAW_LIST_LIMIT (DG_MAX_BYTES/32)
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
#define DG_TEXTURE_READ 0x1005au
#define DG_ARRAY_ELEMENT_STATE 0x1005bu
#define DG_RETAINED_RENDER_READ 0x1005cu
#define DG_BUFFER_TRANSFER 0x1005du
#define DG_RASTER_READ_STAGE 0x1005eu
#define DG_STIPPLE_READ 0x1005fu
enum { DG_BUFFER_STAGE_DATA=1, DG_BUFFER_STAGE_SUBDATA, DG_BUFFER_STAGE_READ,
       DG_BUFFER_STAGE_WRITE, DG_BUFFER_STAGE_FETCH, DG_BUFFER_STAGE_COMMIT, DG_BUFFER_STAGE_CANCEL };
#define DG_RENDER_INLINE_WORDS 1024u
#define DG_RENDER_CHUNK_WORDS (256u*1024u)
struct DGRenderModeReply { uint32_t mode,error; };
_Static_assert(sizeof(struct DGRenderModeReply)==8,"fixed render-mode reply");
static int DGRenderBufferIndex(uint32_t mode)
{
    return mode==GL_SELECT?0:mode==GL_FEEDBACK?1:-1;
}
enum { DG_TEXTURE_READ_LAYOUT, DG_TEXTURE_READ_DATA, DG_TEXTURE_READ_PBO,
       DG_TEXTURE_READ_STAGE, DG_TEXTURE_READ_CHUNK };
#define DG_TEXTURE_READ_CHUNK_BYTES (4u*1024u*1024u)
struct DGTextureReadLayout {
    uint64_t offset,row_stride,image_stride;
    uint32_t row_bytes,rows,images,reserved;
};
_Static_assert(sizeof(struct DGTextureReadLayout)==40,"texture read layout has no process pointers");
/* Return the tight transfer size and the end of the last destination row. */
static size_t DGTextureReadSize(const struct DGTextureReadLayout *l,uint64_t *end)
{
    *end=0;
    if(l->reserved)return SIZE_MAX;
    if(!l->row_bytes || !l->rows || !l->images)
        return (!l->row_bytes && !l->rows && !l->images)?0:SIZE_MAX;
    if(l->row_bytes>SIZE_MAX/l->rows ||
       (uint64_t)l->row_bytes*l->rows>SIZE_MAX/l->images)return SIZE_MAX;
    uint64_t span=l->offset;
    if(l->rows>1 && l->row_stride>(UINT64_MAX-span)/(l->rows-1))return SIZE_MAX;
    span+=(l->rows-1)*l->row_stride;
    if(l->images>1 && l->image_stride>(UINT64_MAX-span)/(l->images-1))return SIZE_MAX;
    span+=(l->images-1)*l->image_stride;
    if(l->row_bytes>UINT64_MAX-span)return SIZE_MAX;
    *end=span+l->row_bytes;
    return (size_t)l->row_bytes*l->rows*l->images;
}
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
enum DGUniformResult { DG_UNIFORM_FLOAT,DG_UNIFORM_INT,DG_UNIFORM_UINT,DG_UNIFORM_DOUBLE };
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
#define DG_VERTEX_LEGACY_SLOTS 32u
#define DG_VERTEX_POINT_SLOT 32u
#define DG_VERTEX_SLOTS 33u
#define DGVertexBit(slot) (UINT64_C(1)<<(slot))
#define DG_VERTEX_EDGE_SLOT 5u
#define DG_VERTEX_INDEX_SLOT 6u
#define DG_VERTEX_WEIGHT_SLOT 7u
#define DG_VERTEX_WEIGHT_MAX 4u /* measured native width; fits a 32-byte immediate value */
#define DG_VERTEX_ARRAY_INTEGER 2u /* normalized field tag, never combined */
#define DG_VERTEX_ARRAY_DOUBLE 3u  /* glVertexAttribLPointer, not conversion to float */
/* Slots 0..4 are vertex/normal/color/secondary/fog, 8..15 texture units,
 * 16..31 generic attributes, 32 point size. pointer is a payload offset or VBO byte offset,
 * never an address in either process. */
struct DGVertexArray { uint32_t slot,size,type,stride,normalized,buffer;uint64_t pointer,bytes; };
_Static_assert(sizeof(struct DGVertexArray)==40,"fixed vertex array descriptor");
/* One immediate CPU element: typed bytes, never a process address. */
struct DGElementValue { uint32_t slot,size,type,normalized;unsigned char data[32]; };
_Static_assert(sizeof(struct DGElementValue)==48,"fixed immediate vertex element");
/* Negotiated stack push: pixel reply prefix plus logical vertex metadata.
 * CPU pointers are always zero; pointer carries only VBO byte offsets. */
struct DGClientStackState {
    struct DGPixelState3D pixel;
    uint32_t vertex_array,array_buffer,element_buffer,client_texture,enabled,reserved;
    struct DGVertexArray arrays[DG_VERTEX_LEGACY_SLOTS];
};
_Static_assert(sizeof(struct DGClientStackState)==1368,"fixed client stack metadata");
struct DGClientStackStatePoint {
    struct DGClientStackState base;
    struct DGVertexArray point;
    uint32_t enabled,reserved;
};
_Static_assert(sizeof(struct DGClientStackStatePoint)==1416,"point-size client stack metadata");
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
{return target==0x8063u || target==0x8064u || target==0x8070u || target==0x851bu ||
    target==0x84f7u || target==0x8c19u || target==0x8c1bu || target==0x900bu;}
/* Native query/generator validation owns nonpositive capacity semantics.
 * These calls carry no array bytes until capacity is positive. Negative
 * and zero-capacity shapes are negotiated; they must not look like malformed
 * wire pointers to an older executor that rejects zero-length spans. */
static size_t DGQueryArrayBytes(int64_t count,size_t size)
{return count<=0?0:DGArrayBytes(count,size);}
/* Evaluator GetMap results are tightly packed, not pixel-store transfers.
 * The Map1 and Map2 target ranges have identical component order. */
static unsigned DGMapDimensions(uint32_t target)
{
    if(target>=0x0d90 && target<=0x0d98)return 1;
    if(target>=0x0db0 && target<=0x0db8)return 2;
    return 0;
}
static unsigned DGMapComponents(uint32_t target)
{
    static const unsigned char components[]={4,1,3,1,2,3,4,3,4};
    unsigned dimensions=DGMapDimensions(target);
    return dimensions?components[target-(dimensions==2?0x0db0:0x0d90)]:0;
}
static size_t DGEvaluatorCoefficientBytes(unsigned width,int32_t uorder,int32_t vorder,size_t element)
{
    if(width<1 || width>4 || uorder<1 || vorder<1 || !element ||
       element>DG_MAX_BYTES/width)return SIZE_MAX;
    uint64_t count=(uint64_t)uorder*vorder;
    return count>DG_MAX_BYTES/element/width ? SIZE_MAX : (size_t)count*width*element;
}
static int DGAppleEvaluatorQuery(uint32_t pname)
{return pname>=0x8a02 && pname<=0x8a09;}
static size_t DGMapQueryBytes(uint32_t target,uint32_t query,
    int32_t uorder,int32_t vorder,size_t element)
{
    unsigned dimensions=DGMapDimensions(target);
    if(!dimensions || !element || element>DG_MAX_BYTES/4)return SIZE_MAX;
    if(query==0x0a01)return dimensions*element;   /* GL_ORDER */
    if(query==0x0a02)return dimensions*2*element; /* GL_DOMAIN */
    if(query!=0x0a00)return SIZE_MAX;
    return DGEvaluatorCoefficientBytes(DGMapComponents(target),uorder,dimensions==2?vorder:1,element);
}
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
static int DGWeightType(uint32_t type)
{return (type>=0x1400 && type<=0x1406) || type==0x140a;}
static int DGIntegerVertexType(uint32_t type)
{ return type>=GL_BYTE && type<=GL_UNSIGNED_INT; }
static int DGColorIndexVertexType(uint32_t type)
{
    return type==GL_UNSIGNED_BYTE || type==GL_SHORT || type==GL_INT ||
        type==GL_FLOAT || type==GL_DOUBLE;
}
static int DGVertexArrayModeValid(const struct DGVertexArray *a)
{
    if(a->slot==DG_VERTEX_POINT_SLOT)return a->size==1 && a->type==GL_FLOAT && !a->normalized;
    if(a->slot==DG_VERTEX_EDGE_SLOT || a->slot==DG_VERTEX_INDEX_SLOT)
        return a->size==1 && !a->normalized &&
            (a->slot==DG_VERTEX_EDGE_SLOT ? a->type==GL_UNSIGNED_BYTE : DGColorIndexVertexType(a->type));
    return a->normalized<=1 || (a->normalized==DG_VERTEX_ARRAY_INTEGER &&
        a->slot>=16 && a->size && DGIntegerVertexType(a->type)) ||
        (a->normalized==DG_VERTEX_ARRAY_DOUBLE && a->slot>=16 &&
         a->size>=1 && a->size<=4 && a->type==GL_DOUBLE);
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

/* EXT_draw_buffers2 and EXT_transform_feedback share these getters.
 * Indexed uniform-buffer and sample-mask state is also accepted by core. */
static size_t DGIndexedStateCount(uint32_t key)
{
    switch(key) {
    case GL_COLOR_WRITEMASK:case GL_SCISSOR_BOX:return 4;
    case GL_BLEND:case GL_SCISSOR_TEST:
    case 0x8c84:case 0x8c85:case 0x8c8f: /* transform-feedback start, size, binding */
    case 0x8a28:case 0x8a29:case 0x8a2a: /* uniform-buffer binding, start, size */
    case 0x8e52: /* sample mask value */
    case 0x80c8:case 0x80c9:case 0x80ca:case 0x80cb: /* blend factors */
    case 0x8009:case 0x883d: /* blend equations */
        return 1;
    default:return 0;
    }
}
static size_t DGIndexedInteger64Count(uint32_t key)
{
    switch(key) {
    case GL_VIEWPORT:case GL_SCISSOR_BOX:return 4;
    case GL_DEPTH_RANGE:return 2;
    case GL_SCISSOR_TEST:case 0x8919: /* sampler binding */
        return 1;
    default:return DGIndexedStateCount(key);
    }
}
enum DGImagingParameters {
    DG_COLOR_TABLE_SET, DG_COLOR_TABLE_GET, DG_CONVOLUTION_SET,
    DG_CONVOLUTION_GET, DG_HISTOGRAM_GET, DG_MINMAX_GET,
};

/* These counts are part of the imaging API, not renderer limits. */
static size_t DGImagingParameterCount(enum DGImagingParameters operation,uint32_t key)
{
    switch(operation) {
    case DG_COLOR_TABLE_SET:case DG_COLOR_TABLE_GET:
        if(key==GL_COLOR_TABLE_SCALE || key==GL_COLOR_TABLE_BIAS)return 4;
        return operation==DG_COLOR_TABLE_GET && key>=GL_COLOR_TABLE_FORMAT &&
            key<=GL_COLOR_TABLE_INTENSITY_SIZE;
    case DG_CONVOLUTION_SET:case DG_CONVOLUTION_GET:
        if(key==GL_CONVOLUTION_FILTER_SCALE || key==GL_CONVOLUTION_FILTER_BIAS ||
           key==GL_CONVOLUTION_BORDER_COLOR)return 4;
        if(key==GL_CONVOLUTION_BORDER_MODE)return 1;
        return operation==DG_CONVOLUTION_GET && key>=GL_CONVOLUTION_FORMAT &&
            key<=GL_MAX_CONVOLUTION_HEIGHT;
    case DG_HISTOGRAM_GET:return key>=GL_HISTOGRAM_WIDTH && key<=GL_HISTOGRAM_SINK;
    case DG_MINMAX_GET:return key==GL_MINMAX_FORMAT || key==GL_MINMAX_SINK;
    }
    return 0;
}

static size_t DGSamplerParameterCount(uint32_t key)
{
    switch(key) {
    case GL_TEXTURE_BORDER_COLOR:return 4;
    case GL_TEXTURE_WRAP_S:case GL_TEXTURE_WRAP_T:case GL_TEXTURE_WRAP_R:
    case GL_TEXTURE_MIN_FILTER:case GL_TEXTURE_MAG_FILTER:
    case GL_TEXTURE_MIN_LOD:case GL_TEXTURE_MAX_LOD:case 0x8501: /* LOD bias */
    case 0x884c:case 0x884d: /* compare mode/function */
    case 0x84fe: /* anisotropy, if the native profile supports it */
    case 0x8a48: /* TEXTURE_SRGB_DECODE_EXT; negotiated separately */
        return 1;
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
/* Restart compares the original index. BaseVertex uses 32-bit unsigned
 * wrapping after a nonnegative sum, independently of the source index size. */
static uint64_t DGBaseVertexSpan(const void *indices,uint32_t count,unsigned size,
    int big_endian,int32_t base,int restart,uint32_t marker)
{
    uint64_t high=0;
    const unsigned char *p=indices;
    if(size!=1 && size!=2 && size!=4)return UINT64_MAX;
    for(uint32_t i=0;i<count;++i,p+=size){
        uint32_t raw=0;
        if(big_endian){for(unsigned j=0;j<size;++j)raw=(raw<<8)|p[j];}
        else memcpy(&raw,p,size);
        if(restart && raw==marker)continue;
        int64_t adjusted=(int64_t)raw+base;
        if(adjusted<0)return UINT64_MAX;
        uint64_t end=(uint64_t)(uint32_t)adjusted+1;
        if(end>high)high=end;
    }
    return high;
}
/* Unknown Get pnames must not be guessed scalar: that would overwrite a
 * caller's allocation for vector results. Expand from audited API contracts. */
static size_t DGPatchLevelCount(uint32_t key)
{
    return key==0x8e73 ? 2 : key==0x8e74 ? 4 : 0;
}
static size_t DGAdditionalStateCount(uint32_t key)
{
    switch(key){
#define DG_ADDITIONAL_QUERY(name,count) case name:return count;
#include "additional-state-query-counts.inc"
#undef DG_ADDITIONAL_QUERY
    default:return 0;
    }
}
static size_t DGGetCount(uint32_t key)
{
    size_t additional=DGAdditionalStateCount(key);if(additional)return additional;
    if(key==0x1700 || key==0x850a || (key>=0x8722 && key<=0x873f))return 16; /* MODELVIEWn_ARB */
    switch(key) {
#define DG_SERVER_QUERY(name,count) case name:return count;
#include "server-state-query-counts.inc"
#undef DG_SERVER_QUERY
    case 0x0ba6:case 0x0ba7:case 0x0ba8:case 0x0b89:
    case 0x8641:case 0x88b7:return 16; /* matrices, ARB current/transpose */
    case 0x0ba2:case 0x0c10:case 0x0c22:case 0x0c23:case 0x0b00:
    case 0x0b03:case 0x0b04:case 0x0b53:case 0x0b66:
    case GL_CURRENT_RASTER_TEXTURE_COORDS:case GL_CURRENT_RASTER_POSITION:return 4;
    case 0x0b70:case 0x0d3a:case 0x0b12:case 0x0b22:case 0x846d:case 0x846e:case 0x825d:return 2;
    case 0x8e73:case 0x8e74:return DGPatchLevelCount(key);
    case 0x0ba0:case 0x0ba1:case 0x0be2:case 0x0b71:case 0x0b90:case 0x0b44:
    case 0x0c00:case 0x0c01:case 0x0c02:case 0x0c11:case 0x0d05:
    case 0x0cf5:case 0x0cf2:case 0x0cf3:case 0x0cf4:case 0x0d02:case 0x0d03:case 0x0d04:
    case 0x8f9d:case 0x8f9e: /* PRIMITIVE_RESTART, PRIMITIVE_RESTART_INDEX */
    case 0x8e72:case 0x8e7d:case 0x8e7e:case 0x8e7f:case 0x8e80:case 0x8e81:case 0x8e82:
    case 0x8e83:case 0x8e84:case 0x8e85:case 0x8e86:case 0x8e89:case 0x8e8a:
    case 0x886c:case 0x886d:case 0x8e1e:case 0x8e1f: /* patch vertices and tessellation limits */
    case 0x8e51:case 0x8e59: /* SAMPLE_MASK, MAX_SAMPLE_MASK_WORDS */
    case 0x9104:case 0x9105: /* multisample texture bindings */
    case 0x910e:case 0x910f:case 0x9110: /* color/depth/integer texture sample limits */
    case 0x88fc: /* MAX_DUAL_SOURCE_DRAW_BUFFERS */
    case 0x825a: /* PROGRAM_PIPELINE_BINDING */
    case 0x825b:case 0x825c:case 0x825e:case 0x825f: /* viewport limits and provoking vertices */
    case 0x8904:case 0x8e5e:case 0x8e5b: /* scalar minimum texel/gather/interpolation offsets */
    case 0x85b2:case 0x8a15:case 0x8a16:
    case 0x0d33:case 0x8069:case 0x8068:case 0x806a:case 0x84f6:case 0x8514:
    case 0x84e0:case 0x84e1:case 0x84e2:case 0x84f8:case 0x8872:case 0x8b4d:case 0x8b8d:
    case 0x8894:case 0x8895:case 0x88ed:case 0x88ef:
    case 0x8c89:case 0x8c8f: /* RASTERIZER_DISCARD, TRANSFORM_FEEDBACK_BUFFER_BINDING */
    case 0x8e23:case 0x8e24: /* transform-feedback paused, active */
    case 0x8ca6:case 0x8caa:case 0x8ca7:case 0x0d56:case 0x0d57:return 1;
    case GL_RED_BITS:case GL_GREEN_BITS:case GL_BLUE_BITS:case GL_ALPHA_BITS:return 1;
    case GL_SUBPIXEL_BITS:case GL_CURRENT_RASTER_POSITION_VALID:
    case GL_CURRENT_RASTER_INDEX:case GL_CURRENT_RASTER_DISTANCE:return 1;
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
    case GL_SELECTION_BUFFER_SIZE:case GL_FEEDBACK_BUFFER_SIZE:case GL_FEEDBACK_BUFFER_TYPE:
    case GL_LIST_BASE:case GL_LIST_INDEX:case GL_LIST_MODE:
    case GL_ATTRIB_STACK_DEPTH:case GL_MODELVIEW_STACK_DEPTH:
    case 0x8640:case 0x864b:case 0x8804: /* ARB program stack/error/enabled */
    case GL_PROJECTION_STACK_DEPTH:case GL_TEXTURE_STACK_DEPTH:return 1;
    case 0x821b:case 0x821c:case 0x821d:case 0x821e:case 0x9126:return 1; /* core version, extensions, flags/profile */
    case 0x85b5:return 1; /* VERTEX_ARRAY_BINDING(_APPLE) */
    case 0x8e28:return 1; /* TIMESTAMP */
    case 0x8a34:return 1; /* UNIFORM_BUFFER_OFFSET_ALIGNMENT */
    case 0x8df9:case 0x8dfa:return 1; /* NUM_SHADER_BINARY_FORMATS, SHADER_COMPILER */
    case 0x87fe:return 1; /* NUM_PROGRAM_BINARY_FORMATS */
    case 0x8c36:case 0x8c37:return 1; /* SAMPLE_SHADING, MIN_SAMPLE_SHADING_VALUE */
    case 0x8919:return 1; /* SAMPLER_BINDING */
    case 0x86a4:case 0x86a5:case 0x86a6:case 0x86a7:return 1; /* ARB vertex blending */
    case 0x8de2:case 0x8de3:case 0x8de4:case 0x8ded:case 0x8def:return 1; /* EXT bindable uniform */
    case 0x8a26:case 0x8a27:return 1; /* APPLE per-vertex point size */
    case 0x8a10:return 2; /* MIN_PBUFFER_VIEWPORT_DIMS_APPLE */
    default:break;
    }
    switch(key) {
#include "direct-get-counts.inc"
    default:return 0;
    }
}
static uint32_t DGStateArrayCountKey(uint32_t key)
{
    return key==0x8df8 ? 0x8df9 : key==0x87ff ? 0x87fe : key==0x86a8 ? 0x86a4 :
        key==0x86a3 ? 0x86a2 : 0;
}
static size_t DGStateGetBytes(uint32_t key,int32_t binary_count,size_t element)
{
    if(DGStateArrayCountKey(key))return DGQueryArrayBytes(binary_count,element);
    size_t n=DGGetCount(key);
    return n ? n*element : SIZE_MAX;
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
/* Historical pre-expansion object-query shapes, for recognizing old peers'
 * one-byte dummy packets only. New queries use their actual scalar/vector
 * contract and leave enum validation to native GL. Do not reuse as a list
 * of supported shader/texture/framebuffer pnames. */
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
    case 0x8741:case 0x8257: /* PROGRAM_BINARY_LENGTH, PROGRAM_BINARY_RETRIEVABLE_HINT */
    case GL_SHADER_TYPE: /* glGetShaderiv: one GLint, used before shader translation. */
    case GL_ACTIVE_ATTRIBUTES:case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
    case 0x8c76:case 0x8c7f:case 0x8c83: /* transform-feedback name length, mode, count */
    case 0x8a35:case 0x8a36: /* active uniform block name length and count */
    case 0x8cd0:case 0x8cd1:case 0x8cd2:case 0x8cd3:
    case 0x8d42:case 0x8d43:case 0x8d44:case 0x8d50:case 0x8d51:case 0x8d52:return 1;
    default:return 0; /* generator rejects zero/unknown before calling host */
    }
}
static GLenum DGPixelMapSizeKey(GLenum map)
{
    return map>=GL_PIXEL_MAP_I_TO_I && map<=GL_PIXEL_MAP_A_TO_A ?
        GL_PIXEL_MAP_I_TO_I_SIZE+map-GL_PIXEL_MAP_I_TO_I : 0;
}
static int DGPixelMapCountValid(GLenum map,GLsizei count,GLint maximum)
{
    /* Only index-addressed maps (I_TO_I through I_TO_A) require 2^n. */
    return count>0 && count<=maximum &&
        (map>=GL_PIXEL_MAP_R_TO_R || !(count&(count-1)));
}
static size_t DGPixelSize(uint32_t format,uint32_t type)
{
    /* APPLE_ycbcr_422: both 2vuy/yuvs orderings use two bytes per pixel,
     * not two independently stored components. Native GL does conversion. */
    if(type==0x85ba || type==0x85bb)return format==0x85b9 ? 2 : 0;
    /* Packed encodings have one storage unit per pixel; the actual native
     * call still validates whether format/type is a legal pairing. */
    switch(type) {
    case 0x8032:case 0x8362:return 1; /* 3_3_2 and 2_3_3_REV */
    case 0x8035:case 0x8367:case 0x8036:case 0x8368:case 0x84fa:
    case 0x8c3b:case 0x8c3e:return 4; /* includes 10F_11F_11F and 5_9_9_9 */
    case 0x8363:case 0x8364:case 0x8033:case 0x8365:case 0x8034:case 0x8366:return 2;
    case 0x8dad:return 8; /* FLOAT_32_UNSIGNED_INT_24_8_REV */
    }
    size_t components=0,bytes=0;
    switch(format) {
    case 0x1900:case 0x1901:case 0x1902:case 0x1903:case 0x1904:case 0x1905:case 0x1906:case 0x1909:
    case 0x8d94:case 0x8d95:case 0x8d96:case 0x8d97:case 0x8d9c:components=1;break;
    case 0x190a:case 0x8227:case 0x8228:case 0x8d9d:components=2;break;
    case 0x1907:case 0x80e0:case 0x8d98:case 0x8d9a:components=3;break;
    case 0x1908:case 0x80e1:case 0x8000:case 0x8d99:case 0x8d9b:components=4;break;
    }
    switch(type) {
    case 0x1400:case 0x1401:bytes=1;break;
    case 0x1402:case 0x1403:case 0x140b:bytes=2;break;
    case 0x1404:case 0x1405:case 0x1406:bytes=4;break;
    }
    return components*bytes;
}
static size_t DGRasterPixelBits(GLenum format,GLenum type)
{
    if(type==GL_BITMAP)return format==GL_COLOR_INDEX || format==GL_STENCIL_INDEX ? 1 : 0;
    return 8*DGPixelSize(format,type);
}
/* These DrawPixels inputs cannot consume pixel memory. Do not fetch a guest
 * pointer before native GL rejects them, including during list playback.
 * This is not a general packed-texture rule: integer TexImage formats have
 * different legal combinations. Native GL still owns the actual error. */
static int DGDrawPixelsNoInput(GLsizei width,GLsizei height,GLenum format,GLenum type)
{
    if(width<0 || height<0 || !DGRasterPixelBits(format,type))return 1;
    switch(type){
    case 0x8032:case 0x8362:case 0x8363:case 0x8364:
        return format!=GL_RGB;
    case 0x8033:case 0x8365:case 0x8034:case 0x8366:
    case 0x8035:case 0x8367:case 0x8036:case 0x8368:
        return format!=GL_RGBA && format!=GL_BGRA;
    default:return 0;
    }
}
/* Compute the final byte touched, including a partially used bitmap byte.
 * A PBO does not carry image data on the wire and is not packet-size limited. */
static uint64_t DGPixelLayoutSpan(int width,int height,size_t bits,
    int row,int alignment,int skiprows,int skippixels,int rowbytes)
{
    if(!bits || width<0 || height<0 || row<0 || skiprows<0 || skippixels<0 || rowbytes<0 ||
       (alignment!=1 && alignment!=2 && alignment!=4 && alignment!=8))return UINT64_MAX;
    if(!width || !height)return 0;
    uint64_t row_size=((uint64_t)(row?row:width)*bits+7)/8;
    uint64_t stride=rowbytes?(uint64_t)rowbytes:((row_size+alignment-1)/alignment)*alignment;
    uint64_t rows=(uint64_t)skiprows+height-1;
    uint64_t tail=(((uint64_t)skippixels+width)*bits+7)/8;
    if(rows && stride>(UINT64_MAX-tail)/rows)return UINT64_MAX;
    return rows*stride+tail;
}
/* Touched bytes only; bitmap skips can begin inside the first touched byte.
 * Native LSB_FIRST/SWAP_BYTES remain in force, not emulated conversions. */
struct DGRasterLayout {uint64_t span,offset,stride,row_bytes;unsigned rows,bit_offset;};
static struct DGRasterLayout DGRasterLayout(int width,int height,size_t bits,
    int row,int alignment,int skiprows,int skippixels,int rowbytes)
{
    struct DGRasterLayout l={UINT64_MAX,0,0,0,0,0};
    l.span=DGPixelLayoutSpan(width,height,bits,row,alignment,skiprows,skippixels,rowbytes);
    if(l.span==UINT64_MAX || !l.span)return l;
    uint64_t row_size=((uint64_t)(row?row:width)*bits+7)/8;
    l.stride=rowbytes?(uint64_t)rowbytes:((row_size+alignment-1)/alignment)*alignment;
    l.offset=(uint64_t)skiprows*l.stride+(uint64_t)skippixels*bits/8;
    l.bit_offset=(unsigned)((uint64_t)skippixels*bits%8);
    l.row_bytes=((uint64_t)width*bits+l.bit_offset+7)/8;l.rows=(unsigned)height;
    return l;
}

enum DGImagingTransfer {
    DG_IMAGING_TABLE, DG_IMAGING_SUBTABLE, DG_IMAGING_GET_TABLE,
    DG_IMAGING_FILTER1D, DG_IMAGING_FILTER2D, DG_IMAGING_GET_FILTER,
    DG_IMAGING_SEPARABLE, DG_IMAGING_GET_SEPARABLE, DG_IMAGING_GET_HISTOGRAM,
    DG_IMAGING_GET_MINMAX,
};

static int DGImagingProxy(GLenum target)
{
    return target==GL_PROXY_COLOR_TABLE || target==GL_PROXY_POST_CONVOLUTION_COLOR_TABLE ||
        target==GL_PROXY_POST_COLOR_MATRIX_COLOR_TABLE;
}

static int DGImagingTargetValid(enum DGImagingTransfer op,GLenum target)
{
    switch(op) {
    case DG_IMAGING_TABLE:
        if(DGImagingProxy(target))return 1;
        /* fall through */
    case DG_IMAGING_SUBTABLE:case DG_IMAGING_GET_TABLE:
        return target==GL_COLOR_TABLE || target==GL_POST_CONVOLUTION_COLOR_TABLE ||
            target==GL_POST_COLOR_MATRIX_COLOR_TABLE;
    case DG_IMAGING_FILTER1D:return target==GL_CONVOLUTION_1D;
    case DG_IMAGING_FILTER2D:return target==GL_CONVOLUTION_2D;
    case DG_IMAGING_GET_FILTER:return target==GL_CONVOLUTION_1D || target==GL_CONVOLUTION_2D;
    case DG_IMAGING_SEPARABLE:case DG_IMAGING_GET_SEPARABLE:return target==GL_SEPARABLE_2D;
    case DG_IMAGING_GET_HISTOGRAM:return target==GL_HISTOGRAM;
    case DG_IMAGING_GET_MINMAX:return target==GL_MINMAX;
    }
    return 0;
}

static size_t DGImagingPixelBits(GLenum format,GLenum type)
{
    if((format>=GL_RED && format<=GL_LUMINANCE_ALPHA) ||
       format==GL_BGR || format==GL_BGRA || format==0x8000 /* ABGR_EXT */)
        return 8*DGPixelSize(format,type);
    return 0;
}

/* Use the same native state queries at the guest wrapper and executor.
 * Size is not supplied by the guest for an existing native table/filter. */
static int DGImagingDimensions(enum DGImagingTransfer op,GLenum target,GLint *width,GLint *height)
{
    *width=-1;*height=1;
    switch(op) {
    case DG_IMAGING_SUBTABLE:case DG_IMAGING_GET_TABLE:
        glGetColorTableParameteriv(target,GL_COLOR_TABLE_WIDTH,width);break;
    case DG_IMAGING_GET_FILTER:case DG_IMAGING_GET_SEPARABLE:
        glGetConvolutionParameteriv(target,GL_CONVOLUTION_WIDTH,width);
        if(*width>=0 && target!=GL_CONVOLUTION_1D){
            *height=-1;glGetConvolutionParameteriv(target,GL_CONVOLUTION_HEIGHT,height);
        }
        break;
    case DG_IMAGING_GET_HISTOGRAM:glGetHistogramParameteriv(target,GL_HISTOGRAM_WIDTH,width);break;
    case DG_IMAGING_GET_MINMAX:*width=2;break;
    default:return 0;
    }
    return *width>=0 && *height>=0;
}

static void DGImagingSpans(enum DGImagingTransfer op,GLint width,GLint height,size_t bits,
    GLint row,GLint alignment,GLint skiprows,GLint skippixels,GLint rowbytes,uint64_t spans[2])
{
    spans[1]=0;
    if(op==DG_IMAGING_GET_MINMAX){spans[0]=2*(bits/8);return;}
    if(op==DG_IMAGING_GET_HISTOGRAM)skiprows=0;
    if(op==DG_IMAGING_SEPARABLE || op==DG_IMAGING_GET_SEPARABLE) {
        spans[1]=DGPixelLayoutSpan(height,1,bits,row,alignment,skiprows,skippixels,rowbytes);
        height=1;
    }
    spans[0]=DGPixelLayoutSpan(width,height,bits,row,alignment,skiprows,skippixels,rowbytes);
}
static struct DGRasterLayout DGImagingRasterLayout(enum DGImagingTransfer op,GLint width,GLint height,size_t bits,
    GLint row,GLint alignment,GLint skiprows,GLint skippixels,GLint rowbytes,unsigned image)
{
    if(image && op!=DG_IMAGING_SEPARABLE && op!=DG_IMAGING_GET_SEPARABLE){
        struct DGRasterLayout empty={0};return empty;
    }
    if(op==DG_IMAGING_GET_MINMAX)return DGRasterLayout(2,1,bits,0,1,0,0,0);
    if(op==DG_IMAGING_GET_HISTOGRAM)skiprows=0;
    if(op==DG_IMAGING_SEPARABLE || op==DG_IMAGING_GET_SEPARABLE){width=image?height:width;height=1;}
    return DGRasterLayout(width,height,bits,row,alignment,skiprows,skippixels,rowbytes);
}
static uint64_t DGPixelSpanWide(int width,int height,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes)
{
    return DGPixelLayoutSpan(width,height,DGRasterPixelBits(format,type),
        row,alignment,skiprows,skippixels,rowbytes);
}
/* Last touched byte, not a full padded final image. Check products before
 * multiplying: legal GL integer states can exceed uint64_t in three axes. */
static uint64_t DGPixelSpan3DWide(int width,int height,int depth,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes,int imageheight,int skipimages)
{
    if(depth<0 || imageheight<0 || skipimages<0)return UINT64_MAX;
    uint64_t bytes=DGPixelSpanWide(width,height,format,type,row,alignment,skiprows,skippixels,rowbytes);
    if(bytes==UINT64_MAX || !width || !height || !depth)return bytes==UINT64_MAX ? UINT64_MAX : 0;
    uint64_t bits=DGRasterPixelBits(format,type);
    uint64_t stride=rowbytes ? (uint64_t)rowbytes :
        (((((uint64_t)(row ? row : width)*bits+7)/8)+alignment-1)/alignment)*alignment;
    uint64_t rows=(uint64_t)(imageheight ? imageheight : height);
    uint64_t images=(uint64_t)skipimages+(uint64_t)depth-1;
    if(images && (rows>UINT64_MAX/images || stride>UINT64_MAX/(images*rows)))return UINT64_MAX;
    uint64_t prefix=images*rows*stride;
    return prefix>UINT64_MAX-bytes ? UINT64_MAX : prefix+bytes;
}
static size_t DGPixelSpan(int width,int height,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes)
{
    uint64_t span=DGPixelSpanWide(width,height,format,type,row,alignment,skiprows,skippixels,rowbytes);
    return span>DG_MAX_BYTES?SIZE_MAX:(size_t)span;
}
static size_t DGPixelSpan3D(int width,int height,int depth,uint32_t format,uint32_t type,
    int row,int alignment,int skiprows,int skippixels,int rowbytes,int imageheight,int skipimages)
{
    uint64_t span=DGPixelSpan3DWide(width,height,depth,format,type,row,alignment,skiprows,skippixels,rowbytes,imageheight,skipimages);
    return span>DG_MAX_BYTES?SIZE_MAX:(size_t)span;
}
#endif
