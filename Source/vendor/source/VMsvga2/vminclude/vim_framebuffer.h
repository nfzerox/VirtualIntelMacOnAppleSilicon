/* Virtual Intel Mac research framebuffer layout ABI (guest-local, x86 LE).
 * This reports actual mode geometry, not an acceleration capability. */
#ifndef VIM_FRAMEBUFFER_CONTRACT_H
#define VIM_FRAMEBUFFER_CONTRACT_H
#include "vim_gl_pixel_formats.h"
#define VIM_FRAMEBUFFER_LAYOUT_KEY "VIMFramebufferLayout"
#define VIM_FRAMEBUFFER_LAYOUT_VERSION 1U
struct VIMFramebufferLayout {
    uint32_t version,width,height,row_bytes,offset,bits_per_pixel;
};
/* A locked CGS surface mapping in the calling task, valid until UnlockMemory.
 * Fixed-width fields keep the kernel/user ABI identical for 32/64-bit clients.
 * This is our explicit extension, not an assumed Apple GetSurfaceInfo layout. */
struct VIMSurfaceMapping {
    uint64_t address,length;
    struct VIMFramebufferLayout layout;
};
/* A metadata snapshot, NOT an exclusion lock or promise that no later writer
 * exists. object is IORegistryEntry's lifetime-unique ID, never a pointer or
 * reusable CGS number. incarnation changes on backing/layout replacement;
 * content_epoch changes on writes, not on a private read-only unlock.
 * Obtain through the 2D client that currently owns LockSurfaceBacking. */
#define VIM_SURFACE_STATE_VERSION 1U
#define VIM_SURFACE_STATE_CPU_WRITER 1U
#define VIM_SURFACE_STATE_CONTEXT_LOCKED 2U
#define VIM_SURFACE_STATE_GPU_NEWER 4U
#define VIM_SURFACE_STATE_GPU_PENDING 8U // future epoch; CPU access not yet current
#define VIM_SURFACE_STATE_GPU_FUTURE_SEALED 16U // exact immutable host image belongs to pending ticket
#define VIM_SURFACE_UNLOCK_READ_ONLY 1U
struct VIMSurfaceState {
    uint32_t version,flags;
    uint64_t object,incarnation,content_epoch;
};
typedef char VIMSurfaceStateSizeCheck[sizeof(struct VIMSurfaceState)==32 ? 1 : -1];
/* Explicit host-shared VRAM capability geometry. Query only while owning the
 * CPU backing lock; object/incarnation/epoch are sampled with that exclusion.
 * offset is relative to the PCI VRAM BAR, never a guest/host virtual pointer.
 * GMR/client backings are deliberately unsupported by this contract. */
#define VIM_SURFACE_VRAM_VERSION 1U
struct VIMSurfaceVRAM {
    uint32_t version,width,height,row_bytes;
    uint64_t offset,length;
    struct VIMSurfaceState state;
};
typedef char VIMSurfaceVRAMSizeCheck[sizeof(struct VIMSurfaceVRAM)==64 ? 1 : -1];
/* A chunk of the prepared client/GMR backing's REAL guest physical pages.
 * Metadata-only first=UINT32_MAX; pages are fetched only on a host cache miss.
 * The fixed reply fits Mavericks' 4096-byte in-band IOKit bound in both widths. */
#define VIM_SURFACE_RAM_VERSION 2U
#define VIM_SURFACE_RAM_PAGE_CHUNK 256U
struct VIMSurfaceRAM {
    struct VIMSurfaceVRAM backing;
    uint32_t first,count,total,reserved;
    uint64_t pages[VIM_SURFACE_RAM_PAGE_CHUNK];
};
typedef char VIMSurfaceRAMSizeCheck[sizeof(struct VIMSurfaceRAM)==2128 ? 1 : -1];
/* Private scoped loan owned by the SAME 2D client as the rendering stream
 * and CPU backing lock. Requires negotiated drained-close support. Serial
 * is endpoint-local, never a pointer. A pin is not permission to unlock CPU
 * ownership: End follows completed host use, or Abort drains the endpoint. */
struct VIMSurfaceBackingLoan {
    struct VIMSurfaceVRAM backing;
    uint64_t serial;
};
typedef char VIMSurfaceBackingLoanSizeCheck[sizeof(struct VIMSurfaceBackingLoan)==72 ? 1 : -1];
#define VIM_RAM_LOAN_READ_ONLY 0U
#define VIM_RAM_LOAN_WRITTEN 1U
#define VIM_RAM_LOAN_ABORT 2U
struct VIMSurfaceBackingPending { uint64_t serial,ticket,namespace_id[2]; };
typedef char VIMSurfaceBackingPendingSize[sizeof(struct VIMSurfaceBackingPending)==32?1:-1];
/* Exact pending GPU image, usable ONLY under the endpoint's GPU-read loan.
 * Not a CPU mapping or a writable drawable. The completion job cannot retire
 * this ticket until all such loans have returned. */
struct VIMSurfaceFutureRead { struct VIMSurfaceState state;uint64_t ticket; };
typedef char VIMSurfaceFutureReadSize[sizeof(struct VIMSurfaceFutureRead)==40?1:-1];
/* Exclusive GPU writer claim. No CPU mapping or page-vector export on a
 * continuation. The previous ticket orders the last RAM writer on the GPU. */
struct VIMSurfaceWriterLoan {struct VIMSurfaceBackingLoan loan;uint64_t previous_ticket;};
typedef char VIMSurfaceWriterLoanSize[sizeof(struct VIMSurfaceWriterLoan)==80?1:-1];
/* Drawing view and allocation are different for WindowServer's primary
 * shadow: CGXGLAccelAccess sets a zero-origin viewport for buffer.w/h, not
 * for the full source allocation. Query under LockSurfaceBacking. */
#define VIM_SURFACE_GEOMETRY_VERSION 1U
struct VIMSurfaceGeometry {
    uint32_t version,source_width,source_height;
    int32_t x,y;
    uint32_t width,height,reserved;
};
/* Additive research contract: reserved==0 retains the CPU-shadow route.
 * Otherwise it is the CGS ID of the shared driver-owned GPU display root,
 * for both primary and companion views. The backing identity still comes
 * from the locked lifetime-unique state, never from this reusable CGS ID. */
typedef char VIMSurfaceGeometrySizeCheck[sizeof(struct VIMSurfaceGeometry)==32 ? 1 : -1];
/* Logical client-backed drawing rectangle, separate from padded allocation.
 * No visibility union or framebuffer resize is implied by this metadata. */
struct VIMSurfaceDrawingSize { uint32_t width,height; };
/* Original CGS regions are bounded to 4096 bytes by set_shape. This fixed
 * copy preserves every possible accepted rectangle without exporting kernel
 * pointers. Coordinates are relative to the full mapped source allocation. */
#define VIM_SURFACE_CLIP_VERSION 1U
#define VIM_SURFACE_CLIP_RECTS 510U
struct VIMSurfaceRect { int16_t x,y,w,h; };
struct VIMSurfaceClip {
    uint32_t version,count;
    int32_t origin_x,origin_y;
    struct VIMSurfaceRect rects[VIM_SURFACE_CLIP_RECTS];
};
/* Old IOExternalMethod structure replies must fit the 4096-byte inline
 * transport. Native 4096-byte regions hold at most 510 eight-byte rects. */
typedef char VIMSurfaceClipSizeCheck[sizeof(struct VIMSurfaceClip)==4096 ? 1 : -1];
#endif
