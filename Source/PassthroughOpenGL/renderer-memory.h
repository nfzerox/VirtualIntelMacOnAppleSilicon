/* Pre-MB CGL properties return a signed GLint byte count. Larger modern GPU
 * budgets must saturate in that representable domain, not become -1. */
#ifndef VIM_RENDERER_MEMORY_H
#define VIM_RENDERER_MEMORY_H
#include <stdint.h>
static inline uint32_t VIMRendererMemoryBytes(int megabytes)
{
    if(megabytes<=0)return 0;
    uint64_t bytes=(uint64_t)(unsigned)megabytes*UINT64_C(1048576);
    return bytes>INT32_MAX?INT32_MAX:(uint32_t)bytes;
}
/* Snow Leopard (10.6) CoreGraphics GLCompositor converts the GLI megabyte
 * field to a 32-bit byte count (mb << 20). On hosts whose GPU reports an exact
 * or near multiple of 4096 MB (e.g. 8 GB Radeon Pro 580 or 24/32/48/64 GB
 * Apple Silicon unified memory), (uint32_t)(mb << 20) wraps below
 * GLCompositorMinimumVRAM (16 MB), disabling Quartz Extreme while ordinary GL
 * apps still work. Saturate to 2047 MB (0x7ff00000 bytes) so both signed
 * 32-bit GLint and unsigned 32-bit byte conversions stay positive and large. */
static inline uint32_t VIMRendererMemoryMegabytes32(int megabytes)
{
    if(megabytes<=0)return 0;
    return megabytes>2047?2047U:(uint32_t)megabytes;
}
#endif
