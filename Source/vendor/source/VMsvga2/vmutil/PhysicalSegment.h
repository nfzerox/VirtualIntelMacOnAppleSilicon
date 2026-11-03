#ifndef VIM_PHYSICAL_SEGMENT_H
#define VIM_PHYSICAL_SEGMENT_H

#include <IOKit/IOMemoryDescriptor.h>

// QEMU consumes guest physical addresses, never an IOMMU/DMA mapping.
// The 10.6 SDK explicitly documents getPhysicalSegment64 as the predecessor
// of getPhysicalSegment(..., kIOMemoryMapperNone). Keep the 64-bit address
// even with Leopard's 32-bit kernel and more than 4 GB of guest RAM.
static inline addr64_t VIMPhysicalSegment(IOMemoryDescriptor* memory,
                                         IOByteCount offset, IOByteCount* length)
{
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    return memory->getPhysicalSegment64(offset, length);
#else
    return memory->getPhysicalSegment(offset, length, kIOMemoryMapperNone);
#endif
}

#endif
