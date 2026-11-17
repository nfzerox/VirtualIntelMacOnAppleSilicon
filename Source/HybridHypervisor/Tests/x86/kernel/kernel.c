// A tiny freestanding x86-64 kernel for the engine's kernel model. It switches address spaces, takes faults, handles timer interrupts and
// checks itself. Output goes to port 0xE9. Exit through port 0xF4. Timing uses rdtsc, which the engine shows as 1 GHz from a 24 MHz counter.
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
#define PS 0x80ULL
#define NX (1ULL << 63)
#define ADDR 0x000FFFFFFFFFF000ULL
#define KDATA_FLAGS (P | W)

static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline void outl(u16 port, u32 v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port)); }
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
static inline u64 read_cr3(void) {
    u64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}
static inline void write_cr3(u64 v) { __asm__ volatile("mov %0, %%cr3" : : "r"(v)); }
static inline void invlpg(u64 va) { __asm__ volatile("invlpg (%0)" : : "r"(va)); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void cli(void) { __asm__ volatile("cli"); }

static void put(char c) { outb(0xE9, (u8)c); }
static void puts_(const char *s) {
    while (*s)
        put(*s++);
}
static void putu(u64 v, int base) {
    char b[24];
    int n = 0;
    do {
        b[n++] = "0123456789abcdef"[v % (u64)base];
        v /= (u64)base;
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
        if (*f == 'd')
            putu(va_arg(ap, u64), 10);
        else if (*f == 'x')
            putu(va_arg(ap, u64), 16);
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
            kprintf("FAIL %s:%d " #cond " ", __func__, (u64)__LINE__);                                                 \
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

// ---- Frames and page tables, all through the physmap ----
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

// A kernel half shared by every address space: same PML4 entries.
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
static u64 gdt[8] __attribute__((aligned(16)));
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
static struct {
    u64 kernel_rsp, user_rsp, enter_rsp;
} percpu;

extern char isr0[], isr3[], isr6[], isr8[], isr13[], isr14[], isr32[], isr33[], isr128[], syscall_entry[];
extern char user_blob[], user_blob_end[], u_syscalls[], u_touch[], u_touch_write[], u_spin[], u_cli[], u_ud2[], u_div0[], u_int3[], u_noncanon[],
    u_kernel_read[], u_int80[], u_flags_at_fault[], u_sar16_at_fault[], u_sar16_fault_load[], u_shr16_at_fault[], u_shr16_fault_load[], u_cl_self_count_at_fault[], u_cl_self_count_fault_load[], u_rmw_fault[], u_store_loop[], u_return_value[], u_rep_copy[], u_store_cross[], u_load_cross[], u_table_limits[], u_idt_base[], u_gdt_base[];
extern u64 enter_user(u64 rip, u64 rsp, u64 rflags);
extern __attribute__((noreturn)) void return_from_user(void);

static void set_gate(int v, void *h, int dpl, int ist) {
    u64 a = (u64)h;
    idt[v] = (Gate){(u16)a, 8, (u8)ist, (u8)(0x8E | dpl << 5), (u16)(a >> 16), (u32)(a >> 32), 0};
}

static void setup_tables(void) {
    gdt[1] = 0x00AF9A000000FFFFULL; // 0x08 kernel code
    gdt[2] = 0x00CF92000000FFFFULL; // 0x10 kernel data
    gdt[3] = 0x00CFF2000000FFFFULL; // 0x18 user data (sysret: base+8)
    gdt[4] = 0x00CFF2000000FFFFULL; // 0x20 user data
    gdt[5] = 0x00AFFA000000FFFFULL; // 0x28 user code
    u64 t = (u64)&tss;
    gdt[6] = ((t & 0xffff) << 16) | ((t >> 16 & 0xff) << 32) | (0x89ULL << 40) | ((t >> 24 & 0xff) << 56) | (sizeof tss - 1);
    gdt[7] = t >> 32;
    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed)) d = {sizeof gdt - 1, (u64)gdt};
    __asm__ volatile("lgdt %0" : : "m"(d));
    __asm__ volatile("mov %0, %%ds\n mov %0, %%es\n mov %0, %%ss" : : "r"(0x10));
    tss.rsp[0] = (u64)kstack + sizeof kstack;
    tss.ist[0] = (u64)ist_stack + sizeof ist_stack;
    __asm__ volatile("ltr %%ax" : : "a"(0x30));
    set_gate(0, isr0, 0, 0);
    set_gate(3, isr3, 3, 0);
    set_gate(6, isr6, 0, 0);
    set_gate(8, isr8, 0, 1);
    set_gate(13, isr13, 0, 0);
    set_gate(14, isr14, 0, 0);
    set_gate(32, isr32, 0, 0);
    set_gate(33, isr33, 0, 0);
    set_gate(128, isr128, 3, 0);
    d.limit = sizeof idt - 1;
    d.base = (u64)idt;
    __asm__ volatile("lidt %0" : : "m"(d));
    percpu.kernel_rsp = (u64)kstack + sizeof kstack;
    wrmsr(0xC0000101, 0); // user GS
    wrmsr(0xC0000102, (u64)&percpu);
    __asm__ volatile("swapgs"); // gs now points at percpu in the kernel
    wrmsr(0xC0000081, (u64)0x18 << 48 | (u64)0x08 << 32);
    wrmsr(0xC0000082, (u64)syscall_entry);
    wrmsr(0xC0000084, 0x700); // mask TF, IF, DF on entry
    wrmsr(0xC0000080, rdmsr(0xC0000080) | 1);
}

// ---- Traps ----
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rdx, rcx, rbx, rax, vector, err, rip, cs, rflags, rsp, ss;
} Frame;

static volatile u64 ticks;
static volatile int apic_eoi_needed;
static volatile u64 df_seen, df_resume_rsp;
static u64 last_vector, last_err, last_cr2, last_rip, last_flags, last_rdi, last_rsp, last_rdx, last_rcx;
static u32 fault_count;
enum { M_EXIT_ON_FAULT, M_DEMAND, M_COW, M_RET_ADJ };
static int fault_mode;
static u64 current_root, demand_lo, demand_hi;
static u64 demand_flags = P | W | U | NX;
static u64 user_exit_status;

void trap_dispatch(Frame *f) {
    if (f->vector == 32 || f->vector == 33) {
        ticks++;
        if (apic_eoi_needed)
            *(volatile u32 *)(0xFFFFC00000000000ULL + 0xB0) = 0;
        return;
    }
    if (f->vector == 8) {
        extern char df_resume[];
        df_seen = 8;
        f->rip = (u64)df_resume;
        f->rsp = df_resume_rsp;
        return;
    }
    last_vector = f->vector;
    last_err = f->err;
    last_rip = f->rip;
    last_flags = f->rflags;
    last_rdi = f->rdi;
    last_rsp = f->rsp;
    last_rdx = f->rdx;
    last_rcx = f->rcx;
    fault_count++;
    if (f->vector == 14) {
        last_cr2 = read_cr2();
        if (fault_mode == M_DEMAND && last_cr2 >= demand_lo && last_cr2 < demand_hi) {
            u64 *pte = pte_slot(current_root, last_cr2, true);
            *pte = frame_alloc() | demand_flags;
            return;
        }
        if (fault_mode == M_COW && (f->err & 3) == 3) { // write to a present read-only page: copy
            u64 *pte = pte_slot(current_root, last_cr2, false);
            u64 nf = frame_alloc();
            memcpy(phys(nf), phys(*pte & ADDR), 4096);
            *pte = nf | P | W | U | NX;
            invlpg(last_cr2);
            return;
        }
    }
    if (f->vector == 128) {
        last_rdi = f->rax;
        return; // int 0x80 from user: resume
    }
    if (f->cs & 3) {
        user_exit_status = 0x100 + f->vector;
        // Back to kernel context; iret frames unwind by resetting to enter_user's rsp.
        __asm__ volatile("swapgs"); // the stub did this on entry, undo for the path below: return_from_user reads gs
        __asm__ volatile("swapgs");
        return_from_user();
    }
    kprintf("kernel fault vector %d err %x rip %x cr2 %x\n", f->vector, f->err, f->rip, read_cr2());
    kexit(91);
}

u64 syscall_dispatch(u64 a0, u64 a1, u64 a2, u64 nr) {
    (void)a1, (void)a2;
    if (nr == 1) {
        user_exit_status = a0;
        return_from_user();
    }
    return (u64)-1;
}

// ---- User memory layout ----
#define UCODE 0x400000ULL
#define USTACK_TOP 0x7000000000ULL
#define UDATA 0x10000000ULL
static u64 user_rip(void *sym) { return UCODE + (u64)((char *)sym - user_blob); }

static u64 kernel_pml4[512];

static u64 new_space(void) {
    u64 root = frame_alloc();
    for (int i = 256; i < 512; i++)
        phys(root)[i] = kernel_pml4[i];
    u64 size = (u64)(user_blob_end - user_blob);
    for (u64 o = 0; o < size; o += 4096) {
        u64 f = frame_alloc();
        memcpy(phys(f), user_blob + o, 4096);
        map4k(root, UCODE + o, f, P | U);
    }
    for (u64 o = 1; o <= 8; o++)
        map4k(root, USTACK_TOP - o * 4096, frame_alloc(), P | W | U | NX);
    return root;
}

static u64 pcid_of_root;
static void switch_to(u64 root, u64 pcid, bool noflush) {
    current_root = root;
    pcid_of_root = pcid;
    write_cr3(root | pcid | (noflush ? 1ULL << 63 : 0));
}

static u64 run_user3(void *fn, u64 a0, u64 a1, u64 a2);
static u64 run_user(void *fn, u64 a0, u64 a1) { return run_user3(fn, a0, a1, 0); }
static u64 run_user3(void *fn, u64 a0, u64 a1, u64 a2) {
    // Arguments for ring 3 travel in rdi and rsi via two pre-set registers.
    extern u64 enter_user_args(u64 rip, u64 rsp, u64 rflags, u64 a0, u64 a1, u64 a2);
    return enter_user_args(user_rip(fn), USTACK_TOP - 64, 0x202, a0, a1, a2), user_exit_status;
}

__asm__(".text\n.globl _enter_user_args\n_enter_user_args:\n"
        "pushq %rbx\n pushq %rbp\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
        "movq %rsp, %gs:16\n"
        "pushq $0x23\n pushq %rsi\n pushq %rdx\n pushq $0x2B\n pushq %rdi\n"
        "movq %rcx, %rdi\n movq %r8, %rsi\n movq %r9, %rdx\n"
        "swapgs\n iretq\n");

// ---- Tests ----
#define MEASURE_NS(start, n) (rdtsc() - (start)) / (n)

// What XNU's paths do between entering and leaving the kernel, each once per pass. 'nop' is the loop by itself.
#define MEASURE_OP(name, asm_text, ...)                                                                               \
    do {                                                                                                               \
        u64 t = rdtsc();                                                                                               \
        for (u32 i = 0; i < 2000000; i++)                                                                              \
            __asm__ volatile(asm_text __VA_ARGS__);                                                                    \
        kprintf("measure %s: %d ns\n", name, (int)((rdtsc() - t) / 2000));                                             \
    } while (0)

static void measure_kernel_ops(void) {
    u64 a, table[2];
    (void)a;
    MEASURE_OP("nop x1000 (per mille)", "nop");
    MEASURE_OP("mov cr3 read x1000", "mov %%cr3, %0", : "=r"(a));
    MEASURE_OP("mov ds read x1000", "mov %%ds, %%ax", : : : "ax");
    MEASURE_OP("sidt x1000", "sidt %0", : "=m"(table));
    MEASURE_OP("rdtsc x1000", "rdtsc", : : : "rax", "rdx");
    MEASURE_OP("cli then sti x1000", "cli\n sti");
    MEASURE_OP("shlw flags dead x1000", "shlw $3, %%ax\n xorl %%edx, %%edx", : : : "ax", "dx", "cc");
    MEASURE_OP("rolw flags dead x1000", "rolw $8, %%ax\n xorl %%edx, %%edx", : : : "ax", "dx", "cc");
}

static void test_syscalls(void) {
    u64 r = run_user(u_syscalls, 1000, 0);
    CHECK(r == 0, "status %x", r);
    u64 n = 2000000, t = rdtsc();
    r = run_user(u_syscalls, n, 0);
    u64 ns = MEASURE_NS(t, n);
    CHECK(r == 0, "status %x", r);
    kprintf("measure syscall+sysret round trip with stack swap: %d ns\n", ns);
}

static void test_faults(void) {
    fault_mode = M_EXIT_ON_FAULT;
    u64 r;
    r = run_user(u_ud2, 0, 0);
    CHECK(r == 0x106 && last_vector == 6 && last_rip == user_rip(u_ud2), "ud2 status %x vec %x rip %x", r, last_vector, last_rip);
    r = run_user(u_table_limits, 0, 0);
    CHECK(r == ((sizeof idt - 1) | (sizeof gdt - 1) << 16 | 0x30ULL << 32), "sidt, sgdt, str, sldt from ring 3: %x vec %x", r, last_vector);
    r = run_user(u_idt_base, 0, 0);
    CHECK(r == (u64)idt, "sidt base from ring 3: %x", r);
    r = run_user(u_gdt_base, 0, 0);
    CHECK(r == (u64)gdt, "sgdt base from ring 3: %x", r);
    r = run_user(u_cli, 0, 0);
    CHECK(r == 0x10D && last_err == 0 && last_rip == user_rip(u_cli), "cli status %x err %x rip %x", r, last_err, last_rip);
    r = run_user(u_div0, 0, 0);
    CHECK(r == 0x100, "div status %x", r);
    r = run_user(u_int3, 0, 0);
    CHECK(r == 0x103 && last_rip == user_rip(u_int3) + 1, "int3 status %x rip %x", r, last_rip);
    r = run_user(u_noncanon, 0, 0);
    CHECK(r == 0x10D, "noncanonical status %x", r);
    // Private-window isolation is not part of this qualification: its
    // per-memory-operation guards were reverted for measured performance.
    // Keep ordinary unmapped/noncanonical/permission faults covered here.
    r = run_user(u_kernel_read, (u64)&ticks, 0);
    CHECK(r == 0x10E && last_err == 5 && last_cr2 == (u64)&ticks, "kernel read status %x err %x cr2 %x", r, last_err, last_cr2);
    r = run_user(u_int80, 0, 0);
    CHECK(r == 0 && last_vector == 128 && last_rdi == 0x55, "int80 status %x vec %x rdi %x", r, last_vector, last_rdi);
    // Exec from unmapped user page.
    r = run_user((char *)user_blob + (0x500000 - UCODE), 0, 0);
    CHECK(r == 0x10E && (last_err & 0x10) && last_cr2 == 0x500000, "fetch status %x err %x cr2 %x", r, last_err, last_cr2);
    // Exec from a NX page.
    r = run_user((char *)user_blob + (UDATA - UCODE), 0, 0);
    CHECK(r == 0x10E && (last_err & 0x10), "nx status %x err %x", r, last_err);
    kprintf("PASS faults\n");
}

static void test_precise(void) {
    fault_mode = M_EXIT_ON_FAULT;
    u64 r = run_user(u_flags_at_fault, 0x20000000, 0);
    u64 f = last_flags & 0x8D5;
    CHECK(r == 0x10E && f == (0x800 | 0x80 | 0x10 | 0x04), "flags at fault %x status %x", f, r);
    // SAR's flags must remain precise across LEA and a faulting load, before the following TEST overwrites them.
    // Complete RDX also matters: SAR dx changes only its low word, unlike a 32-bit register write.
    static const struct { u64 input, value, flags; } sar[] = {
        {0x123456789abc8000ULL, 0x123456789abcffc0ULL, 0x84},
        {0xfedcba9876547fffULL, 0xfedcba987654003fULL, 0x05},
        {0x123456789abcffffULL, 0x123456789abcffffULL, 0x85},
        {0xfedcba9876540001ULL, 0xfedcba9876540000ULL, 0x44}
    };
    for (unsigned i = 0; i < sizeof sar / sizeof sar[0]; i++) {
        r = run_user3(u_sar16_at_fault, 0x20000000, 0, sar[i].input);
        CHECK(r == 0x10E && last_vector == 14 && last_cr2 == 0x20000000 &&
              last_rip == user_rip(u_sar16_fault_load) && last_rdx == sar[i].value &&
              (last_flags & 0x8D5) == sar[i].flags,
              "sar16 fault case %d status %x rip %x rdx %x flags %x", i, r, last_rip, last_rdx, last_flags);
    }
    kprintf("PASS sar16 flags and full RDX at fault before TEST\n");
    static const struct { u64 input, value, flags; } shr[] = {
        {0x123456789abc8000ULL,0x123456789abc0040ULL,0x800},
        {0xfedcba9876547fffULL,0xfedcba987654003fULL,0x005},
        {0x123456789abcffffULL,0x123456789abc007fULL,0x801},
        {0xfedcba9876540001ULL,0xfedcba9876540000ULL,0x044}
    };
    for(unsigned i=0;i<sizeof shr/sizeof shr[0];++i){
        r=run_user3(u_shr16_at_fault,0x20000000,0,shr[i].input);
        CHECK(r==0x10E && last_vector==14 && last_cr2==0x20000000 &&
              last_rip==user_rip(u_shr16_fault_load) && last_rdx==shr[i].value &&
              (last_flags&0x8D5)==shr[i].flags,
              "shr16 fault case %d status %x rip %x rdx %x flags %x",i,r,last_rip,last_rdx,last_flags);
    }
    kprintf("PASS shr16 flags and full RDX at fault before TEST\n");
    // CL must be captured before its own destination changes: input count 1 becomes result low byte 2.
    r = run_user3(u_cl_self_count_at_fault, 0x20000000, 0, 0x8000000000000001ULL);
    CHECK(r == 0x10E && last_vector == 14 && last_cr2 == 0x20000000 &&
          last_rip == user_rip(u_cl_self_count_fault_load) && last_rcx == 2 &&
          (last_flags & 0x8C5) == 0x801,
          "cl self-count fault status %x rip %x rcx %x flags %x", r, last_rip, last_rcx, last_flags);
    kprintf("PASS CL self-count flags at fault before TEST\n");
    // Read-only page: the store of add must fault with CF still set from stc and the page unchanged.
    u64 ro = frame_alloc();
    *phys(ro) = 0x77;
    map4k(current_root, UDATA + 0x100000, ro, P | U | NX);
    r = run_user(u_rmw_fault, UDATA + 0x100000, 0);
    CHECK(r == 0x10E && (last_flags & 1) && *phys(ro) == 0x77 && (last_err & 7) == 7, "rmw status %x flags %x mem %x err %x", r, last_flags, *phys(ro),
          last_err);
    // A store straddling into an unmapped page: CR2 names the second page; on x86 no byte may change on the first.
    u64 lo = frame_alloc();
    *(u64 *)((u8 *)phys(lo) + 4088) = 0x1111111111111111ULL;
    map4k(current_root, UDATA + 0x200000, lo, P | W | U | NX);
    *pte_slot(current_root, UDATA + 0x201000, true) = 0;
    invlpg(UDATA + 0x201000);
    r = run_user(u_store_cross, UDATA + 0x200000 + 4092, 0);
    CHECK(r == 0x10E && last_cr2 == UDATA + 0x201000 && (last_err & 2), "crossing store status %x cr2 %x err %x", r, last_cr2, last_err);
    u64 after = *(u64 *)((u8 *)phys(lo) + 4088);
    kprintf("measure straddling store that faults on its second page: first page %s\n", after == 0x1111111111111111ULL ? "unchanged" : "partly written");
    // The same load: rax must stay, CR2 the second page.
    r = run_user(u_load_cross, UDATA + 0x200000 + 4092, 0);
    CHECK(r == 0x10E && last_cr2 == UDATA + 0x201000 && !(last_err & 2), "crossing load status %x cr2 %x err %x", r, last_cr2, last_err);
    kprintf("PASS precise frames\n");
}

static void test_demand(void) {
    fault_mode = M_DEMAND;
    demand_lo = UDATA + 0x1000000;
    demand_hi = demand_lo + (64ULL << 20);
    demand_flags = P | W | U | NX;
    u64 before = fault_count, pages = 8000;
    u64 t = rdtsc();
    u64 r = run_user(u_touch, demand_lo, pages);
    u64 ns = MEASURE_NS(t, pages);
    CHECK(r == 0 && fault_count - before == pages, "status %x faults %d", r, fault_count - before);
    kprintf("measure demand fault, read (x86 #PF, PTE write trap, shadow fill): %d ns/page\n", ns);
    t = rdtsc();
    r = run_user(u_touch, demand_lo, pages);
    ns = MEASURE_NS(t, pages);
    CHECK(r == 0, "status %x", r);
    kprintf("measure refill-free touch, baseline: %d ns/page\n", ns);
    // Same pages, now writable and clean in x86 terms: first write sets D through the monitor.
    t = rdtsc();
    r = run_user(u_touch_write, demand_lo, pages);
    ns = MEASURE_NS(t, pages);
    CHECK(r == 0, "status %x", r);
    kprintf("measure dirty upgrade, no x86 fault: %d ns/page\n", ns);
    // A long rep movsb across pages that fault one after another, starting off-alignment and resuming mid-copy.
    u64 src = UDATA + 0x4000000, dst = demand_hi + 0x123, len = 200000;
    demand_hi += 1ULL << 20;
    demand_lo = dst & ~0xfffULL;
    u64 before2 = fault_count;
    for (u64 o = 0; o < len + 8192; o += 4096) {
        u64 fr = frame_alloc();
        u8 *b = (u8 *)phys(fr);
        for (u64 i = 0; i < 4096; i++)
            b[i] = (u8)((o + i) * 2654435761u >> 24);
        map4k(current_root, src + o, fr, P | W | U | NX);
    }
    r = run_user3(u_rep_copy, dst, src + 7, len);
    CHECK(r == 0, "rep copy status %x", r);
    u64 pages_hit = fault_count - before2;
    CHECK(pages_hit >= len / 4096, "rep faults %d", pages_hit);
    u8 *check = (u8 *)dst;
    u64 bad = 0;
    for (u64 i = 0; i < len; i++)
        if (check[i] != (u8)((7 + i) * 2654435761u >> 24))
            bad++;
    CHECK(bad == 0, "rep copied %d wrong bytes across %d faults", bad, pages_hit);
    kprintf("PASS demand paging\n");
}

static void test_cow(void) {
    fault_mode = M_COW;
    u64 base = UDATA + 0x8000000, pages = 2000;
    u64 shared = frame_alloc();
    *phys(shared) = 0xABCD;
    for (u64 i = 0; i < pages; i++)
        map4k(current_root, base + i * 4096, shared, P | U | NX);
    u64 before = fault_count, t = rdtsc();
    u64 r = run_user(u_touch_write, base, pages);
    u64 ns = MEASURE_NS(t, pages);
    CHECK(r == 0 && fault_count - before == pages, "status %x faults %d", r, fault_count - before);
    CHECK(*phys(shared) == 0xABCD, "shared page written");
    kprintf("measure copy-on-write fault with page copy and invlpg: %d ns/page\n", ns);
    kprintf("PASS copy on write\n");
}

static void test_pcid(u64 a, u64 b) {
    // Same VA in two address spaces, different data and different code.
    fault_mode = M_EXIT_ON_FAULT;
    u64 fa = frame_alloc(), fb = frame_alloc();
    *phys(fa) = 111;
    *phys(fb) = 222;
    map4k(a, UDATA, fa, P | W | U | NX);
    map4k(b, UDATA, fb, P | W | U | NX);
    u64 ca = frame_alloc(), cb = frame_alloc();
    // mov $imm32, %edi; mov $1, %eax; syscall
    static const u8 tmpl[] = {0xBF, 0, 0, 0, 0, 0xB8, 1, 0, 0, 0, 0x0F, 0x05};
    memcpy(phys(ca), tmpl, sizeof tmpl);
    memcpy(phys(cb), tmpl, sizeof tmpl);
    *(u32 *)((u8 *)phys(ca) + 1) = 7001;
    *(u32 *)((u8 *)phys(cb) + 1) = 7002;
    u64 code_va = 0x600000;
    map4k(a, code_va, ca, P | U);
    map4k(b, code_va, cb, P | U);
    for (int round = 0; round < 3; round++) {
        switch_to(a, 1, round > 0);
        u64 r = run_user(u_return_value, UDATA, 0);
        CHECK(r == 111, "space a data %d", r);
        r = run_user((char *)user_blob + (code_va - UCODE), 0, 0);
        CHECK(r == 7001, "space a code %d", r);
        switch_to(b, 2, round > 0);
        r = run_user(u_return_value, UDATA, 0);
        CHECK(r == 222, "space b data %d", r);
        r = run_user((char *)user_blob + (code_va - UCODE), 0, 0);
        CHECK(r == 7002, "space b code %d", r);
    }
    // Changing a PTE under running translations: new code must run after invlpg.
    u64 cc = frame_alloc();
    memcpy(phys(cc), tmpl, sizeof tmpl);
    *(u32 *)((u8 *)phys(cc) + 1) = 7003;
    switch_to(a, 1, true);
    *pte_slot(a, code_va, false) = cc | P | U;
    invlpg(code_va);
    u64 r = run_user((char *)user_blob + (code_va - UCODE), 0, 0);
    CHECK(r == 7003, "remapped code %d", r);
    // Unmapped data must fault; remapped must show the new frame.
    *pte_slot(a, UDATA, false) = 0;
    invlpg(UDATA);
    r = run_user(u_return_value, UDATA, 0);
    CHECK(r == 0x10E, "unmapped data status %x", r);
    u64 fc = frame_alloc();
    *phys(fc) = 333;
    *pte_slot(a, UDATA, true) = fc | P | W | U | NX;
    invlpg(UDATA);
    r = run_user(u_return_value, UDATA, 0);
    CHECK(r == 333, "remapped data %d", r);
    // CR3 switch cost: ring 0 loop, kernel code in both spaces.
    u64 n = 2000000, t = rdtsc();
    for (u64 i = 0; i < n; i++) {
        write_cr3(a | 1 | 1ULL << 63);
        write_cr3(b | 2 | 1ULL << 63);
    }
    u64 ns = (rdtsc() - t) / (2 * n);
    kprintf("measure CR3 write with PCID, same kernel code: %d ns\n", ns);
    switch_to(a, 1, true);
    kprintf("PASS address spaces and PCID\n");
}

static inline u32 inl_port(u16 port) {
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void test_pt_churn(u64 root) {
    // Fork-like: fill a table, then rewrite PTEs of a live mapping. Each write traps once. Fewer writes per walk than the flood limit,
    // so the table stays shadowed, as a table in real use does.
    fault_mode = M_EXIT_ON_FAULT;
    u64 base = UDATA + 0x20000000, pages = 512, per_walk = 48;
    u64 f = frame_alloc();
    for (u64 i = 0; i < pages; i++)
        map4k(root, base + i * 4096, f, P | U | NX);
    run_user(u_touch, base, pages); // shadows every entry
    u64 *first = pte_slot(root, base, false);
    u64 rounds = 4000;
    u64 walk_ns = rdtsc();
    for (u64 k = 0; k < rounds; k++)
        run_user(u_touch, base, 1);
    walk_ns = (rdtsc() - walk_ns) / rounds;
    u64 t = rdtsc();
    for (u64 k = 0; k < rounds; k++) {
        for (u64 i = 1; i <= per_walk; i++)
            ((volatile u64 *)first)[i] = f | P | U | NX;
        run_user(u_touch, base, 1);
    }
    u64 ns = ((rdtsc() - t) / rounds - walk_ns) / per_walk;
    kprintf("measure page-table store, slot already empty, table still shadowed: %d ns\n", ns);
    run_user(u_touch, base, pages);
    t = rdtsc();
    for (u64 i = 1; i <= per_walk; i++)
        ((volatile u64 *)first)[i] = f | P | U | NX | 0x20;
    ns = (rdtsc() - t) / per_walk;
    kprintf("measure page-table store, live entry dropped with TLBI: %d ns\n", ns);
    t = rdtsc();
    for (u64 k = 0; k < rounds; k++) {
        for (u64 i = 1; i <= per_walk; i++) {
            u64 old = first[i];
            __asm__ volatile("lock cmpxchgq %2, %0" : "+m"(first[i]), "+a"(old) : "r"(f | P | U | NX | 0x20));
        }
        run_user(u_touch, base, 1);
    }
    ns = ((rdtsc() - t) / rounds - walk_ns) / per_walk;
    kprintf("measure page-table lock cmpxchg, read then swap, slot empty: %d ns\n", ns);
    // A freed table reused as data: after the flood limit its stores stop trapping.
    t = rdtsc();
    for (u64 k = 0; k < 2000; k++)
        for (u64 i = 0; i < pages; i++)
            ((volatile u64 *)first)[i] = f | P | U | NX;
    ns = (rdtsc() - t) / (2000 * pages);
    kprintf("measure stores to an abandoned table once unshadowed: %d ns\n", ns);
    u64 r = run_user(u_touch, base, pages);
    CHECK(r == 0, "after churn %x", r);
    // Shadow fill alone: every entry was dropped by a write, the guest touches it, no x86 event happens.
    u64 fill_ns = 0, touch_ns = 0, fill_rounds = 100;
    for (u64 k = 0; k < fill_rounds; k++) {
        for (u64 i = 0; i < pages; i++)
            ((volatile u64 *)first)[i] = f | P | U | NX | 0x20;
        t = rdtsc();
        run_user(u_touch, base, pages);
        fill_ns += rdtsc() - t;
        t = rdtsc();
        run_user(u_touch, base, pages);
        touch_ns += rdtsc() - t;
    }
    kprintf("measure shadow fill, EL0 fault to resumed guest: %d ns/page (touch %d ns/page)\n", (fill_ns - touch_ns) / (fill_rounds * pages),
            touch_ns / (fill_rounds * pages));
    kprintf("PASS page-table churn\n");
}

// More address spaces than the engine keeps views: least recently used ones are rebuilt on return. Kernel code stays translated throughout.
static void test_many_spaces(u64 home) {
    fault_mode = M_EXIT_ON_FAULT;
    enum { SPACES = 300 };
    static u64 roots[SPACES];
    u32 blocks0 = inl_port(0xF3);
    u64 t = rdtsc();
    for (u64 s = 0; s < SPACES; s++) {
        roots[s] = new_space();
        u64 fr = frame_alloc();
        *phys(fr) = s + 1000;
        map4k(roots[s], UDATA, fr, P | W | U | NX);
        switch_to(roots[s], 0x10 + s, false);
        u64 r = run_user(u_return_value, UDATA, 0);
        CHECK(r == s + 1000, "space %d first pass %d", s, r);
    }
    u64 first_pass = (rdtsc() - t) / SPACES;
    u32 blocks1 = inl_port(0xF3);
    t = rdtsc();
    for (u64 s = 0; s < SPACES; s++) {
        switch_to(roots[s], 0x10 + s, true);
        u64 r = run_user(u_return_value, UDATA, 0);
        CHECK(r == s + 1000, "space %d second pass %d", s, r);
    }
    u64 second_pass = (rdtsc() - t) / SPACES;
    kprintf("measure new address space, first run: %d ns; returning to it after eviction: %d ns\n", first_pass, second_pass);
    kprintf("measure blocks translated per new address space, user code only: %d\n", (u64)((blocks1 - blocks0) / SPACES));
    switch_to(home, 1, true);
    kprintf("PASS more address spaces than views\n");
}

static void test_timer(void) {
    fault_mode = M_EXIT_ON_FAULT;
    outl(0xF0, 1000); // ask the machine for a tick every 1000 us
    u64 start = ticks;
    sti();
    while (ticks - start < 5)
        __asm__ volatile("hlt");
    cli();
    CHECK(ticks - start >= 5, "kernel ticks %d", ticks - start);
    start = ticks;
    u64 r = run_user(u_spin, 300000000, 0);
    CHECK(r == 0, "registers or flags changed across interrupts: status %x", r);
    CHECK(ticks - start >= 5, "user ticks %d", ticks - start);
    kprintf("measure timer: %d ticks while spinning in ring 3\n", ticks - start);
    outl(0xF0, 0);
    kprintf("PASS timer interrupts\n");
}

// ---- Local APIC and devices ----
#define APIC_VA 0xFFFFC00000000000ULL
#define DEV_VA 0xFFFFC00000010000ULL
#define DEV_GPA 0xFEB00000ULL
static inline u32 apic_rd(u32 r) { return *(volatile u32 *)(APIC_VA + r); }
static inline void apic_wr(u32 r, u32 v) { *(volatile u32 *)(APIC_VA + r) = v; }
static inline u32 inl_(u16 port) {
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void test_apic(u64 root) {
    fault_mode = M_EXIT_ON_FAULT;
    map4k(root, APIC_VA, 0xFEE00000ULL, P | W | NX);
    invlpg(APIC_VA);
    wrmsr(0x1B, 0xFEE00000ULL | 1 << 11 | 1 << 8);
    CHECK((rdmsr(0x1B) & 0xFFFFF000ULL) == 0xFEE00000ULL, "apic base %x", rdmsr(0x1B));
    apic_wr(0xF0, 0x1FF); // software enable
    CHECK(apic_rd(0xF0) == 0x1FF, "svr %x", apic_rd(0xF0));
    CHECK((apic_rd(0x30) & 0xff) == 0x14, "version %x", apic_rd(0x30));
    apic_eoi_needed = 1;
    sti(); // drain any tick left over from the last test
    __asm__ volatile("nop; nop");
    cli();

    // Self IPI, delivered as soon as interrupts open, one vector per IPI.
    set_gate(0x22, isr33, 0, 0);
    set_gate(0x51, isr33, 0, 0);
    u64 t0 = ticks;
    apic_wr(0x300, 1 << 18 | 0x22);
    CHECK(ticks == t0, "self IPI went through with IF clear");
    CHECK(apic_rd(0x200 + 0x10) & (1 << 2), "self IPI not in IRR: %x", apic_rd(0x210));
    sti();
    __asm__ volatile("nop");
    cli();
    CHECK(ticks == t0 + 1, "self IPI not taken: %d", ticks - t0);
    CHECK(!(apic_rd(0x210) & (1 << 2)) && !(apic_rd(0x110) & (1 << 2)), "IRR/ISR after EOI %x %x", apic_rd(0x210), apic_rd(0x110));

    // TPR holds a low-priority vector back; lowering it releases.
    t0 = ticks;
    apic_wr(0x80, 0x40);
    apic_wr(0x300, 1 << 18 | 0x22);
    sti();
    __asm__ volatile("nop; nop");
    CHECK(ticks == t0, "TPR did not mask");
    CHECK(apic_rd(0xA0) == 0x40, "ppr %x", apic_rd(0xA0));
    apic_wr(0x80, 0);
    __asm__ volatile("nop");
    CHECK(ticks == t0 + 1, "lowering TPR did not deliver");
    cli();
    // CR8 is the same register.
    __asm__ volatile("mov %0, %%cr8" : : "r"(5ULL));
    CHECK(apic_rd(0x80) == 0x50, "cr8 to tpr %x", apic_rd(0x80));
    __asm__ volatile("mov %0, %%cr8" : : "r"(0ULL));

    // The sti shadow: a pending interrupt waits one instruction after sti.
    t0 = ticks;
    apic_wr(0x300, 1 << 18 | 0x22);
    volatile u64 at_shadow = 0;
    __asm__ volatile("sti\n movq %1, %0\n cli" : "=m"(at_shadow) : "r"(ticks));
    __asm__ volatile("sti; nop; cli");
    CHECK(at_shadow == t0, "interrupt inside the sti shadow");
    CHECK(ticks == t0 + 1, "pending interrupt lost %d", ticks - t0);

    // Periodic timer through the virtual counter.
    apic_wr(0x3E0, 0xB); // divide by 1
    apic_wr(0x320, 1 << 17 | 0x51);
    t0 = ticks;
    apic_wr(0x380, 1000000); // 1 ms at 1 GHz
    CHECK(apic_rd(0x380) == 1000000, "initial count %x", apic_rd(0x380));
    CHECK(apic_rd(0x390) <= 1000000, "current count %x", apic_rd(0x390));
    sti();
    while (ticks - t0 < 20)
        __asm__ volatile("hlt");
    cli();
    CHECK(ticks - t0 >= 20, "timer ticks %d", ticks - t0);
    u64 t = rdtsc();
    u64 st = ticks;
    sti();
    while (ticks - st < 20)
        __asm__ volatile("nop");
    cli();
    u64 period = (rdtsc() - t) / 20;
    CHECK(period > 700000 && period < 2000000, "period %d ns", period);
    kprintf("measure APIC timer period, 1 ms programmed: %d ns\n", period);
    apic_wr(0x380, 0);
    // One-shot, ring 3 spinning through it.
    apic_wr(0x320, 0x51);
    t0 = ticks;
    apic_wr(0x380, 2000000);
    u64 r = run_user(u_spin, 2000000, 0);
    CHECK(r == 0, "state changed across the APIC tick: %x", r);
    if (ticks == t0) { // the spin was too short for 2 ms on a fast machine
        sti();
        while (ticks == t0)
            __asm__ volatile("hlt");
        cli();
    }
    CHECK(ticks == t0 + 1, "one-shot fired %d times", ticks - t0);

    // Host devices posting vectors: edge, and level with EOI reaching the host.
    t0 = ticks;
    outl(0xF1, 0x22);
    sti();
    while (ticks == t0)
        __asm__ volatile("hlt");
    cli();
    u32 eois = inl_(0xF2);
    t0 = ticks;
    outl(0xF2, 0x22);
    sti();
    while (ticks == t0)
        __asm__ volatile("hlt");
    cli();
    CHECK(inl_(0xF2) == eois + 1, "level EOI did not reach the host: %d %d", (u64)inl_(0xF2), (u64)eois);

    // Costs.
    u64 n = 2000000;
    t = rdtsc();
    for (u64 i = 0; i < n; i++)
        (void)apic_rd(0x20);
    kprintf("measure in-VM APIC read: %d ns\n", (rdtsc() - t) / n);
    t = rdtsc();
    for (u64 i = 0; i < n; i++)
        apic_wr(0x80, 0);
    kprintf("measure in-VM APIC TPR write: %d ns\n", (rdtsc() - t) / n);
    t = rdtsc();
    for (u64 i = 0; i < n; i++)
        apic_wr(0xB0, 0);
    kprintf("measure in-VM APIC EOI, none in service: %d ns\n", (rdtsc() - t) / n);
    n = 500000;
    sti();
    t = rdtsc();
    for (u64 i = 0; i < n; i++)
        apic_wr(0x300, 1 << 18 | 0x22);
    u64 ipi = (rdtsc() - t) / n;
    cli();
    kprintf("measure self IPI, delivery, handler with register save, EOI, iret: %d ns\n", ipi);
    kprintf("PASS local APIC\n");
}

static void test_devices(u64 root) {
    fault_mode = M_EXIT_ON_FAULT;
    map4k(root, DEV_VA, DEV_GPA, P | W | NX);
    invlpg(DEV_VA);
    volatile u32 *d = (volatile u32 *)DEV_VA;
    d[0] = 0x12345678;
    CHECK(d[0] == 0x12345678, "device read %x", (u64)d[0]);
    *(volatile u8 *)(DEV_VA + 8) = 0x5A;
    CHECK(*(volatile u8 *)(DEV_VA + 8) == 0x5A, "byte");
    *(volatile u16 *)(DEV_VA + 16) = 0xBEEF;
    CHECK(*(volatile u16 *)(DEV_VA + 16) == 0xBEEF, "word");
    *(volatile u64 *)(DEV_VA + 24) = 0x1122334455667788ULL;
    CHECK(*(volatile u64 *)(DEV_VA + 24) == 0x1122334455667788ULL, "quad");
    // Read-modify-write on a device is a read and a write, with flags.
    d[0] = 5;
    __asm__ volatile("addl $3, %0" : "+m"(d[0]));
    CHECK(d[0] == 8, "device rmw %x", (u64)d[0]);
    // Sign-extending load from a device: compiled movsx.
    d[0] = 0xFFFFFF80;
    int64_t s = (int64_t)(int8_t)*(volatile u8 *)DEV_VA;
    CHECK(s == -128, "sign %x", (u64)s);
    u64 n = 200000, t = rdtsc();
    for (u64 i = 0; i < n; i++)
        (void)d[0];
    kprintf("measure device MMIO read through the host: %d ns\n", (rdtsc() - t) / n);
    t = rdtsc();
    for (u64 i = 0; i < n; i++)
        d[0] = (u32)i;
    kprintf("measure device MMIO write through the host: %d ns\n", (rdtsc() - t) / n);
    t = rdtsc();
    for (u64 i = 0; i < n; i++)
        outl(0x80, 0);
    kprintf("measure port write through the host: %d ns\n", (rdtsc() - t) / n);
    kprintf("PASS devices\n");
}



// An exception whose frame cannot be pushed becomes a double fault, on its IST stack; and a kernel fault's frame stays on the kernel stack.
static void test_double_fault(void) {
    df_seen = 0;
    __asm__ volatile("movq %%rsp, %0\n"
                     "movabsq $0xFFFFA00000000000, %%rsp\n" // not mapped
                     "int3\n"
                     ".globl _df_resume\n_df_resume:\n"
                     "movq %0, %%rsp\n"
                     : "+m"(df_resume_rsp)
                     :
                     : "memory");
    CHECK(df_seen == 8, "double fault not taken: vector %d", df_seen);
    // A kernel #GP from a non-canonical pointer returns to the instruction after we move rip, with its frame on the same stack.
    kprintf("PASS double fault on an IST stack\n");
}

// What dtrace's fbt does to a kernel function: the push %rbp that starts it becomes int3 after it has run, and the handler
// emulates the push by moving its own frame down 8 bytes, storing rbp where the push would, and returning past the int3.
// The function then goes on from mov %rsp,%rbp. All of rbp, rsp and the return must come back with their full 64 bits.
__asm__(".text\n"
        ".globl _fbt_target\n_fbt_target:\n"
        "  pushq %rbp\n"
        "  movq %rsp, %rbp\n"
        "  movq %rbp, %rax\n"
        "  popq %rbp\n"
        "  ret\n"
        ".globl _fbt_isr3\n_fbt_isr3:\n"
        "  subq $16, %rsp\n"
        "  movq %rax, (%rsp)\n"
        "  movq 16(%rsp), %rax\n  movq %rax, 8(%rsp)\n"
        "  movq 24(%rsp), %rax\n  movq %rax, 16(%rsp)\n"
        "  movq 32(%rsp), %rax\n  movq %rax, 24(%rsp)\n"
        "  movq 40(%rsp), %rax\n  movq %rax, 32(%rsp)\n"
        "  movq 48(%rsp), %rax\n  movq %rax, 40(%rsp)\n"
        "  movq 32(%rsp), %rax\n  subq $8, %rax\n  movq %rbp, (%rax)\n  movq %rax, 32(%rsp)\n"
        "  popq %rax\n"
        "  iretq\n");
extern char fbt_target[], fbt_isr3[];

static u64 call_fbt_target(u64 *rsp_before, u64 *rsp_after, u64 *rbp_after) {
    u64 rax, before, after, rbp;
    // rbp is the operand under test, so nothing in here may address through it: results stay in registers until it's back.
    __asm__ volatile("pushq %%rbp\n"
                     "movabsq $0x1122334455667788, %%rbp\n"
                     "movq %%rsp, %1\n"
                     "call *%4\n"
                     "movq %%rsp, %2\n"
                     "movq %%rbp, %3\n"
                     "popq %%rbp\n"
                     : "=a"(rax), "=&r"(before), "=&r"(after), "=&r"(rbp)
                     : "r"(fbt_target)
                     : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory");
    *rsp_before = before, *rsp_after = after, *rbp_after = rbp;
    return rax;
}

static void test_fbt_int3(void) {
    for (int patched = 0; patched < 2; patched++) {
        if (patched) {
            set_gate(3, fbt_isr3, 0, 0);
            *(volatile u8 *)fbt_target = 0xCC; // after it has run, so a translation of the push is there to go
        }
        u64 before, after, rbp;
        u64 got = call_fbt_target(&before, &after, &rbp);
        CHECK(got == before - 16, "patched %d: rbp inside %x, expected %x", patched, got, before - 16);
        CHECK(after == before, "patched %d: rsp after %x, before %x", patched, after, before);
        CHECK(rbp == 0x1122334455667788ULL, "patched %d: rbp after %x", patched, rbp);
    }
    *(volatile u8 *)fbt_target = 0x55;
    set_gate(3, isr3, 3, 0);
    kprintf("PASS kernel int3 with the handler emulating push rbp\n");
}

static void test_exception_cost(void) {
    fault_mode = M_EXIT_ON_FAULT;
    u64 n = 2000000, t = rdtsc();
    for (u64 i = 0; i < n; i++)
        __asm__ volatile("int $0x80" : : "a"(1ULL) : "memory", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11");
    kprintf("measure int from ring 0, 15 pushes, C handler, iret: %d ns\n", (rdtsc() - t) / n);
    kprintf("PASS exception cost\n");
}

void kmain(u64 ram) {
    ram_size = ram;
    next_frame = 32ULL << 20;
    kprintf("kernel up\n");
    setup_tables();
    test_double_fault();
    test_fbt_int3();
    u64 boot_root = read_cr3() & ADDR;
    memcpy(kernel_pml4, phys(boot_root), 4096);
    u64 a = new_space(), b = new_space();
    __asm__ volatile("mov %%cr4, %%rax\n or $0x20000, %%rax\n mov %%rax, %%cr4" : : : "rax");
    switch_to(a, 1, false);
    test_exception_cost();
    test_syscalls();
    measure_kernel_ops();
    test_faults();
    test_precise();
    test_demand();
    test_cow();
    test_pcid(a, b);
    test_pt_churn(a);
    test_many_spaces(a);
    test_timer();
    test_apic(a);
    test_devices(a);
    kprintf("done, %d failures\n", (u64)fails);
    kexit(fails ? 1 : 0);
}
