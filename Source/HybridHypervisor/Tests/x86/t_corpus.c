// Real compiler output in volume: ordinary algorithms built at -O0, -O1, -O2, -O3 and -Os, each printing checksums. The transcripts must match
// Rosetta's. Inputs come from a fixed generator seeded by the first argument.
#include "flatlibc.h"

static uint64_t seed;
static uint64_t next64(void) {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}

// SHA-256: rotates, shifts, 32-bit arithmetic, big-endian loads.
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256(const uint8_t *data, uint64_t len, uint32_t h[8]) {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(h, init, sizeof init);
    uint8_t block[64];
    uint64_t done = 0;
    bool padded = false, finished = false;
    while (!finished) {
        uint64_t take = len - done < 64 ? len - done : 64;
        memset(block, 0, 64);
        memcpy(block, data + done, take);
        done += take;
        if (take < 64) {
            if (!padded) {
                block[take] = 0x80;
                padded = true;
            }
            if (take < 56) {
                for (int i = 0; i < 8; i++)
                    block[63 - i] = (uint8_t)((len * 8) >> (8 * i));
                finished = true;
            }
        }
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }
}

// A byte-oriented LZ compressor and decompressor: pointers, memcmp-like loops, narrow types, branches.
static int lz_compress(const uint8_t *in, int n, uint8_t *out) {
    static int16_t table[4096];
    memset(table, 0xff, sizeof table);
    int ip = 0, op = 0;
    while (ip < n) {
        int best_len = 0, best_off = 0;
        if (ip + 3 < n) {
            uint32_t key = (uint32_t)(in[ip] | in[ip + 1] << 8 | in[ip + 2] << 16) * 2654435761u >> 20;
            int cand = table[key];
            table[key] = (int16_t)ip;
            if (cand >= 0 && ip - cand < 4096) {
                int l = 0;
                while (ip + l < n && l < 255 && in[cand + l] == in[ip + l])
                    l++;
                if (l >= 3) {
                    best_len = l;
                    best_off = ip - cand;
                }
            }
        }
        if (best_len) {
            out[op++] = 0xFF;
            out[op++] = (uint8_t)best_len;
            out[op++] = (uint8_t)best_off;
            out[op++] = (uint8_t)(best_off >> 8);
            ip += best_len;
        } else {
            if (in[ip] == 0xFF) {
                out[op++] = 0xFF;
                out[op++] = 0;
            } else
                out[op++] = in[ip];
            ip++;
        }
    }
    return op;
}
static int lz_decompress(const uint8_t *in, int n, uint8_t *out) {
    int ip = 0, op = 0;
    while (ip < n) {
        uint8_t c = in[ip++];
        if (c != 0xFF)
            out[op++] = c;
        else if (in[ip] == 0) {
            out[op++] = 0xFF;
            ip++;
        } else {
            int l = in[ip], off = in[ip + 1] | in[ip + 2] << 8;
            ip += 3;
            for (int i = 0; i < l; i++, op++)
                out[op] = out[op - off];
        }
    }
    return op;
}

// Sorting with a function pointer and a struct, and a jump table.
typedef struct {
    int32_t key;
    uint16_t small;
    uint8_t tag;
    double weight;
} Item;
static int cmp_key(const void *a, const void *b) { return ((const Item *)a)->key - ((const Item *)b)->key; }
static int cmp_weight(const void *a, const void *b) {
    double x = ((const Item *)a)->weight, y = ((const Item *)b)->weight;
    return x < y ? -1 : x > y;
}
static void qsort_(void *base, long n, long size, int (*cmp)(const void *, const void *)) {
    char *b = base;
    char tmp[64];
    if (n < 2)
        return;
    long i = 0, j = n - 1;
    memcpy(tmp, b + (n / 2) * size, (unsigned long)size);
    while (i <= j) {
        while (cmp(b + i * size, tmp) < 0)
            i++;
        while (cmp(b + j * size, tmp) > 0)
            j--;
        if (i <= j) {
            char t[64];
            memcpy(t, b + i * size, (unsigned long)size);
            memcpy(b + i * size, b + j * size, (unsigned long)size);
            memcpy(b + j * size, t, (unsigned long)size);
            i++;
            j--;
        }
    }
    qsort_(b, j + 1, size, cmp);
    qsort_(b + i * size, n - i, size, cmp);
}

static uint32_t interp(const uint8_t *code, int n, uint32_t acc) {
    for (int pc = 0; pc < n; pc++) {
        switch (code[pc] & 15) {
            case 0:
                acc += code[pc];
                break;
            case 1:
                acc ^= acc << 5;
                break;
            case 2:
                acc ^= acc >> 3;
                break;
            case 3:
                acc *= 33;
                break;
            case 4:
                acc = ~acc;
                break;
            case 5:
                acc -= pc;
                break;
            case 6:
                acc = (acc << 7) | (acc >> 25);
                break;
            case 7:
                if (acc & 1)
                    pc++;
                break;
            case 8:
                acc |= 0x10;
                break;
            case 9:
                acc &= 0xFFFFFFF;
                break;
            case 10:
                acc += acc / (code[pc] | 1);
                break;
            case 11:
                acc ^= acc % (code[pc] | 1);
                break;
            default:
                acc += 3;
        }
    }
    return acc;
}

// Multi-precision arithmetic: adc chains through __int128, shifts through two words.
static void bigmul(const uint64_t *a, const uint64_t *b, int n, uint64_t *r) {
    for (int i = 0; i < 2 * n; i++)
        r[i] = 0;
    for (int i = 0; i < n; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < n; j++) {
            unsigned __int128 t = (unsigned __int128)a[i] * b[j] + r[i + j] + carry;
            r[i + j] = (uint64_t)t;
            carry = (uint64_t)(t >> 64);
        }
        r[i + n] = carry;
    }
}

// Floating point: float and double math in matrices, conversions, compares, min and max.
static double matrix_trace(int n, uint64_t s) {
    double a[8][8], b[8][8], c[8][8];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            a[i][j] = (double)(s >> 40) / 1000.0;
            b[i][j] = (double)(int32_t)(s >> 20) / 7.0;
        }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double t = 0;
            for (int k = 0; k < n; k++)
                t += a[i][k] * b[k][j];
            c[i][j] = t;
        }
    double tr = 0;
    for (int i = 0; i < n; i++)
        tr += c[i][i] > 0 ? c[i][i] : -c[i][i] / 3;
    return tr;
}

static float float_mix(uint32_t n) {
    float x = 1.5f, y = 0.25f;
    for (uint32_t i = 0; i < n; i++) {
        x = x * 1.0009765625f + y;
        y = x > 100.0f ? y / 2 : y + 0.125f;
        if (x > 1e6f)
            x = (float)(int)(x / 1000.0f);
        y = (float)(uint32_t)(y * 8.0f) / 8.0f + (float)(i & 3);
    }
    return x + y;
}

static long sum_var(int n, ...) {
    va_list ap;
    va_start(ap, n);
    long s = 0;
    for (int i = 0; i < n; i++)
        s = s * 3 + (i & 1 ? va_arg(ap, long) : (long)va_arg(ap, int));
    va_end(ap);
    return s;
}

typedef struct {
    uint64_t a, b, c;
} Big;
static Big make_big(uint64_t x) {
    Big r = {x, x * 2, x ^ 0xFF};
    return r;
}
static uint64_t use_big(Big b) { return b.a + b.b * b.c; }

int flat_main(unsigned long a0, unsigned long a1) {
    (void)a1;
    seed = a0 ? a0 : 0x123456789ABCDEFULL;
    static uint8_t buf[1 << 18], comp[1 << 19], back[1 << 18];
    for (int round = 0; round < 6; round++) {
        int n = (int)(next64() % (sizeof buf - 64)) + 64;
        // Compressible: runs from a small alphabet, with random stretches.
        for (int i = 0; i < n;) {
            int run = (int)(next64() % 40) + 1;
            uint8_t v = (uint8_t)(next64() & (round & 1 ? 0x0F : 0xFF));
            for (int j = 0; j < run && i < n; j++, i++)
                buf[i] = (uint8_t)(v + (j % 3));
        }
        uint32_t h[8];
        sha256(buf, (uint64_t)n, h);
        printf("sha %08x%08x n=%d\n", h[0], h[7], n);
        int cn = lz_compress(buf, n, comp);
        int dn = lz_decompress(comp, cn, back);
        printf("lz %d->%d->%d same=%d\n", n, cn, dn, memcmp(buf, back, (unsigned long)n) == 0);
        printf("interp %08x\n", interp(buf, n, 0x55));
    }
    static Item items[8192];
    for (int i = 0; i < 8192; i++) {
        uint64_t r = next64();
        items[i].key = (int32_t)(r >> 40);
        items[i].small = (uint16_t)r;
        items[i].tag = (uint8_t)(r >> 16);
        items[i].weight = (double)(int64_t)r / 3.0;
    }
    qsort_(items, 8192, sizeof(Item), cmp_key);
    printf("sorted key %d %d %d\n", items[0].key, items[4096].key, items[8191].key);
    qsort_(items, 8192, sizeof(Item), cmp_weight);
    printf("sorted weight %d %.3f\n", items[4096].tag, items[4096].weight / 1e12);
    uint64_t a[32], b[32], r[64];
    for (int i = 0; i < 32; i++) {
        a[i] = next64();
        b[i] = next64();
    }
    for (int i = 0; i < 40; i++) {
        bigmul(a, b, 32, r);
        a[i % 32] = r[31];
        b[(i * 7) % 32] = r[63];
    }
    printf("bigmul %016lx %016lx\n", r[0], r[63]);
    printf("matrix %.6f %.6f\n", matrix_trace(5, next64()), matrix_trace(8, next64()));
    printf("float %.5f\n", (double)float_mix(100000));
    printf("var %ld\n", sum_var(6, 1, 2L, 3, 4L, 5, 6L));
    printf("big %lu\n", use_big(make_big(next64() >> 40)));
    // MXCSR from compiled code: each rounding mode, with flush-to-zero and denormals-are-zero, control bits read back.
    static const uint32_t modes[] = {0x1F80, 0x3F80, 0x5F80, 0x7F80, 0x9F80, 0x1FC0};
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        uint32_t saved, back;
        volatile double v = 2.5, tiny = 1e-310, third = 1.0 / 3.0;
        volatile float f = 2.5f, small = 1e-40f;
        __asm__ volatile("stmxcsr %0\n ldmxcsr %1" : "=m"(saved) : "m"(modes[i]));
        long r1;
        int r2;
        __asm__ volatile("cvtsd2si %1, %0" : "=r"(r1) : "x"(v));
        __asm__ volatile("cvtss2si %1, %0" : "=r"(r2) : "x"(f));
        double d = tiny * 1.0 + third;
        float g = small * 0.5f;
        double e = tiny * 0.5;
        __asm__ volatile("stmxcsr %0\n ldmxcsr %1" : "=m"(back) : "m"(saved));
        printf("mxcsr %x: %ld %d %.12f %.12f %d %d\n", back & 0xFFC0, r1, r2, d, third, g == 0.0f, e == 0.0);
    }
    char text[128];
    strcpy(text, "the quick brown fox");
    printf("str %lu %d\n", strlen(text), strcmp(text, "the quick"));
    return 0;
}
