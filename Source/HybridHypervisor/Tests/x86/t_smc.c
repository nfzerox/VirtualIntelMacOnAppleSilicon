// Self-modifying code: new bytes must take effect, data beside code must keep working, and neighbours must stay fast.
// Prints results only (timing goes to a separate line with the prefix "time", filtered from comparisons).
#include "flatlibc.h"

typedef uint64_t (*fn)(void);
static double now(void) {
    struct timeval t;
    gettimeofday(&t, 0);
    return (double)t.tv_sec + (double)t.tv_usec * 1e-6;
}

static void emit_ret_imm(uint8_t *p, uint32_t imm) {
    p[0] = 0xB8; // mov eax, imm
    *(uint32_t *)(p + 1) = imm;
    p[5] = 0xC3;
}

int flat_main(unsigned long a0, unsigned long a1) {
    (void)a0, (void)a1;
    uint8_t *code = mmap(0, 3 * 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (code == MAP_FAILED)
        return 9;

    // Patch and call, never stale.
    uint64_t stale = 0;
    emit_ret_imm(code, 1);
    for (uint32_t i = 2; i < 2000; i++) {
        *(uint32_t *)(code + 1) = i;
        if (((fn)code)() != i)
            stale++;
    }
    printf("patch-and-call stale %llu\n", (unsigned long long)stale);

    // A loop that patches the instruction it runs next, in the same x86 block: the store must take effect immediately.
    uint8_t *p = code + 256;
    static const uint8_t same_block[] = {
        0xC6, 0x05, 0x01, 0x00, 0x00, 0x00, 0x07, // movb $7, 1(%rip) : the immediate of the next instruction
        0xB8, 0x05, 0x00, 0x00, 0x00, // mov $5, %eax (immediate byte patched at +8)
        0xC3,
    };
    memcpy(p, same_block, sizeof same_block);
    printf("same-block patch %llu\n", (unsigned long long)((fn)p)());
    memcpy(p, same_block, sizeof same_block);
    printf("same-block patch again %llu\n", (unsigned long long)((fn)p)());

    // A branch rewritten in place: jmp +0 becomes jmp over a mov.
    p = code + 512;
    static const uint8_t branchy[] = {0xB8, 0x01, 0, 0, 0, 0xEB, 0x00, 0xB8, 0x02, 0, 0, 0, 0xC3};
    memcpy(p, branchy, sizeof branchy);
    printf("branch before %llu\n", (unsigned long long)((fn)p)());
    p[6] = 0x05;
    printf("branch after %llu\n", (unsigned long long)((fn)p)());

    // Code calling across a page, the callee rewritten.
    uint8_t *caller = code + 4096 - 16, *callee = code + 4096 + 16;
    caller[0] = 0xE8;
    *(int32_t *)(caller + 1) = (int32_t)(callee - (caller + 5));
    caller[5] = 0xC3;
    emit_ret_imm(callee, 11);
    printf("cross page %llu\n", (unsigned long long)((fn)caller)());
    emit_ret_imm(callee, 12);
    printf("cross page rewritten %llu\n", (unsigned long long)((fn)caller)());

    // Return into a rewritten caller: the shadow return stack must not go back to stale code.
    uint8_t *ret_site = code + 2048;
    ret_site[0] = 0xE8; // call callee
    *(int32_t *)(ret_site + 1) = (int32_t)(callee - (ret_site + 5));
    ret_site[5] = 0xB8; // mov $0x21, %eax
    *(uint32_t *)(ret_site + 6) = 0x21;
    ret_site[10] = 0xC3;
    // The callee rewrites the caller's next instruction, then returns to it.
    uint8_t *q = callee;
    q[0] = 0xC6; // movb $0x22, disp(%rip)
    q[1] = 0x05;
    *(int32_t *)(q + 2) = (int32_t)((ret_site + 6) - (q + 7));
    q[6] = 0x22;
    q[7] = 0xC3;
    printf("return into rewritten code %llx\n", (unsigned long long)((fn)ret_site)());

    // An x87 store as the writer: the monitor makes the value, translated code stores it, and that store is what trips the code page.
    emit_ret_imm(code, 5);
    printf("before x87 store %llu\n", (unsigned long long)((fn)code)());
    int32_t patched = 1234;
    __asm__ volatile("fildl %1\n fistpl %0" : "=m"(*(int32_t *)(code + 1)) : "m"(patched));
    printf("after x87 store %llu\n", (unsigned long long)((fn)code)());

    // Timing, Rosetta's table in research-rosetta.md.
    emit_ret_imm(code + 4096 * 2, 3);
    uint64_t n = 2000;
    double t = now();
    uint64_t s = 0;
    for (uint64_t i = 0; i < n; i++) {
        *(uint32_t *)(code + 1) = (uint32_t)i;
        s += ((fn)code)();
    }
    double patch = (now() - t) / (double)n;
    t = now();
    for (uint64_t i = 0; i < n; i++) {
        code[64] = (uint8_t)i;
        s += ((fn)code)();
    }
    double same = (now() - t) / (double)n;
    n = 2000000;
    t = now();
    for (uint64_t i = 0; i < n; i++) {
        code[4096 * 2 + 128] = (uint8_t)i;
        s += ((fn)code)();
    }
    // The first write in the code page's neighbour faults once at most; after that it is a plain store.
    double next = (now() - t) / (double)n;
    printf("time patch+call %.1f us, same-page data+call %.1f us, next-page data+call %.1f ns (%llu)\n", patch * 1e6, same * 1e6, next * 1e9,
           (unsigned long long)s);
    return 0;
}
