/* Canonical private surface formats, not Apple mode bits or host pointers.
 * Zero preserves every existing BGRA8 wire transaction. RGBA16 is unsigned
 * normalized16, never half float and never silently downconverted to BGRA8. */
#ifndef VIM_GL_PIXEL_FORMATS_H
#define VIM_GL_PIXEL_FORMATS_H
#include <stdint.h>
#define VIM_GL_PIXEL_BGRA8 0U
#define VIM_GL_PIXEL_RGBA16 1U
#define VIM_SURFACE_PIXEL_FORMAT_VERSION 1U
struct VIMSurfacePixelFormat {uint32_t version,format,bits_per_pixel,reserved;};
static inline unsigned VIMGLPixelBytes(unsigned format)
{return format==VIM_GL_PIXEL_BGRA8?4:format==VIM_GL_PIXEL_RGBA16?8:0;}
#endif
