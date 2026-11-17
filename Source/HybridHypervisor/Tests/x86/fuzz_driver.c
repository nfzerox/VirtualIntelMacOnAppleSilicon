// Differential fuzz driver: for each generated form, run with deterministic random inputs and print one line per form with a hash
// of every output. Comparing transcripts between engines finds divergence; `flat_main(form, 1)` prints each round of one form.
#include "flatlibc.h"

typedef struct {
    uint64_t r[16];
    uint64_t x[16][2];
    uint64_t rflags, mxcsr, rsp_delta;
} State;
extern State fuzz_st;
extern void fuzz_call(const void *form);
typedef struct {
    const void *entry;
    const char *name;
    uint32_t mask, fp;
} Form;
extern const Form fuzz_table[];

#define BUF_BYTES 8192
// r15, r13 and r11 of the forms: 32-bit code has ebp, esi and edi for them.
#ifdef __i386__
enum { R_BUF = 5, R_BUF2 = 6, R_INDEX = 7 };
#define FMT_HASH "%016llx"
#else
enum { R_BUF = 15, R_BUF2 = 13, R_INDEX = 11 };
#define FMT_HASH "%016lx"
#endif
static uint64_t buffer[BUF_BYTES / 8] __attribute__((aligned(4096)));

static uint64_t rng_state;
static uint64_t rnd(void) {
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return rng_state = x;
}

static uint64_t special_int(void) {
    static const uint64_t v[] = {0, 1, 2, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff,
                                 0x100000000, 0x7fffffffffffffff, 0x8000000000000000, ~0ULL, 0x0f, 0x10, 0x4040404040404040};
    uint64_t p = rnd();
    if ((p & 3) == 0)
        return v[(p >> 8) % (sizeof v / sizeof v[0])];
    if ((p & 3) == 1)
        return rnd() & 0xff;
    return rnd();
}

static uint64_t fp_pattern64(void) {
    static const uint64_t v[] = {0,
                                 0x8000000000000000,
                                 0x3ff0000000000000,
                                 0xbff0000000000000,
                                 0x7ff0000000000000,
                                 0xfff0000000000000,
                                 0x7ff8000000000000,
                                 0xfff8000000000000,
                                 0x7ff0000000000001, // SNaN
                                 0x0000000000000001, // denormal
                                 0x800fffffffffffff,
                                 0x0010000000000000,
                                 0x41e0000000000000, // 2^31
                                 0xc1e0000000000000,
                                 0x43e0000000000000, // 2^63
                                 0xc3e0000000000000,
                                 0x4000000000000000,
                                 0x3fe0000000000000,
                                 0x4008000000000000};
    uint64_t p = rnd();
    if ((p & 3) == 0)
        return v[(p >> 8) % (sizeof v / sizeof v[0])];
    // A normal number of modest magnitude, so that arithmetic is meaningful.
    return (rnd() & 0x800fffffffffffff) | ((1023 + (rnd() % 64) - 32) << 52);
}
static uint32_t fp_pattern32(void) {
    static const uint32_t v[] = {0, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000, 0x7f800001,
                                 0x00000001, 0x807fffff, 0x00800000, 0x4f000000, 0xcf000000, 0x5f000000, 0xdf000000, 0x40000000, 0x3f000000};
    uint64_t p = rnd();
    if ((p & 3) == 0)
        return v[(p >> 8) % (sizeof v / sizeof v[0])];
    return ((uint32_t)rnd() & 0x807fffff) | ((uint32_t)(127 + (rnd() % 32) - 16) << 23);
}

static void fp_pair(uint64_t out[2]) {
    uint64_t kind = rnd() & 3;
    if (kind == 0) {
        out[0] = fp_pattern64();
        out[1] = fp_pattern64();
    } else if (kind == 1) {
        out[0] = (uint64_t)fp_pattern32() << 32 | fp_pattern32();
        out[1] = (uint64_t)fp_pattern32() << 32 | fp_pattern32();
    } else {
        out[0] = special_int();
        out[1] = special_int();
    }
}

// Specials, then normals near the ends of the range and around one, so that overflow, underflow and cancellation all come up.
static void x87_pattern(uint64_t out[2], bool unsupported) {
    static const uint64_t v[][2] = {
        {0, 0},                                                     {0, 0x8000},
        {0x8000000000000000, 0x3FFF},                               {0x8000000000000000, 0xBFFF},
        {0x8000000000000000, 0x7FFF},                               {0x8000000000000000, 0xFFFF}, // infinities
        {0xC000000000000000, 0x7FFF},                               {0xC000000000000000, 0xFFFF}, // indefinite
        {0xC000000000000123, 0x7FFF},                               {0x8000000000000123, 0x7FFF}, // QNaN, SNaN
        {0xFFFFFFFFFFFFFFFF, 0xFFFF},                               {0xBFFFFFFFFFFFFFFF, 0x7FFF},
        {1, 0},                                                     {0x7FFFFFFFFFFFFFFF, 0x8000}, // denormals
        {0x8000000000000000, 1},                                    {0xFFFFFFFFFFFFFFFF, 0x7FFE}, // smallest normal, largest
        {0x8000000000000000, 0x403E},                               {0xFFFFFFFFFFFFFFFF, 0x403D}, // 2^63, under it
        {0x8000000000000000, 0xC03E},                               {0xC90FDAA22168C235, 0x4000},
    };
    static const uint64_t bad[][2] = {
        {0x8000000000000000, 0},      {0x8000000000000001, 0x8000}, // pseudo-denormals
        {0x4000000000000000, 0x3FFF}, {0, 0x4000},                  // unnormal, pseudo-zero
        {0x4000000000000000, 0x7FFF}, {0, 0x7FFF},                  {1, 0xFFFF}, // pseudo-NaN, pseudo-infinity
    };
    uint64_t p = rnd();
    if (unsupported && (p & 3) == 0) {
        out[0] = bad[(p >> 8) % (sizeof bad / sizeof bad[0])][0];
        out[1] = bad[(p >> 8) % (sizeof bad / sizeof bad[0])][1];
        return;
    }
    if ((p & 3) <= 1) {
        out[0] = v[(p >> 8) % (sizeof v / sizeof v[0])][0];
        out[1] = v[(p >> 8) % (sizeof v / sizeof v[0])][1];
        return;
    }
    uint64_t sign = rnd() & 0x8000, e = rnd() % 8, m = rnd() | 0x8000000000000000;
    uint64_t low = (p >> 16) & 3; // short significands make exact results and ties
    if (low == 1)
        m &= 0xFFFFFFFFFF000000;
    else if (low == 2)
        m &= 0xFFFFFFFFFFFFF800;
    uint64_t exp = e == 0 ? rnd() % 96 + 1 : e == 1 ? 0x7FFE - rnd() % 96 : e == 2 ? 0x3FFF - 16445 + rnd() % 128 + 16383 - 16383 : 0x3FFF - 32 + rnd() % 64;
    out[0] = m;
    out[1] = sign | (exp & 0x7FFF);
}

extern struct {
    uint16_t cw;
    uint8_t depth, pad[13];
    uint64_t regs[8][2];
} fuzz_x87_in;
extern uint8_t fuzz_x87_out[512];

static uint64_t hash;
static void mix(uint64_t v) {
    hash ^= v;
    hash *= 0x100000001b3ULL;
    hash ^= hash >> 29;
}

#ifdef __i386__
// Compared with TCG, which takes the x87 rule for two NaNs where the SDM gives SSE the first source, and keeps a denormal in min and max under DAZ where the engine
// gives zero, as Rosetta does. So no second source holds a NaN, and DAZ stays off: the 64-bit run holds those to Rosetta, and the emitters don't look at the mode.
static const uint64_t mxcsr_choices[] = {0x1F80, 0x1F80, 0x1F80, 0x1F80, 0x3F80, 0x5F80, 0x7F80, 0x9F80};
static void no_nan(uint64_t *v) {
    uint32_t *lanes = (uint32_t *)v;
    for (int i = 0; i < 4; i++)
        if ((lanes[i] & 0x7f800000) == 0x7f800000)
            lanes[i] &= ~0x00800000u;
}
#else
static const uint64_t mxcsr_choices[] = {0x1F80, 0x1F80, 0x1F80, 0x1F80, 0x3F80, 0x5F80, 0x7F80, 0x9F80, 0x1FC0, 0x9FC0};
#endif

static void run_round(const Form *f, bool verbose) {
    for (int i = 0; i < BUF_BYTES / 8; i++)
        buffer[i] = 0;
    // The first kilobyte is what forms touch.
    for (int i = 0; i < 128; i++)
        buffer[i] = f->fp && (i >= 4 && i < 8) ? 0 : special_int();
    uint64_t mem[2];
    fp_pair(mem);
    if (f->fp) {
        buffer[4] = mem[0];
        buffer[5] = mem[1];
    }
    if (f->fp >= 2) {
        static const uint16_t pc[4] = {0, 0x200, 0x300, 0x300};
        fuzz_x87_in.cw = (uint16_t)(0x7F | pc[rnd() & 3] | (rnd() & 3) << 10);
        uint64_t depth = rnd() % 12;
        fuzz_x87_in.depth = (uint8_t)(depth > 8 ? 3 : depth);
        for (int i = 0; i < 8; i++)
            x87_pattern(fuzz_x87_in.regs[i], f->fp == 3);
        uint64_t k = rnd() & 3;
        if (k == 0)
            x87_pattern(mem, f->fp == 3);
        else if (k == 3) {
            mem[0] = special_int();
            mem[1] = rnd();
        }
        buffer[4] = mem[0];
        buffer[5] = mem[1];
    }
    for (int i = 0; i < 16; i++)
        fuzz_st.r[i] = special_int();
    fuzz_st.r[R_BUF] = (uint64_t)buffer;
    fuzz_st.r[R_BUF2] = (uint64_t)buffer + 256;
    fuzz_st.r[R_INDEX] &= 7;
    for (int i = 0; i < 16; i++)
        fp_pair(fuzz_st.x[i]);
#ifdef __i386__
    no_nan(&buffer[4]);
    no_nan(mem);
    for (int i = 2; i < 5; i++)
        no_nan(fuzz_st.x[i]);
#endif
    fuzz_st.rflags = (rnd() & 0x8D5) | 2 | 0x200;
    fuzz_st.mxcsr = f->fp ? mxcsr_choices[rnd() % (sizeof mxcsr_choices / sizeof mxcsr_choices[0])] : 0x1F80;
    uint64_t in_rax = fuzz_st.r[0], in_rbx = fuzz_st.r[3], in_rcx = fuzz_st.r[1], in_flags = fuzz_st.rflags, in_mx = fuzz_st.mxcsr;
    uint64_t in_x1[2] = {fuzz_st.x[1][0], fuzz_st.x[1][1]}, in_x2[2] = {fuzz_st.x[2][0], fuzz_st.x[2][1]};
    fuzz_call(f->entry);
    uint64_t base = (uint64_t)buffer;
    for (int i = 0; i < 16; i++) {
        uint64_t v = fuzz_st.r[i];
        if (v >= base && v < base + BUF_BYTES)
            v -= base; // addresses differ only by nothing today, but keep the transcript address-free
        mix(v);
    }
    mix(fuzz_st.rflags & f->mask);
    if (f->fp) {
        static const int kept[] = {0, 1, 2, 3, 4, 9, 12};
        for (unsigned i = 0; i < sizeof kept / sizeof kept[0]; i++) {
            mix(fuzz_st.x[kept[i]][0]);
            mix(fuzz_st.x[kept[i]][1]);
        }
#ifdef __i386__
        mix(fuzz_st.mxcsr & ~2u); // the comparison here is with TCG, which raises DE by rules of its own. Rosetta's 64-bit run holds DE to account.
#else
        mix(fuzz_st.mxcsr);
#endif
    }
    if (f->fp >= 2) {
        for (int i = 0; i < 6; i++)
            mix(fuzz_x87_out[i]);
        // Empty registers: Rosetta's FXSAVE shows them as the indefinite, a CPU's keeps what was there, so only registers in use count.
        int top = fuzz_x87_out[3] >> 3 & 7;
        for (int i = 0; i < 8; i++)
            if (fuzz_x87_out[4] >> ((top + i) & 7) & 1) {
                // After a stack fault Rosetta treats the empty operand as a NaN and applies the NaN rules, where a CPU's masked response is the indefinite: hybrid.c checks that.
                bool stack_nan = (fuzz_x87_out[2] & 0x40) && (*(uint16_t *)(fuzz_x87_out + 40 + i * 16) & 0x7FFF) == 0x7FFF;
                if (stack_nan)
                    continue;
                mix(*(uint64_t *)(fuzz_x87_out + 32 + i * 16));
                mix(*(uint16_t *)(fuzz_x87_out + 40 + i * 16));
            }
    }
    mix(fuzz_st.rsp_delta);
    for (int i = 0; i < BUF_BYTES / 8; i++)
        mix(buffer[i]);
    if (verbose) {
        if (f->fp >= 2) {
            printf(" x87 in cw=%04x depth=%d mem=%016lx%016lx\n", fuzz_x87_in.cw, fuzz_x87_in.depth, mem[1], mem[0]);
            for (int i = 0; i < fuzz_x87_in.depth; i++)
                printf("  load%d %04lx %016lx\n", i, fuzz_x87_in.regs[i][1], fuzz_x87_in.regs[i][0]);
            printf(" x87 out cw=%02x%02x sw=%02x%02x tag=%02x", fuzz_x87_out[1], fuzz_x87_out[0], fuzz_x87_out[3], fuzz_x87_out[2], fuzz_x87_out[4]);
            for (int i = 0; i < 8; i++)
                printf(" %04x:%016lx", *(uint16_t *)(fuzz_x87_out + 40 + i * 16), *(uint64_t *)(fuzz_x87_out + 32 + i * 16));
            printf("\n");
        }
        if (f->fp >= 2) {
            printf(" area");
            for (int i = 64; i < 176; i += 8)
                printf(" %016lx", *(uint64_t *)((char *)buffer + i));
            printf("\n");
        }
        printf(" regs");
        for (int i = 0; i < 16; i++)
            printf(" %016lx", fuzz_st.r[i] >= base && fuzz_st.r[i] < base + BUF_BYTES ? fuzz_st.r[i] - base : fuzz_st.r[i]);
        printf("\n");
        uint64_t bh = 0;
        for (int i = 0; i < BUF_BYTES / 8; i++)
            bh = bh * 31 + buffer[i];
        printf(" buffer %016lx\n", bh);
        printf(" in rax=%016lx rbx=%016lx rcx=%016lx fl=%04lx mx=%04lx x1=%016lx%016lx x2=%016lx%016lx mem=%016lx%016lx\n", in_rax, in_rbx, in_rcx,
               in_flags, in_mx, in_x1[1], in_x1[0], in_x2[1], in_x2[0], mem[1], mem[0]);
        printf(" out rax=%016lx rbx=%016lx rcx=%016lx rdx=%016lx fl=%04lx/%04lx mx=%04lx x1=%016lx%016lx rsp=%lx mem8=%016lx\n", fuzz_st.r[0],
               fuzz_st.r[3], fuzz_st.r[1], fuzz_st.r[2], fuzz_st.rflags & f->mask, fuzz_st.rflags, fuzz_st.mxcsr, fuzz_st.x[1][1], fuzz_st.x[1][0],
               fuzz_st.rsp_delta, buffer[1]);
    }
}

// a0: 0 all forms in order, n+1 only form n. a1: nonzero prints each round.
int flat_main(unsigned long a0, unsigned long a1) {
    int rounds = 64;
    for (int i = 0; fuzz_table[i].entry; i++) {
        if (a0 && (unsigned long)i != a0 - 1)
            continue;
        const Form *f = &fuzz_table[i];
        rounds = f->fp >= 2 ? 256 : 64; // x87 has more inputs that matter
        rng_state = 0x9E3779B97F4A7C15ULL ^ ((uint64_t)(i + 1) * 0xDEADBEEF);
        hash = 0xcbf29ce484222325ULL;
        for (int r = 0; r < rounds; r++) {
            uint64_t h0 = hash;
            run_round(f, a1 != 0);
            if (a1)
                printf("round %d hash %016lx from %016lx\n", r, hash, h0);
        }
        printf("%4d " FMT_HASH " %s\n", i, hash, f->name);
    }
    return 0;
}
