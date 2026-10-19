/* Native control, not a QEMU change: compare BTI/NOP at indirect targets.
 * Uses ordinary RX pages, not guarded-BTI pages. Never modifies executable
 * memory in place. Does not justify omitting BTI on guarded production pages. */
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(int argc, char **argv) {
    unsigned blocks = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 256;
    unsigned stride = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0) : 64;
    if (!blocks || blocks > 262144 || stride < 64 || stride > 4096 ||
        (stride & (stride-1)) || (blocks & (blocks-1))) return 64;
    const size_t size = blocks * stride;
    printf("blocks=%u stride=%u footprint=%zu\n", blocks, stride, size);
    void *pages[2];
    for (unsigned variant = 0; variant < 2; variant++) {
        pages[variant] = mmap(0,size,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
        if (pages[variant] == MAP_FAILED) return 1;
        for (unsigned i = 0; i < blocks; i++) {
            uint32_t *p = (uint32_t *)((char *)pages[variant] + i*stride);
            p[0] = variant ? 0xd503249f : 0xd503201f; /* BTI J / NOP */
            p[1] = 0x91000400; /* add x0,x0,#1 */
            p[2] = 0xf1000421; /* subs x1,x1,#1 */
            p[3] = 0x54000060; /* b.eq +12 (ret) */
            p[4] = 0x58000090; /* ldr x16,+16 (literal) */
            p[5] = 0xd61f0200; /* br x16 */
            p[6] = 0xd65f03c0; /* ret */
            p[7] = 0xd503201f;
            /* Odd stride traverses every block in a power-of-two ring.
             * Avoid making large-code results a linear prefetch test. */
            *(uint64_t *)(p+8) = (uintptr_t)pages[variant] + ((i+73)%blocks)*stride;
        }
        if (mprotect(pages[variant],size,PROT_READ|PROT_EXEC)) return 2;
        sys_icache_invalidate(pages[variant],size);
    }
    for (unsigned trial = 0; trial < 10; trial++) {
        unsigned v = trial % 4 == 1 || trial % 4 == 2;
        uint64_t (*fn)(uint64_t,uint64_t) = pages[v];
        double start = now();
        uint64_t result = fn(0,10000000);
        printf("trial=%u bti=%u seconds=%.6f check=%llu\n",trial,v,
               now()-start,(unsigned long long)result);
        if (result != 10000000) return 3;
    }
    for (unsigned v=0; v<2; v++) munmap(pages[v],size);
    return 0;
}
