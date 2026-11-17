// QEMU's tests/tcg/i386/test-i386.c, in 64-bit mode, as a flat image. The file is included untouched apart from the section name
// Mach-O requires and the Linux-only register dump in a signal handler no 64-bit test reaches (see build-images.sh). The transcript is
// compared with Rosetta's.
extern void *__start_initcall __asm("section$start$__DATA$initcall");
extern void *__stop_initcall __asm("section$end$__DATA$initcall");
#define main test_i386_main
#include "test-i386-macho.c"
#undef main

int flat_main(unsigned long a0, unsigned long a1) {
    (void)a1;
    for (void **p = &__start_initcall; p != &__stop_initcall; p++)
        ((void (*)(void)) * p)();
    // a0: bit mask of tests to skip.
    void (*const tests[])(void) = {test_bsx, test_xcnt, test_mul, test_jcc, test_loop, test_xchg, test_string, test_misc, test_lea, test_enter, test_conv, test_floats};
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++)
        if (!(a0 >> i & 1))
            tests[i]();
    return 0;
}
