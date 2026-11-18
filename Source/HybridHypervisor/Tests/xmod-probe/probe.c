// Cross-modifying code under Hypervisor.framework, as the Hybrid translator publishes it:
// a host thread writes a code slot, calls sys_icache_invalidate, then store-releases a pointer;
// N vCPUs load-acquire the pointer and BLR to it with no context synchronization of their own.
// Usage: xmod <vcpus> <reuse slots, 0 = fresh slots> <consumer flags> <tso 0/1> <seconds> [writer: host|none]
#include <Hypervisor/Hypervisor.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef uint64_t u64;
typedef uint32_t u32;
#define CHECK(call) do { hv_return_t r_ = (call); if (r_) { fprintf(stderr, "%s failed: 0x%x\n", #call, r_); exit(1); } } while (0)

#define BASE 0x40000000ULL
#define RAM_SIZE (32ULL << 20)
#define SITE (BASE + (1ULL << 20)) // 1 MiB of 128-byte slots, in the executable 2 MiB block
#define SLOTS 8192
#define DATA (BASE + (2ULL << 20))
#define TABLES (BASE + (8ULL << 20))
#define PUB (DATA + 0x000)
#define STOP (DATA + 0x100)
#define GO (DATA + 0x200)
#define COUNTERS (DATA + 0x1000)
#define STACKS (DATA + 0x10000)

extern char x_start[], x_end[], x_consume[], x_vectors[];
#define G(sym) (BASE + (u64)((sym) - x_start))
static char *ram;
#define P(ipa) ((u64 *)(ram + ((ipa) - BASE)))
static volatile u64 *W(u64 ipa) { return (volatile u64 *)P(ipa); }

#define MAX_CPU 12
typedef struct { pthread_t t; int index; hv_vcpu_t v; hv_vcpu_exit_t *x; } Cpu;
static Cpu cpus[MAX_CPU];
static u64 ttbr, flags, tso;
static atomic_int ready;

static void *worker(void *arg) {
    Cpu *c = arg;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    CHECK(hv_vcpu_create(&c->v, &c->x, NULL));
    u64 actlr;
    CHECK(hv_vcpu_get_sys_reg(c->v, HV_SYS_REG_ACTLR_EL1, &actlr));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_ACTLR_EL1, tso ? actlr | 2 : actlr & ~2ULL));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_MAIR_EL1, 0xFF));
    u64 tcr = 16 | 16ULL << 16 | 3ULL << 12 | 1ULL << 10 | 1ULL << 8 | 3ULL << 28 | 1ULL << 26 | 1ULL << 24 | 2ULL << 30 | 1ULL << 32 | 1ULL << 36;
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_TCR_EL1, tcr));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_TTBR0_EL1, ttbr));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_VBAR_EL1, G(x_vectors)));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_SCTLR_EL1, 0x30D00800 | 1 | 1 << 2 | 1 << 12 | 1 << 6));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_CPACR_EL1, 3 << 20));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_SP_EL1, STACKS + 0x10000 * (u64)(c->index + 1) - 16));
    u64 a[8] = {(u64)c->index, PUB, SITE, flags, COUNTERS + 64 * (u64)c->index, 0, STOP, GO};
    for (int i = 0; i < 8; i++)
        CHECK(hv_vcpu_set_reg(c->v, (hv_reg_t)(HV_REG_X0 + i), a[i]));
    CHECK(hv_vcpu_set_reg(c->v, HV_REG_PC, G(x_consume)));
    CHECK(hv_vcpu_set_reg(c->v, HV_REG_CPSR, 0x3C5));
    atomic_fetch_add(&ready, 1);
    for (;;) {
        CHECK(hv_vcpu_run(c->v));
        if (c->x->reason == HV_EXIT_REASON_CANCELED)
            continue;
        if (c->x->reason != HV_EXIT_REASON_EXCEPTION) { fprintf(stderr, "exit reason %d\n", c->x->reason); exit(1); }
        u64 s = c->x->exception.syndrome;
        if (s >> 26 != 0x16) { fprintf(stderr, "syndrome %llx\n", s); exit(1); }
        if ((s & 0xffff) == 2) {
            u64 r[3];
            for (int i = 0; i < 3; i++) hv_vcpu_get_reg(c->v, (hv_reg_t)(HV_REG_X0 + i), &r[i]);
            fprintf(stderr, "vCPU %d: unexpected exception esr %llx elr %llx far %llx\n", c->index, r[0], r[1], r[2]);
            exit(1);
        }
        return NULL; // hvc #0: stopped
    }
}

static void body(u32 *w, u64 gen) {
    u32 lo = (u32)gen & 0xffff, hi = ((u32)gen >> 16) & 0xffff;
    int n = 0;
    w[n++] = 0xD2800000u | lo << 5;              // movz x0, #lo
    w[n++] = 0xF2A00000u | hi << 5;              // movk x0, #hi, lsl 16
    for (u64 i = 0; i < (gen % 24); i++)         // a varying length, so stale lines differ in shape
        w[n++] = 0x91000529u;                    // add x9, x9, #1
    w[n++] = 0xD65F03C0u;                        // ret
}

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: xmod vcpus reuse flags tso seconds [host|none]\n"); return 64; }
    int n = atoi(argv[1]), reuse = atoi(argv[2]);
    flags = strtoull(argv[3], 0, 0);
    tso = strtoull(argv[4], 0, 0);
    double seconds = atof(argv[5]);
    int host_ic = argc < 7 || strcmp(argv[6], "none");
    if (n < 1 || n > MAX_CPU || reuse < 0 || reuse > SLOTS) return 64;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    alarm((unsigned)seconds + 30);
    hv_vm_config_t config = hv_vm_config_create();
    if (__builtin_available(macOS 26.0, *))
        hv_vm_config_set_ipa_granule(config, HV_IPA_GRANULE_16KB);
    CHECK(hv_vm_create(config));
    ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    CHECK(hv_vm_map(ram, BASE, RAM_SIZE, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
    memcpy(ram, x_start, (size_t)(x_end - x_start));
    sys_icache_invalidate(ram, (size_t)(x_end - x_start));
    u64 root = TABLES, l1 = TABLES + 0x1000, l2 = TABLES + 0x2000;
    P(root)[0] = l1 | 3;
    P(l1)[BASE >> 30] = l2 | 3;
    u64 block = 1 | 1ULL << 10 | 3ULL << 8;
    P(l2)[0] = BASE | block | 1ULL << 54;        // code and slots: EL1 rw, executable
    P(l2)[1] = DATA | block | 3ULL << 53;
    P(l2)[4] = TABLES | block | 3ULL << 53;
    ttbr = root | 1ULL << 48;

    // Generation 0 everywhere, so a fresh slot holds zeros only beyond the live ones.
    u32 *site = (u32 *)P(SITE);
    u64 g = 1;
    if (reuse) {
        for (int s = 0; s < reuse; s++) body(site + s * 32, 0);
        sys_icache_invalidate(site, (size_t)reuse * 128);
    }
    *W(PUB) = 0;
    *W(STOP) = 0;
    *W(GO) = 0;
    for (int i = 0; i < n; i++) {
        cpus[i].index = i;
        pthread_create(&cpus[i].t, NULL, worker, &cpus[i]);
    }
    while (atomic_load(&ready) < n) usleep(100);
    usleep(2000);
    *W(GO) = 1;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    u64 end = mach_absolute_time() + (u64)(seconds * 1e9 * tb.denom / tb.numer);
    int slot = 0;
    while (mach_absolute_time() < end) {
        if (reuse) slot = (int)(g % (u64)reuse);
        else if (++slot >= SLOTS) break; // fresh mode: each slot once
        u32 *w = site + slot * 32;
        body(w, g);
        if (host_ic) sys_icache_invalidate(w, 128);
        __atomic_store_n(W(PUB), g << 32 | (u64)slot, __ATOMIC_RELEASE);
        g++;
        for (u64 t = mach_absolute_time() + 24; mach_absolute_time() < t;) ; // ~1 us at 24 MHz
    }
    *W(STOP) = 1;
    for (int i = 0; i < n; i++) pthread_join(cpus[i].t, NULL);
    u64 calls = 0, wrong = 0, unknown = 0;
    for (int i = 0; i < n; i++) {
        u64 *k = P(COUNTERS + 64 * (u64)i);
        calls += k[0]; wrong += k[1]; unknown += k[2];
    }
    printf("vcpus %d %s flags %llu tso %llu writer %s: published %llu, calls %llu, stale-valid %llu, unknown %llu\n", n,
           reuse ? "reuse" : "fresh", flags, tso, host_ic ? "host-ic" : "no-ic", g - 1, calls, wrong, unknown);
    return 0;
}
