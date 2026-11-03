#ifndef VIM_PASSTHROUGH_OPENGL_SURFACE_REGIONS_H
#define VIM_PASSTHROUGH_OPENGL_SURFACE_REGIONS_H
#include <stdint.h>
/* Disjoint destination regions in one atomic content-version publication.
 * Source coordinates refer to the current local drawable, not guest RAM. */
struct DGSurfaceRegion { int32_t x,y,width,height,destination_x,destination_y; };
typedef char DGSurfaceRegionSize[(sizeof(struct DGSurfaceRegion)==24)?1:-1];
#define DG_SURFACE_REGION_LIMIT 256u
#endif
