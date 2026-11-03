/* Fixed-width publication metadata; no guest/host addresses or FBO names. */
#ifndef VIM_DIRECT_IOSURFACE_WRITES_H
#define VIM_DIRECT_IOSURFACE_WRITES_H
#include <stdint.h>
struct DGIosurfaceWrite { uint32_t texture,reserved;uint64_t serial; };
#define DG_IOSURFACE_INLINE_WRITES 256u
/* The bound is the wire payload size, not a maximum number of live images.
 * Ordinary contexts keep their small stack buffer; larger real workloads
 * allocate only as many records as their actual imported image set needs. */
#define DG_IOSURFACE_WRITE_LIMIT ((64u*1024u*1024u)/sizeof(struct DGIosurfaceWrite))
_Static_assert(sizeof(struct DGIosurfaceWrite)==16,"fixed IOSurface write record");
#endif
