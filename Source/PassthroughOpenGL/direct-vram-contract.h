#ifndef VIM_DIRECT_VRAM_CONTRACT_H
#define VIM_DIRECT_VRAM_CONTRACT_H
#include <stdint.h>
/* Rectangles are in the drawing view's CPU row orientation. No pointers. */
struct DGVRAMRect { int32_t x,y,width,height; };
typedef char DGVRAMRectSizeCheck[sizeof(struct DGVRAMRect)==16?1:-1];
/* This is the existing shared BGRA8 import contract, not a display-mode
 * constraint. Ineligible rows retain the ordinary coherent upload/readback
 * route. Check it before asking the guest kernel for backing/page metadata;
 * the host must independently check the same values on every transaction.
 * No address, epoch, mapping or failed-allocation result is cached here. */
static inline int VIMGLSharedRowsSupported(uint64_t width,uint64_t height,uint64_t pitch)
{
    return width && height && width<=8192 && height<=8192 &&
        pitch>=width*4 && pitch<=8192*4 && !(pitch&31);
}
#endif
