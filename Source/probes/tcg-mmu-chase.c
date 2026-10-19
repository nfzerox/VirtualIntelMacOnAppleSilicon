/* Bounded dependent-load probe. Contrast cached translations with deliberately
 * conflicting guest virtual pages; compare with host runs, since real host
 * caches/TLBs also contribute. No user files or persistent settings changed. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static double seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static __attribute__((noinline)) uintptr_t chase(uintptr_t p, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        p = *(volatile uintptr_t *)p;
    }
    return p;
}

static uintptr_t *node(char *base, size_t stride, unsigned i)
{
    /* Vary the cache-line offset inside each page so the sparse layout does
     * not also force every host data-cache access into the same set. */
    return (uintptr_t *)(base + i * stride + (stride >= 4096 ? i * 64 : 0));
}

int main(int argc, char **argv)
{
    unsigned n = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 1000000;
    if (!n || n > 100000000 || n % 32) {
        fprintf(stderr, "count must be a multiple of 32 in 32..100000000\n");
        return 64;
    }
    const size_t strides[] = {64, 4096, 1048576};
    const char *names[] = {"one-guest-page", "32-adjacent-pages",
                           "32-conflicting-pages"};
    setbuf(stdout, NULL);
    printf("host-page-size=%ld loads=%u\n", sysconf(_SC_PAGESIZE), n);
    for (unsigned c = 0; c < 3; ++c) {
        size_t length = 32 * strides[c];
        char *base = mmap(NULL, length, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
        if (base == MAP_FAILED) { perror("mmap"); return 1; }
        /* This LCG visits every node before returning to zero. */
        for (unsigned i = 0; i < 32; ++i) {
            *node(base, strides[c], i) =
                (uintptr_t)node(base, strides[c], (5 * i + 1) & 31);
        }
        if (chase((uintptr_t)base, 3200) != (uintptr_t)base) { return 2; }
        for (unsigned trial = 0; trial < 3; ++trial) {
            double start = seconds();
            uintptr_t end = chase((uintptr_t)base, n);
            double elapsed = seconds() - start;
            printf("%s trial=%u sec=%.6f ns/load=%.2f correct=%d\n",
                   names[c], trial, elapsed, elapsed * 1e9 / n,
                   end == (uintptr_t)base);
            if (end != (uintptr_t)base) { return 3; }
        }
        munmap(base, length);
    }
    return 0;
}
