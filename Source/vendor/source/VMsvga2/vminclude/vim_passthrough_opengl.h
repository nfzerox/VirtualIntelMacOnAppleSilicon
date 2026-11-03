/* Private opt-in research ABI. User requests contain owning-task virtual
 * addresses; QEMU sees VRAM offsets or prepared guest-physical RAM addresses,
 * never task/host virtual pointers. */
#ifndef VIM_PASSTHROUGH_OPENGL_DEVICE_H
#define VIM_PASSTHROUGH_OPENGL_DEVICE_H
#include <stdint.h>
#define VIM_GL_REGISTER 0x1000u
#define VIM_GL_CAPABILITIES_REGISTER 0x1001u
#define VIM_GL_WRITE_CAPABILITIES_REGISTER 0x1002u
#define VIM_GL_WRITE_MAGIC 0x56475731u
#define VIM_GL_SCATTER_CAPABILITIES_REGISTER 0x1003u
#define VIM_GL_SCATTER_MAGIC 0x56475331u
#define VIM_GL_RAM_SCATTER_CAPABILITIES_REGISTER 0x1004u
#define VIM_GL_RAM_SCATTER_MAGIC 0x56475231u
#define VIM_GL_FRAMEBUFFER_BLIT_CAPABILITIES_REGISTER 0x1005u
#define VIM_GL_FRAMEBUFFER_BLIT_MAGIC 0x56474631u
#define VIM_GL_RAM_UPLOAD_CAPABILITIES_REGISTER 0x1006u
#define VIM_GL_RAM_UPLOAD_MAGIC 0x56475531u
#define VIM_GL_MASKED_SCATTER_CAPABILITIES_REGISTER 0x1011u
#define VIM_GL_MASKED_SCATTER_MAGIC 0x56474d31u
#define VIM_GL_EXCHANGE_CAPABILITIES_REGISTER 0x1012u
#define VIM_GL_EXCHANGE_MAGIC 0x56475831u
#define VIM_GL_EXCHANGE_LIMIT 4096u
#define VIM_GL_LARGE_EXCHANGE_CAPABILITIES_REGISTER 0x1016u
#define VIM_GL_LARGE_EXCHANGE_MAGIC 0x56475832u
/* Larger request only; replies retain the4KiB bound. Explicitly negotiated
 * through the device/provider, never attempted and replayed on failure. */
#define VIM_GL_LARGE_EXCHANGE_LIMIT 65536u
#define VIM_GL_BULK_UPLOAD_CAPABILITIES_REGISTER 0x1013u
#define VIM_GL_BULK_UPLOAD_MAGIC 0x56474231u
#define VIM_GL_DOORBELL_CAPABILITIES_REGISTER 0x1014u
#define VIM_GL_DOORBELL_MAGIC 0x56474442u
#define VIM_GL_NOTIFY_CAPABILITIES_REGISTER 0x1015u
#define VIM_GL_NOTIFY_MAGIC 0x56474e31u
#define VIM_GL_NOTIFY_STATUS_PORT 16u
#define VIM_GL_NOTIFY_ENABLE_PORT 20u
/* Write-only, independently addressed command port in the existing I/O BAR.
 * Same VRAM packet/handler as VIM_GL_REGISTER; never changes the index latch.
 * Negotiate first. Older devices keep the existing indexed-write route. */
#define VIM_GL_DOORBELL_PORT 12u
#define VIM_GL_FRAMEBUFFER_BLIT_MAX_SEGMENTS (VIM_GL_CHUNK/24u)
#define VIM_GL_RESULT_CAP_SCATTER 4u
#define VIM_GL_RESULT_CAP_RAM_SCATTER 8u
#define VIM_GL_RESULT_CAP_MASKED_SCATTER 16u
#define VIM_GL_RESULT_CAP_ROW_UPLOAD 32u
#define VIM_GL_RESULT_CAP_EXCHANGE 64u
#define VIM_GL_RESULT_CAP_EXCHANGE_TRAP 128u
#define VIM_GL_RESULT_CAP_DRAINED_CLOSE 256u
#define VIM_GL_RESULT_CAP_LARGE_EXCHANGE 512u
#define VIM_GL_RESULT_CAP_ROW_UPLOAD_TRANSACTION 1024u
#define VIM_GL_EXCHANGE_TRAP 0u
#define VIM_GL_SCATTER_MAX_RANGES 2048u
#define VIM_GL_RAM_SCATTER_MAX_RANGES 16384u
#define VIM_GL_RAM_SCATTER_MAX_ROWS 8192u
#define VIM_GL_MASK_MAX_SPANS 4096u
#define VIM_GL_SCATTER_MAX_BYTES (64u*1024u*1024u)
#define VIM_GL_CAP_DIRECT_READ 1u
#define VIM_GL_RESULT_DIRECT_READ 1u
#define VIM_GL_RESULT_DIRECT_WRITE 2u
#define VIM_GL_MAGIC 0x56474c31u
#define VIM_GL_CHUNK (256u*1024u)
struct VIMGLTransfer { uint32_t action,bytes;uint64_t address; };
struct VIMGLResult { int32_t count;uint32_t reserved; };
/* Appended small-command exchange. In-band owning-client bytes only: no
 * pointers, page pinning or pixel-sized allocation. Exactly one reply span. */
struct VIMGLExchange { uint32_t version,send_bytes,receive_bytes,reserved; };
typedef char VIMGLExchangeSize[(sizeof(struct VIMGLExchange)==16)?1:-1];
struct VIMGLDevicePacket { uint32_t magic,action,token,bytes;int32_t result;uint32_t reserved; };
/* Prepare 0 -> receive 1 -> release 2, masked prepare 3, or upload prepare 4
 * -> send 5 -> release 2. Tokens belong to this connection, not GL.
 * Prepared task ranges stay pinned until release, including stream failure.
 * Negotiated action 6 combines upload prepare, an in-band command prefix,
 * send and release in one synchronous call. It never exports a live token. */
struct VIMGLScatterRequest { uint32_t action,count,token,bytes; };
struct VIMGLScatterRows { uint64_t address,stride;uint32_t row_bytes,reverse; };
struct VIMGLScatterUpload { struct VIMGLScatterRows rows;uint32_t prefix_bytes,reserved; };
#define VIM_GL_UPLOAD_PREFIX_LIMIT 4096u
/* Action 3 prepares sorted, non-overlapping stream spans. Offsets refer to
 * the packed read, not task/physical memory. Gaps are consumed natively and
 * never written to guest storage. Pin the bounded destination span once.
 * count is the number of owning-task VIMGLScatterSpan records at spans_address.
 * The driver snapshots them before validation. This fixed-size request also
 * works through Mavericks' legacy in-band IOUserClient dispatch. */
struct VIMGLScatterMask { struct VIMGLScatterRows rows;uint32_t height,reserved;uint64_t spans_address; };
struct VIMGLScatterSpan { uint32_t offset,bytes; };
struct VIMGLScatterResult { int32_t count;uint32_t token; };
struct VIMGLScatterSegment { uint32_t offset,bytes; };
/* Device actions 8 (receive) and 10 (upload): kind 0 is a VRAM offset;
 * kind 1 is a prepared guest
 * physical RAM address, NOT a task/host virtual address. QEMU validates the
 * complete region and holds a direct DMA map until completion: writable
 * for receives, read-only for uploads (no source dirtying/invalidation). */
struct VIMGLScatterMemorySegment { uint64_t address;uint32_t bytes,kind; };
/* Action 11 extends receive only: kind 2 discards bytes, address must be 0.
 * Old receive/upload actions continue rejecting it. Explicitly negotiated. */
/* Device action 9, token 0: copy prepared backing rows into the visible
 * framebuffer. All entries are validated before ANY destination is written.
 * kind 0 = VRAM offset, 1 = prepared physical RAM. No stream/GL context. */
struct VIMGLFramebufferSegment { uint64_t source;uint32_t destination,bytes,kind,reserved; };
/* Research query/control: 0=query, 1/2=blit off/on, 3/4=RAM upload off/on,
 * 5/6=negotiated bulk upload off/on, 7/8=negotiated doorbell off/on.
 * flags bit 0=blit enabled, 1=blit supported, 2=RAM upload enabled,
 * 3=bulk enabled, 4=bulk supported, 5=doorbell supported, 6=doorbell enabled. */
struct VIMGLFramebufferStats { uint32_t version,flags;uint64_t blits,bytes,fallbacks; };
typedef char VIMGLFramebufferSegmentSize[(sizeof(struct VIMGLFramebufferSegment)==24)?1:-1];
typedef char VIMGLFramebufferStatsSize[(sizeof(struct VIMGLFramebufferStats)==32)?1:-1];
typedef char VIMGLScatterHeaderSize[(sizeof(struct VIMGLScatterRequest)==16)?1:-1];
typedef char VIMGLScatterRowsSize[(sizeof(struct VIMGLScatterRows)==24)?1:-1];
typedef char VIMGLScatterUploadSize[(sizeof(struct VIMGLScatterUpload)==32)?1:-1];
typedef char VIMGLScatterMaskSize[(sizeof(struct VIMGLScatterMask)==40)?1:-1];
typedef char VIMGLScatterSpanSize[(sizeof(struct VIMGLScatterSpan)==8)?1:-1];
typedef char VIMGLScatterResultSize[(sizeof(struct VIMGLScatterResult)==8)?1:-1];
typedef char VIMGLScatterSegmentSize[(sizeof(struct VIMGLScatterSegment)==8)?1:-1];
typedef char VIMGLScatterMemorySegmentSize[(sizeof(struct VIMGLScatterMemorySegment)==16)?1:-1];
typedef char VIMGLScatterTableFits[(VIM_GL_RAM_SCATTER_MAX_RANGES*sizeof(struct VIMGLScatterMemorySegment)<=VIM_GL_CHUNK)?1:-1];
/* Device action 5 receives directly into reserved=validated VRAM offset.
 * Device action 6 sends directly from the same validated VRAM range.
 * Device action 7 receives into a validated list of VRAM segments. Its
 * reserved field is the segment count, and bytes is the complete stream.
 * Never a task pointer or host address. A prepared owning-task descriptor
 * must resolve each reported segment to the actual PCI VRAM range. */
#endif
