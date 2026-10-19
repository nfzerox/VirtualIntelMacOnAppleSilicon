#pragma once

// Derived from IOSurface 255.6.1 (18G103) and donor 290.8.2 getLockResult
// and sMethodDescs. Keep this pure byte translation independently testable.
#include <stdint.h>
#ifdef KERNEL
#include <libkern/libkern.h>
#else
#include <string.h>
#endif

namespace PVGMojaveSurface {
constexpr uint32_t resultSize = 0x8d0;
constexpr uint32_t donorResultSize = 0xf60;
constexpr uint32_t highSierraResultSize = 0x6c8;

// IOSurface 211.15 (17G66) getLockResult: 0x48-byte header, 32 planes
// of 0x34 bytes. No subsampling header or extended-pixel plane trailer.
inline void translateHighSierraResult(void *output, const void *input) {
    auto dst = static_cast<uint8_t *>(output);
    auto src = static_cast<const uint8_t *>(input);
    memcpy(dst, src, 0x48);
    for (unsigned plane = 0; plane < 32; ++plane)
        memcpy(dst + 0x48 + plane * 0x34, src + 0x60 + plane * 0x78, 0x34);
}

inline bool mapSelector(uint32_t native, uint32_t &donor) {
    if (native <= 20) {
        donor = native;
    } else if (native <= 23) {
        donor = native + 5;
    } else if (native == 24) {
        donor = 30;
    } else if (native <= 38) {
        donor = native + 7;
    } else {
        return false;
    }
    return true;
}

inline bool returnsSurface(uint32_t native) {
    return native == 0 || native == 2 || native == 4 ||
        native == 6 || native == 7 || native == 27;
}

inline void translateResult(void *output, const void *input) {
    auto dst = static_cast<uint8_t *>(output);
    auto src = static_cast<const uint8_t *>(input);
    // Common header, including subsampling at 0x4c. Big Sur adds 0x10
    // bytes before its planes; Mojave has no trailing fields.
    memcpy(dst, src, 0x50);
    for (unsigned plane = 0; plane < 32; ++plane) {
        auto d = dst + 0x50 + plane * 0x44;
        auto s = src + 0x60 + plane * 0x78;
        // Dimensions, element layout, and four component descriptors.
        memcpy(d, s, 0x34);
        // Big Sur's compression metadata occupies [0x34,0x68).
        // Mojave instead stores the four extended-pixel counts here.
        memcpy(d + 0x34, s + 0x68, 0x10);
    }
}
} // namespace PVGMojaveSurface
