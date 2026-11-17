/* Optional guest-local READ-only metadata trap. No backing-lock acquisition, state
 * mutation, new graphics wire format or pointer escapes to the host. */
#ifndef VIM_SURFACE_QUERY_H
#define VIM_SURFACE_QUERY_H
#include "vim_framebuffer.h"
#define VIM_SURFACE_QUERY_TRAP 1U
enum { VIM_SURFACE_QUERY_PROBE=0, VIM_SURFACE_QUERY_GEOMETRY,
    VIM_SURFACE_QUERY_STATE, VIM_SURFACE_QUERY_DRAWING_SIZE,
    VIM_SURFACE_QUERY_PIXEL_FORMAT };
static inline unsigned VIMSurfaceQueryBytes(unsigned kind)
{
    switch(kind) {
        case VIM_SURFACE_QUERY_GEOMETRY:return sizeof(struct VIMSurfaceGeometry);
        case VIM_SURFACE_QUERY_STATE:return sizeof(struct VIMSurfaceState);
        case VIM_SURFACE_QUERY_DRAWING_SIZE:return sizeof(struct VIMSurfaceDrawingSize);
        case VIM_SURFACE_QUERY_PIXEL_FORMAT:return sizeof(struct VIMSurfacePixelFormat);
        default:return 0;
    }
}
#endif
