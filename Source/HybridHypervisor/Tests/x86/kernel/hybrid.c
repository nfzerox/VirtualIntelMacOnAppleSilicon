// Checks what the QEMU accelerator relies on, without QEMU: a split RAM map with a hole, single instructions run by
// "the host" (the harness plays TCG, by advancing rip and changing registers), state written back at each, writes
// behind the monitor's back, CR0.TS, level-triggered interrupts. Each fallback site loads r13 with an action and r14
// with the instruction's length, so the harness never has to decode x86.
#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define PHYSMAP 0xFFFF880000000000ULL
#define PHYS(gpa) ((volatile u64 *)(PHYSMAP + (gpa)))
#define HIGH (1ULL << 32) // first RAM above the hole
#define HOLE (2ULL << 30)
#define MARKED 0xA0000ULL // RAM behind it, device in front
#define MAILBOX PHYS(0x3000)
#define PRESENT_RW 3ULL

static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline void outl(u16 port, u32 v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port)); }
static inline u64 rdtsc(void) {
    u32 a, d;
    __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
    return (u64)d << 32 | a;
}
static void puts_(const char *s) {
    while (*s)
        outb(0xE9, (u8)*s++);
}
static void command(u32 n) { outl(0xF8, n); }
static void done(u32 code) {
    outl(0xF4, code);
    for (;;)
        ;
}
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            puts_("FAIL: " #x "\n");                                                                                   \
            done(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

static u64 gdt[9]
    __attribute__((aligned(16))) = {0, 0x00AF9A000000FFFFULL, 0x00CF92000000FFFFULL, [8] = 0x00CFFA000000FFFFULL};
typedef struct {
    u16 off0, sel;
    u8 ist, attr;
    u16 off1;
    u32 off2, r;
} __attribute__((packed)) Gate;
static Gate idt[256] __attribute__((aligned(16)));
volatile u64 nm_count, irq_count, bad_vector;

volatile u64 fp_model, fp_live;

__asm__(".text\n"
        "_nm_stub:\n"
        "  cmpq $0, _fp_model(%rip)\n"
        "  jne 7f\n"
        "  pushq %rax\n"
        "  incq _nm_count(%rip)\n"
        "  movq %cr0, %rax\n"
        "  andq $~8, %rax\n"
        "  movq %rax, %cr0\n"
        "  popq %rax\n"
        "  iretq\n"
        // The lazy FP model of the test below: what a kernel's trap does with state it holds, what it does on the way
        // out of an interrupt.
        "7: pushq %rax\n pushq %rcx\n pushq %rdx\n pushq %rsi\n pushq %rdi\n"
        "  pushq %r8\n pushq %r9\n pushq %r10\n pushq %r11\n"
        "  call _lazy_nm\n"
        "  popq %r11\n popq %r10\n popq %r9\n popq %r8\n"
        "  popq %rdi\n popq %rsi\n popq %rdx\n popq %rcx\n popq %rax\n"
        "  iretq\n"
        "_irq_stub:\n"
        "  incq _irq_count(%rip)\n"
        "  cmpq $0, _fp_model(%rip)\n"
        "  je 9f\n"
        "  pushq %rax\n"
        "  cmpq $0, _fp_live(%rip)\n"
        "  je 8f\n"
        "  clts\n"
        "  jmp 6f\n"
        "8: movq %cr0, %rax\n"
        "  orq $8, %rax\n"
        "  movq %rax, %cr0\n"
        "6: popq %rax\n"
        "9: iretq\n"
        "_bad_stub:\n"
        "  movq $1, _bad_vector(%rip)\n"
        "  hlt\n");
extern char nm_stub[] __asm__("_nm_stub"), irq_stub[] __asm__("_irq_stub"), bad_stub[] __asm__("_bad_stub");

static void gate(int v, void *h) {
    u64 a = (u64)h;
    idt[v] = (Gate){(u16)a, 8, 0, 0x8E, (u16)(a >> 16), (u32)(a >> 32), 0};
}

static void tables(void) {
    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed)) g = {sizeof gdt - 1, (u64)gdt}, i = {sizeof idt - 1, (u64)idt};
    __asm__ volatile("lgdt %0\n lidt %1" : : "m"(g), "m"(i));
    for (int v = 0; v < 32; v++)
        gate(v, bad_stub);
    gate(7, nm_stub);
    gate(0x40, irq_stub);
}

// Harness actions: 1 sets rbx to 0x1234; 2 clears CR0.TS; 3 returns far: rip from r11, cs from r10, pops two; 4
// switches cr3 to r12; 5 sets rax to 7.

// ---- Memory above the hole: tables, data and code ----
static u64 next_frame = HIGH + (64 << 20);
static u64 frame(void) {
    u64 f = next_frame;
    next_frame += 4096;
    for (int i = 0; i < 512; i++)
        PHYS(f)[i] = 0;
    return f;
}

static u64 *pte_of(u64 root, u64 va) {
    u64 t = root;
    for (int level = 3; level > 0; level--) {
        volatile u64 *e = &PHYS(t)[(va >> (12 + 9 * level)) & 0x1ff];
        if (!(*e & 1))
            *e = frame() | PRESENT_RW;
        t = *e & 0x000FFFFFFFFFF000ULL;
    }
    return (u64 *)&PHYS(t)[(va >> 12) & 0x1ff];
}

static u64 read_cr3(void) {
    u64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}
static void write_cr3(u64 v) { __asm__ volatile("mov %0, %%cr3" : : "r"(v)); }

// A new root sharing everything but what callers map: a table page and its contents above 4 GB.
static u64 root_copy(u64 from) {
    u64 r = frame();
    for (int i = 0; i < 512; i++)
        PHYS(r)[i] = PHYS(from)[i];
    return r;
}

#define TEST_VA 0x0000200000000000ULL

static void put_function(u64 gpa, u32 value) {
    // mov eax, imm32; ret
    volatile u8 *p = (volatile u8 *)PHYS(gpa);
    p[0] = 0xB8;
    *(volatile u32 *)(p + 1) = value;
    p[5] = 0xC3;
}

static void test_ram_map(u64 root) {
    CHECK(*PHYS(HIGH + 0x1000) == 0x1122334455667788ULL); // seeded by the host
    *PHYS(HIGH + 0x2000) = 0xAABBCCDDEEFF0011ULL;
    command(1); // the host checks it where it expects it
    // Below the top of RAM but in the hole, and a low page marked as a device.
    CHECK(*PHYS(HOLE + 0x40) == (HOLE + 0x40) * 3);
    *PHYS(HOLE + 0x48) = 77;
    CHECK(MAILBOX[0] == 77);
    CHECK(*PHYS(MARKED) == MARKED * 3);
    // Tables, code and data all in high frames.
    u64 root2 = root_copy(root);
    write_cr3(root2);
    u64 code = frame();
    put_function(code, 41);
    *pte_of(root2, TEST_VA) = code | PRESENT_RW;
    CHECK(((u32 (*)(void))TEST_VA)() == 41);
    write_cr3(root);
}

static void test_fallback_state(void) {
    // Everything the instruction did not touch survives the trip through the host, flags included.
    u64 rbx = 0, r15 = 0xF15F15F15F15F15ULL, flags = 0,
        xmm[2] __attribute__((aligned(16))) = {0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL},
        back[2] __attribute__((aligned(16)));
    __asm__ volatile("movaps %[xi], %%xmm15\n"
                     "stc\n"
                     "mov $1, %%r13\n mov $5, %%r14\n"
                     ".byte 0x66, 0x0F, 0x38, 0x41, 0xC1\n" // phminposuw xmm0, xmm1: the decoder has no SSE4 and #UD is
                                                            // not its call to make
                     "pushfq\n popq %[fl]\n"
                     "movaps %%xmm15, %[xo]\n"
                     : "=b"(rbx), [fl] "=r"(flags), [xo] "=m"(back), "+r"(r15)
                     : [xi] "m"(xmm)
                     : "r13", "r14", "xmm0", "xmm15", "cc");
    CHECK(rbx == 0x1234);
    CHECK(flags & 1);
    CHECK(r15 == 0xF15F15F15F15F15ULL);
    CHECK(back[0] == xmm[0] && back[1] == xmm[1]);

    // Other kinds of stop: x87, far return, an MSR the APIC owns, hlt, cr8.
    __asm__ volatile("mov $0, %%r13\n mov $2, %%r14\n .byte 0xD9, 0xFF\n" : : : "r13", "r14"); // fninit
    __asm__ volatile("mov $0, %%r13\n mov $1, %%r14\n hlt\n" : : : "r13", "r14");
    u64 rax = 0;
    __asm__ volatile("mov $5, %%r13\n mov $4, %%r14\n mov %%cr8, %%rax\n" : "=a"(rax) : : "r13", "r14");
    CHECK(rax == 7);
    __asm__ volatile("mov $5, %%r13\n mov $2, %%r14\n mov $0x1B, %%ecx\n rdmsr\n"
                     : "=a"(rax)
                     :
                     : "r13", "r14", "rcx", "rdx");
    CHECK(rax == 7);
    __asm__ volatile("mov $0, %%r13\n mov $2, %%r14\n mov $0x277, %%ecx\n xor %%eax, %%eax\n xor %%edx, %%edx\n wrmsr\n"
                     :
                     :
                     : "r13", "r14", "rcx", "rax", "rdx");
    rax = 1;
    __asm__ volatile("mov $5, %%r13\n mov $5, %%r14\n .byte 0xF3, 0x48, 0x0F, 0xB8, 0xC1\n"
                     : "=a"(rax)
                     :
                     : "r13", "r14"); // popcnt rax, rcx
    CHECK(rax == 7);
    // lretq, played by the host: it takes the target from r11 and CS from r10.
    rax = 0;
    __asm__ volatile("mov $3, %%r13\n mov $0, %%r14\n"
                     "lea 1f(%%rip), %%r11\n mov $8, %%r10\n"
                     "pushq $8\n pushq %%r11\n"
                     ".byte 0x48, 0xCB\n"
                     "ud2\n"
                     "1: mov $9, %%eax\n"
                     : "=a"(rax)
                     :
                     : "r10", "r11", "r13", "r14");
    CHECK(rax == 9);
    // SIMD to a device: an MMIO form the monitor cannot complete. The instruction is in the host's hands, nothing has
    // happened yet.
    u64 *hole = (u64 *)PHYS(HOLE + 0x80);
    __asm__ volatile("xor %%ebx, %%ebx\n mov $1, %%r13\n mov $4, %%r14\n .byte 0x0F, 0x10, 0x00, 0x90\n"
                     : "=b"(rbx)
                     : "a"(hole)
                     : "r13", "r14", "xmm0");
    CHECK(rbx == 0x1234);
}

static void test_host_changes_cr3(u64 root) {
    u64 root2 = root_copy(root);
    u64 a = frame(), b = frame();
    PHYS(a)[0] = 0xAAAA;
    PHYS(b)[0] = 0xBBBB;
    *pte_of(root, TEST_VA + 0x10000) = a | PRESENT_RW;
    *pte_of(root2, TEST_VA + 0x10000) = b | PRESENT_RW;
    CHECK(*(volatile u64 *)(TEST_VA + 0x10000) == 0xAAAA);
    __asm__ volatile("mov %0, %%r12\n mov $4, %%r13\n mov $3, %%r14\n .byte 0x0F, 0x00, 0xE0\n"
                     :
                     : "r"(root2)
                     : "r12", "r13", "r14"); // verr ax
    CHECK(read_cr3() == root2);
    CHECK(*(volatile u64 *)(TEST_VA + 0x10000) == 0xBBBB);
    write_cr3(root);
}

// Writes made by a device or TCG reach code and tables with no trap in the monitor.
static void test_notes(u64 root) {
    u64 code = frame();
    put_function(code, 1);
    *pte_of(root, TEST_VA + 0x20000) = code | PRESENT_RW;
    u32 (*f)(void) = (u32 (*)(void))(TEST_VA + 0x20000);
    CHECK(f() == 1);
    MAILBOX[1] = code + 1;
    MAILBOX[2] = 2;
    command(2); // writes the 32-bit value at the GPA, notes it
    CHECK(f() == 2);

    u64 *slot = pte_of(root, TEST_VA + 0x30000);
    u64 a = frame(), b = frame();
    PHYS(a)[0] = 0xA;
    PHYS(b)[0] = 0xB;
    *slot = a | PRESENT_RW;
    volatile u64 *p = (volatile u64 *)(TEST_VA + 0x30000);
    CHECK(*p == 0xA);
    MAILBOX[1] = (u64)slot - PHYSMAP;
    MAILBOX[2] = b | PRESENT_RW;
    command(3); // writes the 64-bit value at the GPA, notes it
    CHECK(*p == 0xB);
    command(7); // with so many notes that the ring overflows: everything marked is still handled
    MAILBOX[1] = code + 1;
    MAILBOX[2] = 3;
    command(2);
    CHECK(f() == 3);
}

// dtrace's fbt as XNU does it, with the patch written by TCG: kernel text changes with CR0.WP clear, which the engine
// refuses, so the bytes arrive as a write behind its back. The entry probe turns push %rbp into int3, the return probe
// pop %rbp; the handler emulates whichever it is and returns past it. A translation from before a patch, or one that
// mixed old and new bytes, would come back with rbp, rsp or the return wrong.
__asm__(".text\n"
        ".globl _fbt_trap\n_fbt_trap:\n"
        "  pushq %rax\n"
        "  movq 8(%rsp), %rax\n"
        "  cmpq _fbt_pop_return(%rip), %rax\n"
        "  je 1f\n"
        "  popq %rax\n" // push %rbp: move the frame down 8, put rbp where the push would
        "  subq $16, %rsp\n"
        "  movq %rax, (%rsp)\n"
        "  movq 16(%rsp), %rax\n  movq %rax, 8(%rsp)\n"
        "  movq 24(%rsp), %rax\n  movq %rax, 16(%rsp)\n"
        "  movq 32(%rsp), %rax\n  movq %rax, 24(%rsp)\n"
        "  movq 40(%rsp), %rax\n  movq %rax, 32(%rsp)\n"
        "  movq 48(%rsp), %rax\n  movq %rax, 40(%rsp)\n"
        "  movq 32(%rsp), %rax\n  subq $8, %rax\n  movq %rbp, (%rax)\n  movq %rax, 32(%rsp)\n"
        "  popq %rax\n"
        "  iretq\n"
        "1: movq 32(%rsp), %rax\n" // pop %rbp: from the interrupted stack
        "  movq (%rax), %rbp\n"
        "  addq $8, %rax\n"
        "  movq %rax, 32(%rsp)\n"
        "  popq %rax\n"
        "  iretq\n");
extern char fbt_trap[];
u64 fbt_pop_return;

static u64 call_probed(u64 fn, u64 *rsp_before, u64 *rsp_after, u64 *rbp_after) {
    u64 rax, before, after, rbp;
    // rbp is the operand under test, so nothing in here may address through it.
    __asm__ volatile("pushq %%rbp\n"
                     "movabsq $0x1122334455667788, %%rbp\n"
                     "movq %%rsp, %1\n"
                     "call *%4\n"
                     "movq %%rsp, %2\n"
                     "movq %%rbp, %3\n"
                     "popq %%rbp\n"
                     : "=a"(rax), "=&r"(before), "=&r"(after), "=&r"(rbp)
                     : "r"(fn)
                     : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory");
    *rsp_before = before, *rsp_after = after, *rbp_after = rbp;
    return rax;
}

static void test_fbt_patch(u64 root) {
    static const u8 body[] = {0x55, 0x48, 0x89, 0xe5, 0x48, 0x89, 0xe8, 0x5d, 0xc3}; // push rbp; mov rsp,rbp; mov rbp,rax; pop rbp; ret
    u64 code = frame();
    volatile u8 *p = (volatile u8 *)PHYS(code);
    for (unsigned i = 0; i < sizeof body; i++)
        p[i] = body[i];
    u64 fn = TEST_VA + 0x40000;
    *pte_of(root, fn) = code | PRESENT_RW;
    fbt_pop_return = fn + 8;
    gate(3, fbt_trap);
    // The words TCG writes: bytes 0 to 3 and 4 to 7, each as it is or with its probe byte.
    const u32 entry[2] = {0xe5894855, 0xe58948cc}, ret[2] = {0x5de88948, 0xcce88948};
    static const int steps[][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 1}, {0, 0}};
    for (unsigned s = 0; s < sizeof steps / sizeof steps[0]; s++) {
        MAILBOX[1] = code;
        MAILBOX[2] = entry[steps[s][0]];
        command(2);
        MAILBOX[1] = code + 4;
        MAILBOX[2] = ret[steps[s][1]];
        command(2);
        for (int twice = 0; twice < 2; twice++) { // the second run is on whatever the first translated
            u64 before, after, rbp;
            u64 got = call_probed(fn, &before, &after, &rbp);
            CHECK(got == before - 16);
            CHECK(after == before);
            CHECK(rbp == 0x1122334455667788ULL);
        }
    }
}

static void test_ts(void) {
    u64 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    __asm__ volatile("movaps %%xmm1, %%xmm0" : : : "xmm0");
    CHECK(nm_count == 1); // the handler cleared TS and the instruction ran
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    // A kernel's bcopy with TS set: nothing x86 touched SIMD, so no #NM.
    static u64 from[64], to[64];
    for (int i = 0; i < 64; i++)
        from[i] = (u64)i * 3;
    u64 count = 64;
    u64 *s = from, *d = to;
    __asm__ volatile("cld\n rep movsq" : "+S"(s), "+D"(d), "+c"(count) : : "memory");
    for (int i = 0; i < 64; i++)
        CHECK(to[i] == (u64)i * 3);
    d = to;
    count = 64;
    __asm__ volatile("rep stosq" : "+D"(d), "+c"(count) : "a"(5ULL) : "memory");
    CHECK(to[63] == 5);
    CHECK(nm_count == 1);
    // Every width with TS set, as a kernel's bzero and memset run: the pattern goes out whole, 32 bytes a turn, then the tail.
    static const u64 widths[] = {1, 2, 4, 8};
    for (int w = 0; w < 4; w++) {
        u8 *bytes = (u8 *)to;
        for (int i = 0; i < 512; i++)
            bytes[i] = 0xEE;
        u64 n = (300 + 7) / widths[w], pattern = 0x8877665544332211ULL;
        u8 *at = bytes + 3;
        switch (widths[w]) {
            case 1: __asm__ volatile("rep stosb" : "+D"(at), "+c"(n) : "a"(pattern) : "memory"); break;
            case 2: __asm__ volatile("rep stosw" : "+D"(at), "+c"(n) : "a"(pattern) : "memory"); break;
            case 4: __asm__ volatile("rep stosl" : "+D"(at), "+c"(n) : "a"(pattern) : "memory"); break;
            default: __asm__ volatile("rep stosq" : "+D"(at), "+c"(n) : "a"(pattern) : "memory"); break;
        }
        u64 len = (300 + 7) / widths[w] * widths[w];
        CHECK(n == 0 && at == bytes + 3 + len);
        CHECK(bytes[2] == 0xEE && bytes[3 + len] == 0xEE);
        for (u64 i = 0; i < len; i++)
            CHECK(bytes[3 + i] == (u8)(pattern >> (8 * (i % widths[w]))));
    }
    CHECK(nm_count == 1);
    u32 m;
    __asm__ volatile("stmxcsr %0" : "=m"(m));
    CHECK(nm_count == 2);
    // clts, in the host's hands: the monitor must notice TS went away.
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    __asm__ volatile("mov $2, %%r13\n mov $2, %%r14\n .byte 0x0F, 0x06\n" : : : "r13", "r14");
    __asm__ volatile("movaps %%xmm1, %%xmm0" : : : "xmm0");
    CHECK(nm_count == 2);
}

static void test_level_interrupts(void) {
    __asm__ volatile("cli");
    MAILBOX[1] = 0;
    command(4); // raises the line, kicks: IF is clear, so the vector must stay in the APIC
    for (volatile int i = 0; i < 1000; i++)
        ;
    command(5); // the harness checks it was not acknowledged
    CHECK(irq_count == 0);
    __asm__ volatile("sti\n nop");
    CHECK(irq_count == 1);
    // sti; hlt with one waiting does not sleep.
    __asm__ volatile("cli");
    command(4);
    __asm__ volatile("mov $0, %%r13\n mov $1, %%r14\n sti\n hlt\n" : : : "r13", "r14");
    CHECK(irq_count == 2);
}

static void test_tsc(void) {
    u64 a = rdtsc();
    CHECK(a > 1ULL << 50);
    command(6); // sleeps, measures
    MAILBOX[1] = rdtsc() - a;
    command(8); // checks the rate is 1.2 GHz, as set
}

// Boundary stops land anywhere in here, each a full state round trip with the monitor resyncing.
static void test_busy(void) {
    u64 sum = 0, cf_seen = 0;
    for (u64 i = 1; i <= 40000000; i++) {
        u64 carry;
        __asm__ volatile("add %2, %0\n sbb %1, %1" : "+r"(sum), "=r"(carry) : "r"(i) : "cc");
        cf_seen += carry & 1;
    }
    CHECK(sum == 40000000ULL * 40000001ULL / 2);
    CHECK(cf_seen == 0);
}

// XNU turns on MCE among its first acts, and nothing here needs to care: it must not stop.
static void test_cr4(void) {
    u64 cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4 | 0x40 | 0x100));
    u64 back;
    __asm__ volatile("mov %%cr4, %0" : "=r"(back));
    CHECK(back == (cr4 | 0x40 | 0x100));
}

#ifndef CHURN_ROUNDS
#define CHURN_ROUNDS 2
#endif
#define CHURN_TABLES 200
#define CHURN_VA 0x0000300000000000ULL
// More address spaces than views, each with hundreds of shadow tables: the shape of a real boot's EL1 fault, in a root
// built by an eviction. This did not reproduce that fault, even at 800 tables over 2880 evictions, so it guards
// eviction in general and not the barrier in view_build.
static void test_view_churn(u64 root) {
    static u64 roots[300];
    u64 data = frame();
    for (int i = 0; i < CHURN_TABLES; i++)
        *pte_of(root, CHURN_VA + ((u64)i << 21)) = data | PRESENT_RW;
    for (int i = 0; i < 300; i++)
        roots[i] = root_copy(root);
    for (int round = 0; round < CHURN_ROUNDS; round++)
        for (int i = 0; i < 300; i++) {
            write_cr3(roots[i]);
            for (int t = 0; t < CHURN_TABLES; t++)
                *(volatile u64 *)(CHURN_VA + ((u64)t << 21)) = (u64)t;
        }
    write_cr3(root);
    CHECK(PHYS(data)[0] == CHURN_TABLES - 1);
}

// The VMware backdoor reads the CPU's registers in its device model: it has to run where those are.
static void test_vmport(void) {
    u64 rax = 0;
    __asm__ volatile("mov $5, %%r13\n mov $1, %%r14\n mov $0x5658, %%dx\n inl %%dx, %%eax\n"
                     : "=a"(rax)
                     :
                     : "r13", "r14", "rdx");
    CHECK(rax == 7);
}

// A write to PM1a_CNT is how a guest powers off, so its size and value had better be what the instruction says, with
// the rest of rax not leaking in.
static void test_port_forms(void) {
    __asm__ volatile("mov $0xA5A50000, %%rax\n or $0x2000, %%rax\n mov $0x604, %%dx\n outw %%ax, %%dx"
                     :
                     :
                     : "rax", "rdx");
    __asm__ volatile("mov $0xA5A50020, %%rax\n mov $0x605, %%dx\n outb %%al, %%dx" : : : "rax", "rdx");
    __asm__ volatile("mov $0xA5A50006, %%rax\n outb %%al, $0x70" : : : "rax");
    __asm__ volatile("mov $0x12345678, %%rax\n mov $0x608, %%dx\n outl %%eax, %%dx" : : : "rax", "rdx");
    command(9); // the host checks what it saw
}

// ---- Two tasks' FP state under lazy switching, as XNU does it: xmm in both, the x87 stack in one and MMX in the other
// ---- The state lives only in xmm registers, so C here mustn't use them: this file is built without implicit float.
#define LAZY_STEPS 2000000
typedef struct {
    u8 *area; // fxsave image, in the physmap
    u64 steps;
    bool saved;
    u64 result[12];
} Task;
static Task tasks[2];
static volatile int lazy_current;
static volatile u64 lazy_nm_count;
static const u8 byte_swap[16] __attribute__((aligned(16))) = {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12};

// Each task's own rounding and precision, so that a mix-up changes the numbers. Every 16th step: costs a monitor call
// or several.
static const u16 task_cw[2] = {0xA7F, 0x77F};
static inline void task_fp_seed(int n) {
    __asm__ volatile("fninit\n" : : : "memory");
    if (n == 0)
        __asm__ volatile("fldcw (%0)\n fld1\n fldpi\n" : : "r"(&task_cw[n]) : "memory");
    else
        __asm__ volatile("movq %0, %%mm3\n movq %0, %%mm6\n" : : "r"(0x0102030405060708ULL) : "memory");
}
static inline void task_fp_step(int n, u64 step) {
    if (step & 15)
        return;
    if (n == 0)
        __asm__ volatile("fadd %%st(1), %%st\n fsqrt\n fxch\n fld1\n faddp\n fxch\n" : : : "memory");
    else
        __asm__ volatile("movq %0, %%mm2\n paddq %%mm2, %%mm3\n pxor %%mm3, %%mm6\n psllq $1, %%mm3\n"
                         :
                         : "r"(step)
                         : "memory");
}
static void task_fp_read(int n, u64 *out) {
    if (n == 0)
        __asm__ volatile("fld %%st(0)\n fstpt (%0)\n fld %%st(1)\n fstpt 16(%0)\n fnstcw 32(%0)\n"
                         :
                         : "r"(out)
                         : "memory");
    else
        __asm__ volatile("movq %%mm3, (%0)\n movq %%mm6, 16(%0)\n" : : "r"(out) : "memory");
}

static void task_seed(u64 seed) {
    __asm__ volatile("pxor %%xmm0, %%xmm0\n pxor %%xmm1, %%xmm1\n pxor %%xmm2, %%xmm2\n pxor %%xmm3, %%xmm3\n"
                     "pxor %%xmm4, %%xmm4\n pxor %%xmm5, %%xmm5\n pxor %%xmm6, %%xmm6\n pxor %%xmm8, %%xmm8\n pxor "
                     "%%xmm15, %%xmm15\n"
                     "movq %0, %%xmm0\n movq %0, %%xmm8\n"
                     :
                     : "r"(seed));
}
// Shapes of a SHA-1 round: pshufb, palignr, shuffles, integer adds, xor. No clobbers on purpose: the registers carry
// state from one call to the next.
static inline void task_step(u64 a, u64 b) {
    u64 message[2] __attribute__((aligned(16))) = {a, b};
    __asm__ volatile("movdqu (%0), %%xmm7\n pshufb (%1), %%xmm7\n paddd %%xmm7, %%xmm0\n movdqa %%xmm0, %%xmm3\n "
                     "palignr $4, %%xmm1, %%xmm3\n"
                     "movdqa %%xmm3, %%xmm1\n pxor %%xmm1, %%xmm2\n pshufd $0x39, %%xmm2, %%xmm3\n paddd %%xmm3, "
                     "%%xmm4\n pxor %%xmm4, %%xmm5\n"
                     "movdqa %%xmm5, %%xmm6\n palignr $8, %%xmm0, %%xmm6\n paddq %%xmm6, %%xmm0\n pxor %%xmm0, "
                     "%%xmm8\n paddd %%xmm8, %%xmm15\n"
                     :
                     : "r"(message), "r"(byte_swap)
                     : "memory");
}
static void task_read(u64 *out) {
    __asm__ volatile("movups %%xmm0, (%0)\n movups %%xmm5, 16(%0)\n movups %%xmm15, 32(%0)\n" : : "r"(out) : "memory");
}
// #NM: load this task's state. Runs on the task's stack, with interrupts off through the gate.
void lazy_nm(void) {
    Task *t = &tasks[lazy_current];
    lazy_nm_count++;
    __asm__ volatile("clts");
    if (t->saved)
        __asm__ volatile("fxrstor64 (%0)" : : "r"(t->area) : "memory");
    else
        task_seed(lazy_current + 1), task_fp_seed(lazy_current);
    t->saved = false;
    fp_live = 1;
}

// A context switch: save what's live, set TS, leave the next task to take #NM.
static void lazy_switch(void) {
    __asm__ volatile("cli");
    if (fp_live) {
        Task *t = &tasks[lazy_current];
        __asm__ volatile("fxsave64 (%0)" : : "r"(t->area) : "memory");
        t->saved = true;
        fp_live = 0;
    }
    u64 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    lazy_current ^= 1;
    __asm__ volatile("sti");
}

// The shape of the guest's integrity check: hashing pages that fault in, with the kernel using xmm itself on the way,
// as a code-signing check in vm_fault does.
#define LAZY_PAGES 32
#define LAZY_VA (TEST_VA + 0x10000000)
static u64 lazy_frames[LAZY_PAGES], *lazy_ptes[LAZY_PAGES];
static volatile u64 lazy_pf_count;
__asm__(".text\n"
        "_lazy_pf_stub:\n"
        "  pushq %rax\n pushq %rcx\n pushq %rdx\n pushq %rsi\n pushq %rdi\n"
        "  pushq %r8\n pushq %r9\n pushq %r10\n pushq %r11\n"
        "  pushq %rbp\n"
        "  movq %rsp, %rbp\n"
        "  andq $-16, %rsp\n" // it spills xmm with movaps
        "  call _lazy_pf\n"
        "  movq %rbp, %rsp\n"
        "  popq %rbp\n"
        "  popq %r11\n popq %r10\n popq %r9\n popq %r8\n"
        "  popq %rdi\n popq %rsi\n popq %rdx\n popq %rcx\n popq %rax\n"
        "  addq $8, %rsp\n"
        "  iretq\n");
extern char lazy_pf_stub[] __asm__("_lazy_pf_stub");

void lazy_pf(void) {
    u64 cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    u64 page = (cr2 - LAZY_VA) >> 12;
    if (page >= LAZY_PAGES) {
        puts_("FAIL: a page fault outside the lazy test's pages\n");
        done(3);
    }
    lazy_pf_count++;
    *lazy_ptes[page] = lazy_frames[page] | PRESENT_RW;
    // What a kernel hash does with the registers it borrows. Under TS this takes #NM first, in the kernel, as XNU's
    // does.
    u64 saved[8] __attribute__((aligned(16)));
    __asm__ volatile("movaps %%xmm0, 0(%0)\n movaps %%xmm7, 16(%0)\n movaps %%xmm15, 32(%0)\n"
                     "pxor %%xmm0, %%xmm0\n pcmpeqd %%xmm7, %%xmm7\n paddd %%xmm7, %%xmm15\n"
                     "movaps 0(%0), %%xmm0\n movaps 16(%0), %%xmm7\n movaps 32(%0), %%xmm15\n"
                     :
                     : "r"(saved)
                     : "memory");
}

static inline void task_touch(u64 step) {
    const u8 *at = (const u8 *)LAZY_VA + (step * 40 % (LAZY_PAGES * 4096 - 32));
    __asm__ volatile("movdqu (%0), %%xmm7\n paddd %%xmm7, %%xmm15\n pxor 16(%1), %%xmm2\n"
                     :
                     : "r"(at), "r"((u64)at & ~15ULL)
                     : "memory");
    if ((step & 0x3FF) == 0) { // the page goes away again
        u64 page = (step >> 10) % LAZY_PAGES;
        *lazy_ptes[page] = 0;
        __asm__ volatile("invlpg (%0)" : : "r"(LAZY_VA + (page << 12)));
    }
}

static void test_lazy_fp(u64 root) {
    u64 golden[2][12];
    for (int i = 0; i < LAZY_PAGES; i++) {
        lazy_frames[i] = frame();
        for (int w = 0; w < 512; w++)
            PHYS(lazy_frames[i])[w] = 0x9E3779B97F4A7C15ULL * (u64)(i * 512 + w + 1);
        lazy_ptes[i] = pte_of(root, LAZY_VA + ((u64)i << 12));
    }
    gate(14, lazy_pf_stub);
    for (int n = 0; n < 2; n++) {
        task_seed(n + 1);
        task_fp_seed(n);
        for (u64 i = 0; i < LAZY_STEPS; i++) {
            task_step(i * (n + 3), ~i);
            task_fp_step(n, i);
            task_touch(i);
        }
        task_read(golden[n]);
        task_fp_read(n, &golden[n][6]);
    }
    for (int n = 0; n < 2; n++) {
        tasks[n].area = (u8 *)PHYS(frame());
        tasks[n].steps = 0;
        tasks[n].saved = false;
    }
    __asm__ volatile("cli");
    command(10); // the host starts a timer interrupt
    fp_model = 1;
    fp_live = 0;
    lazy_current = 0;
    u64 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    __asm__ volatile("sti");
    int finished = 0;
    u64 quantum = 1;
    while (finished < 2) {
        Task *t = &tasks[lazy_current];
        int n = lazy_current;
        for (u64 run = 0; run < quantum && t->steps < LAZY_STEPS; run++, t->steps++) {
            task_step(t->steps * (n + 3), ~t->steps);
            task_fp_step(n, t->steps);
            task_touch(t->steps);
            if ((t->steps & 0xFFF) ==
                0) // a fallback in the middle of a task: its xmm state goes through the host and back
                __asm__ volatile("mov $0, %%r13\n mov $2, %%r14\n .byte 0xD9, 0xFF\n" : : : "r13", "r14");
        }
        if (t->steps == LAZY_STEPS) {
            t->steps++;
            task_read(t->result);
            task_fp_read(n, &t->result[6]);
            finished++;
        }
        quantum = quantum * 7 % 4093 + 1;
        lazy_switch();
    }
    __asm__ volatile("cli");
    fp_model = 0;
    command(11);
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 & ~8ULL));
    __asm__ volatile("sti");
    for (int n = 0; n < 2; n++)
        for (int i = 0; i < 12; i++)
            CHECK(tasks[n].result[i] == golden[n][i]);
    __asm__ volatile("emms\n fninit");
    CHECK(lazy_nm_count > 100);
    CHECK(lazy_pf_count > 1000);
    gate(14, bad_stub);
    CHECK(irq_count > 2);
}

// A store straddling into a page-table frame, as a kernel writes file data into a freed table that is still tracked.
// Each byte must land where the instruction says.
static void test_store_into_table(u64 root) {
    u64 va = TEST_VA + 0x600000;
    u64 *slots = pte_of(root, va); // a leaf table in use, so its frame is tracked once it is walked
    u64 table = ((u64)slots - PHYSMAP) & ~0xFFFULL;
    u64 before = frame();
    slots[0] = before | PRESENT_RW;
    slots[1] = table | PRESENT_RW; // and it is mapped as data right after an ordinary page
    volatile u8 *low = (volatile u8 *)va, *high = low + 4096;
    low[0] = 1;
    CHECK(high[9] == (u8)(table >> 8)); // the second slot of the frame, through the mapping: a walk has happened
    static const int widths[] = {8, 4, 2};
    for (int w = 0; w < 3; w++) {
        for (int at = 1; at < widths[w]; at++) {
            for (int i = 0; i < 16; i++) {
                low[4080 + i] = 0;
            }
            u64 value = 0x1122334455667788ULL;
            u64 saved = PHYS(table)[0];
            volatile u8 *to = high - at;
            // If the monitor sends it back, the harness does the store as TCG would, from the mailbox's frames.
            MAILBOX[1] = before;
            MAILBOX[2] = table;
            MAILBOX[3] = (u64)widths[w];
            if (widths[w] == 8)
                __asm__ volatile(
                    "mov %0, %%r11\n mov %1, %%r12\n mov $8, %%r13\n mov $4, %%r14\n .byte 0x4D, 0x89, 0x1C, 0x24\n"
                    :
                    : "r"(value), "r"(to)
                    : "r11", "r12", "r13", "r14", "memory");
            else if (widths[w] == 4)
                __asm__ volatile(
                    "mov %0, %%r11\n mov %1, %%r12\n mov $8, %%r13\n mov $4, %%r14\n .byte 0x45, 0x89, 0x1C, 0x24\n"
                    :
                    : "r"(value), "r"(to)
                    : "r11", "r12", "r13", "r14", "memory");
            else
                __asm__ volatile("mov %0, %%r11\n mov %1, %%r12\n mov $8, %%r13\n mov $5, %%r14\n .byte 0x66, 0x45, "
                                 "0x89, 0x1C, 0x24\n"
                                 :
                                 : "r"(value), "r"(to)
                                 : "r11", "r12", "r13", "r14", "memory");
            for (int i = 0; i < widths[w]; i++) {
                u8 want = (u8)(value >> (i * 8));
                // Through the physmap: a misplaced store lands on the slot that maps `low`, which would fault here
                // instead of failing.
                u8 got = i < at ? ((volatile u8 *)PHYS(before))[4096 - at + i] : ((volatile u8 *)PHYS(table))[i - at];
                CHECK(got == want);
            }
            PHYS(table)[0] = saved;
        }
    }

    // A 16-byte store across two tracked frames runs natively on one opened page at a time: neither may stay writable
    // after it.
    u64 va2 = TEST_VA + 0x800000;
    u64 *slots2 = pte_of(root, va2);
    u64 table2 = ((u64)slots2 - PHYSMAP) & ~0xFFFULL;
    u64 first = frame(), second = frame();
    PHYS(first)[0] = 0xA;
    PHYS(second)[0] = 0xB;
    slots2[3] = first | PRESENT_RW;
    CHECK(*(volatile u64 *)(va2 + 3 * 4096) == 0xA);
    // The second page gets a table of its own: rewriting the slot that maps the first page would take its writable
    // mapping away and hide the bug.
    u64 *slots3 = pte_of(root, TEST_VA + 0xA00000);
    u64 table3 = ((u64)slots3 - PHYSMAP) & ~0xFFFULL;
    slots3[1] = first | PRESENT_RW;
    CHECK(*(volatile u64 *)(TEST_VA + 0xA00000 + 4096) == 0xA);
    slots[0] = table2 | PRESENT_RW;
    slots[1] = table3 | PRESENT_RW;
    CHECK(low[3 * 8 + 1] == (u8)(first >> 8));
    CHECK(high[9] == (u8)(first >> 8));
    u64 value = 0x1122334455667788ULL;
    volatile u8 *to = high - 8;
    MAILBOX[1] = table2;
    MAILBOX[2] = table3;
    MAILBOX[3] = 16;
    __asm__ volatile("mov %0, %%r11\n mov %1, %%r12\n mov $8, %%r13\n mov $5, %%r14\n movq %%r11, %%xmm0\n punpcklqdq "
                     "%%xmm0, %%xmm0\n"
                     ".byte 0x41, 0x0F, 0x11, 0x04, 0x24\n"
                     :
                     : "r"(value), "r"(to)
                     : "r11", "r12", "r13", "r14", "xmm0", "memory");
    CHECK(PHYS(table2)[511] == value);
    CHECK(PHYS(table3)[0] == value);
    PHYS(table3)[0] = 0;
    PHYS(table2)[511] = 0;
    *(volatile u64 *)(va + 3 * 8) =
        second | PRESENT_RW; // through the mapping: only write protection tells the monitor of this one
    CHECK(*(volatile u64 *)(va2 + 3 * 4096) == 0xB);
    slots[0] = before | PRESENT_RW;
    slots[1] = 0;

    // A load that starts in RAM and ends in a device: emulating it at FAR would read the device for all 8 bytes.
    u64 got;
    __asm__ volatile("mov %1, %%r12\n mov $5, %%r13\n mov $4, %%r14\n .byte 0x49, 0x8B, 0x04, 0x24\n"
                     : "=a"(got)
                     : "r"(PHYSMAP + (1ULL << 30) - 4)
                     : "r12", "r13", "r14", "memory");
    CHECK(got == 7);
}

// A bulk copy into a page table: every entry it reaches must be seen, past the first few slots too.
static void test_rep_into_table(u64 root) {
    enum { COUNT = 16 };
    u64 frames[2];
    frames[0] = frame();
    frames[1] = frame();
    PHYS(frames[0])[0] = 0xA;
    PHYS(frames[1])[0] = 0xB;
    u64 va = TEST_VA + 0x400000;
    u64 *slots = pte_of(root, va);
    static u64 fresh[COUNT] __attribute__((aligned(32)));
    for (int i = 0; i < COUNT; i++) {
        slots[i] = frames[0] | PRESENT_RW;
        fresh[i] = frames[1] | PRESENT_RW;
    }
    for (int i = 0; i < COUNT; i++)
        CHECK(*(volatile u64 *)(va + (u64)i * 4096) == 0xA);
    u64 count = COUNT;
    u64 *s = fresh, *d = slots;
    __asm__ volatile("cld\n rep movsq" : "+S"(s), "+D"(d), "+c"(count) : : "memory");
    for (int i = 0; i < COUNT; i++)
        CHECK(*(volatile u64 *)(va + (u64)i * 4096) == 0xB);
}

// ---- A small process in ring 3, for the tally's process log: a fault, a wait4 that returns a status, an exit ----
static u64 tss[13];
u64 user_saved_rsp, swap_slot, swap_value;
__asm__(".text\n"
        "_user_blob:\n"
        "  int3\n"
        "  leaq -16(%rsp), %rsi\n"
        "  movq $-1, %rdi\n"
        "  movl $0x2000007, %eax\n"
        "  syscall\n"
        "  leaq -16(%rsp), %rsi\n" // a wait4 that fails: errno in rax, CF set, and the status word as it was
        "  movq $-2, %rdi\n"
        "  movl $0x2000007, %eax\n"
        "  syscall\n"
        "  movl $7, %edi\n"
        "  movl $0x2000001, %eax\n"
        "  syscall\n"
        "  ud2\n"
        "_user_blob_end:\n"
        "_user_int3:\n"
        "  iretq\n"
        "_user_syscall:\n"
        "  cmpl $0x2000009, %eax\n" // the shared region test: a store the kernel makes for it, mid-call
        "  jne 2f\n"
        "  movq _swap_slot(%rip), %rax\n"
        "  movq _swap_value(%rip), %rdx\n"
        "  movq %rdx, (%rax)\n"
        "  sysretq\n"
        "2: cmpl $0x2000001, %eax\n"
        "  je 1f\n"
        "  cmpq $-2, %rdi\n"
        "  jne 3f\n"
        "  movl $0x41, (%rsi)\n"
        "  movl $10, %eax\n"
        "  orq $1, %r11\n"
        "  sysretq\n"
        "3: movl $0x300, (%rsi)\n"
        "  movl $77, %eax\n"
        "  sysretq\n"
        "1: movq _user_saved_rsp(%rip), %rsp\n"
        "  retq\n"
        ".globl _enter_user\n"
        "_enter_user:\n" // rdi rip, rsi rsp. Registers the C caller expects kept are kept by its asm clobbers.
        "  movq %rsp, _user_saved_rsp(%rip)\n"
        "  pushq $0x23\n pushq %rsi\n pushq $0x202\n pushq $0x2B\n pushq %rdi\n"
        "  iretq\n");
extern char user_blob[] __asm__("_user_blob"), user_blob_end[] __asm__("_user_blob_end"),
    user_int3[] __asm__("_user_int3"), user_syscall[] __asm__("_user_syscall");
void enter_user(u64 rip, u64 rsp);

static u64 shared_root_copy(u64 root, u64 directory, u64 *third_out) {
    u64 r = frame(), third = frame();
    for (int i = 0; i < 512; i++) {
        PHYS(r)[i] = PHYS(root)[i];
        PHYS(third)[i] = 0;
    }
    PHYS(r)[255] = third | PRESENT_RW | 4;
    PHYS(third)[510] = directory | PRESENT_RW | 4;
    if (third_out)
        *third_out = third;
    return r;
}

#define SHARED_VA 0x7FFF80000000ULL

// mov $value, %ebx; jmp +0; mov $exit, %eax; syscall. A jump, so that two blocks get linked.
static void shared_code(u64 page, u32 value) {
    static const u8 tail[] = {0xEB, 0x00, 0xB8, 0x01, 0x00, 0x00, 0x02, 0x0F, 0x05};
    volatile u8 *to = (volatile u8 *)PHYS(page);
    to[0] = 0xBB;
    for (int i = 0; i < 4; i++)
        to[1 + i] = (u8)(value >> (i * 8));
    for (u32 i = 0; i < sizeof tail; i++)
        to[5 + i] = tail[i];
}

// Runs the code at the shared address in this address space, and says what it was.
static u32 shared_run(u64 r, u64 home) {
    u64 value;
    write_cr3(r);
    __asm__ volatile("call _enter_user"
                     : "=b"(value)
                     : "D"(SHARED_VA), "S"(SHARED_VA + 4096)
                     : "rax", "rcx", "rdx", "rbp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "memory");
    write_cr3(home);
    return (u32)value;
}

static u32 shared_run_with_stack(u64 r, u64 home) {
    u64 value;
    write_cr3(r);
    __asm__ volatile("call _enter_user"
                     : "=b"(value)
                     : "D"(SHARED_VA), "S"(SHARED_VA + 8192)
                     : "rax", "rcx", "rdx", "rbp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "memory");
    write_cr3(home);
    return (u32)value;
}

// Several address spaces nested on one page directory, as XNU's shared region is: one translation serves them all, and
// each change must reach each of them.
static void test_shared_region(u64 root) {
    u64 directory = frame(), table = frame(), code = frame(), third_a, third_b;
    for (int i = 0; i < 512; i++) {
        PHYS(directory)[i] = 0;
        PHYS(table)[i] = 0;
    }
    PHYS(directory)[0] = table | PRESENT_RW | 4;
    PHYS(table)[0] = code | PRESENT_RW | 4;
    shared_code(code, 1);
    u64 a = shared_root_copy(root, directory, &third_a), b = shared_root_copy(root, directory, &third_b);
    for (int i = 0; i < 3; i++) {
        CHECK(shared_run(a, root) == 1);
        CHECK(shared_run(b, root) == 1);
    }
    // The page moves to another frame, for everyone.
    u64 code2 = frame();
    shared_code(code2, 2);
    PHYS(table)[0] = code2 | PRESENT_RW | 4;
    CHECK(shared_run(a, root) == 2);
    CHECK(shared_run(b, root) == 2);
    // The code is written.
    shared_code(code2, 3);
    CHECK(shared_run(b, root) == 3);
    CHECK(shared_run(a, root) == 3);
    // The directory's own slot changes.
    u64 table2 = frame(), code3 = frame();
    for (int i = 0; i < 512; i++)
        PHYS(table2)[i] = 0;
    shared_code(code3, 4);
    PHYS(table2)[0] = code3 | PRESENT_RW | 4;
    PHYS(directory)[0] = table2 | PRESENT_RW | 4;
    CHECK(shared_run(a, root) == 4);
    CHECK(shared_run(b, root) == 4);
    // One process leaves for a directory of its own, as unnesting does, while the other stays. Then it comes back.
    u64 own = frame(), own_table = frame(), own_code = frame();
    for (int i = 0; i < 512; i++) {
        PHYS(own)[i] = 0;
        PHYS(own_table)[i] = 0;
    }
    shared_code(own_code, 5);
    PHYS(own)[0] = own_table | PRESENT_RW | 4;
    PHYS(own_table)[0] = own_code | PRESENT_RW | 4;
    PHYS(third_b)[510] = own | PRESENT_RW | 4;
    for (int i = 0; i < 2; i++) {
        CHECK(shared_run(b, root) == 5);
        CHECK(shared_run(a, root) == 4);
    }
    PHYS(third_b)[510] = directory | PRESENT_RW | 4;
    CHECK(shared_run(b, root) == 4);
    // Another process at the same address, never sharing.
    u64 c = shared_root_copy(root, own, 0);
    CHECK(shared_run(c, root) == 5);
    CHECK(shared_run(a, root) == 4);
    // The first process, which holds the marks, leaves. Later changes must still reach the others, and a newcomer
    // shares.
    PHYS(third_a)[510] = 0;
    CHECK(shared_run(b, root) == 4);
    u64 d = shared_root_copy(root, directory, 0);
    CHECK(shared_run(d, root) == 4);
    shared_code(code3, 6);
    CHECK(shared_run(b, root) == 6);
    CHECK(shared_run(d, root) == 6);
    u64 code4 = frame();
    shared_code(code4, 7);
    PHYS(table2)[0] = code4 | PRESENT_RW | 4;
    CHECK(shared_run(d, root) == 7);
    CHECK(shared_run(b, root) == 7);
    // Its tables are freed and poisoned, and nothing walks them to any effect.
    PHYS(a)[255] = 0xDEADBEEFDEADBEEFULL;
    CHECK(shared_run(b, root) == 7);
    // Processes come and go, more than there are views, with a change at each step reaching them all.
    u32 value = 8;
    u64 longer = shared_root_copy(root, directory, 0);
    for (int i = 0; i < 300; i++) {
        u64 third, child = shared_root_copy(root, directory, &third);
        CHECK(shared_run(child, root) == value - 1);
        CHECK(shared_run(longer, root) == value - 1);
        if (i % 3 == 0) {
            shared_code(code4, value - 1);
        } else {
            u64 next = frame();
            shared_code(next, value);
            PHYS(table2)[0] = next | PRESENT_RW | 4;
            CHECK(shared_run(child, root) == value);
            value++;
        }
        PHYS(third)[510] = 0; // it exits
        CHECK(shared_run(longer, root) == value - 1);
        CHECK(shared_run(d, root) == value - 1);
    }
}

// A program's direct call into the shared region can't link, so it looks its target up, and that must keep up with the target.
static void test_call_between_keys(u64 root) {
    u64 directory = frame(), table = frame(), starter = frame(), callee = frame(), callee2 = frame();
    u64 own = frame(), own_table = frame(), caller = frame(), stack = frame(), third_a, third_b;
    for (int i = 0; i < 512; i++) {
        PHYS(directory)[i] = PHYS(table)[i] = PHYS(own)[i] = PHYS(own_table)[i] = 0;
    }
    PHYS(directory)[0] = table | PRESENT_RW | 4;
    PHYS(table)[0] = starter | PRESENT_RW | 4;
    PHYS(table)[1] = callee | PRESENT_RW | 4;
    shared_code(starter, 1);
    static const u8 ret_value[] = {0xBB, 7, 0, 0, 0, 0xC3}; // mov $7, %ebx; ret
    volatile u8 *c1 = (volatile u8 *)PHYS(callee), *c2 = (volatile u8 *)PHYS(callee2);
    for (u32 i = 0; i < sizeof ret_value; i++)
        c1[i] = c2[i] = ret_value[i];
    c2[1] = 8;
    u64 a = shared_root_copy(root, directory, &third_a), b = shared_root_copy(root, directory, &third_b);
    for (int i = 0; i < 3; i++) { // two address spaces on one directory: it counts as shared from here
        CHECK(shared_run(a, root) == 1);
        CHECK(shared_run(b, root) == 1);
    }
    // In a directory of a's own, the page just below: mov $50, %ecx; 1: call SHARED_VA + 4096; dec %ecx; jnz 1b; mov $exit, %eax; syscall
    u64 at = SHARED_VA - 4096;
    PHYS(third_a)[509] = own | PRESENT_RW | 4;
    PHYS(own)[511] = own_table | PRESENT_RW | 4;
    PHYS(own_table)[511] = caller | PRESENT_RW | 4;
    PHYS(own_table)[510] = stack | PRESENT_RW | 4;
    static const u8 calls[] = {0xB9, 50, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0xFF, 0xC9, 0x75, 0xF7, 0xB8, 0x01, 0x00, 0x00, 0x02, 0x0F, 0x05};
    volatile u8 *to = (volatile u8 *)PHYS(caller);
    for (u32 i = 0; i < sizeof calls; i++)
        to[i] = calls[i];
    *(volatile u32 *)(to + 6) = (u32)(SHARED_VA + 4096 - (at + 10));
    u64 value;
    #define RUN_CALLER() do { \
        write_cr3(a); \
        __asm__ volatile("call _enter_user" : "=b"(value) : "D"(at), "S"(at) : "rax", "rcx", "rdx", "rbp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "memory"); \
        write_cr3(root); \
    } while (0)
    RUN_CALLER();
    CHECK(value == 7);
    RUN_CALLER();
    CHECK(value == 7);
    // The target moves to another frame, for everyone.
    PHYS(table)[1] = callee2 | PRESENT_RW | 4;
    RUN_CALLER();
    CHECK(value == 8);
    // And is written.
    c2[1] = 9;
    RUN_CALLER();
    CHECK(value == 9);
}

// A return needs no lookup: a process that leaves the directory mid-call must not come back to the shared translation
// of its caller. shared page: call helper; mov $value, %ebx; mov $exit, %eax; syscall
static void shared_caller(u64 page, u32 value) {
    static const u8 head[] = {0xE8, 0xFB, 0x1F, 0x00, 0x00, 0xBB};
    static const u8 tail[] = {0xB8, 0x01, 0x00, 0x00, 0x02, 0x0F, 0x05};
    volatile u8 *to = (volatile u8 *)PHYS(page);
    for (u32 i = 0; i < sizeof head; i++)
        to[i] = head[i];
    for (int i = 0; i < 4; i++)
        to[sizeof head + (u32)i] = (u8)(value >> (i * 8));
    for (u32 i = 0; i < sizeof tail; i++)
        to[sizeof head + 4 + i] = tail[i];
}

static void test_shared_return(u64 root) {
    u64 directory = frame(), table = frame(), code = frame(), stack = frame(), helper = frame(), own = frame(),
        own_table = frame(), own_code = frame();
    // helper, at the second page after the caller: mov $swap, %eax; syscall; ret
    static const u8 body[] = {0xB8, 0x09, 0x00, 0x00, 0x02, 0x0F, 0x05, 0xC3};
    for (u32 i = 0; i < sizeof body; i++)
        ((volatile u8 *)PHYS(helper))[i] = body[i];
    shared_caller(code, 21);
    shared_caller(own_code, 22);
    PHYS(directory)[0] = table | PRESENT_RW | 4;
    PHYS(table)[0] = code | PRESENT_RW | 4;
    PHYS(table)[1] = stack | PRESENT_RW | 4;
    PHYS(table)[2] = helper | PRESENT_RW | 4;
    PHYS(own)[0] = own_table | PRESENT_RW | 4;
    PHYS(own_table)[0] = own_code | PRESENT_RW | 4;
    PHYS(own_table)[1] = stack | PRESENT_RW | 4;
    PHYS(own_table)[2] = helper | PRESENT_RW | 4;
    u64 third_a, third_b;
    u64 a = shared_root_copy(root, directory, &third_a), b = shared_root_copy(root, directory, &third_b);
    // Warm, so that the return stack and the links lead straight back.
    static u64 unused;
    swap_slot = (u64)&unused;
    swap_value = 0;
    for (int i = 0; i < 4; i++) {
        CHECK(shared_run_with_stack(a, root) == 21);
        CHECK(shared_run_with_stack(b, root) == 21);
    }
    swap_slot = (u64)&PHYS(third_b)[510];
    swap_value = own | PRESENT_RW | 4;
    CHECK(shared_run_with_stack(b, root) == 22);
    CHECK(shared_run_with_stack(a, root) == 21);
    swap_slot = (u64)&unused;
    PHYS(third_b)[510] = directory | PRESENT_RW | 4;
    CHECK(shared_run_with_stack(b, root) == 21);
}

// rcpps and rsqrtps as TCG computes them: exact, against divps and sqrtps here, with no flag raised even where those
// raise one.
// The TSC deadline goes to the APIC with no step on TCG, whose stand-in here would leave this alone. r13 0: nothing
// played.
static void test_tsc_deadline(void) {
    __asm__ volatile(
        "mov $0, %%r13\n mov $2, %%r14\n mov $0x6E0, %%ecx\n mov $0x89ABCDEF, %%eax\n mov $0x1234, %%edx\n wrmsr\n"
        :
        :
        : "r13", "r14", "rcx", "rax", "rdx");
    command(17); // the harness checks what reached it
}

static void test_reciprocals(void) {
    static const u32 values[12]
        __attribute__((aligned(16))) = {0x3F800000, 0x40490FDB, 0xC0000000, 0x00000000, 0x80000000, 0x7F800000,
                                        0xFF800000, 0x7FC00001, 0x7F800001, 0x00000001, 0x3EAAAAAB, 0x7F7FFFFF};
    static const u32 ones[4] __attribute__((aligned(16))) = {0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000};
    for (int at = 0; at < 12; at += 4) {
        u32 got[4] __attribute__((aligned(16))), want[4] __attribute__((aligned(16))),
            scalar[4] __attribute__((aligned(16)));
        u32 before, after, clean = 0x1F80;
        __asm__ volatile("ldmxcsr %0" : : "m"(clean));
        __asm__ volatile("movaps %2, %%xmm1\n rcpps %%xmm1, %%xmm2\n movaps %%xmm2, %0\n stmxcsr %1\n"
                         : "=m"(got), "=m"(before)
                         : "m"(values[at])
                         : "xmm1", "xmm2");
        CHECK(before == clean);
        __asm__ volatile("movaps %1, %%xmm2\n divps %2, %%xmm2\n movaps %%xmm2, %0\n"
                         : "=m"(want)
                         : "m"(ones), "m"(values[at])
                         : "xmm2");
        for (int i = 0; i < 4; i++)
            CHECK(got[i] == want[i]);
        __asm__ volatile("ldmxcsr %0" : : "m"(clean));
        __asm__ volatile("movaps %2, %%xmm1\n rsqrtps %%xmm1, %%xmm2\n movaps %%xmm2, %0\n stmxcsr %1\n"
                         : "=m"(got), "=m"(after)
                         : "m"(values[at])
                         : "xmm1", "xmm2");
        CHECK(after == clean);
        __asm__ volatile("sqrtps %2, %%xmm1\n movaps %1, %%xmm2\n divps %%xmm1, %%xmm2\n movaps %%xmm2, %0\n"
                         : "=m"(want)
                         : "m"(ones), "m"(values[at])
                         : "xmm1", "xmm2");
        for (int i = 0; i < 4; i++)
            CHECK(got[i] == want[i]);
        // Scalar: only the low lane.
        __asm__ volatile("ldmxcsr %0" : : "m"(clean));
        __asm__ volatile("movaps %1, %%xmm2\n rcpss %2, %%xmm2\n movaps %%xmm2, %0\n"
                         : "=m"(scalar)
                         : "m"(ones), "m"(values[at])
                         : "xmm2");
        __asm__ volatile("movaps %1, %%xmm2\n divps %2, %%xmm2\n movaps %%xmm2, %0\n"
                         : "=m"(want)
                         : "m"(ones), "m"(values[at])
                         : "xmm2");
        CHECK(scalar[0] == want[0] && scalar[1] == ones[1] && scalar[2] == ones[2] && scalar[3] == ones[3]);
        __asm__ volatile("movaps %1, %%xmm2\n rsqrtss %2, %%xmm2\n movaps %%xmm2, %0\n"
                         : "=m"(scalar)
                         : "m"(ones), "m"(values[at])
                         : "xmm2");
        __asm__ volatile("sqrtps %2, %%xmm1\n movaps %1, %%xmm2\n divps %%xmm1, %%xmm2\n movaps %%xmm2, %0\n"
                         : "=m"(want)
                         : "m"(ones), "m"(values[at])
                         : "xmm1", "xmm2");
        CHECK(scalar[0] == want[0] && scalar[1] == ones[1]);
        __asm__ volatile("ldmxcsr %0" : : "m"(clean));
    }
}

// What a kernel's lazy switch does to one task's x87 stack or MMX registers: #NM, fxsave, another task using them,
// fxrstor. Each must come back whole. And MMX over a live stack does what a CPU does, which a program mixing the two
// without emms finds out: TOP goes to 0, so fstp reads another register.
static void test_x87_across_a_switch(void) {
    static u8 area[512] __attribute__((aligned(16)));
    static const double two = 2.0, three = 3.0;
    u64 cr0;
    double got = 0, other = 0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    u64 nm = nm_count;
    __asm__ volatile("fninit\n fldl %0\n fldl %1\n" : : "m"(two), "m"(three)); // 2 below 3
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8)); // switched away
    __asm__ volatile("fninit"); // the next task's first FP use takes #NM, and its handler clears TS
    CHECK(nm_count == nm + 1);
    __asm__ volatile("fldpi\n fstpl %0\n" : "=m"(other));
    CHECK(other > 3.14 && other < 3.15);
    // That lost the stack, as nothing saved it: now as a kernel does it.
    __asm__ volatile("fninit\n fldl %0\n fldl %1\n fxsave64 (%2)\n" : : "m"(two), "m"(three), "r"(area) : "memory");
    __asm__ volatile("fninit\n fld1\n fld1\n fldpi\n fxrstor64 (%0)\n" : : "r"(area) : "memory");
    __asm__ volatile("fstpl %0" : "=m"(got));
    CHECK(got == 3.0);
    __asm__ volatile("fstpl %0" : "=m"(got));
    CHECK(got == 2.0);
    // Registers saved under MMX come back to MMX.
    u64 mm = 0x1122334455667788ULL, out = 0;
    __asm__ volatile(
        "emms\n movq %0, %%mm3\n fxsave64 (%1)\n emms\n fninit\n pxor %%mm3, %%mm3\n emms\n fxrstor64 (%1)\n"
        :
        : "m"(mm), "r"(area)
        : "memory", "mm3");
    __asm__ volatile("movq %%mm3, %0\n emms\n" : "=m"(out));
    CHECK(out == mm);
    // Without emms: fld at physical 7, TOP back to 0 by the MMX, and fstp reads physical 0, not what fld loaded. MMX
    // writes leave that register's exponent all ones, so it isn't a plain 0.
    u64 zero = 0;
    __asm__ volatile(
        "fninit\n emms\n movq %0, %%mm0\n emms\n fninit\n fldl %1\n movq %%mm3, %%mm3\n fstpl %2\n emms\n fninit\n"
        :
        : "m"(zero), "m"(three), "m"(got)
        : "mm0", "mm3", "memory");
    CHECK(got != 3.0);
}

// Kernel code, shared by every address space. A process exiting must leave its translations for the rest, and changes
// to it must still reach them all.
#define KERNEL_SHARED_VA 0xFFFF960000000000ULL

static u32 kernel_shared_run(u64 r, u64 home) {
    write_cr3(r);
    u32 value = ((u32 (*)(void))KERNEL_SHARED_VA)();
    write_cr3(home);
    return value;
}

static void test_kernel_domain(u64 root) {
    enum { SLOT = (KERNEL_SHARED_VA >> 39) & 0x1ff };
    u64 code = frame(), code2 = frame();
    put_function(code, 1);
    u64 *slot = pte_of(root, KERNEL_SHARED_VA);
    *slot = code | PRESENT_RW;
    u64 a = root_copy(root), b = root_copy(root);
    for (int i = 0; i < 3; i++) {
        CHECK(kernel_shared_run(a, root) == 1);
        CHECK(kernel_shared_run(b, root) == 1);
    }
    put_function(code2, 2);
    *slot = code2 | PRESENT_RW;
    __asm__ volatile("invlpg (%0)" : : "r"(KERNEL_SHARED_VA));
    CHECK(kernel_shared_run(a, root) == 2);
    CHECK(kernel_shared_run(b, root) == 2);
    put_function(code2, 3);
    CHECK(kernel_shared_run(b, root) == 3);
    CHECK(kernel_shared_run(a, root) == 3);
    // One space alone changes its slot: it sees its own, and the other is not disturbed. Not the first to run this
    // code, as that one holds the marks.
    u64 own = frame();
    put_function(own, 4);
    PHYS(b)[SLOT] = 0;
    *pte_of(b, KERNEL_SHARED_VA) = own | PRESENT_RW;
    for (int i = 0; i < 2; i++) {
        CHECK(kernel_shared_run(b, root) == 4);
        CHECK(kernel_shared_run(a, root) == 3);
    }
    PHYS(b)[SLOT] = PHYS(root)[SLOT];
    CHECK(kernel_shared_run(b, root) == 3);
    // Processes exit, each after running kernel code: the harness checks that this does not keep killing blocks.
    for (int i = 0; i < 300; i++) {
        u64 child = root_copy(root);
        write_cr3(child);
        for (int n = 0; n < 8; n++)
            CHECK(((u32 (*)(void))KERNEL_SHARED_VA)() == 3);
        CHECK(pte_of(child, TEST_VA) != 0);
        puts_("");
        write_cr3(root);
        for (int k = 256; k < 512; k++)
            PHYS(child)[k] = 0;
    }
    CHECK(kernel_shared_run(b, root) == 3);
    put_function(code2, 5);
    CHECK(kernel_shared_run(b, root) == 5);
    CHECK(kernel_shared_run(a, root) == 5);
}

// More blocks than a view's lookup table holds, each reached by a link. A full table left the monitor's lookup
// searching forever for an empty slot.
static void test_many_blocks(u64 root) {
    enum { PAGES = 40 };
    u64 va = TEST_VA + 0x20000000;
    for (int p = 0; p < PAGES; p++) {
        u64 page = frame();
        *pte_of(root, va + (u64)p * 4096) = page | PRESENT_RW;
        volatile u8 *to = (volatile u8 *)PHYS(page);
        for (int i = 0; i < 4096; i += 2) { // jmp +0: each one ends a block
            to[i] = 0xEB;
            to[i + 1] = 0;
        }
        if (p == PAGES - 1)
            to[4094] = 0xC3;
    }
    for (int round = 0; round < 2; round++)
        ((void (*)(void))va)();
}

// A linked loop has no way out but an interrupt or a stop request, and it must take them. The host raises the line from
// another thread once it is running.
static void test_linked_loop(void) {
    u64 irqs = irq_count, cr0;
    __asm__ volatile("sti");
    for (int i = 0; i < 3; i++) {
        command(15);
        __asm__ volatile("1: cmpq %0, %1\n je 1b\n" : : "r"(irqs), "m"(irq_count));
        CHECK(irq_count > irqs);
        irqs = irq_count;
    }
    (void)cr0;
}

// A block that dies with nothing wrong with it comes back, and one whose bytes changed doesn't: nor one whose code would have
// been different because of bytes after its end, which decide whether it must compute the flags.
static void test_reuse(u64 root) {
    u64 code = frame(), va = TEST_VA + 0x40200000;
    volatile u8 *p = (volatile u8 *)PHYS(code);
    // xor %eax,%eax; cmp $5,%edi; jmp past what decoding the jmp reads, so only the look ahead for live flags reads the target
    static const u8 start[] = {0x31, 0xC0, 0x83, 0xFF, 0x05, 0xEB, 0x40};
    static const u8 flags_dead[] = {0x31, 0xC0, 0x90, 0xC3}, flags_read[] = {0x0F, 0x92, 0xC0, 0xC3}; // xor %eax,%eax, or setc %al; then ret
    for (u64 i = 0; i < sizeof start; i++)
        p[i] = start[i];
    for (u64 i = 0; i < 4; i++)
        p[0x47 + i] = flags_dead[i];
    u32 (*f)(u32) = (u32 (*)(u32))va;
    *pte_of(root, va) = code | PRESENT_RW;
    __asm__ volatile("invlpg (%0)" : : "r"(va));
    CHECK(f(1) == 0);
    // The same frame mapped again: the block dies, and its bytes are the same.
    *pte_of(root, va) = code | PRESENT_RW | (1ULL << 6);
    __asm__ volatile("invlpg (%0)" : : "r"(va));
    CHECK(f(1) == 0);
    // Only what follows the first block changes.
    for (u64 i = 0; i < 4; i++)
        p[0x47 + i] = flags_read[i];
    CHECK(f(1) == 1);
    CHECK(f(9) == 0);
    // And what the first block is itself.
    p[4] = 0x00; // cmp $0,%edi
    CHECK(f(1) == 0);
    // The same bytes on another frame: a store there must reach what runs.
    u64 other = frame();
    volatile u8 *q = (volatile u8 *)PHYS(other);
    for (u64 i = 0; i < 0x4B; i++)
        q[i] = p[i];
    *pte_of(root, va) = other | PRESENT_RW;
    __asm__ volatile("invlpg (%0)" : : "r"(va));
    CHECK(f(1) == 0);
    q[4] = 0x05;
    CHECK(f(1) == 1);
}

// A call is linked after its first run, so a hot one costs nothing like a trip through the monitor, which is over 100 ns.
static __attribute__((noinline)) u32 called(u32 x) { return x + 1; }
static void test_calls_link(void) {
    u64 best_direct = ~0ULL;
    for (int round = 0; round < 20 && best_direct >= 50; round++) {
        u64 t0 = __builtin_ia32_rdtsc();
        u32 x = 0;
        for (u32 i = 0; i < 100000; i++)
            x = called(x);
        CHECK(x == 100000);
        u64 t = (__builtin_ia32_rdtsc() - t0) / 100000;
        best_direct = t < best_direct ? t : best_direct;
    }
    CHECK(best_direct < 50);
}

// An IPI: the monitor answers for ICR, in the page the harness calls the APIC's, and only the send reaches the host.
static void test_icr(void) {
    volatile u32 *apic = (volatile u32 *)PHYS(0xD0000000ULL);
    apic[0x310 / 4] = 0x03000000;
    CHECK(apic[0x310 / 4] == 0x03000000);
    command(21); // nothing yet
    apic[0x300 / 4] = 0x000040DD;
    CHECK(apic[0x300 / 4] == 0x000040DD);
    command(22); // the harness has it, once
    apic[0x300 / 4 + 1] = 5; // the page's other registers are the device's
    CHECK(apic[0x320 / 4] == (u32)(0xD0000320ULL * 3));
}

// A machine reset or a snapshot load: RAM changes unseen, the engine is told to forget, and what runs next is what is there.
static void test_forget_all(u64 root) {
    u64 code = frame(), va = TEST_VA + 0x40600000;
    put_function(code, 5);
    *pte_of(root, va) = code | PRESENT_RW;
    __asm__ volatile("invlpg (%0)" : : "r"(va));
    u32 (*f)(void) = (u32 (*)(void))va;
    CHECK(f() == 5);
    MAILBOX[1] = code + 1; // the function's mov $value, %eax
    MAILBOX[2] = 6;
    u64 rbx = 0;
    __asm__ volatile("mov $30, %%r13\n mov $5, %%r14\n .byte 0x66, 0x0F, 0x38, 0x41, 0xC1\n" : "=b"(rbx) : : "r13", "r14", "xmm0");
    CHECK(f() == 6);
}

static u32 crc32c_soft(u32 crc, u64 value, int bytes) {
    for (int i = 0; i < bytes; i++) {
        crc ^= (u8)(value >> (i * 8));
        for (int b = 0; b < 8; b++)
            crc = crc & 1 ? crc >> 1 ^ 0x82F63B78u : crc >> 1;
    }
    return crc;
}

// moffs in 64-bit mode: an address of eight bytes, and nothing else to decode.
static void test_moffs(void) {
    static volatile u64 cell __attribute__((used));
    u64 r = 0x1122334455667788ULL, got;
    __asm__ volatile(".byte 0x48, 0xa3\n .quad %c1" : : "a"(r), "i"(&cell) : "memory"); // mov %rax, cell
    CHECK(cell == r);
    __asm__ volatile(".byte 0xa1\n .quad %c1" : "=a"(got) : "i"(&cell), "0"(~0ULL) : "memory"); // mov cell, %eax
    CHECK(got == (u32)r);
    __asm__ volatile(".byte 0xa2\n .quad %c1" : : "a"(0x99ULL), "i"(&cell) : "memory"); // mov %al, cell
    CHECK(cell == 0x1122334455667799ULL);
}

// strcmp's use of pcmpistri, as Mavericks has it: equal each, negative, least significant index.
static void test_pcmpistri(void) {
    static const char a[16] __attribute__((aligned(16))) = "hello, wo", b[16] __attribute__((aligned(16))) = "hello, world";
    u32 index, flags;
    __asm__ volatile("movdqa %2, %%xmm1\n pcmpistri $0x1a, %3, %%xmm1\n pushfq\n pop %q1" : "=c"(index), "=r"(flags) : "m"(*a), "m"(*b) : "xmm1", "cc");
    CHECK(index == 9); // where they first differ
    CHECK(flags & 1); // CF: a difference
    CHECK(flags & 0x40); // ZF: b ends here
    CHECK(flags & 0x80); // SF: a ends here
    CHECK(!(flags & 0x800) && !(flags & 4) && !(flags & 0x10)); // OF: not at the first, PF and AF clear
}

static void test_crc32(void) {
    volatile u64 from_memory = 0x123456789ABCDEF0ULL;
    u64 v = 0x0FEDCBA987654321ULL;
    u64 r = 0xFFFFFFFF00000005ULL; // the top half goes
    __asm__ volatile("crc32q %1, %0" : "+r"(r) : "r"(v));
    CHECK(r == crc32c_soft(5, v, 8));
    u32 d = 7;
    __asm__ volatile("crc32l %1, %0" : "+r"(d) : "r"((u32)v));
    CHECK(d == crc32c_soft(7, v, 4));
    d = 9;
    __asm__ volatile("crc32w %1, %0" : "+r"(d) : "r"((u16)v));
    CHECK(d == crc32c_soft(9, v, 2));
    d = 11;
    __asm__ volatile("crc32b %b1, %0" : "+r"(d) : "q"((u8)v));
    CHECK(d == crc32c_soft(11, v, 1));
    d = 13;
    __asm__ volatile("mov $0x4142, %%eax\n crc32b %%ah, %0" : "+r"(d) : : "rax");
    CHECK(d == crc32c_soft(13, 0x41, 1));
    r = 15;
    __asm__ volatile("crc32q %1, %0" : "+r"(r) : "m"(from_memory));
    CHECK(r == crc32c_soft(15, from_memory, 8));
    d = 17;
    __asm__ volatile("crc32b %1, %0" : "+r"(d) : "m"(*(volatile u8 *)&from_memory));
    CHECK(d == crc32c_soft(17, from_memory, 1));
}

// The locked forms a kernel updates PTEs with, on a tracked table: the value lands, and so does the change of mapping.
static void test_locked_pte(u64 root) {
    u64 a = frame(), b = frame(), va = TEST_VA + 0x40A00000;
    PHYS(a)[0] = 0xAAAA;
    PHYS(b)[0] = 0xBBBB;
    volatile u64 *pte = pte_of(root, va);
    *pte = a | PRESENT_RW;
    __asm__ volatile("invlpg (%0)" : : "r"(va));
    CHECK(*(volatile u64 *)va == 0xAAAA); // the table is tracked now
    __asm__ volatile("lock andq %1, %0" : "+m"(*pte) : "r"(~2ULL)); // no longer writable
    __asm__ volatile("invlpg (%0)" : : "r"(va));
    CHECK(!(*pte & 2));
    CHECK(*(volatile u64 *)va == 0xAAAA);
    __asm__ volatile("lock orq %1, %0" : "+m"(*pte) : "r"(2ULL));
    CHECK(*pte == (a | PRESENT_RW | 0x60ULL) || (*pte & ~0x60ULL) == (a | PRESENT_RW));
    __asm__ volatile("lock xorq %1, %0" : "+m"(*pte) : "r"(a ^ b)); // now the other frame
    __asm__ volatile("invlpg (%0)" : : "r"(va));
    CHECK(*(volatile u64 *)va == 0xBBBB);
    __asm__ volatile("lock xorq %1, %0" : "+m"(*pte) : "r"(a ^ b));
    __asm__ volatile("invlpg (%0)" : : "r"(va));
    CHECK(*(volatile u64 *)va == 0xAAAA);
    __asm__ volatile("lock addq %1, %0" : "+m"(*pte) : "r"(b - a));
    __asm__ volatile("invlpg (%0)" : : "r"(va));
    CHECK(*(volatile u64 *)va == 0xBBBB);
    u64 old = 0;
    __asm__ volatile("lock xaddq %0, %1" : "+r"(old), "+m"(*pte));
    CHECK((old & 0x000FFFFFFFFFF000ULL) == b);
}

// An interrupt raised just after a hlt ends it, polled in the monitor or through TCG if the host is slow, and the handler
// returns past the hlt.
static void test_hlt_poll(void) {
    __asm__ volatile("sti");
    for (int i = 0; i < 100; i++) {
        u64 irqs = irq_count;
        command(20);
        __asm__ volatile("mov $0, %%r13\n mov $1, %%r14\n hlt\n" : : : "r13", "r14");
        while (irq_count == irqs) // TCG's hlt, played by the harness, doesn't wait
            __asm__ volatile("pause");
    }
    __asm__ volatile("cli");
}

// A second vCPU joins through the C API, as an application processor does under QEMU: it takes fallbacks and kicks here
// while the first works on, with blocks shared between them.
static volatile u64 second_count, second_done, second_direct_reads, second_code_va, second_code_frame;
static volatile u32 *volatile second_code_done;
static bool second_joined;
static u8 second_stack[16384] __attribute__((aligned(16)));

void second_vcpu(void) {
    for (int i = 0; i < 2000; i++) {
        u64 rbx = 0;
        __asm__ volatile("mov $1, %%r13\n mov $5, %%r14\n .byte 0x66, 0x0F, 0x38, 0x41, 0xC1\n"
                         : "=b"(rbx)
                         :
                         : "r13", "r14", "xmm0");
        CHECK(rbx == 0x1234);
        CHECK(((u32 (*)(void))KERNEL_SHARED_VA)() != 0);
        second_count++;
    }
    second_done = 1;
    for (;;) { // the first vCPU's device memory comes and goes under this
        u64 now = *(volatile u64 *)PHYS(0xC0000000ULL);
        CHECK(now == 0x0123456789ABCDEFULL || now == 0xC0000000ULL * 3);
        second_direct_reads++;
        if (second_code_va) { // the first vCPU sits in 32-bit code meanwhile, and what is translated here is still 64-bit
            volatile u8 *p = (volatile u8 *)PHYS(second_code_frame);
            u64 (*f)(u32) = (u64 (*)(u32))second_code_va;
            p[0] = 0xB1; // mov $i, %cl; movslq %edi, %rax; ret
            p[2] = 0x48;
            p[3] = 0x63;
            p[4] = 0xC7;
            p[5] = 0xC3;
            for (int i = 0; i < 200; i++) {
                p[1] = (u8)i;
                CHECK(f(0xFFFFFFFFu) == ~0ULL);
            }
            second_code_va = 0;
            *second_code_done = 1;
        }
    }
}

static void test_second_vcpu(u64 root) {
    MAILBOX[1] = 0;
    MAILBOX[2] = (u64)second_vcpu;
    MAILBOX[3] = (u64)second_stack + sizeof second_stack - 8;
    command(18);
    if (!MAILBOX[1])
        return;
    second_joined = 1;
    for (u64 turns = 0; !second_done; turns++) {
        u64 rbx = 0;
        __asm__ volatile("mov $1, %%r13\n mov $5, %%r14\n .byte 0x66, 0x0F, 0x38, 0x41, 0xC1\n"
                         : "=b"(rbx)
                         :
                         : "r13", "r14", "xmm0");
        CHECK(rbx == 0x1234);
        // Notes for the shared ring, of a frame holding code they both run, and the blocks die under the other vCPU.
        if (!(turns & 255)) {
            u64 code = frame();
            put_function(code, 7);
            *pte_of(root, KERNEL_SHARED_VA) = code | PRESENT_RW;
            __asm__ volatile("invlpg (%0)" : : "r"(KERNEL_SHARED_VA));
            CHECK(((u32 (*)(void))KERNEL_SHARED_VA)() == 7);
        }
        CHECK(turns < 100000000);
    }
    CHECK(second_count == 2000);
    // A device's memory leaves while the other vCPU reads it: its shadows need dropping there, under the lock, and it may
    // get to the hole before it next enters the monitor.
    for (int round = 0; round < 200; round++) {
        command(13);
        for (u64 until = second_direct_reads + 100; second_direct_reads < until;)
            __asm__ volatile("pause");
        command(19);
        for (u64 until = second_direct_reads + 100; second_direct_reads < until;)
            __asm__ volatile("pause");
    }
}

static void test_process_log(u64 root) {
    u64 t = (u64)tss;
    gdt[3] = 0x00CFF2000000FFFFULL;
    gdt[4] = 0x00CFF2000000FFFFULL;
    gdt[5] = 0x00AFFA000000FFFFULL;
    gdt[6] =
        ((t & 0xffff) << 16) | ((t >> 16 & 0xff) << 32) | (0x89ULL << 40) | ((t >> 24 & 0xff) << 56) | (sizeof tss - 1);
    gdt[7] = t >> 32;
    static u8 kernel_stack[4096] __attribute__((aligned(16)));
    tss[0] = (u64)(kernel_stack + sizeof kernel_stack) << 32; // rsp0 sits at byte 4
    tss[1] = (u64)(kernel_stack + sizeof kernel_stack) >> 32;
    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed)) g = {sizeof gdt - 1, (u64)gdt};
    __asm__ volatile("lgdt %0\n ltr %%ax" : : "m"(g), "a"(0x30));
    u64 a = (u64)user_int3;
    idt[3] = (Gate){(u16)a, 8, 0, 0xEE, (u16)(a >> 16), (u32)(a >> 32), 0};
    __asm__ volatile("wrmsr" : : "c"(0xC0000081), "a"(0), "d"(0x00180008));
    a = (u64)user_syscall;
    __asm__ volatile("wrmsr" : : "c"(0xC0000082), "a"((u32)a), "d"((u32)(a >> 32)));

    u64 va = TEST_VA + 0x800000;
    u64 *slot = pte_of(root, va);
    u64 table = root;
    for (int level = 3; level > 0; level--) { // user access needs U at every level
        volatile u64 *e = &PHYS(table)[(va >> (12 + 9 * level)) & 0x1ff];
        *e |= 4;
        table = *e & 0x000FFFFFFFFFF000ULL;
    }
    u64 page = frame();
    u8 *to = (u8 *)PHYS(page);
    for (int i = 0; i < user_blob_end - user_blob; i++)
        to[i] = (u8)user_blob[i];
    *slot = page | PRESENT_RW | 4;
    __asm__ volatile("call _enter_user"
                     :
                     : "D"(va), "S"(va + 4096)
                     : "rax", "rbx", "rcx", "rdx", "rbp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
                       "memory");
    // The harness checks the log. The gate needs putting back for nothing else: nothing here takes a breakpoint again.
}

// A table the guest stopped using and now keeps data in. Trapping every store there cost one boot 250,000 stores a
// second.
static void test_stale_table(u64 root) {
    u64 va = TEST_VA + 0x40000000; // a page directory entry of its own
    u64 *leaf = pte_of(root, va);
    u64 table = ((u64)leaf - PHYSMAP) & ~0xFFFULL;
    u64 third = PHYS(root)[(va >> 39) & 0x1ff] & 0x000FFFFFFFFFF000ULL;
    volatile u64 *directory = PHYS(PHYS(third)[(va >> 30) & 0x1ff] & 0x000FFFFFFFFFF000ULL);
    leaf[0] = frame() | PRESENT_RW;
    CHECK(*(volatile u64 *)va != 1); // walked: the frame is tracked
    directory[(va >> 21) & 0x1ff] = 0; // no longer a table
    for (int round = 0; round < 500; round++)
        for (int i = 0; i < 512; i++)
            PHYS(table)[i] = (u64)(round + i);
    CHECK(PHYS(table)[511] == 499 + 511);
}

// Device memory mapped into the VM: no exit for it, each side sees the other's bytes, and once it is gone the device
// answers again.
static void test_direct_map(void) {
    volatile u64 *direct = PHYS(0xC0000000ULL);
    command(13); // the host maps it, with a pattern in place
    CHECK(direct[0] == 0x0123456789ABCDEFULL);
    CHECK(direct[8191] == 0xFEDCBA9876543210ULL);
    direct[1] = 0x1111222233334444ULL;
    static u64 from[16];
    for (int i = 0; i < 16; i++)
        from[i] = 0xA5A5000000000000ULL + (u64)i;
    u64 count = 16, *s = from;
    u64 *d = (u64 *)&direct[64];
    __asm__ volatile("cld\n rep movsq" : "+S"(s), "+D"(d), "+c"(count) : : "memory");
    u64 old = __sync_val_compare_and_swap(&direct[2], 0, 7ULL);
    CHECK(old == 0);
    command(14); // the host checks those and takes it away
    CHECK(direct[0] == 0xC0000000ULL * 3); // the harness's device
}

// With the line raised under cli, the vector comes after the one instruction following sti, not before it. A function
// of its own: inline, the compiler unrolls the caller's loop, and each pass would be new code, whose first run comes
// back through the monitor whatever sti does.
static __attribute__((noinline)) void sti_with_a_vector_waiting(void) {
    __asm__ volatile("cli");
    u64 irqs = irq_count, seen = ~0ULL, later = ~0ULL;
    command(4);
    __asm__ volatile("sti\n mov %2, %0\n nop\n mov %2, %1\n" : "=&r"(seen), "=&r"(later) : "m"(irq_count));
    CHECK(seen == irqs);
    CHECK(later == irqs + 1);
}

// The inlined forms: each must see what the monitor's state says, and sti must keep its one-instruction shadow with an
// interrupt waiting.
static void test_inline_ops(u64 root) {
    u64 cr3, cr0, cr4;
    __asm__ volatile("mov %%cr3, %0\n mov %%cr0, %1\n mov %%cr4, %2" : "=r"(cr3), "=r"(cr0), "=r"(cr4));
    CHECK((cr3 & 0x000FFFFFFFFFF000ULL) == (root & 0x000FFFFFFFFFF000ULL));
    CHECK(cr0 & 0x80000000ULL);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    u64 again;
    __asm__ volatile("mov %%cr0, %0" : "=r"(again));
    CHECK(again == (cr0 | 8));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    u16 ds;
    __asm__ volatile("mov %%ds, %0" : "=r"(ds));
    CHECK(ds == 0x10);
    struct __attribute__((packed)) {
        u16 limit;
        u64 base;
    } idtr;
    __asm__ volatile("sidt %0" : "=m"(idtr));
    CHECK(idtr.base == (u64)idt && idtr.limit == sizeof(idt) - 1);
    u64 before, after;
    __asm__ volatile("rdtsc\n shl $32, %%rdx\n or %%rdx, %%rax" : "=a"(before) : : "rdx");
    __asm__ volatile("rdtsc\n shl $32, %%rdx\n or %%rdx, %%rax" : "=a"(after) : : "rdx");
    CHECK(after > before);
    u64 flags;
    __asm__ volatile("cli\n pushfq\n popq %0" : "=r"(flags));
    CHECK(!(flags & 0x200));
    __asm__ volatile("sti\n pushfq\n popq %0" : "=r"(flags));
    CHECK(flags & 0x200);
    // Again once linked: then nothing but sti itself leaves the code to take it.
    for (int pass = 0; pass < 3; pass++)
        sti_with_a_vector_waiting();
}

static void test_ptest_flags(void) {
    u64 flags, set = 0xFFFFFFFFFFFFFFFFULL;
    __asm__ volatile("movq %1, %%xmm0\n movq %1, %%xmm1\n"
                     "mov $0x7f, %%al\n add $1, %%al\n" // sets SF, OF and AF
                     "ptest %%xmm0, %%xmm1\n pushfq\n popq %0\n"
                     : "=r"(flags)
                     : "r"(set)
                     : "rax", "xmm0", "xmm1", "cc");
    CHECK((flags & 0x8D5) == 0x01); // both all ones: CF only, of CF PF AF ZF SF OF
}

static u64 tsc(void) {
    u32 lo, hi;
    __asm__ volatile("lfence\n rdtsc" : "=a"(lo), "=d"(hi));
    return (u64)hi << 32 | lo;
}

static u64 copy_megabyte(void) {
    u64 count = 1 << 20, t0 = tsc();
    volatile u8 *s = (volatile u8 *)PHYS(0x20000000), *d = (volatile u8 *)PHYS(0x21000000);
    __asm__ volatile("cld\n rep movsb" : "+S"(s), "+D"(d), "+c"(count) : : "memory");
    return tsc() - t0;
}

// A kernel or a program that copies with TS set, as it is after a context switch, and nothing to clear it: no x86 SIMD
// ran, so no #NM. Costing a trap per byte made one lc-bench test look hung.
static void test_rep_speed_under_ts(void) {
    for (int i = 0; i < 1 << 20; i += 4096)
        *(volatile u8 *)((u64)PHYS(0x20000000) + (u64)i) = (u8)(i >> 12);
    copy_megabyte();
    u64 plain = copy_megabyte(), cr0, nm = nm_count;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    u64 under_ts = copy_megabyte();
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 & ~8ULL));
    CHECK(nm_count == nm);
    CHECK(*(volatile u8 *)((u64)PHYS(0x21000000) + 4096 * 200) == 200);
    if (under_ts >= plain * 200) {
        MAILBOX[1] = plain;
        MAILBOX[2] = under_ts;
        command(12);
    }
}

// A dead process: the kernel frees its tables and poisons them while its view still holds translated code. Nothing may
// walk those tables as though they meant something.
static void test_poisoned_tables(u64 root) {
    u64 va = TEST_VA + 0xE00000, code = frame(), other = frame();
    for (int i = 0; i < 512; i++)
        PHYS(other)[i] = PHYS(root)[i];
    *(volatile u8 *)PHYS(code) = 0xC3; // ret
    pte_of(root, va)[0] = code | PRESENT_RW;
    __asm__ volatile("mov %0, %%cr3" : : "r"(other));
    ((void (*)(void))va)();
    CHECK(*(volatile u8 *)va == 0xC3); // translated from, and mapped for data, in that view
    __asm__ volatile("mov %0, %%cr3" : : "r"(root));
    PHYS(other)[(va >> 39) & 0x1ff] = 0xDEADBEEFDEADBEEFULL;
}

// MMX registers live with the engine between fallbacks, and TCG needs them, with the tags as MMX leaves them, at each
// one.
static void test_mmx(void) {
    u64 value = 0x1122334455667788ULL, out = 0, after = 0;
    // Action 9 checks and swaps what TCG would see: mm3 holds value, MMX has entered; it hands back ~value.
    __asm__ volatile(
        "movq %1, %%mm3\n mov $9, %%r13\n mov $5, %%r14\n .byte 0x66, 0x0F, 0x38, 0x41, 0xC1\n movq %%mm3, %0\n"
        : "=r"(out)
        : "r"(value)
        : "r13", "r14", "xmm0", "memory");
    CHECK(out == ~value);
    __asm__ volatile("emms\n mov $10, %%r13\n mov $5, %%r14\n .byte 0x66, 0x0F, 0x38, 0x41, 0xC1\n"
                     :
                     :
                     : "r13", "r14", "xmm0", "memory");
    // TS: #NM for each form, including those that need no vector register to do their job.
    u64 nm = nm_count, cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    __asm__ volatile("movq %1, %%mm3\n movq %%mm3, %0\n" : "=r"(after) : "r"(value) : "memory");
    CHECK(nm_count == nm + 1);
    CHECK(after == value);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    __asm__ volatile("emms");
    CHECK(nm_count == nm + 2);
}

// A split store onto a tracked frame whose second page is missing: the fault names that page, and the store then
// finishes.
static void test_split_store_fault(u64 root) {
    u64 *slots = pte_of(root, TEST_VA + 0xC00000);
    u64 table = ((u64)slots - PHYSMAP) & ~0xFFFULL;
    slots[0] = frame() | PRESENT_RW;
    CHECK(*(volatile u64 *)(TEST_VA + 0xC00000) != 1); // walked, so the frame is tracked
    *lazy_ptes[0] = table | PRESENT_RW;
    *lazy_ptes[1] = 0;
    __asm__ volatile("invlpg (%0)\n invlpg (%1)" : : "r"(LAZY_VA), "r"(LAZY_VA + 4096));
    PHYS(lazy_frames[1])[0] = 0;
    gate(14, lazy_pf_stub);
    u64 count = lazy_pf_count, value = 0x1122334455667788ULL;
    *(volatile u8 *)LAZY_VA; // the first page is mapped, so only the second can refuse
    __asm__ volatile("movq %0, (%1)" : : "r"(value), "r"(LAZY_VA + 4096 - 3) : "memory");
    gate(14, bad_stub);
    CHECK(lazy_pf_count == count + 1);
    CHECK((PHYS(table)[511] >> 40) == (value & 0xFFFFFF));
    CHECK(PHYS(lazy_frames[1])[0] == value >> 24);
    PHYS(table)[511] = 0;
}

// 32-bit ring 3 with the host taking instructions: state as the host sees it, top halves the host left, and the host
// moving the vCPU to 64-bit mode at a stop. In by iretq, out by sysenter, to here.
__asm__(".text\n"
        "_compat_ride:\n" // rdi: rip, r13 and r14 are the harness's
        "  movq %rsp, _compat_rsp(%rip)\n"
        "  movw $0x23, %ax\n movw %ax, %ds\n movw %ax, %es\n"
        "  pushq $0x23\n pushq $0x40001000\n pushq $2\n pushq $0x43\n pushq %rdi\n"
        "  iretq\n"
        "_compat_back:\n"
        "  movq _compat_rsp(%rip), %rsp\n"
        "  movw $0x10, %dx\n movw %dx, %ds\n movw %dx, %es\n"
        "  movq %rax, _compat_out+0(%rip)\n movq %rcx, _compat_out+8(%rip)\n movq %rdi, _compat_out+16(%rip)\n"
        "  ret\n");
u64 compat_rsp, compat_out[3];
extern void compat_ride(u64 rip);
extern char compat_back[];

static void test_compat(u64 root) {
    static const u8 code[] = {
        0x8B, 0x05, 0x00, 0x10, 0x00, 0x40, // 1: mov eax, [0x40001000], until the second vCPU has done
        0x85, 0xC0, // test eax, eax
        0x74, 0xF6, // je 1b
        0xB8, 0x11, 0x22, 0x33, 0x44, // mov eax, 0x44332211
        0x27, // daa: the host checks 32-bit state, and sets eax and a top half in edx
        0x8D, 0x3A, // lea edi, [edx]
        0x2F, // das: the host sends the vCPU to 64-bit mode with a wide rax
        0x48, 0x89, 0xC1, // mov rcx, rax. In 32-bit mode this would be dec eax; mov ecx, eax
        0x0F, 0x34, // sysenter
    };
    u64 va = 0x40000000, f = frame();
    for (int level = 3; level > 0; level--) { // user at each level, as pte_of doesn't give
        u64 t = root;
        for (int l = 3; l > level; l--)
            t = PHYS(t)[(va >> (12 + 9 * l)) & 0x1ff] & 0x000FFFFFFFFFF000ULL;
        volatile u64 *e = &PHYS(t)[(va >> (12 + 9 * level)) & 0x1ff];
        if (!(*e & 1))
            *e = frame() | PRESENT_RW;
        *e |= 4;
    }
    *pte_of(root, va) = f | PRESENT_RW | 4;
    for (unsigned i = 0; i < sizeof code; i++)
        ((volatile u8 *)&PHYS(f)[0])[i] = code[i];
    u64 flag = frame();
    *pte_of(root, va + 0x1000) = flag | PRESENT_RW | 4;
    volatile u32 *done_flag = (volatile u32 *)PHYS(flag);
    *done_flag = !second_joined;
    if (second_joined) {
        second_code_frame = frame();
        second_code_done = done_flag;
        *pte_of(root, TEST_VA + 0x40800000) = second_code_frame | PRESENT_RW;
        second_code_va = TEST_VA + 0x40800000;
    }
    __asm__ volatile("wrmsr" : : "c"(0x174), "a"(0x08), "d"(0));
    __asm__ volatile("wrmsr" : : "c"(0x176), "a"((u32)(u64)compat_back), "d"((u32)((u64)compat_back >> 32)));
    __asm__ volatile("mov $11, %%r13\n mov $1, %%r14\n call _compat_ride"
                     :
                     : "D"(va)
                     : "rax", "rbx", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "r13", "r14", "memory", "cc");
    CHECK(compat_out[0] == 0x100000055ULL); // the host's, in 64-bit mode
    CHECK(compat_out[1] == 0x100000055ULL);
    CHECK(compat_out[2] == 5); // lea through a register whose top half the host left
}

// ---- x87: what Rosetta can't be compared on, by the SDM, and what TCG sees at a fallback ----
extern volatile u64 gp_count;
volatile u64 gp_count;
__asm__(".text\n"
        "_gp_skip5:\n" // #GP from the five-byte fxrstor64 below: count it and step over
        "  incq _gp_count(%rip)\n"
        "  addq $8, %rsp\n"
        "  addq $5, (%rsp)\n"
        "  iretq\n");
extern char gp_skip5[] __asm__("_gp_skip5");

static inline u16 x87_sw(void) {
    u16 v;
    __asm__ volatile("fnstsw %0" : "=a"(v));
    return v;
}

static void test_x87(void) {
    u8 *area = (u8 *)PHYS(frame());
    u16 sw;
    u64 lo;
    // Stack underflow, masked: the indefinite, IE and SF, C1 clear.
    __asm__ volatile("fninit\n fadd %%st(1), %%st\n" : : : "memory");
    sw = x87_sw();
    CHECK((sw & 0x7F) == 0x41 && !(sw & 0x200));
    __asm__ volatile("fnclex\n fxsave64 (%0)" : : "r"(area) : "memory");
    CHECK(*(u64 *)(area + 32) == 0xC000000000000000ULL && *(u16 *)(area + 40) == 0xFFFF);
    // A NaN in ST0 doesn't change that, where Rosetta runs the NaN rules against its empty register.
    __asm__ volatile("fninit\n fldt (%0)\n fadd %%st(3), %%st\n fxsave64 (%1)"
                     :
                     : "r"((const u8[10]){1, 0, 0, 0, 0, 0, 0, 0xC0, 0xFF, 0x7F}), "r"(area)
                     : "memory");
    CHECK(*(u64 *)(area + 32) == 0xC000000000000000ULL && *(u16 *)(area + 40) == 0xFFFF);
    // Stack overflow: the same with C1 set, TOP moved, and the register overwritten.
    __asm__ volatile("fninit\n fld1\n fld1\n fld1\n fld1\n fld1\n fld1\n fld1\n fld1\n fnclex\n fldpi\n"
                     :
                     :
                     : "memory");
    sw = x87_sw();
    CHECK((sw & 0x7F) == 0x41 && (sw & 0x200) && (sw >> 11 & 7) == 7);
    // fbld as well, which Rosetta lets through.
    __asm__ volatile("fnclex\n fbld (%0)\n" : : "r"((const u8[10]){0x12, 0x34}) : "memory");
    sw = x87_sw();
    CHECK((sw & 0x41) == 0x41 && (sw & 0x200));
    // Popping an empty register stores the indefinite. An integer store gives the integer indefinite.
    double d = 1;
    int i32 = 0;
    __asm__ volatile("fninit\n fstpl %0\n" : "=m"(d));
    CHECK(*(u64 *)&d == 0xFFF8000000000000ULL);
    __asm__ volatile("fninit\n fistpl %0\n" : "=m"(i32));
    CHECK((u32)i32 == 0x80000000);

    // Empty registers keep what they held: a CPU's image shows it, tagged empty.
    __asm__ volatile("fninit\n fldpi\n fstp %%st(0)\n fxsave64 (%0)" : : "r"(area) : "memory");
    CHECK(area[4] == 0 && *(u64 *)(area + 32 + 15 * 0) != 0xC000000000000000ULL);
    CHECK(*(u64 *)(area + 32 + 16 * 7) == 0xC90FDAA22168C235ULL && *(u16 *)(area + 40 + 16 * 7) == 0x4000);

    // Instruction and data pointers, and the opcode. r12 as the base, so the ModRM byte is known; a label gives the
    // rip.
    u64 at, cell = 0;
    __asm__ volatile("fninit\n lea 1f(%%rip), %0\n mov %1, %%r12\n 1: .byte 0x41, 0xDD, 0x04, 0x24\n" // fldl (%r12)
                     "fxsave64 (%2)\n"
                     : "=&r"(at)
                     : "r"(&cell), "r"(area)
                     : "r12", "memory");
    CHECK(*(u16 *)(area + 6) == 0x504 && *(u64 *)(area + 8) == at && *(u64 *)(area + 16) == (u64)&cell);
    // The small format and fnstenv's: reserved words all ones, opcode beside the selector.
    __asm__ volatile("fnstenv (%0)" : : "r"(area) : "memory");
    CHECK(*(u32 *)(area + 0) == 0xFFFF037F && *(u32 *)(area + 12) == (u32)at &&
          *(u32 *)(area + 16) == (0x504u << 16 | 8) && *(u32 *)(area + 20) == (u32)(u64)&cell);
    CHECK(*(u16 *)(area + 8) == 0x7FFF); // the one register in use, physical 7, holds zero: tag 01
    __asm__ volatile("fldcw (%0)\n" : : "r"((const u16[1]){0x37F}) : "memory");
    __asm__ volatile("fxsave (%0)" : : "r"(area) : "memory");
    CHECK(*(u32 *)(area + 8) == (u32)at && *(u16 *)(area + 12) == 8 && *(u32 *)(area + 16) == (u32)(u64)&cell);
    // A control instruction moves neither pointer.
    __asm__ volatile("fnstcw (%0)\n fnclex\n fxsave64 (%1)" : : "r"(&cell), "r"(area) : "memory");
    CHECK(*(u64 *)(area + 8) == at);
    // Data16 images are the short ones.
    __asm__ volatile(".byte 0x66, 0xD9, 0x30\n" : : "a"(area) : "memory"); // data16 fnstenv (%rax)
    CHECK(*(u16 *)(area + 0) == 0x37F && *(u16 *)(area + 2) >> 11 == 7);

    // fxsave leaves the end of the image alone, and fxrstor brings back everything it holds. ST order, not physical.
    for (int w = 52; w < 64; w++)
        ((u64 *)area)[w] = 0x5A5A5A5A5A5A5A5AULL;
    u64 xmm3[2] __attribute__((aligned(16))) = {0x1111222233334444ULL, 0x5555666677778888ULL},
                back[2] __attribute__((aligned(16)));
    __asm__ volatile("movaps (%1), %%xmm3\n fninit\n fld1\n fldpi\n fxsave64 (%0)\n fninit\n pxor %%xmm3, %%xmm3\n "
                     "fxrstor64 (%0)\n movaps %%xmm3, (%2)\n"
                     :
                     : "r"(area), "r"(xmm3), "r"(back)
                     : "memory", "xmm3");
    CHECK(back[0] == xmm3[0] && back[1] == xmm3[1]);
    CHECK(((u64 *)area)[52] == 0x5A5A5A5A5A5A5A5AULL && ((u64 *)area)[63] == 0x5A5A5A5A5A5A5A5AULL);
    CHECK(area[4] == 0xC0 && *(u64 *)(area + 32) == 0xC90FDAA22168C235ULL &&
          *(u64 *)(area + 48) == 0x8000000000000000ULL);
    __asm__ volatile("fstpl %0\n" : "=m"(d));
    __asm__ volatile("fstp %st(0)");
    CHECK(*(u64 *)&d == 0x400921FB54442D18ULL);
    // Reserved MXCSR bits: #GP, nothing restored.
    gate(13, gp_skip5);
    *(u32 *)(area + 24) = 0x10000 | 0x1F80;
    __asm__ volatile("fninit\n fldz\n mov %0, %%r12\n .byte 0x49, 0x0F, 0xAE, 0x0C, 0x24\n"
                     :
                     : "r"(area)
                     : "r12", "memory");
    gate(13, bad_stub);
    CHECK(gp_count == 1);
    sw = x87_sw();
    CHECK((sw >> 11 & 7) == 7);
    __asm__ volatile("fninit");

    // MMX and the stack: TOP 0, all valid, exponents all ones where written; emms empties the tags and leaves TOP.
    __asm__ volatile("fninit\n fld1\n fld1\n movq %0, %%mm2\n fxsave64 (%1)\n"
                     :
                     : "r"(lo = 0x0123456789ABCDEFULL), "r"(area)
                     : "memory");
    CHECK((*(u16 *)(area + 2) >> 11 & 7) == 0 && area[4] == 0xFF && *(u64 *)(area + 32 + 2 * 16) == lo &&
          *(u16 *)(area + 40 + 2 * 16) == 0xFFFF);
    __asm__ volatile("emms\n fxsave64 (%0)" : : "r"(area) : "memory");
    CHECK(area[4] == 0 && (*(u16 *)(area + 2) >> 11 & 7) == 0);
    // ...and x87 can read what MMX left: a NaN whose significand is the MMX value, by physical number.
    __asm__ volatile("fninit\n movq %0, %%mm7\n emms\n fld %%st(7)\n fxsave64 (%1)\n"
                     :
                     : "r"(lo), "r"(area)
                     : "memory");
    CHECK(*(u16 *)(area + 2) >> 11 == 7);

    // A fault on a store changes nothing: the page faults in and the instruction runs once. TOP moved once, the value
    // is whole.
    u64 before = lazy_pf_count;
    *lazy_ptes[2] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(LAZY_VA + (2ULL << 12)));
    gate(14, lazy_pf_stub);
    u8 *split = (u8 *)LAZY_VA + (2ULL << 12) - 4; // an 80-bit store whose tail is on the missing page
    *(volatile u64 *)(split - 8) = 0;
    __asm__ volatile("fninit\n fld1\n fldpi\n fstpt (%0)\n" : : "r"(split) : "memory");
    CHECK(lazy_pf_count == before + 1);
    sw = x87_sw();
    CHECK((sw >> 11 & 7) == 7);
    CHECK(*(u64 *)split == 0xC90FDAA22168C235ULL && *(u16 *)(split + 8) == 0x4000);
    // And a load: nothing pushed until it arrives.
    *lazy_ptes[3] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(LAZY_VA + (3ULL << 12)));
    before = lazy_pf_count;
    __asm__ volatile("fninit\n fldt (%0)\n" : : "r"((u8 *)LAZY_VA + (3ULL << 12)) : "memory");
    sw = x87_sw();
    CHECK(lazy_pf_count == before + 1 && (sw >> 11 & 7) == 7);
    // A store to the page that's missing, under TS: #NM comes first and clears TS, then the fault.
    __asm__ volatile("fninit\n fld1");
    *lazy_ptes[4] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(LAZY_VA + (4ULL << 12)));
    u64 nm = nm_count, cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    before = lazy_pf_count;
    __asm__ volatile("fstpl (%0)\n" : : "r"((u8 *)LAZY_VA + (4ULL << 12)) : "memory");
    CHECK(nm_count >= nm + 1 && lazy_pf_count == before + 1);
    // Register forms, control forms and fwait under TS.
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    nm = nm_count;
    __asm__ volatile("fninit");
    CHECK(nm_count == nm + 1);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    __asm__ volatile("fnstsw %%ax" : : : "ax");
    CHECK(nm_count == nm + 2);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 8));
    __asm__ volatile("fxsave64 (%0)" : : "r"(area) : "memory");
    CHECK(nm_count == nm + 3);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 0x8 | 0x2)); // MP and TS: fwait traps too
    __asm__ volatile("fwait");
    CHECK(nm_count == nm + 4);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    gate(14, bad_stub);

    // At a fallback TCG gets all of it. Action 13 checks the stack as this leaves it, and writes ST0 and the status
    // word for the way back.
    __asm__ volatile("fninit\n fldcw (%0)\n fld1\n fldpi\n" : : "r"((const u16[1]){0xB7F}) : "memory");
    __asm__ volatile("mov $13, %%r13\n mov $2, %%r14\n .byte 0xD9, 0xFF\n fxsave64 (%0)"
                     :
                     : "r"(area)
                     : "r13", "r14", "memory");
    CHECK(*(u64 *)(area + 32) == 0xFEDCBA9876543210ULL && *(u16 *)(area + 40) == 0x4005 &&
          (*(u16 *)(area + 2) & 0x7F) == 0x20);
    // An unmasked exception takes the operation to TCG (action 14 checks that nothing moved). Flags now unmasked are
    // pending, and fwait goes too.
    __asm__ volatile("fninit\n fld1\n fld1\n fldcw (%0)\n" : : "r"((const u16[1]){0x37E}) : "memory");
    __asm__ volatile("mov $14, %%r13\n mov $2, %%r14\n faddp\n" : : : "r13", "r14");
    __asm__ volatile("fninit\n fld1\n fldz\n fdivrp\n fldcw (%0)\n" : : "r"((const u16[1]){0x37B}) : "memory");
    sw = x87_sw();
    CHECK((sw & 0x80) && (sw & 4));
    __asm__ volatile("mov $15, %%r13\n mov $1, %%r14\n fwait\n" : : : "r13", "r14");
    __asm__ volatile("fninit");
}

// x87 operands go through translated loads and stores, so devices and tracked frames work as for any move. fxsave,
// written from the monitor, goes to TCG for a tracked frame.
static void test_x87_memory(u64 root) {
    u64 got = 0;
    __asm__ volatile("fninit\n fldl (%0)\n fstpl (%1)" : : "r"(PHYS(MARKED)), "r"(&got) : "memory");
    CHECK(got == MARKED * 3);
    u64 writes = 0x1234;
    __asm__ volatile("fildq (%1)\n fistpq (%0)" : : "r"(PHYS(MARKED)), "r"(&writes) : "memory");
    CHECK(MAILBOX[0] == 0x1234);
    __asm__ volatile("fldpi\n fstpt (%0)" : : "r"(PHYS(MARKED)) : "memory");
    CHECK(MAILBOX[0] == 0xC90FDAA22168C235ULL);

    u64 va = TEST_VA + 0x800000;
    u64 *slots = pte_of(root, va);
    u64 table = ((u64)slots - PHYSMAP) & ~0xFFFULL;
    slots[0] = frame() | PRESENT_RW;
    slots[1] = table | PRESENT_RW;
    volatile u8 *high = (volatile u8 *)va + 4096;
    CHECK(high[9] == (u8)(table >> 8));
    u64 target = frame();
    PHYS(target)[0] = 0x77;
    u64 entry = target | PRESENT_RW;
    __asm__ volatile("fildq (%1)\n fistpq (%0)" : : "r"(high + 64), "r"(&entry) : "memory");
    CHECK(*(volatile u64 *)(va + (8ULL << 12)) == 0x77);
    u64 rax = 0;
    __asm__ volatile("mov %2, %%r12\n mov $5, %%r13\n mov $5, %%r14\n .byte 0x49, 0x0F, 0xAE, 0x04, 0x24\n"
                     : "=a"(rax)
                     : "a"(rax), "r"(high + 1024)
                     : "r12", "r13", "r14", "memory");
    CHECK(rax == 7);
    slots[1] = 0;
    slots[8] = 0;
    __asm__ volatile("mov %%cr3, %%rax\n mov %%rax, %%cr3" : : : "rax");
}

void kmain(void) {
    __asm__ volatile("mov $0x10, %%ax\n mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%ss\n" : : : "ax");
    tables();
    u64 root = read_cr3();
    test_ram_map(root);
    test_fallback_state();
    test_host_changes_cr3(root);
    test_notes(root);
    test_fbt_patch(root);
    test_ts();
    test_level_interrupts();
    test_tsc();
    test_busy();
    test_cr4();
    test_vmport();
    test_port_forms();
    test_store_into_table(root);
    test_rep_into_table(root);
    test_lazy_fp(root);
    test_split_store_fault(root);
    test_mmx();
    test_x87();
    test_x87_memory(root);
    test_stale_table(root);
    test_poisoned_tables(root);
    test_rep_speed_under_ts();
    test_ptest_flags();
    test_inline_ops(root);
    test_direct_map();
    test_tsc_deadline();
    test_reciprocals();
    test_x87_across_a_switch();
    test_kernel_domain(root);
    test_many_blocks(root);
    test_linked_loop();
    test_hlt_poll();
    test_reuse(root);
    test_calls_link();
    test_icr();
    test_forget_all(root);
    test_moffs();
    test_pcmpistri();
    test_crc32();
    test_locked_pte(root);
    test_second_vcpu(root);
    test_process_log(root);
    test_shared_region(root);
    test_call_between_keys(root);
    test_shared_return(root);
    test_view_churn(root);
    test_compat(root);
    CHECK(!bad_vector);
    puts_("hybrid checks passed\n");
    done(0);
}
