#include "PVGGLConfig.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct PVGGLConfig {
    void *native;
    /* malloc alignment and this pointer-sized offset satisfy the x86_64 ABI. */
    uint8_t donor[PVGGLDonorConfigSize];
};

PVGGLConfig *PVGGLConfigCreate(void *native, size_t nativeSize) {
    if (!native || nativeSize != PVGGLNativeConfigSize) return NULL;
    PVGGLConfig *config = calloc(1, sizeof(*config));
    if (!config) return NULL;
    config->native = native;
    memcpy(config->donor, native, PVGGLNativeConfigSize);
    return config;
}

void *PVGGLConfigDonor(PVGGLConfig *config) {
    return config ? config->donor : NULL;
}

int PVGGLConfigPublish(PVGGLConfig *config) {
    if (!config) return 0;
    config->donor[0x19] &= 0x80;
    memcpy(config->native, config->donor, PVGGLNativeConfigSize);
    return 1;
}

void PVGGLConfigDestroy(PVGGLConfig *config) {
    free(config);
}
