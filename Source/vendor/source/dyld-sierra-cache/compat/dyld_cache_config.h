#pragma once

// Sierra XNU 3789.70.16, osfmk/mach/shared_region.h. The modern SDK
// advertises a different x86_64 region which Sierra cannot map.
#define SIERRA_SHARED_REGION_BASE_I386    0x90000000ULL
#define SIERRA_SHARED_REGION_SIZE_I386    0x20000000ULL
#define SIERRA_SHARED_REGION_BASE_X86_64  0x00007FFF70000000ULL
#define SIERRA_SHARED_REGION_SIZE_X86_64  0x000000008FE00000ULL

// The offline entry point only emits Intel caches; upstream ARM templates
// still reference these period values during compilation.
#define ARM_SHARED_REGION_START          0x1A000000ULL
#define ARM_SHARED_REGION_SIZE           0x26000000ULL
#define ARM64_SHARED_REGION_START        0x180000000ULL
#define ARM64_SHARED_REGION_SIZE         0x40000000ULL
