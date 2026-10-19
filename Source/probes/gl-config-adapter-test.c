#include "../compat/PVGOpenGLProbe/PVGGLConfig.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t native[PVGGLNativeConfigSize];
    uint8_t adjacent[32];
} NativeAllocation;

int main(void) {
    assert(!PVGGLConfigCreate(NULL, PVGGLNativeConfigSize));
    NativeAllocation a, b;
    memset(&a, 0xa5, sizeof(a)); memset(&b, 0x5a, sizeof(b));
    assert(!PVGGLConfigCreate(a.native, PVGGLNativeConfigSize - 1));
    assert(!PVGGLConfigCreate(a.native, PVGGLDonorConfigSize));
    assert(!PVGGLConfigPublish(NULL));
    assert(!PVGGLConfigDonor(NULL)); PVGGLConfigDestroy(NULL);
    for (unsigned iteration = 0; iteration < 1000; ++iteration) {
        PVGGLConfig *ca = PVGGLConfigCreate(a.native, sizeof(a.native));
        PVGGLConfig *cb = PVGGLConfigCreate(b.native, sizeof(b.native));
        assert(ca && cb);
        uint8_t *da = PVGGLConfigDonor(ca), *db = PVGGLConfigDonor(cb);
        assert(da != a.native && db != b.native && da != db);
        assert(!memcmp(da, a.native, sizeof(a.native)));
        for (unsigned i = 0x250; i < 0x260; ++i) assert(da[i] == 0);
        /* Simulate donor initialization and later group changes. */
        memset(da + 0x230, 0x13, 0x20);
        memset(da + 0x250, 0xe7, 0x10);
        da[0x19] = 0x82;
        assert(PVGGLConfigPublish(ca));
        assert(a.native[0x19] == 0x80 && da[0x19] == 0x80);
        assert(!memcmp(a.native, da, sizeof(a.native)));
        for (unsigned i = 0; i < sizeof(a.adjacent); ++i) assert(a.adjacent[i] == 0xa5);
        for (unsigned i = 0; i < sizeof(b); ++i) assert(((uint8_t *)&b)[i] == 0x5a);
        memset(da + 0x230, 0x27, 0x20);
        assert(PVGGLConfigPublish(ca));
        for (unsigned i = 0x250; i < 0x260; ++i) assert(da[i] == 0xe7);
        PVGGLConfigDestroy(ca);
        /* Independent context remains usable after the first is destroyed. */
        db[0x19] = 2;
        assert(PVGGLConfigPublish(cb)); assert(b.native[0x19] == 0);
        for (unsigned i = 0; i < sizeof(b.adjacent); ++i) assert(b.adjacent[i] == 0x5a);
        PVGGLConfigDestroy(cb);
        memset(&b, 0x5a, sizeof(b));
    }
    puts("GL config ownership/prefix/canary tests passed (1000 context pairs)");
    return 0;
}
