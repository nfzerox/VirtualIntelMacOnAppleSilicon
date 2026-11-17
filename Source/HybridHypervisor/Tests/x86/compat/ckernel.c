// A freestanding 64-bit kernel that runs 32-bit ring-3 code as XNU does: user CS, DS and the cthread GS in an LDT, in, sysenter and sysexit, iret into compatibility mode.
// The same image runs under the engine and under QEMU's TCG (Tests/diff-compat.sh): its checks hold both to what this file says, and the transcripts are compared. "measure"
// lines and the timer test are the engine's alone. Output goes to port 0xE9, exit to port 0xF4.
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define PHYSMAP 0xFFFF880000000000ULL
#define P 1ULL
#define W 2ULL
#define U 4ULL
#define ADDR 0x000FFFFFFFFFF000ULL

// As user32.s.
#define UCODE 0x100000ULL
#define USTACK_TOP 0xBFFF0000ULL
#define MAILBOX 0x0F100000ULL
#define GS_AREA 0x0F000000ULL
#define RO_PAGE 0x0F200000ULL
#define UNMAPPED 0x0F300000ULL
#define PAGE_LAST 0x0F500000ULL
#define DEMAND 0x20000000ULL
#define SYSENTER_CS 0x07
#define USER_CS 0x17
#define USER_DS 0x1f
#define USER_CTHREAD 0x37
#define KERNEL_LDT 0x40

enum {
    T_BASIC, T_WRAP, T_INT80, T_SYSENTER, T_SYSENTER_LOOP, T_SYSCALL,
    T_PF_READ, T_PF_WRITE, T_PF_PUSH, T_PF_CALL, T_PF_RET, T_PF_PUSHA, T_PF_POPA, T_PF_REP,
    T_GP_CLI, T_GP_CR, T_GP_KERNEL_DS, T_NP_DS, T_SS_NP, T_GP_RO_SS, T_GP_NULL_SS, T_GP_FAR, T_GP_GATE,
    T_UD, T_INT3, T_DE, T_UD_VEX,
    T_FAR, T_SEGMENTS, T_SMC, T_TOUCH, T_REP_RESUME, T_SPIN, T64_ENTRY,
    M_FUZZ = 100, M_I386 = 101,
};

static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline void outl(u16 port, u32 v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port)); }
static inline u32 inl(u16 port) {
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline u64 rdtsc(void) {
    u32 a, d;
    __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
    return (u64)d << 32 | a;
}
static inline void wrmsr(u32 m, u64 v) { __asm__ volatile("wrmsr" : : "c"(m), "a"((u32)v), "d"((u32)(v >> 32))); }
static inline u64 rdmsr(u32 m) {
    u32 a, d;
    __asm__ volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(m));
    return (u64)d << 32 | a;
}
static inline u64 read_cr2(void) {
    u64 v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}
static void put(char c) { outb(0xE9, (u8)c); }
static void puts_(const char *s) {
    while (*s)
        put(*s++);
}
static void putx(u64 v) {
    char b[24];
    int n = 0;
    do {
        b[n++] = "0123456789abcdef"[v % 16];
        v /= 16;
    } while (v);
    while (n)
        put(b[--n]);
}
static void kprintf(const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    for (; *f; f++) {
        if (*f != '%') {
            put(*f);
            continue;
        }
        f++;
        if (*f == 'x')
            putx(va_arg(ap, u64));
        else if (*f == 's')
            puts_(va_arg(ap, const char *));
    }
    va_end(ap);
}
static __attribute__((noreturn)) void kexit(u32 code) {
    for (;;)
        outl(0xF4, code);
}
static u32 fails;
#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            kprintf("FAIL %s:%x " #cond " ", __func__, (u64)__LINE__);                                                 \
            kprintf(__VA_ARGS__);                                                                                      \
            kprintf("\n");                                                                                             \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

void *memset(void *d, int v, unsigned long n) {
    u8 *p = d;
    while (n--)
        *p++ = (u8)v;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n) {
    u8 *p = d;
    const u8 *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}

// ---- Frames and page tables ----
static u64 next_frame, ram_size;
static inline u64 *phys(u64 pa) { return (u64 *)(PHYSMAP + pa); }
static u64 frame_alloc(void) {
    u64 f = next_frame;
    next_frame += 4096;
    if (next_frame > ram_size) {
        kprintf("out of frames\n");
        kexit(90);
    }
    memset(phys(f), 0, 4096);
    return f;
}
static u64 *pte_slot(u64 root, u64 va, bool create) {
    u64 table = root;
    for (int level = 3; level > 0; level--) {
        u64 *e = phys(table) + ((va >> (12 + 9 * level)) & 0x1ff);
        if (!(*e & P)) {
            if (!create)
                return 0;
            *e = frame_alloc() | P | W | U;
        }
        table = *e & ADDR;
    }
    return phys(table) + ((va >> 12) & 0x1ff);
}
static void map4k(u64 root, u64 va, u64 pa, u64 flags) { *pte_slot(root, va, true) = pa | flags; }

// ---- Descriptor tables ----
static u64 gdt[13] __attribute__((aligned(16)));
static u64 ldt[10] __attribute__((aligned(16)));
static struct {
    u32 r0;
    u64 rsp[3];
    u64 r1;
    u64 ist[7];
    u64 r2;
    u16 r3, iomap;
} __attribute__((packed)) tss;
typedef struct {
    u16 off0, sel;
    u8 ist, attr;
    u16 off1;
    u32 off2, r;
} __attribute__((packed)) Gate;
static Gate idt[256] __attribute__((aligned(16)));
static u8 kstack[32768] __attribute__((aligned(16)));
static u8 ist_stack[8192] __attribute__((aligned(16)));

extern char isr0[], isr3[], isr6[], isr8[], isr11[], isr12[], isr13[], isr14[], isr32[], isr128[], sysenter_entry[], user_blob[], user_blob_end[], sysenter_stack_top[];
extern void enter_compat(u64 rip, u64 esp, u64 eax, u64 ebx, u64 edx, u64 ecx);
extern void enter_user64(u64 rip, u64 rsp, u64 rax);
extern void enter_kernel32(u64 rip,u64 rsp);
extern char kernel32_blob[],kernel32_blob_end[];
extern void enter_sysret(u64 rip, u64 esp, u64 eax, u64 ebx);
extern __attribute__((noreturn)) void return_from_user(void);

static void set_gate(int v, void *h, int dpl, int ist) {
    u64 a = (u64)h;
    idt[v] = (Gate){(u16)a, 8, (u8)ist, (u8)(0x8E | dpl << 5), (u16)(a >> 16), (u32)(a >> 32), 0};
}
static u64 descriptor(u64 base, u64 limit, u64 flags) { // flags: access in bits 40..47 and size in 52..55
    return (limit & 0xffff) | (base & 0xffffff) << 16 | (limit >> 16 & 0xf) << 48 | (base >> 24 & 0xff) << 56 | flags;
}

static void setup_tables(void) {
    gdt[1] = 0x00AF9A000000FFFFULL; // 0x08 kernel code
    gdt[2] = 0x00CF92000000FFFFULL; // 0x10 kernel data
    gdt[3] = 0x00CFFA000000FFFFULL; // 0x18 user code, 32-bit: sysretl
    gdt[4] = 0x00CFF2000000FFFFULL; // 0x20 user data
    gdt[5] = 0x00AFFA000000FFFFULL; // 0x28 user code, 64-bit
    u64 t = (u64)&tss;
    gdt[6] = ((t & 0xffff) << 16) | ((t >> 16 & 0xff) << 32) | (0x89ULL << 40) | ((t >> 24 & 0xff) << 56) | (sizeof tss - 1);
    gdt[7] = t >> 32;
    u64 l = (u64)ldt;
    gdt[8] = ((l & 0xffff) << 16) | ((l >> 16 & 0xff) << 32) | (0x82ULL << 40) | ((l >> 24 & 0xff) << 56) | (sizeof ldt - 1);
    gdt[9] = l >> 32;
    gdt[10]=0x00CF9A000000FFFFULL; // 0x50 supervisor code,32-bit under IA-32e
    gdt[11]=descriptor(0x720000,0x5ff,0x92ULL<<40|0x4ULL<<52); // 0x58 per-CPU GS
    gdt[12]=descriptor(0x730000,0x7ff,0xF2ULL<<40|0xCULL<<52); // 0x60 based user-copy ES
    // As XNU lays out its LDT: the sysexit arithmetic reaches 0x17 and 0x1f from 0x07.
    ldt[0] = 0x00AF9A000000FFFFULL; // 0x07 sysenter code
    ldt[1] = 0x00CF92000000FFFFULL; // 0x0f sysenter data
    ldt[2] = 0x00CFFA000000FFFFULL; // 0x17 user code
    ldt[3] = 0x00CFF2000000FFFFULL; // 0x1f user data
    ldt[4] = 0x00AFFA000000FFFFULL; // 0x27 user code, 64-bit
    ldt[5] = 0x00CFF0000000FFFFULL; // 0x2f user data, read-only
    ldt[6] = descriptor(GS_AREA, 1, 0xF2ULL << 40 | 0xCULL << 52); // 0x37 cthread: 8 KB
    ldt[7] = 0x00CF92000000FFFFULL; // 0x3f kernel data
    ldt[8] = 0x00CF72000000FFFFULL; // 0x47 user data, not present
    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed)) d = {sizeof gdt - 1, (u64)gdt};
    __asm__ volatile("lgdt %0" : : "m"(d));
    __asm__ volatile("mov %0, %%ds\n mov %0, %%es\n mov %0, %%ss" : : "r"(0x10));
    __asm__ volatile("lldt %%ax" : : "a"(KERNEL_LDT));
    tss.rsp[0] = (u64)kstack + sizeof kstack;
    tss.ist[0] = (u64)ist_stack + sizeof ist_stack;
    __asm__ volatile("ltr %%ax" : : "a"(0x30));
    set_gate(0, isr0, 0, 0);
    set_gate(3, isr3, 3, 0);
    set_gate(6, isr6, 0, 0);
    set_gate(8, isr8, 0, 1);
    set_gate(11, isr11, 0, 0);
    set_gate(12, isr12, 0, 0);
    set_gate(13, isr13, 0, 0);
    set_gate(14, isr14, 0, 0);
    set_gate(32, isr32, 0, 0);
    set_gate(128, isr128, 3, 0);
    d.limit = sizeof idt - 1;
    d.base = (u64)idt;
    __asm__ volatile("lidt %0" : : "m"(d));
    wrmsr(0xC0000081, (u64)0x18 << 48 | (u64)0x08 << 32);
    wrmsr(0xC0000084, 0x700); // mask TF, IF, DF on entry
    wrmsr(0xC0000080, rdmsr(0xC0000080) | 1);
    wrmsr(0x174, SYSENTER_CS);
    wrmsr(0x175, (u64)sysenter_stack_top);
    wrmsr(0x176, (u64)sysenter_entry);
}

// ---- Traps ----
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rdx, rcx, rbx, rax, vector, err, rip, cs, rflags, rsp, ss;
} Frame;

static volatile u64 ticks;
static Frame last;
static u64 last_cr2, user_status;
static enum { M_EXIT_ON_FAULT, M_DEMAND } fault_mode;
static u64 current_root, demand_lo, demand_hi;
static u32 demand_faults, sys_count;

u64 sys(u64 n, u64 a, u64 b, u64 c) {
    sys_count++;
    switch ((u32)n) {
        case 1: // exit
        case 0x2000001:
            user_status = (u32)a;
            return_from_user();
        case 3:
            return (u32)a + 1;
        case 0x2000004: { // write, through the user's pages
            const u8 *p = (const u8 *)(u64)(u32)b;
            for (u64 i = 0; i < (u32)c; i++)
                put(p[i]);
            return (u32)c;
        }
    }
    return ~0ULL;
}

void trap_dispatch(Frame *f) {
    if (f->vector == 32) {
        ticks++;
        return;
    }
    if (f->vector == 128) {
        f->rax = sys(f->rax, f->rbx, f->rcx, f->rdx);
        return;
    }
    if (f->vector == 14 && fault_mode == M_DEMAND && read_cr2() >= demand_lo && read_cr2() < demand_hi) {
        *pte_slot(current_root, read_cr2(), true) = frame_alloc() | P | W | U;
        demand_faults++;
        return;
    }
    last = *f;
    last_cr2 = f->vector == 14 ? read_cr2() : 0;
    if (f->cs & 3) {
        user_status = 0x100 + f->vector;
        return_from_user();
    }
    kprintf("kernel fault vector %x err %x rip %x cr2 %x\n", f->vector, f->err, f->rip, read_cr2());
    kexit(91);
}

// ---- Address spaces ----
static u64 kernel_pml4[512];
static u64 new_space(void) {
    u64 root = frame_alloc();
    for (int i = 256; i < 512; i++)
        phys(root)[i] = kernel_pml4[i];
    u64 size = (u64)(user_blob_end - user_blob);
    for (u64 o = 0; o < size; o += 4096) {
        u64 f = frame_alloc();
        memcpy(phys(f), user_blob + o, 4096);
        map4k(root, UCODE + o, f, P | W | U);
    }
    for (u64 o = 1; o <= 64; o++)
        map4k(root, USTACK_TOP - o * 4096, frame_alloc(), P | W | U);
    map4k(root, MAILBOX, frame_alloc(), P | W | U);
    map4k(root, GS_AREA, frame_alloc(), P | W | U);
    u64 ro = frame_alloc();
    *phys(ro) = 0x77;
    map4k(root, RO_PAGE, ro, P | U);
    map4k(root, PAGE_LAST, frame_alloc(), P | W | U);
    map4k(root, 0, frame_alloc(), P | W | U);
    map4k(root, 0x1000, frame_alloc(), P | W | U);
    map4k(root, 0xFFFFF000, frame_alloc(), P | W | U);
    return root;
}
static void switch_to(u64 root) {
    current_root = root;
    __asm__ volatile("mov %0, %%cr3" : : "r"(root));
}

static u64 run32(int test, u64 ebx) {
    enter_compat(UCODE, USTACK_TOP - 64, test, ebx, 0, 0);
    return user_status;
}

// ---- Tests ----
static void test_basics(void) {
    u64 r = run32(T_BASIC, 0);
    CHECK(r == 0, "status %x", r);
    r = run32(T_WRAP, 0);
    CHECK(r == 0, "wrap status %x", r);
    kprintf("PASS 32-bit forms and wraps\n");
}

// The engine's port for the blocks it has translated, which QEMU's machine answers with nothing useful.
static bool on_engine(void) {
    // Avoid a dword read atF3 straddling QEMU's isa-debug-exit portF4.
    u32 v = inl(0x510);
    return v && v != ~0u;
}

static void expect(const char *name, int test, u64 vector, u64 err);

static void test_entries(void) {
    u64 r = run32(T_INT80, 0);
    CHECK(r == 0, "int status %x", r);
    r = run32(T_SYSENTER, 0);
    CHECK(r == 0, "sysenter status %x", r);
    CHECK(rdmsr(0x174) == SYSENTER_CS && rdmsr(0x176) == (u64)sysenter_entry, "sysenter MSRs");
    enter_sysret(UCODE, USTACK_TOP - 64, T_INT80, 0);
    CHECK(user_status == 0, "in by sysretl: status %x", user_status);
    expect("#UD syscall", T_SYSCALL, 6, 0);
    u64 n = 2000000, t = rdtsc();
    r = run32(T_SYSENTER_LOOP, n);
    CHECK(r == 0, "sysenter loop status %x", r);
    if (on_engine())
        kprintf("measure sysenter + sysexit round trip: %x ns (hex)\n", (rdtsc() - t) / n);
    kprintf("PASS int, sysenter, sysexit, syscall, sysret\n");
}

static u32 mailbox(void) { return *(volatile u32 *)MAILBOX; }

static void expect(const char *name, int test, u64 vector, u64 err) {
    u64 r = run32(test, 0);
    CHECK(r == 0x100 + vector, "%s status %x", name, r);
    CHECK(last.err == err, "%s err %x", name, last.err);
    CHECK(last.rip == mailbox(), "%s rip %x, not %x", name, last.rip, (u64)mailbox());
    CHECK(last.cs == USER_CS && last.ss == USER_DS, "%s cs %x ss %x", name, last.cs, last.ss);
    CHECK((last.rflags & 0x202) == 0x202, "%s flags %x", name, last.rflags);
}

static void test_faults(void) {
    expect("#PF read", T_PF_READ, 14, 4);
    CHECK(last_cr2 == UNMAPPED, "cr2 %x", last_cr2);
    CHECK((last.rflags & 0x8D5) == (0x800 | 0x80 | 0x10 | 0x04), "flags before a load that faults: %x", last.rflags);
    CHECK(last.rsi == UNMAPPED, "esi %x", last.rsi);
    expect("#PF write", T_PF_WRITE, 14, 7);
    CHECK(last_cr2 == RO_PAGE && *(volatile u32 *)RO_PAGE == 0x77, "cr2 %x", last_cr2);
    expect("#PF push", T_PF_PUSH, 14, 6);
    CHECK(last_cr2 == UNMAPPED && last.rsp == UNMAPPED + 4, "cr2 %x esp %x", last_cr2, last.rsp);
    expect("#PF call", T_PF_CALL, 14, 6);
    CHECK(last_cr2 == UNMAPPED && last.rsp == UNMAPPED + 4, "cr2 %x esp %x", last_cr2, last.rsp);
    expect("#PF ret", T_PF_RET, 14, 4);
    CHECK(last_cr2 == UNMAPPED && last.rsp == UNMAPPED, "cr2 %x esp %x", last_cr2, last.rsp);
    // TCG stores pushad from the lowest address and a CPU from the highest, so CR2 differs: only the page and the registers are claimed.
    expect("#PF pushad", T_PF_PUSHA, 14, 6);
    CHECK(last.rsp == UNMAPPED + 8, "esp %x", last.rsp);
    expect("#PF popad", T_PF_POPA, 14, 4);
    CHECK((last_cr2 & ~0xfffULL) == PAGE_LAST + 4096, "cr2 %x", last_cr2);
    // TCG has changed registers by the time the load faults. A real pop sequence restarts, and the engine's emitter holds this for itself.
    CHECK(!on_engine() || last.rax == 0x11111111 && last.rcx == 0x22222222 && last.rdx == 0x33333333 && last.rbx == 0x44444444 && last.rbp == 0x55555555 &&
              last.rsi == 0x66666666 && last.rdi == 0x77777777 && last.rsp == PAGE_LAST + 4096 - 16,
          "registers changed before a fault");
    // The engine's bulk copy restarts a 32-byte chunk and a CPU stops at the element: the relation of the three registers is what holds in both.
    expect("#PF rep", T_PF_REP, 14, 6);
    CHECK(last.rdi + last.rcx * 4 == PAGE_LAST + 4096 - 16 + 32 && last.rsi - last.rdi == MAILBOX - (PAGE_LAST + 4096 - 16), "rep edi %x ecx %x esi %x", last.rdi, last.rcx, last.rsi);
    expect("#GP cli", T_GP_CLI, 13, 0);
    expect("#GP mov cr", T_GP_CR, 13, 0);
    expect("#GP kernel data", T_GP_KERNEL_DS, 13, 0x3c);
    expect("#NP", T_NP_DS, 11, 0x44);
    expect("#SS", T_SS_NP, 12, 0x44);
    expect("#GP read-only ss", T_GP_RO_SS, 13, 0x2c);
    expect("#GP null ss", T_GP_NULL_SS, 13, 0);
    expect("#GP far to data", T_GP_FAR, 13, 0x2c);
    // The SDM's error code is the vector times 8, plus 2. TCG's do_interrupt64 multiplies by 16, so only the vector is compared.
    u64 r = run32(T_GP_GATE, 0);
    CHECK(r == 0x10D && (last.err & 3) == 2, "absent gate status %x err %x", r, last.err);
    expect("#UD", T_UD, 6, 0);
    r = run32(T_INT3, 0);
    CHECK(r == 0x103 && last.rip == mailbox(), "int3 status %x rip %x", r, last.rip);
    expect("#DE", T_DE, 0, 0);
    expect("#UD VEX", T_UD_VEX, 6, 0);
    kprintf("PASS #PF, #GP, #NP, #SS and others from 32-bit code\n");
}

static void test_far(void) {
    u64 r = run32(T_FAR, 0);
    CHECK(r == 0, "far status %x", r);
    // iret to 64-bit ring 3, whose lretq enters compatibility mode, keeping the low half of what 64-bit code left.
    enter_user64(*(volatile u32 *)(UCODE + 8), USTACK_TOP - 64, 0);
    CHECK(user_status == 0x123, "from 64-bit ring 3: status %x", user_status);
    kprintf("PASS far transfers\n");
}

static void test_segments(void) {
    volatile u32 *gs = (volatile u32 *)GS_AREA;
    gs[0] = 0xC0DE0000;
    gs[1] = 0xC0DE0001;
    gs[2] = 0xC0DE0002;
    u64 r = run32(T_SEGMENTS, 0);
    CHECK(r == 0, "segments status %x", r);
    // A new thread's descriptor under the same selector takes effect at the kernel's next load, as XNU's context switch relies on.
    u64 frame = frame_alloc();
    map4k(current_root, GS_AREA + 0x100000, frame, P | W | U);
    ldt[6] = descriptor(GS_AREA + 0x100000, 1, 0xF2ULL << 40 | 0xCULL << 52);
    *(volatile u32 *)(GS_AREA + 0x100000) = 0xC0DE0000;
    *(volatile u32 *)(GS_AREA + 0x100004) = 0xC0DE0001;
    *(volatile u32 *)(GS_AREA + 0x100008) = 0xC0DE0002;
    r = run32(T_SEGMENTS, 0);
    CHECK(r == 0, "segments on a second base: status %x", r);
    CHECK(*(volatile u32 *)(GS_AREA + 8) == 0xC0DE0004 && *(volatile u32 *)(GS_AREA + 0x100008) == 0xC0DE0004, "gs writes went astray: %x %x", (u64)*(volatile u32 *)(GS_AREA + 8), (u64)*(volatile u32 *)(GS_AREA + 0x100008));
    ldt[6] = descriptor(GS_AREA, 1, 0xF2ULL << 40 | 0xCULL << 52);
    kprintf("PASS segments, LDT and cthread base\n");
}

static void test_memory(u64 home) {
    u64 r = run32(T_SMC, 0);
    CHECK(r == 0, "smc status %x", r);
    switch_to(new_space()); // the rep copy below compares with the blob as built, which the test above patched
    fault_mode = M_DEMAND;
    demand_lo = DEMAND;
    demand_hi = DEMAND + (16ULL << 20);
    demand_faults = 0;
    r = 0;
    enter_compat(UCODE, USTACK_TOP - 64, T_TOUCH, DEMAND, 0, 2000);
    CHECK(user_status == 0 && demand_faults == 2000, "demand status %x faults %x", user_status, (u64)demand_faults);
    // A long rep movsb, starting off-alignment, faulting at each page, resuming with exact registers.
    u64 dst = DEMAND + (8ULL << 20) + 0x123;
    demand_faults = 0;
    enter_compat(UCODE, USTACK_TOP - 64, T_REP_RESUME, dst, 0, 200000);
    CHECK(user_status == 0 && demand_faults >= 200000 / 4096, "rep resume status %x faults %x", user_status, (u64)demand_faults);
    u64 bad = 0;
    for (u64 i = 0; i < 200000; i++)
        if (((volatile u8 *)dst)[i] != ((u8 *)user_blob)[i])
            bad++;
    CHECK(bad == 0, "rep copied %x wrong bytes", bad);
    fault_mode = M_EXIT_ON_FAULT;
    switch_to(home);
    kprintf("PASS code that changes, demand paging, rep across faults\n");
}

// The same VA holding different code in two address spaces, each 32-bit, and a third with 64-bit code at it.
static void test_spaces(u64 a) {
    u64 b = new_space();
    // mov $imm32, %ebx; mov $1, %eax; int $0x80. In 64-bit mode the same bytes do the same.
    static const u8 tmpl[] = {0xBB, 0, 0, 0, 0, 0xB8, 1, 0, 0, 0, 0xCD, 0x80};
    u64 ca = frame_alloc(), cb = frame_alloc();
    memcpy(phys(ca), tmpl, sizeof tmpl);
    memcpy(phys(cb), tmpl, sizeof tmpl);
    *(u32 *)((u8 *)phys(ca) + 1) = 7001;
    *(u32 *)((u8 *)phys(cb) + 1) = 7002;
    u64 va = 0x600000;
    map4k(a, va, ca, P | U);
    map4k(b, va, cb, P | U);
    for (int round = 0; round < 3; round++) {
        switch_to(a);
        enter_compat(va, USTACK_TOP - 64, 0, 0, 0, 0);
        CHECK(user_status == 7001, "space a %x", user_status);
        switch_to(b);
        enter_compat(va, USTACK_TOP - 64, 0, 0, 0, 0);
        CHECK(user_status == 7002, "space b %x", user_status);
    }
    switch_to(a);
    // The same frame and rip run by 64-bit ring 3 and 32-bit ring 3, turn about: one view each.
    for (int round = 0; round < 3; round++) {
        enter_user64(va, USTACK_TOP - 64, 0);
        CHECK(user_status == 7001, "64-bit at the same rip %x", user_status);
        enter_compat(va, USTACK_TOP - 64, 0, 0, 0, 0);
        CHECK(user_status == 7001, "32-bit at the same rip %x", user_status);
    }
    kprintf("PASS address spaces and modes at one rip\n");
}

// Leopard-style: two private directories reference one actual 2MB user page
// table. Exercise sharing, mode separation, parent/leaf remap and permissions.
static u64 *directory_slot(u64 root,u64 va) {
    for(int level=3;level>1;level--)
        root=phys(root)[(va>>(12+9*level))&511]&ADDR;
    return phys(root)+((va>>21)&511);
}
static void check_shared32(u64 root,u64 va,u64 expected) {
    switch_to(root);
    enter_compat(va,USTACK_TOP-64,0,0,0,0);
    CHECK(user_status==expected,"shared32 status %x wanted %x",user_status,expected);
}
static void test_shared32_tables(u64 home) {
    u64 a=new_space(),b=new_space(),va=0x98000000,alias=0x9a000000;
    u64 ca=frame_alloc(),cb=frame_alloc();
    // 0x43 is INC EBX in 32-bit code, a REX prefix on NOP in 64-bit code.
    const u8 code[]={0xBB,0,0,0,0,0x43,0x90,0xB8,1,0,0,0,0xCD,0x80};
    memcpy(phys(ca),code,sizeof code);memcpy(phys(cb),code,sizeof code);
    *(u32 *)((u8 *)phys(ca)+1)=7001;*(u32 *)((u8 *)phys(cb)+1)=7101;
    map4k(a,va,ca,P|U);map4k(b,va,cb,P|U);map4k(a,alias,cb,P|U);
    u64 *pa=directory_slot(a,va),*pb=directory_slot(b,va),private_b=*pb;
    *pb=*pa;*directory_slot(a,alias)=*pa;
    for(int i=0;i<4;i++){check_shared32(a,va,7002);check_shared32(b,va,7002);}
    enter_user64(va,USTACK_TOP-64,0);
    CHECK(user_status==7001,"shared table confused code modes %x",user_status);
    check_shared32(a,alias,7002);
    *pb=private_b;
    check_shared32(b,va,7102);check_shared32(a,va,7002);
    *pb=*pa;
    check_shared32(b,va,7002);
    *pte_slot(a,va,false)=cb|P|U;
    check_shared32(a,va,7102);check_shared32(b,va,7102);
    *(volatile u32 *)((u8 *)phys(cb)+1)=7201;
    check_shared32(b,va,7202);check_shared32(a,alias,7202);
    *pte_slot(a,va,false)|=1ULL<<63;
    switch_to(b);last.vector=0;fault_mode=M_EXIT_ON_FAULT;
    enter_compat(va,USTACK_TOP-64,0,0,0,0);
    CHECK(last.vector==14&&last_cr2==va&&(last.err&0x15)==0x15,"shared32 NX was ignored");
    *pte_slot(a,va,false)&=~(1ULL<<63);
    check_shared32(a,va,7202);check_shared32(b,va,7202);
    // Removing a directory or a PML4 edge must also forget shared lookups;
    // the monitor reaches the tracked table through the old shadow subtree.
    phys(private_b&ADDR)[(va>>12)&511]=ca|P|U;
    u64 original_root=phys(b)[0],*pdpt=phys(original_root&ADDR);
    u64 original_directory=pdpt[(va>>30)&511],different_directory=frame_alloc();
    memcpy(phys(different_directory),phys(original_directory&ADDR),4096);
    phys(different_directory)[(va>>21)&511]=private_b;
    pdpt[(va>>30)&511]=different_directory|P|W|U;
    check_shared32(b,va,7002);check_shared32(a,va,7202);
    pdpt[(va>>30)&511]=original_directory;
    check_shared32(b,va,7202);
    u64 different_root_child=frame_alloc();
    memcpy(phys(different_root_child),phys(original_root&ADDR),4096);
    phys(different_root_child)[(va>>30)&511]=different_directory|P|W|U;
    phys(b)[0]=different_root_child|P|W|U;
    check_shared32(b,va,7002);check_shared32(a,va,7202);
    phys(b)[0]=original_root;
    check_shared32(b,va,7202);
    switch_to(home);
    // The existing real supervisor fixture also runs from a second CR3 with
    // the same physical kernel table; privilege and decode mode stay distinct.
    map4k(b,0x700000,frame_alloc(),P|W);
    *directory_slot(b,0x700000)=*directory_slot(home,0x700000);
    for(unsigned i=0;i<4;i++) {
        switch_to(i&1?b:home);
        *(volatile u32 *)0x720000=0xc0def00d;
        user_status=~0ULL;
        enter_kernel32(0x700000,0x711000-64);
        CHECK(user_status==0,"shared supervisor status %x",user_status);
    }
    switch_to(home);
}

static void test_timer(void) {
    if (!on_engine()) // the tick is the engine harness's device
        return;
    outl(0xF0, 1000);
    u64 start = ticks;
    u64 t = rdtsc();
    u64 r = run32(T_SPIN, 300000000);
    kprintf("measure 32-bit spin iteration, ten instructions: %x ps (hex)\n", (rdtsc() - t) * 1000 / 300000000);
    CHECK(r == 0, "registers or flags changed across interrupts: status %x", r);
    CHECK(ticks - start >= 5, "ticks %x", ticks - start);
    outl(0xF0, 0);
    kprintf("measure ticks while spinning in 32-bit ring 3: %x (hex)\n", ticks - start);
}

static void test_payloads(void) {
    u64 t = rdtsc();
    run32(M_FUZZ, 0);
    CHECK(user_status == 0, "fuzz status %x", user_status);
    if (on_engine())
        kprintf("measure fuzz: %x ms (hex)\n", (rdtsc() - t) / 1000000);
    t = rdtsc();
    run32(M_I386, 0);
    CHECK(user_status == 0, "test-i386 status %x", user_status);
    if (on_engine())
        kprintf("measure test-i386: %x ms (hex)\n", (rdtsc() - t) / 1000000);
}

void kmain(u64 ram) {
    ram_size = ram;
    next_frame = 32ULL << 20;
    kprintf("kernel up\n");
    setup_tables();
    u64 boot_root;
    __asm__ volatile("mov %%cr3, %0" : "=r"(boot_root));
    memcpy(kernel_pml4, phys(boot_root & ADDR), 4096);
    u64 a = new_space();
    switch_to(a);
    map4k(a,0x700000,frame_alloc(),P|W);
    map4k(a,0x710000,frame_alloc(),P|W);
    map4k(a,0x720000,frame_alloc(),P|W);
    map4k(a,0x730000,frame_alloc(),P|W);
    CHECK(kernel32_blob_end-kernel32_blob<4096,"kernel32 fixture fits one page");
    memcpy((void *)0x700000,kernel32_blob,kernel32_blob_end-kernel32_blob);
    *(volatile u32 *)0x720000=0xc0def00d;
    user_status=~0ULL;
    enter_kernel32(0x700000,0x711000-64);
    CHECK(user_status==0,"supervisor compatibility status %x",user_status);
    kprintf("PASS supervisor compatibility arithmetic,GS,CLI/STI,CR3 and64-bit interrupt return\n");
    test_basics();
    test_entries();
    test_faults();
    test_far();
    test_segments();
    test_memory(a);
    test_spaces(a);
    test_shared32_tables(a);
    test_timer();
    kprintf("self checks done, %x failures\n", (u64)fails);
    test_payloads();
    kprintf("done, %x failures\n", (u64)fails);
    kexit(fails ? 1 : 0);
}
