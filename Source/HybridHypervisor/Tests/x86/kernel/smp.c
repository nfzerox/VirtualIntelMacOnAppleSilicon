// A freestanding x86-64 kernel run by several vCPUs at once, through `engine-run --smp`. It checks what they must agree on: shared memory, x86 memory order, shootdowns,
// IPIs, code changed under another vCPU, page tables changed under another vCPU. Every vCPU enters at kstart with its index; none goes through INIT or SIPI.
// Output through port 0xE9, exit through 0xF4. rdtsc is 1 GHz from a 24 MHz counter, so figures are means over long loops.
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
#define NX (1ULL << 63)
#define ADDR 0x000FFFFFFFFFF000ULL
#define MAX_CPUS 8
#define APIC_VA 0xFFFF9000FEE00000ULL
#define TEST_VA 0xFFFF900100000000ULL // data pages tests remap
#define CODE_VA 0xFFFF900200000000ULL // code pages tests rewrite and remap
#define USER_VA 0x10000000ULL
#define V_IPI 64
#define V_PONG 65
#define V_LEAVE 66

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
static inline u64 read_cr3(void) {
    u64 v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}
static inline void write_cr3(u64 v) { __asm__ volatile("mov %0, %%cr3" : : "r"(v)); }
static inline void invlpg(u64 va) { __asm__ volatile("invlpg (%0)" : : "r"(va)); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void pause(void) { __asm__ volatile("pause"); }
static inline void serialize(void) {
    u32 a = 0, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
}
void *memset(void *d, int v, unsigned long n) {
    u8 *p = d;
    while (n--)
        *(volatile u8 *)p++ = (u8)v;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n) {
    u8 *p = d;
    const u8 *q = s;
    while (n--)
        *(volatile u8 *)p++ = *q++;
    return d;
}

// ---- Output, under a lock, and only whole lines ----
static volatile u32 print_lock;
static void lock(volatile u32 *l) {
    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE))
        pause();
}
static void unlock(volatile u32 *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }
static void puts_(const char *s) {
    while (*s)
        outb(0xE9, (u8)*s++);
}
static void putu(u64 v) {
    char b[24];
    int n = 0;
    do {
        b[n++] = "0123456789"[v % 10];
        v /= 10;
    } while (v);
    while (n)
        outb(0xE9, (u8)b[--n]);
}
static void say(const char *a, u64 v, const char *b) {
    lock(&print_lock);
    puts_(a);
    putu(v);
    puts_(b);
    puts_("\n");
    unlock(&print_lock);
}
static volatile u32 fails;
#define CHECK(cond, what, v)                                                                                                                                         \
    do {                                                                                                                                                             \
        if (!(cond)) {                                                                                                                                               \
            say("FAIL " what ": ", (u64)(v), "");                                                                                                                    \
            __atomic_fetch_add(&fails, 1, __ATOMIC_RELAXED);                                                                                                         \
        }                                                                                                                                                            \
    } while (0)

// ---- Machine ----
static u32 ncpus;
static u64 next_frame, ram_size, boot_root;
static volatile u32 frame_lock;
static inline u64 *phys(u64 pa) { return (u64 *)(PHYSMAP + pa); }
static u64 frame_alloc(void) {
    lock(&frame_lock);
    u64 f = next_frame;
    next_frame += 4096;
    unlock(&frame_lock);
    if (f + 4096 > ram_size) {
        say("out of frames ", f, "");
        outl(0xF4, 90);
    }
    memset(phys(f), 0, 4096);
    return f;
}
static u64 *pte_slot(u64 root, u64 va) {
    u64 table = root;
    for (int level = 3; level > 0; level--) {
        u64 *e = phys(table) + ((va >> (12 + 9 * level)) & 0x1ff);
        if (!(*e & P)) {
            u64 f = frame_alloc();
            u64 want = f | P | W | U, none = 0;
            __atomic_compare_exchange_n(e, &none, want, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE); // loser's frame just leaks: a test kernel
        }
        table = *e & ADDR;
    }
    return phys(table) + ((va >> 12) & 0x1ff);
}

static u64 gdt[6 + 2 * MAX_CPUS] __attribute__((aligned(16)));
static struct {
    u32 r0;
    u64 rsp[3];
    u64 r1;
    u64 ist[7];
    u64 r2;
    u16 r3, iomap;
} __attribute__((packed)) tss[MAX_CPUS];
typedef struct {
    u16 off0, sel;
    u8 ist, attr;
    u16 off1;
    u32 off2, r;
} __attribute__((packed)) Gate;
static Gate idt[256] __attribute__((aligned(16)));
static u8 kstacks[MAX_CPUS][32768] __attribute__((aligned(16)));
static u8 sstacks[MAX_CPUS][4096] __attribute__((aligned(16)));
static struct {
    u64 syscall_rsp, user_rsp, enter_rsp;
    volatile u64 pf_count, ipi_count;
    u64 pad[3];
} percpu[MAX_CPUS] __attribute__((aligned(64)));

extern char isr13[], isr14[], isr64[], isr65[], isr66[], syscall_entry[], user_blob[], user_blob_end[], load_resume[];
extern __attribute__((noreturn)) void leave_user(u64 saved);
extern u64 load_or_fault(u64 va);

static inline u32 apic_rd(u32 r) { return *(volatile u32 *)(APIC_VA + r); }
static inline void apic_wr(u32 r, u32 v) { *(volatile u32 *)(APIC_VA + r) = v; }
static inline u32 this_cpu(void) { return apic_rd(0x20) >> 24; }
static void send_ipi(u32 cpu, u32 vector) {
    apic_wr(0x310, cpu << 24);
    apic_wr(0x300, vector);
}
static void send_ipi_others(u32 vector) { apic_wr(0x300, 3 << 18 | vector); }

static void set_gate(int v, void *h) {
    u64 a = (u64)h;
    idt[v] = (Gate){(u16)a, 8, 0, 0x8E, (u16)(a >> 16), (u32)(a >> 32), 0};
}

typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rdx, rcx, rbx, rax, vector, err, rip, cs, rflags, rsp, ss;
} Frame;

// What this kernel's handlers do, set by each test.
static volatile u64 shoot_va;
static volatile u32 acks, pong;
static volatile u64 ping_count;
static volatile bool ping_replies;

void trap(Frame *f) {
    u32 me = this_cpu();
    switch (f->vector) {
        case 14:
            if (f->rip == (u64)load_resume - 3) { // the mov in load_or_fault
                percpu[me].pf_count++;
                f->rax = ~0ULL;
                f->rip = (u64)load_resume;
                return;
            }
            say("unexpected page fault at rip ", f->rip, "");
            outl(0xF4, 91);
            return;
        case V_IPI:
            percpu[me].ipi_count++;
            if (shoot_va)
                invlpg(shoot_va);
            if (ping_replies)
                send_ipi(0, V_PONG);
            __atomic_fetch_add(&acks, 1, __ATOMIC_ACQ_REL);
            apic_wr(0xB0, 0);
            return;
        case V_PONG:
            __atomic_fetch_add(&pong, 1, __ATOMIC_ACQ_REL);
            apic_wr(0xB0, 0);
            return;
        case V_LEAVE:
            leave_user(percpu[me].enter_rsp);
        default:
            say("unexpected vector ", f->vector, "");
            say("  at rip ", f->rip, "");
            outl(0xF4, 92);
    }
}

// ---- Rendezvous ----
static volatile u32 bar_count, bar_gen;
static void barrier(void) {
    u32 gen = __atomic_load_n(&bar_gen, __ATOMIC_ACQUIRE);
    if (__atomic_add_fetch(&bar_count, 1, __ATOMIC_ACQ_REL) == ncpus) {
        __atomic_store_n(&bar_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&bar_gen, gen + 1, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&bar_gen, __ATOMIC_ACQUIRE) == gen)
            pause();
    }
}

// ---- Shared memory: every locked form, and a control that must lose updates ----
static struct {
    volatile u64 q;
    volatile u32 d;
    volatile u16 w;
    volatile u8 b;
    u32 plain_lock;
    volatile u64 plain;
    volatile u64 bits;
    volatile u64 pair[2] __attribute__((aligned(16)));
} shared __attribute__((aligned(64)));

#define ATOMIC_ROUNDS 300000ULL
static void atomics_run(u32 me, int form) {
    for (u64 i = 0; i < ATOMIC_ROUNDS; i++) {
        switch (form) {
            case 0:
                __asm__ volatile("lock addq $1, %0" : "+m"(shared.q));
                break;
            case 1:
                __asm__ volatile("lock incq %0" : "+m"(shared.q));
                break;
            case 2:
                __asm__ volatile("lock subq $-1, %0" : "+m"(shared.q));
                break;
            case 3: {
                u64 one = 1;
                __asm__ volatile("lock xaddq %0, %1" : "+r"(one), "+m"(shared.q));
                break;
            }
            case 4:
                __asm__ volatile("lock addl $1, %0" : "+m"(shared.d));
                break;
            case 5:
                __asm__ volatile("lock addw $1, %0" : "+m"(shared.w));
                break;
            case 6:
                __asm__ volatile("lock addb $1, %0" : "+m"(shared.b));
                break;
            case 7: { // cmpxchg loop
                u64 old = shared.q, next;
                do
                    next = old + 1;
                while (!__atomic_compare_exchange_n(&shared.q, &old, next, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
                break;
            }
            case 8: { // a mutex from xchg around plain memory
                u32 v = 1;
                do
                    __asm__ volatile("xchgl %0, %1" : "+r"(v), "+m"(shared.plain_lock));
                while (v && (v = 1));
                shared.plain = shared.plain + 1;
                __asm__ volatile("" : : : "memory");
                shared.plain_lock = 0; // a plain store releases under TSO
                break;
            }
            case 9: { // each vCPU sets and clears its own bit: one that loses an update sees it
                u8 was;
                __asm__ volatile("lock btsq %2, %1\n setc %0" : "=q"(was), "+m"(shared.bits) : "r"((u64)me));
                if (was)
                    __atomic_fetch_add(&fails, 1, __ATOMIC_RELAXED);
                __asm__ volatile("lock btrq %2, %1\n setc %0" : "=q"(was), "+m"(shared.bits) : "r"((u64)me));
                if (!was)
                    __atomic_fetch_add(&fails, 1, __ATOMIC_RELAXED);
                break;
            }
            case 10: { // cmpxchg16b: the pair must always hold matching halves
                u64 lo = shared.pair[0], hi = shared.pair[1];
                u8 ok;
                __asm__ volatile("lock cmpxchg16b %1\n sete %0" : "=q"(ok), "+m"(*(volatile __int128 *)shared.pair), "+a"(lo), "+d"(hi) : "b"(lo + 1), "c"(hi + 1));
                if (ok) {
                    __atomic_fetch_add(&shared.q, 1, __ATOMIC_RELAXED);
                } else {
                    i--;
                    if (lo != hi)
                        __atomic_fetch_add(&fails, 1, __ATOMIC_RELAXED);
                }
                break;
            }
            case 11: // the control: not locked, so updates are lost only if vCPUs really overlap
                __asm__ volatile("addq $1, %0" : "+m"(shared.q));
                break;
        }
    }
}

static void test_atomics(u32 me) {
    static const char *const names[] = {"lock add q", "lock inc q", "lock sub q", "lock xadd q", "lock add d", "lock add w", "lock add b", "cmpxchg", "xchg mutex", "lock bts btr", "cmpxchg16b", "plain add"};
    for (int form = 0; form < 12; form++) {
        barrier();
        if (me == 0)
            memset((void *)&shared, 0, sizeof shared);
        barrier();
        u64 t0 = rdtsc();
        atomics_run(me, form);
        u64 t = rdtsc() - t0;
        barrier();
        if (me == 0) {
            u64 want = ATOMIC_ROUNDS * ncpus;
            u64 got = form == 4 ? shared.d : form == 5 ? shared.w : form == 6 ? shared.b : form == 8 ? shared.plain : shared.q;
            if (form == 5)
                want &= 0xffff;
            if (form == 6)
                want &= 0xff;
            if (form == 9)
                want = got = 0;
            lock(&print_lock);
            puts_("  ");
            puts_(names[form]);
            puts_(": ");
            putu(t / ATOMIC_ROUNDS);
            puts_(" ns each, ");
            if (form == 11) {
                putu(want - got);
                puts_(" of ");
                putu(want);
                puts_(" updates lost, as an unlocked add may\n");
            } else {
                puts_(got == want ? "none lost\n" : "LOST\n");
            }
            unlock(&print_lock);
            if (form != 11)
                CHECK(got == want, "lost updates", want - got);
            if (form == 11 && got == want) // on a busy host the threads may just not have overlapped, which says nothing against the locked forms but makes them weaker evidence
                say("  plain adds lost nothing, so the vCPUs did not overlap this time: ", got, " adds");
        }
    }
    if (me == 0)
        puts_("PASS shared memory\n");
}

// ---- x86 memory order ----
static struct {
    volatile u64 x __attribute__((aligned(64)));
    volatile u64 y __attribute__((aligned(64)));
    volatile u64 round __attribute__((aligned(64)));
    volatile u64 done __attribute__((aligned(64)));
    volatile u64 stop __attribute__((aligned(64)));
    volatile u64 r[4] __attribute__((aligned(64)));
} lit;
static bool tso_on;

// Message passing: a reader that sees the second store must see the first. Allowed to fail only with EnTSO off.
static void litmus_mp(u32 me) {
    u64 reads = 3000000, bad = 0;
    barrier();
    if (me == 0) {
        lit.x = lit.y = lit.stop = 0;
    }
    barrier();
    if (me == 0) {
        for (u64 i = 1; !lit.stop; i++) {
            lit.x = i;
            lit.y = i;
        }
    } else if (me == 1) {
        for (u64 i = 0; i < reads; i++) {
            u64 ry = lit.y;
            u64 rx = lit.x;
            if (rx < ry)
                bad++;
        }
        lit.stop = 1;
        say("  message passing, load order and store order: reordered ", bad, " times in 3M reads");
        if (tso_on)
            CHECK(bad == 0, "message passing reordered", bad);
    }
    barrier();
}

// The same through SSE stores and loads, which take another path through the translator.
static void litmus_mp_sse(u32 me) {
    u64 reads = 3000000, bad = 0;
    static volatile u64 sx[2] __attribute__((aligned(64))), sy[2] __attribute__((aligned(64)));
    barrier();
    if (me == 0) {
        sx[0] = sy[0] = lit.stop = 0;
    }
    barrier();
    if (me == 0) {
        for (u64 i = 1; !lit.stop; i++) {
            __asm__ volatile("movq %0, %%xmm0\n movdqa %%xmm0, %1\n movdqa %%xmm0, %2" : : "r"(i), "m"(sx[0]), "m"(sy[0]) : "xmm0", "memory");
        }
    } else if (me == 1) {
        for (u64 i = 0; i < reads; i++) {
            u64 ry, rx;
            __asm__ volatile("movdqa %1, %%xmm1\n movq %%xmm1, %0" : "=r"(ry) : "m"(sy[0]) : "xmm1");
            __asm__ volatile("movdqa %1, %%xmm2\n movq %%xmm2, %0" : "=r"(rx) : "m"(sx[0]) : "xmm2");
            if (rx < ry)
                bad++;
        }
        lit.stop = 1;
        say("  message passing through movdqa: reordered ", bad, " times in 3M reads");
        if (tso_on)
            CHECK(bad == 0, "SSE message passing reordered", bad);
    }
    barrier();
}

// Two vCPUs run one short shape together many times. cpu 0 resets and starts each round; both spread their start a little so that the windows meet.
enum { SHAPE_SB, SHAPE_SB_MFENCE, SHAPE_SB_XCHG, SHAPE_LB, SHAPE_22W };
static inline void spin(u64 n) {
    for (volatile u64 i = 0; i < n; i++)
        ;
}
static void shape_run(u32 me, int shape, u64 i, u64 *r) {
    spin(me ? (i >> 6) & 15 : i & 63); // the second vCPU learns of the round one cache miss late, and this sweeps the windows across each other
    volatile u64 *mine = me ? &lit.y : &lit.x, *other = me ? &lit.x : &lit.y;
    switch (shape) {
        case SHAPE_SB:
            *mine = 1;
            *r = *other;
            break;
        case SHAPE_SB_MFENCE:
            *mine = 1;
            __asm__ volatile("mfence" : : : "memory");
            *r = *other;
            break;
        case SHAPE_SB_XCHG: {
            u64 v = 1;
            __asm__ volatile("xchgq %0, %1" : "+r"(v), "+m"(*mine));
            *r = *other;
            break;
        }
        case SHAPE_LB:
            *r = *other;
            *mine = 1;
            break;
        case SHAPE_22W:
            *mine = 1;
            *other = 2;
            break;
    }
}
static u64 litmus_shape(u32 me, int shape, u64 rounds) {
    u64 hits = 0;
    barrier();
    if (me == 0)
        lit.round = lit.done = 0;
    barrier();
    if (me > 1) {
        barrier();
        return 0;
    }
    for (u64 i = 1; i <= rounds; i++) {
        u64 r = 0;
        if (me == 0) {
            lit.x = lit.y = 0;
            lit.round = i;
        } else {
            while (lit.round != i)
                ;
        }
        shape_run(me, shape, i, &r);
        lit.r[me] = r;
        if (me == 1) {
            lit.done = i;
        } else {
            while (lit.done != i)
                ;
            u64 r0 = lit.r[0], r1 = lit.r[1];
            if (shape == SHAPE_22W)
                hits += lit.x == 1 && lit.y == 1; // each vCPU's first store landed last: stores reordered
            else if (shape == SHAPE_LB)
                hits += r0 == 1 && r1 == 1;
            else
                hits += r0 == 0 && r1 == 0;
        }
    }
    barrier();
    return hits;
}

// Independent reads of independent writes, four vCPUs: readers may not disagree on the order of two stores.
static void litmus_iriw(u32 me) {
    if (ncpus < 4)
        return;
    u64 rounds = 300000, hits = 0;
    barrier();
    if (me == 0)
        lit.round = lit.done = lit.stop = 0;
    barrier();
    static volatile u64 arrived __attribute__((aligned(64)));
    if (me == 0)
        arrived = 0;
    barrier();
    for (u64 i = 1; i <= rounds; i++) {
        if (me == 0) {
            while (arrived != (ncpus - 1) * (i - 1))
                ;
            lit.x = lit.y = 0;
            lit.round = i;
            spin(i & 3);
            lit.x = 1;
        } else {
            while (lit.round != i)
                ;
            spin((i + me) & 3);
            if (me == 1) {
                lit.y = 1;
            } else if (me == 2) {
                lit.r[0] = lit.x;
                lit.r[1] = lit.y;
            } else if (me == 3) {
                lit.r[2] = lit.y;
                lit.r[3] = lit.x;
            }
            __atomic_fetch_add(&arrived, 1, __ATOMIC_ACQ_REL);
        }
        if (me == 0) {
            while (arrived != (ncpus - 1) * i)
                ;
            hits += lit.r[0] == 1 && lit.r[1] == 0 && lit.r[2] == 1 && lit.r[3] == 0;
        }
    }
    barrier();
    if (me == 0) {
        say("  independent reads of independent writes: disagreed ", hits, " times in 300K rounds");
        if (tso_on)
            CHECK(hits == 0, "IRIW", hits);
    }
}

static void test_tso(u32 me) {
    if (ncpus < 2)
        return;
    litmus_mp(me);
    litmus_mp_sse(me);
    u64 rounds = 400000, h;
    h = litmus_shape(me, SHAPE_SB, rounds);
    if (me == 0) {
        say("  store buffering, which x86 allows: seen ", h, " times in 400K rounds");
        CHECK(h > 0 || ncpus > 4, "store buffering never seen, so this test can't see much", h); // with more, the two that matter rarely overlap
    }
    h = litmus_shape(me, SHAPE_SB_MFENCE, rounds);
    if (me == 0) {
        say("  store buffering with mfence: seen ", h, " times");
        if (tso_on)
            CHECK(h == 0, "mfence let a load pass a store", h);
    }
    h = litmus_shape(me, SHAPE_SB_XCHG, rounds);
    if (me == 0) {
        say("  store buffering with xchg: seen ", h, " times");
        if (tso_on)
            CHECK(h == 0, "xchg let a load pass a store", h);
    }
    h = litmus_shape(me, SHAPE_LB, rounds);
    if (me == 0) {
        say("  load buffering: seen ", h, " times");
        if (tso_on)
            CHECK(h == 0, "load buffering", h);
    }
    h = litmus_shape(me, SHAPE_22W, rounds);
    if (me == 0) {
        say("  two plus two writes: seen ", h, " times");
        if (tso_on)
            CHECK(h == 0, "two plus two writes", h);
    }
    litmus_iriw(me);
    if (me == 0)
        puts_("PASS memory order\n");
}

// ---- The clock ----
static void test_clock(u32 me) {
    static volatile u64 latest __attribute__((aligned(64)));
    u64 behind = 0, n = 2000000;
    barrier();
    for (u64 i = 0; i < n; i++) {
        u64 seen = latest; // load before rdtsc, and rdtsc is taken in the monitor, so it can't pass the load
        u64 t = rdtsc();
        if (t + 1000 < seen) // the guest's clock has 41 ns steps
            behind++;
        latest = t;
    }
    if (me == 0) {
        say("  TSC behind another vCPU's by over a microsecond: ", behind, " of 2M");
        CHECK(behind == 0, "TSC disagrees between vCPUs", behind);
    }
    barrier();
    if (me == 0)
        puts_("PASS clock\n");
}

// ---- IPIs ----
static void test_ipi(u32 me) {
    if (ncpus < 2)
        return;
    u64 n = 20000;
    barrier();
    if (me == 0) {
        ping_replies = true;
        acks = pong = 0;
        shoot_va = 0;
        lit.stop = 0;
        lit.round = 0;
    }
    barrier();
    sti();
    if (me == 1) { // busy in a loop while pinged
        while (!lit.stop)
            pause();
    } else if (me == 0) {
        u64 t0 = rdtsc();
        for (u64 i = 0; i < n; i++) {
            send_ipi(1, V_IPI);
            while (pong != i + 1)
                pause();
        }
        u64 t = rdtsc() - t0;
        say("measure IPI there and back, peer busy, one way: ", t / n / 2, " ns");
        lit.stop = 1;
    }
    cli();
    barrier();
    // The peer asleep in hlt.
    if (me == 0) {
        lit.stop = 0;
        pong = 0;
    }
    barrier();
    if (me == 1) {
        sti();
        while (!lit.stop) {
            __asm__ volatile("hlt");
            sti();
        }
        cli();
    } else if (me == 0) {
        sti();
        u64 slow = 2000, t0 = rdtsc();
        for (u64 i = 0; i < slow; i++) {
            send_ipi(1, V_IPI);
            while (pong != i + 1)
                pause();
            spin(2000); // let it get back to sleep
        }
        u64 t = rdtsc() - t0;
        say("measure IPI there and back with the peer in hlt, one way, with about 2 us of spinning per round left in: ", t / slow / 2, " ns");
        lit.stop = 1;
        send_ipi(1, V_IPI);
        cli();
    }
    barrier();
    // Masked: nothing runs until the peer opens interrupts, and then exactly once.
    if (me == 0) {
        ping_replies = false;
        acks = 0;
        lit.round = 0;
    }
    barrier();
    if (me == 1) {
        u64 before = percpu[1].ipi_count;
        lit.round = 1;
        while (lit.round != 2)
            pause();
        spin(20000);
        CHECK(percpu[1].ipi_count == before, "IPI delivered with interrupts off", percpu[1].ipi_count - before);
        CHECK(apic_rd(0x200 + 0x20) & 1, "IPI not pending in IRR", apic_rd(0x220));
        sti();
        spin(1000);
        cli();
        CHECK(percpu[1].ipi_count == before + 1, "masked IPI not delivered once", percpu[1].ipi_count - before);
    } else if (me == 0) {
        while (lit.round != 1)
            pause();
        send_ipi(1, V_IPI);
        lit.round = 2;
    }
    barrier();
    // To all others at once.
    if (me == 0)
        acks = 0;
    barrier();
    sti();
    if (me == 0) {
        for (u32 i = 1; i <= 200; i++) {
            send_ipi_others(V_IPI);
            while (acks != i * (ncpus - 1))
                pause();
        }
        lit.stop = 7;
    } else {
        while (lit.stop != 7)
            pause();
    }
    cli();
    barrier();
    if (me == 0)
        puts_("PASS IPIs\n");
}

// ---- Shootdown: one vCPU remaps a page, the others read it. After their acks, none may see the old frame ----
#define TAGS 64
static u64 tag_frames[TAGS];
static volatile u64 sd_gen;
static void test_shootdown(u32 me) {
    if (ncpus < 2)
        return;
    u64 va = TEST_VA, rounds = 20000;
    barrier();
    if (me == 0) {
        for (u64 i = 0; i < TAGS; i++) {
            tag_frames[i] = frame_alloc();
            *phys(tag_frames[i]) = i;
        }
        *pte_slot(boot_root, va) = tag_frames[0] | P | W | NX;
        sd_gen = 0;
        lit.stop = 0;
        ping_replies = false;
        shoot_va = va;
    }
    barrier();
    sti();
    if (me == 0) {
        u64 *slot = pte_slot(boot_root, va);
        u64 total = 0;
        for (u64 r = 1; r <= rounds; r++) {
            u64 t0 = rdtsc();
            *slot = 0;
            invlpg(va);
            u32 want = acks + ncpus - 1;
            send_ipi_others(V_IPI);
            while (acks != want)
                pause();
            total += rdtsc() - t0;
            sd_gen = r * 2 - 1;
            *slot = tag_frames[r & (TAGS - 1)] | P | W | NX;
            sd_gen = r * 2;
            spin(300);
        }
        lit.stop = 8;
        say("measure shootdown, store to the table, IPI to the others, their invlpg and ack: ", total / rounds, " ns");
    } else {
        u64 reads = 0, stale = 0;
        while (lit.stop != 8) {
            u64 g = sd_gen;
            u64 v = load_or_fault(va);
            reads++;
            if (v == ~0ULL)
                continue;
            u64 now = sd_gen;
            if (now - g > 16) // this vCPU was off a core for long enough that the tag can't be judged
                continue;
            u64 d = (v - (g + 1) / 2) & (TAGS - 1);
            if (d >= TAGS / 2)
                stale++;
        }
        say("  a reader's stale values after shootdowns: ", stale, "");
        say("    of reads: ", reads, "");
        say("    of which faulted: ", percpu[me].pf_count, "");
        CHECK(stale == 0, "read through a mapping after its shootdown was acknowledged", stale);
    }
    cli();
    barrier();
    if (me == 0) {
        shoot_va = 0;
        puts_("PASS shootdown\n");
    }
}

// ---- Code changed under a running vCPU ----
typedef u32 (*CodeFn)(void);
static volatile u64 smc_gen;
static volatile u32 in_helper, helper_go;
static void write_code_mov_ret(u64 at, u32 imm) { // mov eax, imm32; ret
    volatile u8 *p = (volatile u8 *)at;
    p[0] = 0xB8;
    *(volatile u32 *)(p + 1) = imm;
    p[5] = 0xC3;
}

void smc_helper(void) { // The caller's continuation, in the page being patched, was translated before this returns to it.
    in_helper = 1;
    while (!helper_go)
        pause();
    serialize();
}

static void test_smc(u32 me) {
    if (ncpus < 2)
        return;
    u64 code = CODE_VA, rounds = 5000;
    barrier();
    if (me == 0) {
        u64 f = frame_alloc();
        *pte_slot(boot_root, code) = f | P | W;
        write_code_mov_ret(code, 0);
        smc_gen = 0;
        lit.stop = 0;
    }
    barrier();
    // Patch and call: after a serialising instruction that follows seeing the new value, a call must run the new code.
    if (me == 0) {
        for (u64 r = 1; r <= rounds; r++) {
            write_code_mov_ret(code, (u32)r);
            smc_gen = r;
            while (lit.round != r)
                pause();
        }
    } else if (me == 1) {
        u64 stale = 0;
        for (u64 r = 1; r <= rounds; r++) {
            while (smc_gen != r)
                pause();
            serialize();
            u32 got = ((CodeFn)code)();
            if (got != r)
                stale++;
            lit.round = r;
        }
        say("  patch and call from another vCPU, stale results: ", stale, " of 5000");
        CHECK(stale == 0, "ran stale code after cross-modifying and serialising", stale);
    }
    barrier();
    if (me == 0)
        lit.round = 0;
    barrier();
    // Patch while the other vCPU keeps calling, so that some stores land while it is being translated. Once it has seen the round and serialised, it must get that round's value.
    if (me == 0)
        lit.round = 0;
    barrier();
    if (me == 0) {
        for (u64 r = 1; r <= rounds * 4; r++) {
            write_code_mov_ret(code, (u32)r);
            spin(r & 255);
            write_code_mov_ret(code, (u32)r); // a second store, with the frame maybe just marked again
            smc_gen = r + 1000000;
            while (lit.round != r)
                pause();
        }
    } else if (me == 1) {
        u64 stale = 0;
        for (u64 r = 1; r <= rounds * 4; r++) {
            while (smc_gen != r + 1000000)
                ((CodeFn)code)();
            serialize();
            if (((CodeFn)code)() != r)
                stale++;
            lit.round = r;
        }
        say("  patched while being called and translated, stale results: ", stale, " of 20000");
        CHECK(stale == 0, "a store during translation left stale code", stale);
    }
    barrier();
    if (me == 0)
        lit.round = 0;
    barrier();
    // Patch the code a return comes back to: the return stack holds a pointer into the dead block.
    // movabs rax, smc_helper; call rax; mov eax, imm32; ret
    u64 code2 = CODE_VA + 0x1000;
    if (me == 0) {
        u64 f = frame_alloc();
        *pte_slot(boot_root, code2) = f | P | W;
        volatile u8 *p = (volatile u8 *)code2;
        p[0] = 0x48;
        p[1] = 0xB8;
        *(volatile u64 *)(p + 2) = (u64)smc_helper;
        p[10] = 0xFF;
        p[11] = 0xD0;
        write_code_mov_ret(code2 + 12, 0);
    }
    barrier();
    if (me == 0) {
        for (u64 r = 1; r <= rounds; r++) {
            while (!in_helper)
                pause();
            in_helper = 0;
            write_code_mov_ret(code2 + 12, (u32)r);
            helper_go = 1;
            while (lit.round != r)
                pause();
            helper_go = 0;
        }
    } else if (me == 1) {
        u64 stale = 0;
        for (u64 r = 1; r <= rounds; r++) {
            u32 got = ((CodeFn)code2)();
            if (got != r)
                stale++;
            lit.round = r;
            while (helper_go)
                pause();
        }
        say("  return into code patched meanwhile, stale results: ", stale, " of 5000");
        CHECK(stale == 0, "returned into stale code after serialising", stale);
    }
    barrier();
    if (me == 0)
        lit.round = 0;
    barrier();
    // Remap a code page under running translations, with a shootdown.
    u64 code3 = CODE_VA + 0x2000;
    static u64 frames[TAGS];
    if (me == 0) {
        for (u64 i = 0; i < TAGS; i++) {
            frames[i] = frame_alloc();
            write_code_mov_ret(PHYSMAP + frames[i], (u32)i);
        }
        *pte_slot(boot_root, code3) = frames[0] | P | W;
        shoot_va = code3;
        lit.stop = 0;
        sd_gen = 0;
    }
    barrier();
    sti();
    if (me == 0) {
        u64 *slot = pte_slot(boot_root, code3);
        for (u64 r = 1; r <= rounds; r++) {
            *slot = frames[r & (TAGS - 1)] | P | W;
            invlpg(code3);
            u32 was[MAX_CPUS]; // each vCPU's own count: a total would pass on an extra one from one vCPU, with another yet to see the IPI
            for (u32 i = 1; i < ncpus; i++)
                was[i] = __atomic_load_n(&percpu[i].ipi_count, __ATOMIC_ACQUIRE);
            send_ipi_others(V_IPI);
            for (u32 i = 1; i < ncpus; i++)
                while (__atomic_load_n(&percpu[i].ipi_count, __ATOMIC_ACQUIRE) == was[i])
                    pause();
            sd_gen = r;
            spin(100);
        }
        lit.stop = 9;
    } else if (me == 1) {
        u64 stale = 0, calls = 0;
        while (lit.stop != 9) {
            u64 g = sd_gen;
            u32 got = ((CodeFn)code3)();
            calls++;
            u64 now = sd_gen;
            if (now - g < TAGS / 4 && ((got - g) & (TAGS - 1)) >= TAGS / 2) { // from before the acknowledged shootdown g
                if (!stale) {
                    say("  first stale: generation ", g, "");
                    say("    and ran the code of ", got, "");
                    say("    with the generation now ", now, "");
                }
                stale++;
            }
        }
        say("  code page remapped, shootdown done, stale results: ", stale, "");
        say("    of calls: ", calls, "");
        CHECK(stale == 0, "ran code through a mapping after its shootdown", stale);
    } else {
        while (lit.stop != 9)
            pause();
    }
    cli();
    barrier();
    if (me == 0) {
        shoot_va = 0;
        puts_("PASS code changed under a running vCPU\n");
    }
}

// One vCPU storing into code that others call. Once a store is visible, no call begun after it may run the old bytes, even
// when one vCPU translates just as another finds that translation: it takes three to see this.
static void test_smc_visible(u32 me) {
    static volatile u64 generation;
    u64 code = CODE_VA + 0x3000;
    if (me == 0) {
        u64 f = frame_alloc();
        u8 *p = (u8 *)(PHYSMAP + f);
        p[3] = 0xB8; // mov $0, %eax; ret, with the value on a four-byte boundary
        *(u32 *)(p + 4) = 0;
        p[8] = 0xC3;
        *pte_slot(boot_root, code) = f | P | W;
        generation = 0;
        lit.stop = 0;
    }
    barrier();
    if (me == 0) {
        for (u32 g = 1; g <= 300000; g++) {
            __atomic_store_n((volatile u32 *)(code + 4), g, __ATOMIC_SEQ_CST);
            __atomic_store_n(&generation, g, __ATOMIC_SEQ_CST);
        }
        lit.stop = 9;
    } else {
        u64 stale = 0, last_got = 0, last_want = 0;
        while (lit.stop != 9) {
            u32 want = (u32)__atomic_load_n(&generation, __ATOMIC_SEQ_CST);
            u32 a = 0, b, c = 0, d;
            __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d)); // serialising: what was translated before this is no excuse
            u32 got = ((CodeFn)(code + 3))();
            if (got < want) {
                stale++;
                last_got = got;
                last_want = want;
            }
        }
        if (stale) {
            say("  last stale result ", last_got, "");
            say("    against its generation ", last_want, "");
        }
        CHECK(stale == 0, "ran code older than a store it saw", stale);
    }
    barrier();
    if (me == 0)
        puts_("PASS a store to code is seen by every call after it\n");
}

// ---- Page tables changed by several vCPUs at once, in one leaf table ----
#define PT_PAGES 128
static void pt_stress(u32 me, u64 rounds, u64 *fills, u64 *stores) {
    u64 base = TEST_VA + 0x200000 + (u64)me * PT_PAGES * 4096, t_fill = 0, t_store = 0;
    u64 frame = frame_alloc();
    u64 frame2 = frame_alloc();
    *phys(frame) = 0x1111 + me;
    *phys(frame2) = 0x2222 + me;
    for (u64 r = 0; r < rounds; r++) {
        u64 f = r & 1 ? frame2 : frame, want = *phys(f);
        u64 t0 = rdtsc();
        for (u64 i = 0; i < PT_PAGES; i++)
            *pte_slot(boot_root, base + i * 4096) = f | P | W | NX;
        u64 t1 = rdtsc();
        for (u64 i = 0; i < PT_PAGES; i++) {
            u64 got = *(volatile u64 *)(base + i * 4096);
            if (got != want) {
                __atomic_fetch_add(&fails, 1, __ATOMIC_RELAXED);
                say("FAIL stale mapping after a table store, on vCPU ", me, "");
            }
        }
        u64 t2 = rdtsc();
        t_store += t1 - t0;
        t_fill += t2 - t1;
    }
    *fills = t_fill / rounds / PT_PAGES;
    *stores = t_store / rounds / PT_PAGES;
}

// Code and busy data on one page, as a guest can have. Each store takes the code's translations with it, while another vCPU keeps needing them: translating starts over and over, and has
// to get somewhere without the monitor's stack growing each time.
#define MIXED_VA (CODE_VA + 0x10000)
static volatile u64 mixed_stop;

static void test_mixed_page(u32 me) {
    if (ncpus < 2)
        return;
    barrier();
    if (me == 0) {
        u64 f = frame_alloc();
        *pte_slot(boot_root, MIXED_VA) = f | P | W;
        write_code_mov_ret(MIXED_VA, 1234);
        mixed_stop = 0;
    }
    barrier();
    if (me == 0) {
        volatile u64 *data = (volatile u64 *)(MIXED_VA + 0x800);
        while (!mixed_stop)
            *data = *data + 1;
    } else if (me == 1) {
        for (u64 r = 0; r < 200000; r++)
            CHECK(((CodeFn)MIXED_VA)() == 1234, "code on a page with busy data", r);
        mixed_stop = 1;
    }
    barrier();
    if (me == 0)
        say("  code and busy data on one page: ", *(volatile u64 *)(MIXED_VA + 0x800), " stores");
}

static void test_tables(u32 me) {
    u64 fills, stores, rounds = 2000;
    barrier();
    if (me == 0) { // a walk that exists first, so that table creation isn't in the figures
        pte_slot(boot_root, TEST_VA + 0x200000);
        pt_stress(me, rounds, &fills, &stores);
        say("measure a table store dropping a live entry, alone: ", stores, " ns");
        say("measure a shadow fill after it, alone, the load included: ", fills, " ns");
    }
    barrier();
    pt_stress(me, rounds, &fills, &stores);
    if (me == 0) {
        say("measure a table store dropping a live entry, all vCPUs at it in one table: ", stores, " ns");
        say("measure a shadow fill after it, all vCPUs at it: ", fills, " ns");
    }
    barrier();
    if (me == 0)
        puts_("PASS page tables\n");
}

static u64 roots[MAX_CPUS][2];
static void test_cr3(u32 me) {
    u64 n = 500000, t;
    barrier();
    roots[me][0] = frame_alloc();
    roots[me][1] = frame_alloc();
    for (int r = 0; r < 2; r++)
        memcpy(phys(roots[me][r]), phys(boot_root), 4096);
    barrier();
    if (me == 0) {
        t = rdtsc();
        for (u64 i = 0; i < n; i++)
            write_cr3(roots[0][i & 1]);
        say("measure CR3 write between two address spaces, alone: ", (rdtsc() - t) / n, " ns");
    }
    barrier();
    t = rdtsc();
    for (u64 i = 0; i < n; i++)
        write_cr3(roots[me][i & 1]);
    t = (rdtsc() - t) / n;
    write_cr3(boot_root);
    if (me == 0)
        say("measure CR3 write, all vCPUs at it: ", t, " ns");
    barrier();
}

// ---- Ring 3: what must not have got slower ----
static void user_run(u32 me, u64 syscalls, u64 recursions) {
    extern u64 enter_user_args(u64, u64, u64, u64, u64);
    enter_user_args(USER_VA, USER_VA + 0x10000 + me * 0x10000 + 0xFF00, (u64)&percpu[me].enter_rsp, syscalls, recursions);
}

static void test_user(u32 me) {
    u64 n = 3000000, t;
    barrier();
    if (me == 0) {
        u64 code = frame_alloc();
        *pte_slot(boot_root, USER_VA) = code | P | U;
        memcpy(phys(code), user_blob, (u64)(user_blob_end - user_blob));
    }
    barrier();
    for (u64 i = 0; i < 16; i++)
        *pte_slot(boot_root, USER_VA + 0x10000 + me * 0x10000 + i * 4096) = frame_alloc() | P | W | U | NX;
    barrier();
    if (me == 0) {
        t = rdtsc();
        user_run(me, n, 0);
        say("measure syscall + sysret, alone in the address space: ", (rdtsc() - t) / n, " ns");
    }
    barrier();
    t = rdtsc();
    user_run(me, n, 0);
    t = (rdtsc() - t) / n;
    if (me == 0)
        say("measure syscall + sysret, all vCPUs in one address space: ", t, " ns");
    barrier();
    u64 r = 300000;
    if (me == 0) {
        t = rdtsc();
        user_run(me, 0, r);
        say("measure call and return, 40 deep and back, alone: ", (rdtsc() - t) / r, " ns");
    }
    barrier();
    t = rdtsc();
    user_run(me, 0, r);
    t = (rdtsc() - t) / r;
    if (me == 0)
        say("measure call and return, 40 deep and back, all vCPUs in one address space: ", t, " ns");
    barrier();
    if (me == 0)
        puts_("PASS ring 3 together\n");
}

// ---- Boot ----
static volatile u32 tables_ready;
void kmain(u64 ram, u64 me) {
    if (me == 0) {
        ram_size = ram;
        next_frame = 32ULL << 20;
        boot_root = read_cr3() & ADDR;
        ncpus = inl(0xF5);
        tso_on = inl(0xF6) != 0;
        gdt[1] = 0x00AF9A000000FFFFULL; // 0x08 kernel code
        gdt[2] = 0x00CF92000000FFFFULL; // 0x10 kernel data
        gdt[3] = 0x00CFF2000000FFFFULL; // 0x18 sysret base
        gdt[4] = 0x00CFF2000000FFFFULL; // 0x20 user data
        gdt[5] = 0x00AFFA000000FFFFULL; // 0x28 user code
        set_gate(13, isr13);
        set_gate(14, isr14);
        set_gate(V_IPI, isr64);
        set_gate(V_PONG, isr65);
        set_gate(V_LEAVE, isr66);
        idt[V_LEAVE].attr |= 3 << 5;
        *pte_slot(boot_root, APIC_VA) = 0xFEE00000ULL | P | W | NX;
        __atomic_store_n(&tables_ready, 1, __ATOMIC_RELEASE);
    } else {
        while (!__atomic_load_n(&tables_ready, __ATOMIC_ACQUIRE))
            pause();
    }
    u64 t = (u64)&tss[me];
    gdt[6 + 2 * me] = ((t & 0xffff) << 16) | ((t >> 16 & 0xff) << 32) | (0x89ULL << 40) | ((t >> 24 & 0xff) << 56) | (sizeof tss[0] - 1);
    gdt[7 + 2 * me] = t >> 32;
    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed)) d = {sizeof gdt - 1, (u64)gdt}, i = {sizeof idt - 1, (u64)idt};
    __asm__ volatile("lgdt %0" : : "m"(d));
    __asm__ volatile("lidt %0" : : "m"(i));
    tss[me].rsp[0] = (u64)kstacks[me] + sizeof kstacks[me];
    __asm__ volatile("ltr %%ax" : : "a"(0x30 + 16 * (u32)me));
    wrmsr(0x1B, 0xFEE00000ULL | 1 << 11 | (me ? 0 : 1 << 8));
    apic_wr(0xF0, 0x1FF);
    CHECK(this_cpu() == me, "APIC ID", this_cpu());
    // syscall: kernel at 0x08, sysret base 0x18.
    wrmsr(0xC0000080, 0xD01);
    wrmsr(0xC0000081, 0x0018000800000000ULL);
    wrmsr(0xC0000082, (u64)syscall_entry);
    wrmsr(0xC0000084, 0x200);
    percpu[me].syscall_rsp = (u64)sstacks[me] + sizeof sstacks[me];
    wrmsr(0xC0000101, (u64)&percpu[me]);
    wrmsr(0xC0000102, 0);

    if (me == 0)
        say("vCPUs: ", ncpus, tso_on ? ", EnTSO on" : ", EnTSO off: memory order is expected to fail, and the rest is skipped");
    barrier();
    test_tso((u32)me);
    if (tso_on) {
        test_atomics((u32)me);
        test_clock((u32)me);
        test_ipi((u32)me);
        test_shootdown((u32)me);
        test_smc((u32)me);
        test_smc_visible((u32)me);
        test_mixed_page(me);
    test_tables((u32)me);
        test_cr3((u32)me);
        test_user((u32)me);
    }
    barrier();
    if (me == 0) {
        say("done, ", fails, " failures");
        outl(0xF4, fails ? 1 : 0);
    }
    for (;;)
        __asm__ volatile("cli; hlt");
}
