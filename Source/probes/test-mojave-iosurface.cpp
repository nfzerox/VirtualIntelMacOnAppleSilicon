#include "../compat/PVGIOSurfaceABI/PVGMojaveSurface.h"
#include <assert.h>
#include <stdio.h>

int main() {
    uint8_t src[0xf60], dst[0x8d0 + 16];
    for (unsigned i = 0; i < sizeof(src); ++i) src[i] = (i * 31 + i / 256) & 255;
    memset(dst, 0xa5, sizeof(dst));
    PVGMojaveSurface::translateResult(dst, src);
    assert(memcmp(dst, src, 0x50) == 0);
    for (unsigned i = 0; i < 32; ++i) {
        assert(memcmp(dst + 0x50 + i * 0x44, src + 0x60 + i * 0x78, 0x34) == 0);
        assert(memcmp(dst + 0x84 + i * 0x44, src + 0xc8 + i * 0x78, 0x10) == 0);
    }
    for (unsigned i = 0x8d0; i < sizeof(dst); ++i) assert(dst[i] == 0xa5);
    const uint32_t expected[] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,
        26,27,28,30,32,33,34,35,36,37,38,39,40,41,42,43,44,45
    };
    for (unsigned i = 0; i < 39; ++i) {
        uint32_t result = 999;
        assert(PVGMojaveSurface::mapSelector(i, result) && result == expected[i]);
    }
    uint32_t result = 999;
    assert(!PVGMojaveSurface::mapSelector(39, result) && result == 999);
    puts("Mojave IOSurface translation and selector tests passed");
}
