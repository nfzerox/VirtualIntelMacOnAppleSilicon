// Compare output with the same x86 binary on the host (Rosetta) and guest.
// Covers result bits and MXCSR exception flags, all rounding/DAZ/FTZ modes.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <emmintrin.h>

static uint64_t rng = 0xbadc0ffeedead123ULL;
static uint64_t next(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng;
}
static uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ULL; }
#define OP(insn) __asm__ volatile(                                      \
    "ldmxcsr %2\n\t" insn " %3,%0\n\tstmxcsr %1"                     \
    : "+x"(a), "=m"(flags) : "m"(mode), "x"(b) : "memory")

int main(void) {
    unsigned saved = _mm_getcsr();
    for (unsigned controls = 0; controls < 32; ++controls) {
        unsigned mode = 0x1f80 | ((controls & 3) << 13) |
                        ((controls & 4) ? 0x40 : 0) |
                        ((controls & 8) ? 0x8000 : 0) |
                        ((controls & 16) ? 0x20 : 0);
        uint64_t h = 1469598103934665603ULL;
        for (unsigned i = 0; i < 10000; ++i) {
            uint64_t av = next(), bv = next();
            // Include many ordinary/exact numbers, alongside arbitrary bits.
            if (i & 1) {
                double x = (int)(av & 0xffff) - 32768;
                double y = (int)(bv & 0xffff) - 32768;
                memcpy(&av, &x, 8); memcpy(&bv, &y, 8);
            }
            for (unsigned op = 0; op < 4; ++op) {
                double a, b; unsigned flags; uint64_t result;
                memcpy(&a, &av, 8); memcpy(&b, &bv, 8);
                switch (op) {
                case 0: OP("addsd"); break;
                case 1: OP("subsd"); break;
                case 2: OP("mulsd"); break;
                default: OP("divsd"); break;
                }
                memcpy(&result, &a, 8); h = mix(mix(h, result), flags);
            }
            for (unsigned op = 0; op < 4; ++op) {
                float a, b; unsigned flags; uint32_t result;
                uint32_t aa = av, bb = bv;
                memcpy(&a, &aa, 4); memcpy(&b, &bb, 4);
                switch (op) {
                case 0: OP("addss"); break;
                case 1: OP("subss"); break;
                case 2: OP("mulss"); break;
                default: OP("divss"); break;
                }
                memcpy(&result, &a, 4); h = mix(mix(h, result), flags);
            }
        }
        _mm_setcsr(saved);
        printf("mxcsr=%04x hash=%016llx\n", mode, (unsigned long long)h);
    }
    _mm_setcsr(saved);
}
