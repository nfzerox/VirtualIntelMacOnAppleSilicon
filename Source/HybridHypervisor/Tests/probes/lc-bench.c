// LightClick performance baseline: one binary for bare x86, Rosetta and guests. Prints `name value unit`
// lines; `#` lines are comments. Compare result files with scripts/lc-bench-compare. No files, no uploads.
//
// x86_64 (guest, Rosetta):
//   xcrun clang -arch x86_64 -mmacosx-version-min=10.9 -O2 Source/probes/lc-bench.c -o lc-bench
// Native arm64 (same tests, for the translation overhead): swap to -arch arm64.
//
// Run: lc-bench [-t SECONDS]. SECONDS is the approximate measuring time per test (default 1); each test takes
// about 1.4x that, plus setup. The chase tests' setup is extra and largest at 512 MB.
//
// Comparison caveats:
// - pagefault touches getpagesize() strides. Rosetta reports 4 KB on 16 KB host pages, so only one touch in four
//   there is a real fault, and native arm64 faults a 16 KB page each time.
// - cas_contended needs two vCPUs to contend; on one vCPU it measures timeslicing.
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

// arm64 would otherwise fuse a*m+b into fmadd, which x86 at this target cannot.
#pragma STDC FP_CONTRACT OFF

#define NOINLINE __attribute__((noinline))
#define TRIALS 5

typedef uint64_t (*chunk_fn)(uint64_t iterations);

// Every chunk's result lands here, so no loop's work is dead.
static volatile uint64_t sink;
// Read at run time, so the compiler cannot fold constants into the loops.
static volatile uint64_t opaque_seed = UINT64_C(0x824213e41bc67321);
static volatile double opaque_one = 1.0;
static double budget = 1.0;
// Seconds a chunk spent on its own setup, not on what it measures.
static double excluded;

static void die(const char *what) {
    perror(what);
    exit(1);
}

static double now(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static double timed_chunk(chunk_fn fn, uint64_t iterations) {
    excluded = 0;
    double start = now();
    uint64_t result = fn(iterations);
    double elapsed = now() - start - excluded;
    sink ^= result;
    return elapsed > 0 ? elapsed : 1e-9; // gettimeofday can step backward
}

// Grow a chunk until it fills one trial, which also warms caches and translations, then report the median trial.
static void measure(const char *name, const char *unit, double scale, int rate, chunk_fn fn) {
    double slice = budget / TRIALS, trials[TRIALS];
    uint64_t iterations = 1;
    for (;;) {
        double elapsed = timed_chunk(fn, iterations);
        if (elapsed >= slice * 0.7)
            break;
        iterations = elapsed < slice / 16 ? iterations * 16 : (uint64_t)(iterations * slice / elapsed * 1.1) + 1;
    }
    for (int i = 0; i < TRIALS; i++) {
        double elapsed = timed_chunk(fn, iterations);
        int j = i;
        for (; j > 0 && trials[j - 1] > elapsed; j--)
            trials[j] = trials[j - 1];
        trials[j] = elapsed;
    }
    double median = trials[TRIALS / 2];
    printf("%s %.3f %s\n", name, rate ? scale * iterations / median : scale * median / iterations, unit);
}

static void *region(size_t bytes) {
    void *base = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return base == MAP_FAILED ? NULL : base;
}

static uint32_t shuffle_state = 12345;
static uint32_t shuffle_next(void) {
    shuffle_state ^= shuffle_state << 13;
    shuffle_state ^= shuffle_state >> 17;
    shuffle_state ^= shuffle_state << 5;
    return shuffle_state;
}

// Integer ALU.

static NOINLINE uint64_t alu_chunk(uint64_t n) {
    uint64_t x = opaque_seed;
    for (uint64_t i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        x += i * UINT64_C(6364136223846793005);
    }
    return x;
}

// Macro-generated distinct functions: four per digit, named and parameterized by their digits.
#define X1(M, p) M(p##0) M(p##1) M(p##2) M(p##3)
#define X2(M, p) X1(M, p##0) X1(M, p##1) X1(M, p##2) X1(M, p##3)
#define X3(M, p) X2(M, p##0) X2(M, p##1) X2(M, p##2) X2(M, p##3)
#define X4(M, p) X3(M, p##0) X3(M, p##1) X3(M, p##2) X3(M, p##3)
#define X5(M, p) X4(M, p##0) X4(M, p##1) X4(M, p##2) X4(M, p##3)
#define X6(M, p) X5(M, p##0) X5(M, p##1) X5(M, p##2) X5(M, p##3)
#define X7(M, p) X6(M, p##0) X6(M, p##1) X6(M, p##2) X6(M, p##3)

// Branchy code with indirect calls.

#define BRANCH_TARGET(p)                                                                                               \
    static NOINLINE uint64_t b##p(uint64_t x, uint64_t y) { return (x ^ 0x##p) * UINT64_C(6364136223846793005) + y; }
X2(BRANCH_TARGET, )
#define BRANCH_POINTER(p) b##p,
static uint64_t (*const branch_targets[16])(uint64_t, uint64_t) = {X2(BRANCH_POINTER, )};

static NOINLINE uint64_t branchy_chunk(uint64_t n) {
    uint64_t v = opaque_seed, a = 0;
    for (uint64_t i = 0; i < n; i++) {
        v ^= v << 13;
        v ^= v >> 7;
        v ^= v << 17;
        // The empty asm keeps these real, unpredictable branches rather than conditional moves.
        if (v & 1) {
            __asm__ volatile("");
            a += v >> 3;
        } else {
            __asm__ volatile("");
            a ^= v << 1;
        }
        if (v & 2) {
            __asm__ volatile("");
            a = (a << 7) | (a >> 57);
        }
        a = branch_targets[(v >> 24) & 15](a, i);
    }
    return a ^ v;
}

// Memory chase: dependent loads over one shuffled cycle of cache lines.

static void **chase_cursor;

static NOINLINE uint64_t chase_chunk(uint64_t n) {
    void **p = chase_cursor;
    while (n--)
        p = *p;
    chase_cursor = p;
    return (uintptr_t)p;
}

static void chase(const char *name, size_t bytes) {
    const size_t stride = 64, nodes = bytes / stride;
    unsigned char *base = region(bytes);
    uint32_t *order = region(nodes * sizeof(*order));
    if (!base || !order) {
        // A small guest should still produce every other result.
        printf("# skip %s: no memory\n", name);
        return;
    }
    for (size_t i = 0; i < nodes; i++)
        order[i] = (uint32_t)i;
    for (size_t i = nodes - 1; i > 0; i--) {
        size_t j = shuffle_next() % (i + 1);
        uint32_t held = order[i];
        order[i] = order[j];
        order[j] = held;
    }
    for (size_t i = 0; i < nodes; i++)
        *(void **)(base + order[i] * stride) = base + order[(i + 1) % nodes] * stride;
    void **start = (void **)(base + order[0] * stride);
    munmap(order, nodes * sizeof(*order));
    // Self-check: exactly one cycle through every node. This also touches the whole set.
    chase_cursor = start;
    for (size_t i = 1; i < nodes; i++) {
        chase_chunk(1);
        if (chase_cursor == start) {
            fprintf(stderr, "%s: chase cycle is short\n", name);
            exit(1);
        }
    }
    chase_chunk(1);
    if (chase_cursor != start) {
        fprintf(stderr, "%s: chase cycle does not close\n", name);
        exit(1);
    }
    measure(name, "ns/load", 1e9, 0, chase_chunk);
    munmap(base, bytes);
}

// memcpy throughput.

#define COPY_BYTES (1024 * 1024)
static unsigned char *copy_source, *copy_target;
// Through a variable, so the compiler keeps the library call.
static void *(*volatile copy_function)(void *, const void *, size_t) = memcpy;

static NOINLINE uint64_t memcpy_chunk(uint64_t n) {
    uint64_t total = 0;
    for (uint64_t i = 0; i < n; i++) {
        copy_source[i % COPY_BYTES]++;
        copy_function(copy_target, copy_source, COPY_BYTES);
        total += copy_target[(i * 4099) % COPY_BYTES];
    }
    return total;
}

// Each copy path directly: which one libc's memcpy takes depends on the CPUID the guest sees.
#if defined(__x86_64__)
static NOINLINE uint64_t rep_movsb_chunk(uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        void *to = copy_target;
        const void *from = copy_source;
        size_t count = COPY_BYTES;
        __asm__ volatile("rep movsb" : "+D"(to), "+S"(from), "+c"(count) : : "memory");
    }
    return copy_target[n % COPY_BYTES];
}

static NOINLINE uint64_t rep_stosq_chunk(uint64_t n) {
    for (uint64_t i = 0; i < n; i++) {
        void *to = copy_target;
        size_t count = COPY_BYTES / 8;
        __asm__ volatile("rep stosq" : "+D"(to), "+c"(count) : "a"(i) : "memory");
    }
    return copy_target[n % COPY_BYTES];
}

static NOINLINE uint64_t movups_copy_chunk(uint64_t n) {
    for (uint64_t i = 0; i < n; i++)
        for (size_t at = 0; at < COPY_BYTES; at += 64)
            __asm__ volatile("movups (%0),%%xmm0\n\tmovups 16(%0),%%xmm1\n\tmovups 32(%0),%%xmm2\n\t"
                             "movups 48(%0),%%xmm3\n\tmovups %%xmm0,(%1)\n\tmovups %%xmm1,16(%1)\n\t"
                             "movups %%xmm2,32(%1)\n\tmovups %%xmm3,48(%1)"
                             :
                             : "r"(copy_source + at), "r"(copy_target + at)
                             : "memory", "xmm0", "xmm1", "xmm2", "xmm3");
    return copy_target[n % COPY_BYTES];
}

// Integer and shuffle SSE on registers, so softmmu is not in it. punpck, pshuflw, pshufhw and pshufd test
// PVG_TCG_INLINE_SSE_PACKED; pmaddwd, packuswb and psadbw are helper calls, with mask, sum and i live across
// their helpers. The asm fixes the instructions, which intrinsics would let the compiler merge.
typedef long long int2 __attribute__((vector_size(16)));

static NOINLINE uint64_t sse_int_shuffle_chunk(uint64_t n) {
    uint64_t sum = opaque_seed;
    int2 a = {(long long)sum, (long long)~sum}, b = {(long long)(sum * 3), (long long)(sum * 5)}, t;
    const int2 step = {(long long)(sum | 1), (long long)((sum << 1) | 1)}, keep = {-1LL ^ 0x0100, -1LL ^ 0x0100};
    uint32_t mask;
    for (uint64_t i = 0; i < n; i++) {
        __asm__ volatile("movdqa %[a],%[t]\n\tpunpcklbw %[b],%[a]\n\tpunpckhwd %[b],%[t]\n\t"
                         "pshuflw $0x1b,%[a],%[a]\n\tpshufhw $0xb1,%[t],%[t]\n\tpshufd $0x4e,%[t],%[t]\n\t"
                         "paddd %[t],%[a]\n\tpand %[keep],%[a]\n\tmovdqa %[a],%[t]\n\t"
                         "pcmpeqb %[b],%[t]\n\tpmovmskb %[t],%[mask]\n\t"
                         "movdqa %[a],%[t]\n\tpmaddwd %[b],%[t]\n\tpackuswb %[a],%[t]\n\tpsadbw %[b],%[t]\n\t"
                         "paddd %[t],%[a]\n\tpaddd %[step],%[b]"
                         : [a] "+x"(a), [b] "+x"(b), [t] "=&x"(t), [mask] "=&r"(mask)
                         : [keep] "x"(keep), [step] "x"(step));
        sum += mask + i;
    }
    return sum ^ (uint64_t)a[0] ^ (uint64_t)b[1];
}
#endif

// Floating point: the a and c sequences settle on ordinary normal values, so neither overflows nor goes denormal.

typedef float float4 __attribute__((vector_size(16)));

static NOINLINE uint64_t sse_float_chunk(uint64_t n) {
    float one = (float)opaque_one;
    float4 a = {one, one * 2, one * 3, one * 4}, c = a;
    const float4 m = {0.999f * one, 0.998f * one, 0.997f * one, 0.996f * one};
    const float4 b = {one, one, one, one};
    const float4 d = {1.5f * one, 1.25f * one, 1.75f * one, 2.0f * one};
    for (uint64_t i = 0; i < n; i++) {
        a = a * m + b;
        c = (c + a) / d;
    }
    return (uint64_t)(c[0] + c[1] + c[2] + c[3]);
}

static NOINLINE uint64_t scalar_double_chunk(uint64_t n) {
    double one = opaque_one, a = one, c = one;
    const double m = 0.999 * one, d = 1.5 * one;
    for (uint64_t i = 0; i < n; i++) {
        a = a * m + one;
        c = (c + a) / d;
    }
    return (uint64_t)c;
}

// Atomics.

static volatile uint32_t cas_word __attribute__((aligned(64)));
static uint32_t cas_target;

static NOINLINE uint64_t cas_uncontended_chunk(uint64_t n) {
    uint32_t expected = cas_word;
    for (uint64_t i = 0; i < n; i++)
        expected = __sync_val_compare_and_swap(&cas_word, expected, expected + 1) + 1;
    return expected;
}

// Both threads race one counter to the same total, so neither runs on alone after the other finishes.
static void *cas_racer(void *unused) {
    (void)unused;
    for (;;) {
        uint32_t seen = __atomic_load_n(&cas_word, __ATOMIC_RELAXED);
        if (seen >= cas_target)
            return NULL;
        __sync_val_compare_and_swap(&cas_word, seen, seen + 1);
    }
}

static NOINLINE uint64_t cas_contended_chunk(uint64_t n) {
    pthread_t helper;
    cas_word = 0;
    cas_target = (uint32_t)n;
    if (pthread_create(&helper, NULL, cas_racer, NULL))
        die("pthread_create");
    cas_racer(NULL);
    pthread_join(helper, NULL);
    return cas_word;
}

// Kernel entry. getppid because Darwin's getpid can answer from a userspace cache.

static NOINLINE uint64_t syscall_chunk(uint64_t n) {
    uint64_t total = 0;
    for (uint64_t i = 0; i < n; i++)
        total += (uint64_t)getppid();
    return total;
}

// Page fault: only the touches count. Fixed-size maps keep a fast host from mapping gigabytes.

static NOINLINE uint64_t pagefault_chunk(uint64_t n) {
    const size_t page = (size_t)getpagesize(), batch = 4096;
    uint64_t total = 0;
    while (n) {
        size_t pages = n < batch ? (size_t)n : batch;
        double start = now();
        volatile unsigned char *base = region(pages * page);
        if (!base)
            die("mmap");
        excluded += now() - start;
        for (size_t i = 0; i < pages; i++)
            base[i * page] = 1;
        total += base[0];
        start = now();
        munmap((void *)base, pages * page);
        excluded += now() - start;
        n -= pages;
    }
    return total;
}

static NOINLINE uint64_t fork_chunk(uint64_t n) {
    uint64_t total = 0;
    for (uint64_t i = 0; i < n; i++) {
        int status;
        pid_t child = fork();
        if (child < 0)
            die("fork");
        if (child == 0)
            _exit(0);
        while (waitpid(child, &status, 0) < 0)
            if (errno != EINTR)
                die("waitpid");
        total += (uint64_t)status + (uint64_t)child;
    }
    return total;
}

// Thread context switch: each round trip blocks twice, once on each thread.

static int ping_pipe[2], pong_pipe[2];

static void transfer(int fd, int writing) {
    char value = 1;
    ssize_t n;
    do {
        n = writing ? write(fd, &value, 1) : read(fd, &value, 1);
    } while (n < 0 && errno == EINTR);
    if (n != 1)
        die("pipe transfer");
}

static void *pong_thread(void *count) {
    for (uintptr_t i = (uintptr_t)count; i > 0; i--) {
        transfer(ping_pipe[0], 0);
        transfer(pong_pipe[1], 1);
    }
    return NULL;
}

static NOINLINE uint64_t ctxsw_chunk(uint64_t n) {
    pthread_t helper;
    if (pipe(ping_pipe) || pipe(pong_pipe))
        die("pipe");
    if (pthread_create(&helper, NULL, pong_thread, (void *)(uintptr_t)n))
        die("pthread_create");
    for (uint64_t i = 0; i < n; i++) {
        transfer(ping_pipe[1], 1);
        transfer(pong_pipe[0], 0);
    }
    pthread_join(helper, NULL);
    close(ping_pipe[0]);
    close(ping_pipe[1]);
    close(pong_pipe[0]);
    close(pong_pipe[1]);
    return n;
}

// Large code: 16384 distinct functions of several basic blocks each, called in shuffled order. The first pass has
// to translate or page in every one and can only happen once per process, so it is one sample and its own line.

#define CODE_FUNCTIONS 16384
#define CODE_FUNCTION(p)                                                                                               \
    static NOINLINE uint32_t f##p(uint32_t x) {                                                                        \
        x = x * (0x##p | 1u) + 0x##p;                                                                                  \
        if (x & 0x10) {                                                                                                \
            __asm__ volatile("");                                                                                      \
            x ^= (x >> 7) + 0x##p;                                                                                     \
        } else {                                                                                                       \
            __asm__ volatile("");                                                                                      \
            x += (x << 3) ^ 0x##p;                                                                                     \
        }                                                                                                              \
        return x;                                                                                                      \
    }
X7(CODE_FUNCTION, )
#define CODE_POINTER(p) f##p,
static uint32_t (*const code[CODE_FUNCTIONS])(uint32_t) = {X7(CODE_POINTER, )};
static uint16_t code_order[CODE_FUNCTIONS];

static NOINLINE uint64_t largecode_chunk(uint64_t n) {
    static uint32_t next;
    uint32_t x = (uint32_t)opaque_seed, at = next;
    for (uint64_t i = 0; i < n; i++) {
        x = code[code_order[at]](x);
        at = (at + 1) % CODE_FUNCTIONS;
    }
    next = at;
    return x;
}

static void largecode(void) {
    for (uint32_t i = 0; i < CODE_FUNCTIONS; i++)
        code_order[i] = (uint16_t)i;
    for (uint32_t i = CODE_FUNCTIONS - 1; i > 0; i--) {
        uint32_t j = shuffle_next() % (i + 1);
        uint16_t held = code_order[i];
        code_order[i] = code_order[j];
        code_order[j] = held;
    }
    double elapsed = timed_chunk(largecode_chunk, CODE_FUNCTIONS);
    printf("largecode_cold %.3f ns/function\n", elapsed * 1e9 / CODE_FUNCTIONS);
    measure("largecode_warm", "ns/call", 1e9, 0, largecode_chunk);
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "-t") == 0)
        budget = strtod(argv[2], NULL);
    if ((argc != 1 && argc != 3) || !(budget > 0)) {
        fprintf(stderr, "usage: %s [-t SECONDS_PER_TEST]\n", argv[0]);
        return 64;
    }
    setbuf(stdout, NULL);
#if defined(__x86_64__)
    const char *arch = "x86_64";
#elif defined(__i386__)
    const char *arch = "i386";
#else
    const char *arch = "arm64";
#endif
    printf("# lc-bench arch=%s t=%g\n", arch, budget);

    measure("alu", "ns/iter", 1e9, 0, alu_chunk);
    measure("branchy", "ns/iter", 1e9, 0, branchy_chunk);
    chase("chase_16k", 16 * 1024);
    chase("chase_1m", 1024 * 1024);
    chase("chase_64m", 64 * 1024 * 1024);
    chase("chase_512m", 512 * 1024 * 1024);

    copy_source = region(COPY_BYTES);
    copy_target = region(COPY_BYTES);
    if (!copy_source || !copy_target)
        die("mmap");
    memset(copy_target, 0, COPY_BYTES);
    measure("memcpy", "MB/s", COPY_BYTES / 1e6, 1, memcpy_chunk);
#if defined(__x86_64__)
    measure("rep_movsb", "MB/s", COPY_BYTES / 1e6, 1, rep_movsb_chunk);
    measure("rep_stosq", "MB/s", COPY_BYTES / 1e6, 1, rep_stosq_chunk);
    measure("movups_copy", "MB/s", COPY_BYTES / 1e6, 1, movups_copy_chunk);
#endif

    measure("sse_float", "ns/iter", 1e9, 0, sse_float_chunk);
    measure("scalar_double", "ns/iter", 1e9, 0, scalar_double_chunk);
#if defined(__x86_64__)
    measure("sse_int_shuffle", "ns/iter", 1e9, 0, sse_int_shuffle_chunk);
#endif
    measure("cas_uncontended", "ns/op", 1e9, 0, cas_uncontended_chunk);
    measure("cas_contended", "ns/op", 1e9, 0, cas_contended_chunk);
    measure("syscall", "ns/call", 1e9, 0, syscall_chunk);
    measure("pagefault", "ns/page", 1e9, 0, pagefault_chunk);
    measure("fork_wait", "us/fork", 1e6, 0, fork_chunk);
    measure("ctxsw", "us/roundtrip", 1e6, 0, ctxsw_chunk);
    largecode();
    return 0;
}
