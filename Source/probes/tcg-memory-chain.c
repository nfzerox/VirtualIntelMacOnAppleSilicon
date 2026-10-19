// Dependent loads: vary page working set without long benchmark runs.
// No privileged operations or guest files. Same x86 binary runs in Rosetta.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>

static double seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    int spread = argc == 2 && strcmp(argv[1], "--spread") == 0;
    if (argc > 2 || (argc == 2 && !spread)) return 64;
    const size_t counts[] = {16, 64, 256, 1024, 4096};
    const size_t strides[] = {64, 4096};
    const size_t iterations = 2000000;
    setbuf(stdout, NULL);
    for (size_t si = 0; si < 2; ++si) {
        for (size_t ci = 0; ci < 5; ++ci) {
            size_t n = counts[ci], stride = strides[si], bytes = n * stride;
            unsigned char *base = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANON, -1, 0);
            size_t *order = malloc(n * sizeof(*order));
            if (base == MAP_FAILED || !order) return 1;
            for (size_t i = 0; i < n; ++i) order[i] = i;
            uint32_t seed = 12345;
            for (size_t i = n - 1; i > 0; --i) {
                seed = seed * 1664525u + 1013904223u;
                size_t j = seed % (i + 1), tmp = order[i];
                order[i] = order[j]; order[j] = tmp;
            }
            // Keep the same page sequence, distributing cache-line offsets.
            for (size_t i = 0; i < n; ++i) {
                size_t page = order[i];
                order[i] = page * stride +
                    (spread && stride == 4096 ? (page % 64) * 64 : 0);
            }
            for (size_t i = 0; i < n; ++i)
                *(uintptr_t *)(base + order[i]) =
                    (uintptr_t)(base + order[(i + 1) % n]);
            uintptr_t p = (uintptr_t)(base + order[0]);
            for (size_t i = 0; i < n * 4; ++i)
                p = *(volatile uintptr_t *)p;
            for (unsigned trial = 0; trial < 3; ++trial) {
                p = (uintptr_t)(base + order[0]);
                double start = seconds();
                for (size_t i = 0; i < iterations; ++i)
                    p = *(volatile uintptr_t *)p;
                double elapsed = seconds() - start;
                uintptr_t expected = (uintptr_t)(base + order[iterations % n]);
                printf("nodes=%zu stride=%zu spread=%d trial=%u ns/load=%.3f check=%s\n",
                       n, stride, spread, trial, elapsed * 1e9 / iterations,
                       p == expected ? "PASS" : "FAIL");
                if (p != expected) return 2;
            }
            munmap(base, bytes);
            free(order);
        }
    }
    return 0;
}
