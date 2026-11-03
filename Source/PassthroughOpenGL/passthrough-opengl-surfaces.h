/* Private retained-resource API. These are not GL names or guest pointers.
 * A resource key must identify a kernel surface and backing incarnation,
 * not a reusable CGS surface number. The connection's guest boot namespace
 * scopes that key. Leases belong to the current context; retain a consumer
 * lease until all of its texture aliases have been deleted/replaced.
 *
 * Storage is a complete BGRA8 rectangle resource; negotiated origin-zero
 * logical views may omit backing padding. Offset primary drawing views are
 * a different contract and are not implemented by a smaller texture extent. */
#ifndef VIM_PASSTHROUGH_OPENGL_SURFACES_H
#define VIM_PASSTHROUGH_OPENGL_SURFACES_H
#include <stdint.h>
#include <stddef.h>
#include "passthrough-opengl-surface-regions.h"
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
#include "direct-vram-contract.h"
/* Canonical BGRA8 bytes from an already CPU-locked guest IOSurface. Native
 * host binding preserves RGB sampling vs raw attachment-alpha semantics.
 * -1 means unsupported before transport, 0 failure, 1 completed import. */
int DGImportIOSurfaceTexture(unsigned internal,int width,int height,const void *pixels,size_t stride);
int DGImportIOSurfaceTextureFormat(unsigned internal,unsigned format,unsigned type,int width,int height,const void *pixels,size_t stride);
int DGSupportsVRAMDrawables(void);
int DGSupportsRAMDrawables(void);
/* Same-endpoint prepared pin and CPU exclusion, not a borrowed IOKit handle.
 * Begin: 1=active, -1=unsupported/untouched fallback, 0=hard failure.
 * End: 1=committed handoff, -1=no active scope. Ordinary state means completed
 * GPU use/CPU-current epoch; explicit research GPU_PENDING instead names the
 * future epoch while an independent kernel job still owns CPU exclusion.
 * 0=failure (never permits fallback/reuse on cancellation alone).
 * Only a full, unflipped single-buffer RAM drawable is currently eligible. */
int DGSupportsBackingRenderScopes(void);
int DGBeginBackingRender(uint32_t,const struct VIMFramebufferLayout *);
int DGEndBackingRender(void *,struct VIMSurfaceState *);
/* RAM import returns -2 for a metadata cache miss requiring pinned pages. */
int DGImportRAMDrawable(const struct VIMSurfaceVRAM *,int,int,int,int,int,const uint64_t *,unsigned);
int DGVRAMStats(uint64_t[4]); // imports, publishes, import bytes, published bytes
/* Caller holds the CPU surface lock through synchronous GPU completion.
 * 1=completed, -1=unsupported before touching storage, 0=hard failure.
 * No guest pointers or pixel payloads cross these transactions. */
int DGImportVRAMDrawable(const struct VIMSurfaceVRAM *,int,int,int,int,int);
int DGPublishVRAMDrawable(const struct VIMSurfaceVRAM *,int,int,int,int,int,const struct DGVRAMRect *,unsigned);
/* Import just completed CPU-copy rectangles, preserving earlier GPU drawing
 * elsewhere. Requires the SAME already imported shared backing, fresh locked
 * state and the negotiated patch capability. One completed GPU transaction;
 * no pixel payload or new page-list import. -1 means untouched fallback. */
int DGImportSharedDrawableRects(const struct VIMSurfaceVRAM *,int,int,int,int,int,const struct DGVRAMRect *,unsigned);
int DGSupportsSurfaceRegions(void);
/* Ordered, live snapshot: row length, alignment, buffer, skip rows/pixels,
 * swap bytes and LSB first. No state cache. Unsupported peers return false. */
int DGGetUnpackSnapshot(int32_t values[7]);
/* One ordered native Flush/Finish followed by the actual virtual draw FBO.
 * -1 unsupported (no command sent), 0 error, 1 valid same-call observation.
 * This is not a GL state cache and must not outlive binding changes. */
int DGFlushDrawableState(int finish,uint32_t *draw_framebuffer);
int DGPublishSurfaceRegions(uint32_t lease,const struct DGSurfaceRegion *regions,unsigned count,
    int flip,int back,uint64_t expected_epoch,uint64_t published_epoch);
/* Same pixels/epoch/fence contract as publication (or in-place copy) then
 * SealSurfaceVersion. One native submission on capable hosts, exact old
 * two-command fallback otherwise. This does NOT grant CPU ownership. */
int DGPublishSealedSurfaceRegions(uint32_t lease,const struct DGSurfaceRegion *regions,unsigned count,
    int flip,int back,uint64_t expected_epoch,uint64_t published_epoch,int copy);
/* Canonical surface-to-itself blit. All sources are from the same pre-copy
 * epoch, even when destinations overlap source regions. No CPU pixel path. */
int DGSupportsSurfaceCopies(void);
int DGCopySurfaceRegions(uint32_t lease,const struct DGSurfaceRegion *regions,unsigned count,
    uint64_t expected_epoch,uint64_t published_epoch);
uint32_t DGAcquireSurface(uint64_t resource,uint64_t incarnation,int width,int height);
uint32_t DGAcquireTypedSurface(uint64_t resource,uint64_t incarnation,int width,int height,unsigned format);
int DGSupportsTypedSurfaces(void);
int DGReadTypedDrawable(void *pixels,int width,int height,unsigned format);
int DGReleaseSurface(uint32_t lease);
int DGReleaseSurfaceInContext(void *context,uint32_t lease);
int DGSupportsSurfaceEpochs(void);
int DGSupportsSurfaceHolds(void);
int DGSupportsSurfaceViews(void);
int DGSupportsBackDrawableAliases(void);
int DGBindBackDrawableTexture(unsigned internal,int width,int height,int flip);
int DGRetireSurface(uint32_t lease);
/* Connection-owned resource hold, independent of a native CGL context. A
 * shared texture must retain storage even if its importing context dies
 * before another context first binds it. Handle is local, never on wire. */
void *DGCreateSurfaceHold(void *context,uint64_t object,uint64_t incarnation,int width,int height);
/* Seal in producer order, then retain that immutable version. An epoch of
 * zero requests the compatibility mutable allocation hold, never an implicit seal. */
int DGSealSurfaceVersion(uint32_t lease,uint64_t epoch);
/* Explicit full-display presentation of a sealed immutable version. Returns
 * a submission ticket, not GPU completion. Zero means unavailable/backpressure;
 * caller must preserve the display damage and retry or use its CPU fallback. */
uint64_t DGPresentSurfaceVersion(uint32_t lease,uint64_t epoch);
int DGCopySurfaceVersionTexture(uint32_t lease,uint64_t epoch,unsigned internal,int width,int height);
int DGSupportsSurfaceVersionTextures(void);
int DGSupportsBackingFutureTextures(void);
int DGSupportsBackingWriterScopes(void);
int DGSupportsBackingWriterScopesInContext(void*);
int DGCopyBackingFutureTexture(uint64_t ticket,const struct VIMSurfaceState *,unsigned internal,int width,int height);
void *DGCreateSurfaceVersionHold(void *context,uint64_t object,uint64_t incarnation,int width,int height,uint64_t epoch);
/* Independent restricted service connection. Does not create a GL context,
 * depend on the GL codec layout, or alter the thread's current GL context. */
void *DGCreateSurfaceReadHold(uint64_t object,uint64_t incarnation,int width,int height);
void DGReleaseSurfaceHold(void *hold);
/* Context-independent CPU materialization of a specific coherent version.
 * BGRA8 rows, same canonical orientation as the retained resource. Failure
 * leaves caller storage untouched. Never use this as a GPU presentation. */
int DGReadSurfaceHoldRect(void *hold,uint64_t epoch,int x,int y,int width,int height,void *pixels,size_t stride);
int DGSurfaceEpoch(uint32_t lease,uint64_t *epoch);
int DGUploadSurface(uint32_t lease,const void *pixels,int width,int height,unsigned stride,uint64_t epoch);
int DGPublishSurfaceEpoch(uint32_t lease,int flip,int back,uint64_t epoch);
/* Publish the virtual drawable's front (back=0) or back color buffer. Flip
 * once for an ordinary GL window's bottom-up -> canonical CGS row order. */
int DGPublishSurface(uint32_t lease,int flip,int back);
/* Bind live storage to the caller's bound nonzero RECTANGLE texture.
 * Rebinding that same texture after producer flush sees later publications;
 * no CPU upload or new CGLTexImageIOSurface2D call is required. */
int DGBindSurface(uint32_t lease,unsigned internal);
/* Logical origin-zero texture extent may be smaller than the allocation.
 * Requires negotiated view support; backing identity/epochs remain full size. */
int DGBindSurfaceView(uint32_t lease,unsigned internal,int width,int height);
int DGSupportsDrawableAliases(void);
/* Alias the current GPU front storage, in its local drawing-view coordinates.
 * This is not a global/kernel-surface import or a pixel snapshot. */
int DGBindDrawableTexture(unsigned internal,int width,int height);
/* Ordinary GL windows have bottom-up render storage but their private CGS
 * destination textures are top-down. Host-only GPU copies retain liveness. */
int DGSupportsFlippedDrawableAliases(void);
int DGSupportsCroppedFlippedDrawableAliases(void);
int DGBindFlippedDrawableTexture(unsigned internal,int width,int height);
int DGSupportsSurfaceDrawables(void);
/* Explicit shared drawables can publish identical-coordinate regions with
 * epoch validation. Separate negotiation keeps old peers on by-value copies. */
int DGSupportsSurfaceDrawablePublication(void);
/* Full canonical, single-buffered render target backed by this retained
 * resource itself. Requires matching dimensions and negotiated support. */
int DGAttachSurfaceDrawable(uint32_t lease);
int DGSupportsSurfaceRegionImport(void);
int DGImportSurfaceRegion(uint32_t lease,uint64_t epoch,int x,int y,int width,int height,int flip);
int DGSupportsSurfacePatchImport(void);
int DGImportSurfacePatch(uint32_t lease,uint64_t epoch,int x,int y,int width,int height,
    int destination_x,int destination_y,int flip);
/* Ordered CPU-to-GPU subrectangle, not a whole-drawable reimport. Preserve
 * all other GPU pixels, including commands queued before a 2D surface copy.
 * x/y use bottom-up GL drawable coordinates; top_down reverses source rows. */
int DGImportDrawablePatch(const void *pixels,int x,int y,int width,int height,
    unsigned stride,int top_down);
/* Tightly packed canonical BGRA8, bottom-up GL coordinates. Private reads
 * ignore and preserve application pack/PBO/color-transfer state. Callers
 * without negotiated support retain the existing full readback path. */
int DGSupportsDrawableReadRects(void);
int DGReadDrawableRect(void *pixels,int x,int y,int width,int height);
/* Numeric proof of the actual kernel transport route, not a capability
 * claim. Socket clients return false. Queries do not generate GL traffic. */
int DGReadTransferStats(uint64_t *direct_bytes,uint64_t *staged_bytes);
int DGRowUploadStats(uint64_t *payload_bytes,uint64_t *source_bytes);
/* Large wrapper inputs consumed without an intermediate guest heap image;
 * promoted replies/multi-span packets remain owned and writable. */
int DGBorrowedPacketStats(uint64_t *borrowed_bytes,uint64_t *promoted_bytes);
/* A prepared direct VRAM row transfer. Reverse reverses only destination
 * row order. 1=success, 0=transfer failure (possible prefix writes), -1=not
 * available before any read command was sent; caller can use its old path. */
int DGSupportsScatterReads(void);
int DGSupportsRAMScatterReads(void);
int DGReadDrawableRectRows(void *pixels,size_t stride,int reverse,int x,int y,int width,int height);
/* Sorted disjoint byte spans within that packed BGRA read. Gaps are drained
 * in native QEMU, without touching destination pixels/pitch holes. */
struct DGReadSpan { uint32_t offset,bytes; };
int DGSupportsMaskedScatterReads(void);
int DGReadDrawableRectMasked(void *pixels,size_t stride,int reverse,int x,int y,int width,int height,
    const struct DGReadSpan *spans,unsigned count);
#endif
