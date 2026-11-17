// Defined-flag oracle for shifts whose destination also supplies CL. No native reference result is used.
// Engine/Rosetta run the identical image; arg0=1/0 identifies the intended runner, without changing expectations.
#include "flatlibc.h"
typedef struct { uint64_t value, flags, input_after, flags_input_after; } Result;
typedef void (*Shift)(uint64_t, uint64_t, Result *);
#define SHIFT(OP, WIDTH, REG) \
    extern void OP##WIDTH(uint64_t, uint64_t, Result *); \
    __asm__(".text\n.globl _" #OP #WIDTH "\n_" #OP #WIDTH ":\n" \
            "pushq %rsi\n popfq\n movq %rdi, %rcx\n " #OP #WIDTH " %cl, %" REG "\n" \
            "pushfq\n popq %rax\n movq %rcx, 0(%rdx)\n movq %rax, 8(%rdx)\n" \
            "movq %rdi, 16(%rdx)\n movq %rsi, 24(%rdx)\n ret\n");
SHIFT(shl, q, "rcx") SHIFT(shr, q, "rcx") SHIFT(sar, q, "rcx")
SHIFT(shl, l, "ecx") SHIFT(shr, l, "ecx") SHIFT(sar, l, "ecx")
static Shift shifts[2][3] = {{shlq, shrq, sarq}, {shll, shrl, sarl}};
static const char *names[3] = {"shl", "shr", "sar"};
static uint64_t checks;
static int check(unsigned width, unsigned kind, uint64_t input, uint64_t before) {
    unsigned bits = width ? 32 : 64, count = (unsigned)input & (bits - 1);
    uint64_t mask = width ? 0xffffffffULL : ~0ULL, value = input & mask, result = value;
    if (count) {
        if (kind == 0) result = (value << count) & mask;
        else {
            result = value >> count;
            if (kind == 2 && (value & (1ULL << (bits - 1))))
                result |= mask ^ (mask >> count);
        }
    }
    uint64_t flag_mask = 0x8d5, flags = before & flag_mask;
    if (count) {
        unsigned parity = 0;
        for (unsigned i = 0; i < 8; i++) parity ^= (unsigned)(result >> i) & 1;
        unsigned cf = (unsigned)(value >> (kind == 0 ? bits - count : count - 1)) & 1;
        flags = cf | (!parity ? 4 : 0) | (!result ? 0x40 : 0) | ((result >> (bits - 1)) ? 0x80 : 0);
        if (count == 1) {
            unsigned of = kind == 0 ? (unsigned)(result >> (bits - 1)) ^ cf :
                          kind == 1 ? (unsigned)(value >> (bits - 1)) : 0;
            if (of) flags |= 0x800;
        }
        flag_mask &= ~0x10ULL; // AF is undefined for nonzero shifts.
        if (count != 1) flag_mask &= ~0x800ULL; // OF is defined only for effective count 1.
        // Nonzero masked counts here are below operand width, so the shifted-out CF is defined.
    }
    Result got;
    shifts[width][kind](input, before, &got);
    checks++;
    if (got.value != result || (got.flags & flag_mask) != (flags & flag_mask) ||
        got.input_after != input || got.flags_input_after != before) {
        printf("FAIL cl-self-count %s%u input=%llx count=%u before=%llx value=%llx expected=%llx flags=%llx expected=%llx mask=%llx inputs=%llx/%llx\n",
               names[kind], bits, input, count, before, got.value, result, got.flags, flags, flag_mask,
               got.input_after, got.flags_input_after);
        return 1;
    }
    return 0;
}
int flat_main(unsigned long engine_mode, unsigned long unused) {
    (void)unused;
    if (engine_mode > 1) return 64;
    // First case deliberately exposes count loss: SHL64(0x8000000000000001,1) => 2, CF=OF=1.
    // Rereading CL from that result instead uses count 2, selecting the wrong carry/overflow source.
    const unsigned counts[] = {1,0,2,7,31,32,63,64,65,255};
    const uint64_t bases[] = {0x8000000000000000ULL, 0, 0xffffffffffffff00ULL,
        0x7fffffffffffff00ULL, 0x1234567880000000ULL, 0xfedcba987fffff00ULL,
        0xaaaaaaaaaaaaaa00ULL, 0x5555555555555500ULL};
    const unsigned flags_bits[] = {1,4,0x10,0x40,0x80,0x800};
    for (unsigned width = 0; width < 2; width++)
        for (unsigned kind = 0; kind < 3; kind++)
            for (unsigned c = 0; c < sizeof counts / sizeof counts[0]; c++)
                for (unsigned v = 0; v < sizeof bases / sizeof bases[0]; v++)
                    for (unsigned f = 0; f < 64; f++) {
                        uint64_t before = 0x202;
                        for (unsigned i = 0; i < 6; i++) if (f & (1 << i)) before |= flags_bits[i];
                        uint64_t input = (bases[v] & ~255ULL) | counts[c];
                        if (check(width, kind, input, before)) return 1;
                    }
    printf("PASS cl-self-count %llu defined-flag/input checks\n", checks);
    return 0;
}
