#include <stdio.h>
#include <stdint.h>
#include <time.h>
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
int main(void) {
    for (int trial = 0; trial < 3; trial++) {
        double start = now();
        for (unsigned i = 0; i < 100000; i++) {
            uint32_t a = 0, b, c = 0, d;
            __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d) :: "memory");
        }
        printf("leaf0: %.3f us/call\n", (now() - start) * 10);
    }
}
