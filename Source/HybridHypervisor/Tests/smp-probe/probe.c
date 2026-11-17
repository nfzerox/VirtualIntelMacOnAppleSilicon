// What multi-vCPU would cost, measured in a VM set up as the engine sets one up: 16 KB host granule, 4 KB stage 1, EnTSO, EL1 code.
// Timing is wall time on the host over long loops, best of three, so figures of a few ns are means with the loop's own cost taken off.
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
#define CHECK(call)                                                                                                                                                  \
    do {                                                                                                                                                             \
        hv_return_t r_ = (call);                                                                                                                                     \
        if (r_) {                                                                                                                                                    \
            fprintf(stderr, "%s failed: 0x%x\n", #call, r_);                                                                                                         \
            exit(1);                                                                                                                                                 \
        }                                                                                                                                                            \
    } while (0)

#define BASE 0x40000000ULL
#define RAM_SIZE (32ULL << 20)
#define DATA (BASE + (2ULL << 20))
#define WINDOW (BASE + (4ULL << 20)) // 4 KB pages, not global, ASID 1
#define FRAMES (BASE + (6ULL << 20))
#define TABLES (BASE + (8ULL << 20))
#define SITE (BASE + (1ULL << 20))
#define ASID 1ULL
#define STOP (DATA + 0x000)
#define GO (DATA + 0x100)
#define LOCK (DATA + 0x200)
#define EPOCH (DATA + 0x300)
#define GEN (DATA + 0x400)
#define PATCH_FLAG (DATA + 0x500)
#define TURN (DATA + 0x600)
#define STACKS (DATA + 0x10000)

extern char g_start[], g_end[], g_vectors[], g_nolock[], g_lock[], g_ticket[], g_load[], g_load_none[], g_ldset[], g_writer[], g_spin[], g_hammer[], g_tlbi_va[],
    g_tlbi_asid[], g_tlbread[], g_tlbwrite[], g_exec[], g_ctr[], g_turn[];
#define G(sym) (BASE + (u64)((sym) - g_start))

static char *ram;
#define P(ipa) ((u64 *)(ram + ((ipa) - BASE)))
static volatile u64 *W(u64 ipa) { return (volatile u64 *)P(ipa); }
static double tick_ns;
static double now_ns(void) { return mach_absolute_time() * tick_ns; }

// ---- One worker thread per vCPU, as hv_vcpu_run must stay on the creating thread ----
typedef struct {
    pthread_t thread;
    int index;
    hv_vcpu_t v;
    hv_vcpu_exit_t *x;
    u64 pc, a[9], r[4];
    atomic_int state; // 0 idle, 1 asked to run, 2 running, 3 done
    u64 yield_usleep;
} Cpu;
#define MAX_CPU 4
static Cpu cpus[MAX_CPU];
static u64 ttbr;

static void set_up(Cpu *c) {
    CHECK(hv_vcpu_create(&c->v, &c->x, NULL));
    u64 actlr;
    CHECK(hv_vcpu_get_sys_reg(c->v, HV_SYS_REG_ACTLR_EL1, &actlr));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_ACTLR_EL1, actlr | 2));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_MAIR_EL1, 0xFF));
    u64 tcr = 16 | 16ULL << 16 | 3ULL << 12 | 1ULL << 10 | 1ULL << 8 | 3ULL << 28 | 1ULL << 26 | 1ULL << 24 | 2ULL << 30 | 1ULL << 32 | 1ULL << 36;
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_TCR_EL1, tcr));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_TTBR0_EL1, ttbr));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_VBAR_EL1, G(g_vectors)));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_SCTLR_EL1, 0x30D00800 | 1 | 1 << 2 | 1 << 12 | 1 << 6));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_CPACR_EL1, 3 << 20));
    CHECK(hv_vcpu_set_sys_reg(c->v, HV_SYS_REG_SP_EL1, STACKS + 0x10000 * (u64)(c->index + 1) - 16));
}

static void *worker(void *arg) {
    Cpu *c = arg;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    set_up(c);
    atomic_store(&c->state, 3);
    for (;;) {
        while (atomic_load(&c->state) != 1)
            usleep(20);
        atomic_store(&c->state, 2);
        for (int i = 0; i < 9; i++)
            CHECK(hv_vcpu_set_reg(c->v, (hv_reg_t)(HV_REG_X0 + (i < 8 ? i : 16)), c->a[i]));
        CHECK(hv_vcpu_set_reg(c->v, HV_REG_X27, PATCH_FLAG));
        CHECK(hv_vcpu_set_reg(c->v, HV_REG_PC, c->pc));
        CHECK(hv_vcpu_set_reg(c->v, HV_REG_CPSR, 0x3C5));
        for (;;) {
            CHECK(hv_vcpu_run(c->v));
            if (c->x->reason != HV_EXIT_REASON_EXCEPTION) {
                if (c->x->reason == HV_EXIT_REASON_CANCELED)
                    continue;
                fprintf(stderr, "exit reason %d\n", c->x->reason);
                exit(1);
            }
            u64 s = c->x->exception.syndrome;
            if (s >> 26 != 0x16) {
                u64 pc;
                hv_vcpu_get_reg(c->v, HV_REG_PC, &pc);
                fprintf(stderr, "syndrome %llx pc %llx va %llx\n", s, pc, c->x->exception.virtual_address);
                exit(1);
            }
            if ((s & 0xffff) == 1) { // an exit to the host, so another thread may have invalidated while this vCPU was not on a core
                if (c->yield_usleep)
                    usleep((useconds_t)c->yield_usleep);
                continue;
            }
            for (int i = 0; i < 4; i++)
                hv_vcpu_get_reg(c->v, (hv_reg_t)(HV_REG_X0 + i), &c->r[i]);
            if ((s & 0xffff) == 2) {
                fprintf(stderr, "UNEXPECTED exception class %llx at %llx far %llx\n", c->r[0], c->r[1], c->r[2]);
                exit(1);
            }
            break;
        }
        atomic_store(&c->state, 3);
    }
    return NULL;
}

static void start(int i, char *sym, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a16) {
    Cpu *c = &cpus[i];
    while (atomic_load(&c->state) != 3)
        ;
    c->pc = G(sym);
    u64 args[9] = {a0, a1, a2, a3, a4, a5, STOP, GO, a16};
    memcpy(c->a, args, sizeof args);
    atomic_store(&c->state, 1);
    while (atomic_load(&c->state) != 2)
        ;
}
static void wait_for(int i) {
    while (atomic_load(&cpus[i].state) != 3)
        ;
}

// Starts a routine on each of `n` vCPUs and the background on `nbg` more, and returns the wall ns the timed ones took.
static double run_timed(int n, char *sym, u64 count, u64 a1, u64 a2, u64 a3, char *bg, int nbg, u64 ba1, u64 ba2) {
    *W(STOP) = 0;
    *W(GO) = 0;
    for (int i = 0; i < n; i++)
        start(i, sym, count, a1, a2, a3, 0, 0, 0);
    for (int i = 0; i < nbg; i++)
        start(n + i, bg, 0, ba1, ba2, 0, 0, 0, 0);
    usleep(2000);
    double t0 = now_ns();
    *W(GO) = 1;
    for (int i = 0; i < n; i++)
        wait_for(i);
    double t1 = now_ns();
    *W(STOP) = 1;
    for (int i = 0; i < nbg; i++)
        wait_for(n + i);
    return t1 - t0;
}
static double best(int n, char *sym, u64 count, u64 a1, u64 a2, u64 a3, char *bg, int nbg, u64 ba1, u64 ba2) {
    double b = 1e30;
    for (int r = 0; r < 3; r++) {
        double t = run_timed(n, sym, count, a1, a2, a3, bg, nbg, ba1, ba2);
        if (t < b)
            b = t;
    }
    return b / (double)count;
}

// ---- Measurements ----
static void locks(void) {
    u64 n = 20000000;
    double loop = best(1, g_nolock, n, 0, 0, 0, NULL, 0, 0, 0);
    double tas = best(1, g_lock, n, LOCK, 0, 0, NULL, 0, 0, 0);
    double tick = best(1, g_ticket, n, LOCK, 0, 0, NULL, 0, 0, 0);
    printf("lock + unlock, uncontended: test-and-cas %.2f ns, ticket %.2f ns, over a loop of %.2f ns\n", tas - loop, tick - loop, loop);
    n = 2000000;
    // How long a spin of `hold` lasts, so that holds below match a monitor path.
    u64 hold = 400;
    double h = best(1, g_nolock, n, 0, hold, 0, NULL, 0, 0, 0) - loop;
    printf("critical section used below: %.0f ns, about a shadow fill's work without its trap\n", h);
    // Far below these rates the difference is lost in the noise of the single-vCPU baseline: see the alternating case below for that.
    static const u64 gaps[] = {0, 4000};
    for (int g = 0; g < 2; g++) {
        u64 count = n / 8;
        double unit = best(1, g_nolock, count, 0, hold, gaps[g], NULL, 0, 0, 0);
        for (int vcpus = 2; vcpus <= 4; vcpus += 2) {
            *W(LOCK) = 0;
            double a = best(vcpus, g_lock, count, LOCK, hold, gaps[g], NULL, 0, 0, 0);
            *P(LOCK) = 0;
            double b = best(vcpus, g_ticket, count, LOCK, hold, gaps[g], NULL, 0, 0, 0);
            printf("  %d vCPUs, each locking every %6.0f ns (%8.0f locks/s in all): test-and-cas adds %7.1f ns per acquire, ticket %7.1f\n", vcpus, unit, vcpus / unit * 1e9, a - unit, b - unit);
        }
    }
    // No waiting at all, but the line always last held elsewhere: what a lock costs when several vCPUs use it rarely.
    double pass = 1e30, locked = 1e30;
    for (int r = 0; r < 3; r++) {
        for (int with = 0; with < 2; with++) {
            u64 count = 2000000;
            *W(TURN) = 0;
            *W(LOCK) = 0;
            *W(STOP) = 0;
            *W(GO) = 0;
            start(0, g_turn, count, with ? LOCK : 0, TURN, 0, 0, 0, 0);
            start(1, g_turn, count, with ? LOCK : 0, TURN, 1, 0, 0, 0);
            usleep(2000);
            double t0 = now_ns();
            *W(GO) = 1;
            wait_for(0);
            wait_for(1);
            double t = (now_ns() - t0) / (double)count / 2;
            if (with)
                locked = t < locked ? t : locked;
            else
                pass = t < pass ? t : pass;
        }
    }
    printf("two vCPUs taking turns, one way: %.1f ns; with a lock and unlock each turn: %.1f ns, so %.1f ns per acquire on a line last held elsewhere\n", pass, locked, locked - pass);
}

static void loads(void) {
    u64 n = 100000000;
    double base = best(1, g_load_none, n, EPOCH, 0, 0, NULL, 0, 0, 0);
    double quiet = best(1, g_load, n, EPOCH, 0, 0, NULL, 0, 0, 0);
    printf("epoch check, load and compare, the line never written: %.2f ns over a loop of %.2f\n", quiet - base, base);
    double every = best(1, g_load, n, EPOCH, 0, 0, g_writer, 1, EPOCH, 4000);
    printf("  another vCPU writing the line about once per microsecond: %.2f ns\n", every - base);
    every = best(1, g_load, n, EPOCH, 0, 0, g_writer, 1, EPOCH, 40000);
    printf("  written about once per 10 us: %.2f ns\n", every - base);
    double l = best(1, g_ldset, n / 4, EPOCH, 0, 0, NULL, 0, 0, 0);
    printf("ldset on an accessed bit, uncontended: %.2f ns\n", l - base);
}

static void tlbi(void) {
    u64 n = 200000, op = ASID << 48 | (WINDOW >> 12);
    double a = best(1, g_tlbi_va, n, op, 0, 0, NULL, 0, 0, 0);
    double asid = best(1, g_tlbi_asid, n, ASID << 48, 0, 0, NULL, 0, 0, 0);
    printf("tlbi vae1is, dsb, isb. Peers exist but stay out of the VM: %.0f ns; aside1is %.0f ns\n", a, asid);
    a = best(1, g_tlbi_va, n, op, 0, 0, g_spin, 1, 0, 0);
    asid = best(1, g_tlbi_asid, n, ASID << 48, 0, 0, g_spin, 1, 0, 0);
    printf("  a peer spinning in the VM: %.0f ns; aside1is %.0f ns\n", a, asid);
    a = best(1, g_tlbi_va, n, op, 0, 0, g_hammer, 1, WINDOW, 512);
    asid = best(1, g_tlbi_asid, n, ASID << 48, 0, 0, g_hammer, 1, WINDOW, 512);
    printf("  a peer loading through 512 pages: %.0f ns; aside1is %.0f ns\n", a, asid);
    a = best(1, g_tlbi_va, n, op, 0, 0, g_hammer, 3, WINDOW, 512);
    asid = best(1, g_tlbi_asid, n, ASID << 48, 0, 0, g_hammer, 3, WINDOW, 512);
    printf("  three peers loading through 512 pages: %.0f ns; aside1is %.0f ns\n", a, asid);
}

// One writer remaps a page, one reader checks it never sees a mapping older than the writer's completed invalidate. The local and absent cases are controls: they show the reader's TLB really held the old entry.
static void reach_once(const char *name, u64 mode, u64 yield, u64 yield_us) {
    u64 rounds = yield_us ? 3000 : 300000, *slot = P(TABLES + 0x4000);
    for (u64 i = 0; i < 64; i++)
        *P(FRAMES + i * 4096) = i;
    u64 attrs = 3 | 1ULL << 10 | 3ULL << 8 | 1ULL << 11 | 3ULL << 53;
    *slot = FRAMES | attrs;
    *W(GEN) = 0;
    *W(STOP) = 0;
    *W(GO) = 0;
    cpus[1].yield_usleep = yield_us;
    start(1, g_tlbread, 0, GEN, WINDOW, yield, 0, 0, 0);
    start(0, g_tlbwrite, rounds, GEN, TABLES + 0x4000, FRAMES | attrs, ASID << 48 | (WINDOW >> 12), mode, yield_us ? 40000 : 2000);
    usleep(2000);
    double t0 = now_ns();
    *W(GO) = 1;
    wait_for(0);
    double t1 = now_ns();
    usleep(100);
    *W(STOP) = 1;
    wait_for(1);
    cpus[1].yield_usleep = 0;
    printf("  %-44s %10llu reads, %8llu stale, %7llu faults, %5.0f ns per round\n", name, cpus[1].r[0], cpus[1].r[1], cpus[1].r[2], (t1 - t0) / (double)rounds);
    // Back to a clean state for whatever runs next.
    *slot = FRAMES | attrs;
    start(0, g_tlbi_asid, 1, ASID << 48, 0, 0, 0, 0, 0);
    wait_for(0);
}

static void reach(void) {
    printf("broadcast invalidate reaching a peer:\n");
    reach_once("tlbi vae1is, peer running", 0, 0, 0);
    reach_once("tlbi vae1is, peer exits to the host often", 0, 256, 0);
    reach_once("tlbi vae1is, peer asleep in the host each 1K", 0, 1024, 50);
    reach_once("control: tlbi vae1, local only", 1, 0, 0);
    reach_once("control: no invalidate", 2, 0, 0);
}

static void patching(void) {
    u32 *site = (u32 *)P(SITE);
    u32 add = 0x91000529u, svc = 0xD4000021u, ret = 0xD65F03C0u; // add x9, x9, #1; svc #1
    site[0] = add;
    site[1] = ret;
    sys_icache_invalidate(site, 8);
    *W(PATCH_FLAG) = 0;
    *W(STOP) = 0;
    *W(GO) = 0;
    start(1, g_exec, 0, SITE, 0, 0, 0, 0, 0);
    usleep(1000);
    *W(GO) = 1;
    usleep(1000);
    int rounds = 20000, lost = 0, late = 0;
    double *lat = malloc(sizeof(double) * (size_t)rounds);
    for (int i = 0; i < rounds; i++) {
        double t0 = now_ns();
        site[0] = svc;
        sys_icache_invalidate(site, 4);
        while (!*W(PATCH_FLAG)) {
            if (now_ns() - t0 > 1e8) {
                if (++lost <= 3) printf("  lost at round %d\n", i);
                if (lost >= 3) { lat[i] = 1e8; rounds = i + 1; goto done; }
                break;
            }
        }
        lat[i] = now_ns() - t0;
        site[0] = add;
        sys_icache_invalidate(site, 4);
        for (double t2 = now_ns(); now_ns() - t2 < 5000;)
            ;
        *W(PATCH_FLAG) = 0;
        for (double t2 = now_ns(); now_ns() - t2 < 5000;)
            ;
        if (*W(PATCH_FLAG)) { // the peer still ran the old svc 5 us after the word went back
            late++;
            *W(PATCH_FLAG) = 0;
        }
    }
done:
    *W(STOP) = 1;
    wait_for(1);
    printf("patching a word under a running peer, the host writing and calling sys_icache_invalidate as the translator does: %d rounds, the peer ran the site %llu times\n", rounds, cpus[1].r[0]);
    for (int i = 1; i < rounds; i++) {
        double v = lat[i];
        int j = i;
        for (; j > 0 && lat[j - 1] > v; j--)
            lat[j] = lat[j - 1];
        lat[j] = v;
    }
    start(1, g_ctr, 0, 0, 0, 0, 0, 0, 0);
    wait_for(1);
    u64 ctr = cpus[1].r[0];
        printf("  host store to peer trapping: median %.0f ns, 99%% %.0f ns, max %.0f ns; never within 100 ms: %d; old word still running 5-10 us after restoring: %d; no other exception class seen\n", lat[rounds / 2],
           lat[rounds * 99 / 100], lat[rounds - 1], lost, late);
    printf("  CTR_EL0 %llx: IDC %llu DIC %llu\n", ctr, ctr >> 28 & 1, ctr >> 29 & 1);
    free(lat);
}

int main(int argc, char **argv) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    tick_ns = (double)tb.numer / tb.denom;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(120);
    hv_vm_config_t config = hv_vm_config_create();
    if (__builtin_available(macOS 26.0, *))
        hv_vm_config_set_ipa_granule(config, HV_IPA_GRANULE_16KB);
    CHECK(hv_vm_create(config));
    ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    CHECK(hv_vm_map(ram, BASE, RAM_SIZE, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
    memcpy(ram, g_start, (size_t)(g_end - g_start));

    u64 root = TABLES, l1 = TABLES + 0x1000, l2 = TABLES + 0x2000, l3 = TABLES + 0x4000;
    P(root)[0] = l1 | 3;
    P(l1)[BASE >> 30] = l2 | 3;
    u64 block = 1 | 1ULL << 10 | 3ULL << 8;
    P(l2)[0] = BASE | block | 1ULL << 54; // EL1 rw, executable
    P(l2)[1] = DATA | block | 3ULL << 53;
    P(l2)[2] = l3 | 3;
    P(l2)[3] = FRAMES | block | 3ULL << 53;
    P(l2)[4] = TABLES | block | 3ULL << 53;
    for (u64 i = 0; i < 512; i++)
        P(l3)[i] = (FRAMES + i * 4096) | 3 | 1ULL << 10 | 3ULL << 8 | 1ULL << 11 | 3ULL << 53;
    ttbr = root | ASID << 48;

    for (int i = 0; i < MAX_CPU; i++) {
        cpus[i].index = i;
        pthread_create(&cpus[i].thread, NULL, worker, &cpus[i]);
    }
    for (int i = 0; i < MAX_CPU; i++)
        wait_for(i);
    const char *only = argc > 1 ? argv[1] : "";
    if (!*only || !strcmp(only, "locks"))
        locks();
    if (!*only || !strcmp(only, "loads"))
        loads();
    if (!*only || !strcmp(only, "tlbi"))
        tlbi();
    if (!*only || !strcmp(only, "reach"))
        reach();
    if (!*only || !strcmp(only, "patch"))
        patching();
    return 0;
}
