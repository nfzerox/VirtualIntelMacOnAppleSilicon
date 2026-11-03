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
#endif
