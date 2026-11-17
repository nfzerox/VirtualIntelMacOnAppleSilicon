// Compiled long double code, whose result is compared with Rosetta's, then what each x87 path costs (lines starting
// "time", which the comparison drops).
#include "flatlibc.h"

static double now(void) {
    struct timeval t;
    gettimeofday(&t, 0);
    return (double)t.tv_sec + (double)t.tv_usec * 1e-6;
}

static uint64_t hash;
static void mix(long double v) {
    uint64_t w[2] = {0, 0};
    memcpy(w, &v, 10);
    hash = (hash ^ w[0] ^ w[1] << 48) * 0x100000001b3ULL;
    hash ^= hash >> 29;
}

static void mandelbrot(void) {
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < 40; x++) {
            long double cr = -2 + x * 0.075L, ci = -1.5L + y * 0.075L, zr = 0, zi = 0;
            int n = 0;
            for (; n < 200 && zr * zr + zi * zi < 4; n++) {
                long double t = zr * zr - zi * zi + cr;
                zi = 2 * zr * zi + ci;
                zr = t;
            }
            mix(zr + zi + n);
        }
}

static void conversions(void) {
    volatile double d = 1.0 / 3;
    volatile float f = 2.0f / 7;
    for (int i = -500; i < 500; i++) {
        long double v = d * i + f;
        mix(v);
        mix((double)v);
        mix((float)v);
        mix((long)v);
        mix((int)(v * 1e6L));
        mix((long double)(short)(i * 7));
    }
}

static void rounding(void) {
    static const uint16_t cw[] = {0x37F, 0x77F, 0xB7F, 0xF7F, 0x27F, 0x7F, 0x67F};
    for (unsigned k = 0; k < sizeof cw / sizeof cw[0]; k++) {
        __asm__ volatile("fldcw %0" : : "m"(cw[k]));
        long double a = 1, b = 3;
        for (int i = 0; i < 200; i++) {
            a = a / b + i;
            b = b * 1.0625L;
            mix(a);
        }
    }
    __asm__ volatile("fldcw %0" : : "m"(cw[0]));
}

static double per_op(double seconds, long n) { return seconds / n * 1e9; }

#define TIME(name, ops, body)                                                                                          \
    do {                                                                                                               \
        double t0 = now();                                                                                             \
        for (long i = 0; i < 2000000; i++) {                                                                           \
            body;                                                                                                      \
        }                                                                                                              \
        printf("time %-16s %6.1f ns per operation\n", name, per_op(now() - t0, 2000000L * (ops)));                     \
    } while (0)

int flat_main(unsigned long a0, unsigned long a1) {
    (void)a0;
    (void)a1;
    hash = 0xcbf29ce484222325ULL;
    mandelbrot();
    conversions();
    rounding();
    printf("hash %016lx\n", hash);

    static double cell = 1.5;
    static char image[512] __attribute__((aligned(16)));
    TIME("fld, fstp m64", 2, __asm__ volatile("fldl %0\n fstpl %0" : "+m"(cell)));
    TIME("fld1, fadd, fstp", 3, __asm__ volatile("fld1\n fadd %%st(0), %%st\n fstp %%st(0)" : :));
    TIME("fmul m64", 3, __asm__ volatile("fld1\n fmull %0\n fstp %%st(0)" : : "m"(cell)));
    TIME("fdiv", 4, __asm__ volatile("fld1\n fldpi\n fdivrp\n fstp %%st(0)" : :));
    TIME("fnstcw, fldcw", 2, __asm__ volatile("fnstcw %0\n fldcw %0" : "+m"(*(uint16_t *)image)));
    TIME("fxsave64", 1, __asm__ volatile("fxsave64 %0" : "=m"(image)));
    TIME("fxrstor64", 1, __asm__ volatile("fxrstor64 %0" : : "m"(image)));
    return 0;
}
