// Bulk string instructions: throughput and correctness at sizes around the chunk boundaries, overlaps and both directions.
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
    __asm__ volatile("rep movsq" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
}
static void rep_stosb(void *d, int v, uint64_t n) {
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(v) : "memory");
}
static void rep_stosq(void *d, uint64_t v, uint64_t n) {
    __asm__ volatile("rep stosq" : "+D"(d), "+c"(n) : "a"(v) : "memory");
}

static uint8_t a[1 << 20] __attribute__((aligned(4096))), b[1 << 20] __attribute__((aligned(4096)));

static uint64_t hash_buf(const uint8_t *p, uint64_t n) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint64_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x100000001b3ULL;
    return h;
}

int flat_main(unsigned long a0, unsigned long a1) {
    (void)a0, (void)a1;
    for (uint64_t i = 0; i < sizeof a; i++)
        a[i] = (uint8_t)(i * 2654435761u >> 24);
    static const uint64_t sizes[] = {0, 1, 7, 8, 15, 16, 31, 32, 33, 63, 64, 65, 100, 4095, 4096, 4097, 65536};
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        uint64_t n = sizes[k];
        memset(b, 0, 70000);
        rep_movsb(b + 3, a + 5, n);
        printf("movsb %llu %016llx\n", n, hash_buf(b, 70000));
        memset(b, 0, 70000);
        rep_movsq(b + 8, a + 16, n / 8);
        printf("movsq %llu %016llx\n", n, hash_buf(b, 70000));
        memset(b, 0, 70000);
        rep_stosb(b + 1, 0x5A, n);
        printf("stosb %llu %016llx\n", n, hash_buf(b, 70000));
        memset(b, 0, 70000);
        rep_stosq(b + 8, 0x1122334455667788ULL, n / 8);
        printf("stosq %llu %016llx\n", n, hash_buf(b, 70000));
    }
    // Forward overlap that propagates a pattern, each small distance.
    for (uint64_t dist = 1; dist < 40; dist += 3) {
        memcpy(b, a, 1000);
        rep_movsb(b + dist, b, 500);
        printf("overlap+ %llu %016llx\n", dist, hash_buf(b, 1000));
        memcpy(b, a, 1000);
        rep_movsb(b, b + dist, 500);
        printf("overlap- %llu %016llx\n", dist, hash_buf(b, 1000));
    }
    // Backward copies with DF set.
    memcpy(b, a, 1000);
    __asm__ volatile("std\n rep movsb\n cld" : : "D"(b + 700), "S"(b + 500), "c"(300) : "memory");
    printf("backward %016llx\n", hash_buf(b, 1000));

    double t = now();
    uint64_t rounds = 2000;
    for (uint64_t r = 0; r < rounds; r++)
        rep_movsb(b, a, 1 << 18);
    printf("time rep movsb 256 KB: %.0f MB/s\n", (double)rounds * (1 << 18) / (now() - t) / 1e6);
    t = now();
    for (uint64_t r = 0; r < rounds; r++)
        rep_movsq(b, a, (1 << 18) / 8);
    printf("time rep movsq 256 KB: %.0f MB/s\n", (double)rounds * (1 << 18) / (now() - t) / 1e6);
    t = now();
    for (uint64_t r = 0; r < rounds; r++)
        rep_stosq(b, r, (1 << 18) / 8);
    printf("time rep stosq 256 KB: %.0f MB/s\n", (double)rounds * (1 << 18) / (now() - t) / 1e6);
    return 0;
}
