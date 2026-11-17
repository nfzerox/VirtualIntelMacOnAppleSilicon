// The 32-bit payload's C side: QEMU's test-i386 and the fuzz driver, chosen by the kernel, printing through int 0x80. test-i386 is unmodified except as
// i386_source.py says.
#include "test-i386-32.c"

extern void *__start_initcall;
extern void *__stop_initcall;
int fuzz_main(unsigned long a0, unsigned long a1);

// What clang asks of libgcc for 64-bit integers in 32-bit code. fuzz_driver and printf divide, nothing here converts a double.
typedef unsigned long long u64_;
static u64_ divmod(u64_ n, u64_ d, u64_ *rem) {
    u64_ q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = r << 1 | (n >> i & 1);
        if (r >= d) {
            r -= d;
            q |= 1ULL << i;
        }
    }
    *rem = r;
    return q;
}
u64_ __udivdi3(u64_ n, u64_ d) {
    u64_ r;
    return divmod(n, d, &r);
}
u64_ __umoddi3(u64_ n, u64_ d) {
    u64_ r;
    divmod(n, d, &r);
    return r;
}
long long __divdi3(long long n, long long d) {
    u64_ r, q = divmod(n < 0 ? -(u64_)n : n, d < 0 ? -(u64_)d : d, &r);
    return (n < 0) != (d < 0) ? -(long long)q : (long long)q;
}
long long __moddi3(long long n, long long d) {
    u64_ r;
    divmod(n < 0 ? -(u64_)n : n, d < 0 ? -(u64_)d : d, &r);
    return n < 0 ? -(long long)r : (long long)r;
}
u64_ __fixunsdfdi(double v) { // the printing of a double, from x87 through a truncating control word as compilers do it
    u64_ r;
    unsigned short cw, trunc;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    trunc = cw | 0xC00;
    if (v >= 9223372036854775808.0) {
        v -= 9223372036854775808.0;
        __asm__ volatile("fldcw %2\n fldl %1\n fistpq %0\n fldcw %3" : "=m"(r) : "m"(v), "m"(trunc), "m"(cw));
        return r + (1ULL << 63);
    }
    __asm__ volatile("fldcw %2\n fldl %1\n fistpq %0\n fldcw %3" : "=m"(r) : "m"(v), "m"(trunc), "m"(cw));
    return r;
}

void abort(void) { exit(4); }

void c_main(int mode) {
    int status = 0;
    if (mode == 100)
        status = fuzz_main(0, 0);
    else {
        for (void **p = &__start_initcall; p != &__stop_initcall; p++)
            ((void (*)(void)) * p)();
        test_bsx();
        test_xcnt();
        test_mul();
        test_jcc();
        test_loop();
        test_xchg();
        test_string();
        test_misc();
        test_lea();
        test_self_modifying_code();
        test_floats();
        test_enter();
        test_conv();
    }
    exit(status);
}
