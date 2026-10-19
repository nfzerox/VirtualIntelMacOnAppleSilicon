// Packed SSE result/exception differential and short performance probe.
// Run the same x86_64 binary under Rosetta and in the guest. No uploads.
#include <emmintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t state = UINT64_C(0x824213e41bc67321);
static uint64_t next(void) {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    return state;
}
static uint64_t mix(uint64_t h, uint64_t v) {
    return (h ^ v) * UINT64_C(1099511628211);
}
// Keep time conversion out of the MXCSR-controlled arithmetic interval.
static __attribute__((noinline)) double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static const uint64_t special64[] = {
    0, UINT64_C(0x8000000000000000), 1, UINT64_C(0x8000000000000001),
    UINT64_C(0x000fffffffffffff), UINT64_C(0x0010000000000000),
    UINT64_C(0x0010000000000001), UINT64_C(0x7fefffffffffffff),
    UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000),
    UINT64_C(0x7ff8000000001234), UINT64_C(0x7ff0000000001234),
    UINT64_C(0xfff8000000005678), UINT64_C(0xfff0000000005678),
    UINT64_C(0x3ff0000000000000), UINT64_C(0xbff0000000000000)
};
static const uint32_t special32[] = {
    0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
    0x00800001, 0x7f7fffff, 0x7f800000, 0xff800000,
    0x7fc01234, 0x7f801234, 0xffc05678, 0xff805678, 0x3f800000, 0xbf800000
};
#define OP(insn) __asm__ volatile(                                      \
    "ldmxcsr %2\n\t" insn " %3,%0\n\tstmxcsr %1"                     \
    : "+x"(a), "=m"(flags) : "m"(mode), "x"(b) : "memory")

static void differential(void) {
    unsigned saved = _mm_getcsr();
    int detail = getenv("PVG_SSE_DETAIL") != NULL;
    for (unsigned controls = 0; controls < 32; ++controls) {
        unsigned mode = 0x1f80 | ((controls & 3) << 13) |
            ((controls & 4) ? 0x40 : 0) | ((controls & 8) ? 0x8000 : 0) |
            ((controls & 16) ? 0x20 : 0);
        uint64_t h = UINT64_C(1469598103934665603);
        uint64_t canonical = h;
        for (unsigned i = 0; i < 2048; ++i) {
            for (unsigned bits = 32; bits <= 64; bits += 32) {
                uint64_t av[2], bv[2];
                for (unsigned j = 0; j < 2; ++j) {
                    av[j] = next(); bv[j] = next();
                }
                if (i % 4 == 0) {
                    if (bits == 64) {
                        for (unsigned j = 0; j < 2; ++j) {
                            av[j] = special64[((i / 4) + j * 3) % 16];
                            bv[j] = special64[((i / 64) + j * 5) % 16];
                        }
                    } else {
                        uint32_t aa[4], bb[4];
                        for (unsigned j = 0; j < 4; ++j) {
                            aa[j] = special32[((i / 4) + j * 3) % 16];
                            bb[j] = special32[((i / 64) + j * 5) % 16];
                        }
                        memcpy(av, aa, 16); memcpy(bv, bb, 16);
                    }
                } else if (i % 4 != 1) {
                    // Finite ordinary operands, near 1 or a broader exponent.
                    if (bits == 64) {
                        for (unsigned j = 0; j < 2; ++j) {
                            uint64_t mask = i % 4 == 2 ? UINT64_C(0x800fffffffffffff)
                                                       : UINT64_C(0x80ffffffffffffff);
                            av[j] = (av[j] & mask) | UINT64_C(0x3f00000000000000);
                            bv[j] = (bv[j] & mask) | UINT64_C(0x3f00000000000000);
                        }
                    } else {
                        for (unsigned j = 0; j < 2; ++j) {
                            uint64_t mask = i % 4 == 2 ? UINT64_C(0x807fffff807fffff)
                                                       : UINT64_C(0x87ffffff87ffffff);
                            av[j] = (av[j] & mask) | UINT64_C(0x3800000038000000);
                            bv[j] = (bv[j] & mask) | UINT64_C(0x3800000038000000);
                        }
                    }
                }
                if (i % 4 == 2) {
                    // Exactness boundaries: powers of two, their adjacent
                    // representable numbers, cancellation and exponent gaps
                    // on both sides of the 24/53-bit precision thresholds.
                    static const uint64_t mant64[8] = {
                        0, 1, 2, 3, UINT64_C(0xfffffffffffff),
                        UINT64_C(0xffffffffffffe), UINT64_C(0x8000000000000),
                        UINT64_C(0x4000000000000)
                    };
                    static const uint32_t mant32[8] = {
                        0, 1, 2, 3, 0x7fffff, 0x7ffffe, 0x400000, 0x200000
                    };
                    if (bits == 64) {
                        unsigned gap = (i / 4) % 64;
                        for (unsigned j = 0; j < 2; ++j) {
                            av[j] = (av[j] & UINT64_C(0x8000000000000000)) |
                                UINT64_C(0x4000000000000000) |
                                mant64[((i / 256) + j) % 8];
                            bv[j] = (bv[j] & UINT64_C(0x8000000000000000)) |
                                ((uint64_t)(0x400 - gap) << 52) |
                                mant64[((i / 64) + j * 3) % 8];
                        }
                    } else {
                        unsigned gap = (i / 4) % 40;
                        uint32_t aa[4], bb[4];
                        memcpy(aa, av, 16); memcpy(bb, bv, 16);
                        for (unsigned j = 0; j < 4; ++j) {
                            aa[j] = (aa[j] & 0x80000000) | 0x40000000 |
                                mant32[((i / 160) + j) % 8];
                            bb[j] = (bb[j] & 0x80000000) |
                                ((0x80 - gap) << 23) |
                                mant32[((i / 40) + j * 3) % 8];
                        }
                        memcpy(av, aa, 16); memcpy(bv, bb, 16);
                    }
                }
                for (unsigned op = 0; op < 4; ++op) {
                    __m128i a, b; unsigned flags; uint64_t result[2];
                    memcpy(&a, av, 16); memcpy(&b, bv, 16);
                    if (bits == 64) {
                        switch (op) {
                        case 0: OP("addpd"); break;
                        case 1: OP("subpd"); break;
                        case 2: OP("mulpd"); break;
                        default: OP("divpd"); break;
                        }
                    } else {
                        switch (op) {
                        case 0: OP("addps"); break;
                        case 1: OP("subps"); break;
                        case 2: OP("mulps"); break;
                        default: OP("divps"); break;
                        }
                    }
                    memcpy(result, &a, 16);
                    if (detail && controls == 0 && i < 64) {
                        printf("i=%u bits=%u op=%u a=%016llx,%016llx b=%016llx,%016llx r=%016llx,%016llx flags=%x\n",
                               i, bits, op, (unsigned long long)av[0],
                               (unsigned long long)av[1], (unsigned long long)bv[0],
                               (unsigned long long)bv[1], (unsigned long long)result[0],
                               (unsigned long long)result[1], flags);
                    }
                    h = mix(mix(mix(h, result[0]), result[1]), flags);
                    // Rosetta and baseline QEMU choose different payloads when
                    // both inputs are NaNs. Keep the raw hash for QEMU A/B;
                    // additionally compare values/flags modulo NaN payloads.
                    if (bits == 64) {
                        for (unsigned j = 0; j < 2; ++j) {
                            if ((result[j] & UINT64_C(0x7fffffffffffffff)) >
                                UINT64_C(0x7ff0000000000000))
                                result[j] = UINT64_C(0x7ff8000000000000);
                        }
                    } else {
                        uint32_t words[4]; memcpy(words, result, 16);
                        for (unsigned j = 0; j < 4; ++j) {
                            if ((words[j] & 0x7fffffff) > 0x7f800000)
                                words[j] = 0x7fc00000;
                        }
                        memcpy(result, words, 16);
                    }
                    canonical = mix(mix(mix(canonical, result[0]), result[1]), flags);
                }
            }
        }
        _mm_setcsr(saved);
        printf("mxcsr=%04x hash=%016llx canonical=%016llx\n", mode,
               (unsigned long long)h, (unsigned long long)canonical);
    }
    _mm_setcsr(saved);
}

static void performance(unsigned n) {
    unsigned saved = _mm_getcsr(), flags;
    double t, dt, out[2]; float out32[4];
    __m128d d = _mm_set_pd(1.002, 1.001), m = _mm_set1_pd(1.00000001);
    __m128 f = _mm_set_ps(1.004f, 1.003f, 1.002f, 1.001f);
    __m128 mf = _mm_set1_ps(1.00000011920928955078125f);
    t = now(); _mm_setcsr(0x1fa0);
    for (unsigned i = 0; i < n; ++i) {
        __asm__ volatile("mulpd %1,%0" : "+x"(d) : "x"(m));
    }
    flags = _mm_getcsr(); dt = now() - t; _mm_storeu_pd(out, d);
    printf("packed-f64 n=%u sec=%.6f check=%.12g,%.12g flags=%x\n",
           n, dt, out[0], out[1], flags);
    t = now(); _mm_setcsr(0x1fa0);
    for (unsigned i = 0; i < n; ++i) {
        __asm__ volatile("mulps %1,%0" : "+x"(f) : "x"(mf));
    }
    flags = _mm_getcsr(); dt = now() - t; _mm_storeu_ps(out32, f);
    printf("packed-f32 n=%u sec=%.6f check=%.9g,%.9g,%.9g,%.9g flags=%x\n",
           n, dt, out32[0], out32[1], out32[2], out32[3], flags);
    d = _mm_set1_pd(1); m = d;
    t = now(); _mm_setcsr(0x1f80);
    for (unsigned i = 0; i < n; ++i) {
        __asm__ volatile("mulpd %1,%0" : "+x"(d) : "x"(m));
    }
    flags = _mm_getcsr(); dt = now() - t; _mm_storeu_pd(out, d);
    printf("packed-exact-f64 n=%u sec=%.6f check=%.12g,%.12g flags=%x\n",
           n, dt, out[0], out[1], flags);
    f = _mm_set1_ps(1); mf = f;
    t = now(); _mm_setcsr(0x1f80);
    for (unsigned i = 0; i < n; ++i) {
        __asm__ volatile("mulps %1,%0" : "+x"(f) : "x"(mf));
    }
    flags = _mm_getcsr(); dt = now() - t; _mm_storeu_ps(out32, f);
    printf("packed-exact-f32 n=%u sec=%.6f check=%.9g,%.9g,%.9g,%.9g flags=%x\n",
           n, dt, out32[0], out32[1], out32[2], out32[3], flags);
    f = _mm_set_ps(1, 1, 1, 0); mf = _mm_set1_ps(1);
    t = now(); _mm_setcsr(0x1fa0);
    for (unsigned i = 0; i < n; ++i) {
        __asm__ volatile("mulps %1,%0" : "+x"(f) : "x"(mf));
    }
    flags = _mm_getcsr(); dt = now() - t; _mm_storeu_ps(out32, f);
    printf("packed-zero-lane-f32 n=%u sec=%.6f check=%.9g,%.9g,%.9g,%.9g flags=%x\n",
           n, dt, out32[0], out32[1], out32[2], out32[3], flags);
    _mm_setcsr(saved);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    if (argc > 1) performance((unsigned)strtoul(argv[1], NULL, 10));
    else differential();
    return 0;
}
