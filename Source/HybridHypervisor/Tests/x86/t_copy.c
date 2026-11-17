// Copy routines a guest libc can choose between, at sizes from a cache line to past the last level: which suits the engine.
#include "flatlibc.h"

static double now(void) {
    struct timeval t;
    gettimeofday(&t, 0);
    return (double)t.tv_sec + (double)t.tv_usec * 1e-6;
}

static void rep_movsb(void *d, const void *s, uint64_t n) {
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
}
static void rep_movsq(void *d, const void *s, uint64_t n) {
    n /= 8;
    __asm__ volatile("rep movsq" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
}
// Mojave's SSE path in spirit: 64 bytes a turn, unaligned loads, aligned stores.
static void sse_loop(void *d, const void *s, uint64_t n) {
    __asm__ volatile("1: movdqu (%1), %%xmm0; movdqu 16(%1), %%xmm1; movdqu 32(%1), %%xmm2; movdqu 48(%1), %%xmm3\n"
                     "movdqa %%xmm0, (%0); movdqa %%xmm1, 16(%0); movdqa %%xmm2, 32(%0); movdqa %%xmm3, 48(%0)\n"
                     "add $64, %0; add $64, %1; sub $64, %2; jnz 1b"
                     : "+r"(d), "+r"(s), "+r"(n)
                     :
                     : "memory", "xmm0", "xmm1", "xmm2", "xmm3");
}
static void sse_nt(void *d, const void *s, uint64_t n) {
    __asm__ volatile("1: movdqu (%1), %%xmm0; movdqu 16(%1), %%xmm1; movdqu 32(%1), %%xmm2; movdqu 48(%1), %%xmm3\n"
                     "movntdq %%xmm0, (%0); movntdq %%xmm1, 16(%0); movntdq %%xmm2, 32(%0); movntdq %%xmm3, 48(%0)\n"
                     "add $64, %0; add $64, %1; sub $64, %2; jnz 1b; sfence"
                     : "+r"(d), "+r"(s), "+r"(n)
                     :
                     : "memory", "xmm0", "xmm1", "xmm2", "xmm3");
}
static void scalar_loop(void *d, const void *s, uint64_t n) {
    uint64_t *x = d;
    const uint64_t *y = s;
    for (uint64_t i = 0; i < n / 8; i += 4) {
        x[i] = y[i];
        x[i + 1] = y[i + 1];
        x[i + 2] = y[i + 2];
        x[i + 3] = y[i + 3];
    }
}

#define BIG (32u << 20)
static uint8_t a[BIG] __attribute__((aligned(4096))), b[BIG] __attribute__((aligned(4096)));

int flat_main(unsigned long a0, unsigned long a1) {
    (void)a0, (void)a1;
    for (uint64_t i = 0; i < BIG; i++)
        a[i] = (uint8_t)(i * 2654435761u >> 24);
    memcpy(b, a, BIG);
    static const struct {
        const char *name;
        void (*copy)(void *, const void *, uint64_t);
    } routines[] = {{"rep_movsb", rep_movsb}, {"rep_movsq", rep_movsq}, {"sse_64", sse_loop},
                    {"sse_nt", sse_nt},       {"scalar_64", scalar_loop}, {"flatlibc", (void (*)(void *, const void *, uint64_t))memcpy}};
    static const uint64_t sizes[] = {256, 4096, 65536, 1 << 20, 16 << 20};
    printf("%-10s", "MB/s");
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++)
        printf(" %9lluB", (unsigned long long)sizes[k]);
    printf("\n");
    for (unsigned r = 0; r < sizeof routines / sizeof routines[0]; r++) {
        printf("%-10s", routines[r].name);
        for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
            uint64_t n = sizes[k], reps = (256ull << 20) / n, best = 0;
            double best_s = 1e9;
            for (int trial = 0; trial < 5; trial++) {
                double t = now();
                for (uint64_t i = 0; i < reps; i++)
                    routines[r].copy(b + (i * n) % (BIG - n) / 64 * 64, a + 64, n);
                t = now() - t;
                if (t < best_s)
                    best_s = t;
            }
            best = (uint64_t)(reps * n / best_s / 1e6);
            printf(" %10llu", (unsigned long long)best);
        }
        printf("\n");
    }
    return 0;
}
