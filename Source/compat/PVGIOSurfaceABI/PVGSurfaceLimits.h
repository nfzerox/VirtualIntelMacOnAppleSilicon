#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef KERNEL
#include <libkern/libkern.h>
#else
#include <string.h>
#endif

namespace PVGSurfaceLimits {
constexpr uint32_t supportedDimension = 8192;
// The donor overrides the generic defaults in populateAccelConfig(config).
// Recover its independent config-relative store, not a fixed struct offset.
inline bool findPopulatedDimensionOffset(const uint8_t *code, size_t size,
    uint32_t &output) {
    if (!code) return false;
    uint32_t found = 0; unsigned matches = 0;
    for (size_t i = 0; i + 7 <= size; ++i) {
        if (code[i] != 0xc7 || (i && (code[i-1] & 0xf0) == 0x40)) continue;
        uint32_t offset = 0, value;size_t immediate;
        if (code[i+1] == 0x46) { // mov imm32, disp8(%rsi)
            offset = code[i+2];if (offset >= 0x80) continue;
            immediate = i+3;
        } else if (code[i+1] == 0x86 && i+10 <= size) {
            memcpy(&offset,code+i+2,4);immediate = i+6;
        } else continue;
        memcpy(&value,code+immediate,4);
        if (value != 0x10001000 || offset < 0x10 || offset > 0x400 || offset % 4) continue;
        found = offset;++matches;
    }
    if (matches != 1) return false;
    output = found;return true;
}
// Recover the private field from the initializer AND its dimension validator.
// No guest-build address/ivar offset is assumed. Unknown layouts fail closed.
inline bool findDimensionOffset(const uint8_t *init, size_t initSize,
    const uint8_t *validator, size_t validatorSize, uint32_t &output) {
    if (!init || !validator) return false;
    uint32_t found = 0; unsigned matches = 0;
    for (size_t i = 0; i + 10 <= initSize; ++i) {
        if (init[i] != 0xc7 || init[i+1] != 0x87) continue;
        uint32_t offset, value;
        memcpy(&offset, init+i+2, 4); memcpy(&value, init+i+6, 4);
        if (value != 0x10001000 || offset < 0x100 || offset > 0x10000 || offset % 4) continue;
        unsigned widths = 0, heights = 0;
        for (size_t j = 0; j + 7 <= validatorSize; ++j) {
            if (validator[j] != 0x0f || validator[j+1] != 0xb7) continue;
            uint32_t displacement; memcpy(&displacement, validator+j+3, 4);
            widths += validator[j+2] == 0x88 && displacement == offset;
            heights += validator[j+2] == 0x80 && displacement == offset+2;
        }
        if (widths == 1 && heights == 1) { found = offset; ++matches; }
    }
    if (matches != 1) return false;
    output = found; return true;
}
} // namespace PVGSurfaceLimits
