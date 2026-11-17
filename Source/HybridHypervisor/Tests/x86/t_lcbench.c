// The arithmetic in Source/probes/lc-bench.c as a flat image: same source, tests that need no kernel beyond mmap and the clock.
// memcpy is flatlibc's SSE2 loop on both sides, unlike the full lc-bench under Rosetta, which has libSystem's.
#define main lc_main
#include "lc-bench.c"
#undef main

// a0: budget per test in hundredths of a second.
int flat_main(unsigned long a0, unsigned long a1) {
    (void)a1;
    budget = a0 ? (double)a0 / 100.0 : 0.5;
    printf("# lc-bench flat t=%.2f\n", budget);
    measure("alu", "ns/iter", 1e9, 0, alu_chunk);
    measure("branchy", "ns/iter", 1e9, 0, branchy_chunk);
    chase("chase_16k", 16 * 1024);
    chase("chase_1m", 1024 * 1024);
    chase("chase_64m", 64 * 1024 * 1024);
    copy_source = region(COPY_BYTES);
    copy_target = region(COPY_BYTES);
    if (!copy_source || !copy_target)
        die("mmap");
    memset(copy_target, 0, COPY_BYTES);
    measure("memcpy", "MB/s", COPY_BYTES / 1e6, 1, memcpy_chunk);
    measure("sse_float", "ns/iter", 1e9, 0, sse_float_chunk);
    measure("scalar_double", "ns/iter", 1e9, 0, scalar_double_chunk);
    measure("cas_uncontended", "ns/op", 1e9, 0, cas_uncontended_chunk);
    largecode();
    printf("# checksum %016lx\n", (unsigned long)sink);
    return 0;
}
