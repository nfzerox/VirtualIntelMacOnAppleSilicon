// Standalone targeted --smp fixture, never the broad SMP suite.
// Compile with existing smpstubs.s plus keeper-smp-user.s. No monitor writes.
#define kmain keeper_unused_broad_kmain
#define trap keeper_unused_broad_trap
#include "smp.c"
#undef trap
#undef kmain
#include <stddef.h>

#define SHARED_TEST 0x7FFF10000000ULL
#define CONTROL_VA USER_VA
#define MAIL_VA (USER_VA + 0x2000)
#define A_BIT 0x20ULL
#define PHASES 15
#define WAIT_TICKS 5000000000ULL // engine-run's existing 1 GHz guest TSC

typedef struct {
    u64 warm_target, target, epoch, wait_for_go;
    volatile u64 ready, go, warm, value, outcome, done;
    u64 deadline;
    volatile u64 timeout;
} KeeperMail;
_Static_assert(sizeof(KeeperMail) == 96, "user mailbox size");
_Static_assert(offsetof(KeeperMail, ready) == 32 && offsetof(KeeperMail, go) == 40, "user gate offsets");
_Static_assert(offsetof(KeeperMail, warm) == 48 && offsetof(KeeperMail, value) == 56, "user result offsets");
_Static_assert(offsetof(KeeperMail, outcome) == 64 && offsetof(KeeperMail, done) == 72, "user outcome offsets");
_Static_assert(offsetof(KeeperMail, deadline) == 80 && offsetof(KeeperMail, timeout) == 88, "user watchdog offsets");

typedef struct {
    const char *name;
    u32 warm_page, page, warm_value, value, fault_error, writes;
} KeeperCase;
static const KeeperCase cases[PHASES] = {
    {"remap",                0, 0, 17, 33, 0, 1},
    {"neighbor_after_remap", 1, 1, 55, 55, 0, 0},
    {"synonym_after_remap",  2, 2, 17, 17, 0, 0},
    {"accessed_rearm",       0, 0, 33, 33, 0, 1},
    {"nx_revoke",            0, 0, 33,  0, 0x15, 1},
    {"nx_restore",           1, 0, 55, 33, 0, 1},
    {"us_revoke",            0, 0, 33,  0, 0x15, 1},
    {"us_restore",           1, 0, 55, 33, 0, 1},
    {"present_revoke",       0, 0, 33,  0, 0x14, 1},
    {"present_restore",      1, 0, 55, 33, 0, 1},
    {"threshold_burst",      0, 0, 33, 33, 0, 65},
    {"smc_remapped",         0, 0, 33, 49, 0, 1},
    {"smc_synonym",          2, 2, 17, 50, 0, 1},
    {"remapped_final",       0, 0, 49, 49, 0, 0},
    {"neighbor_final",       1, 1, 55, 55, 0, 0},
};

static u64 root_a, root_b, shared_directory, frames[3], mail_frames[MAX_CPUS];
static volatile u64 *shared_slot, *burst_slots;
static KeeperMail *mail[MAX_CPUS];
static volatile u64 command_epoch, shoot_epoch, ipi_epoch[MAX_CPUS], completed_epoch[MAX_CPUS];
static volatile u32 peers_start, finish_readers, quiescent[MAX_CPUS];
static u64 fault_rip[MAX_CPUS], fault_cr2[MAX_CPUS], fault_err[MAX_CPUS];
static volatile u32 entered[MAX_CPUS];
extern char keeper_user_blob[], keeper_user_blob_end[];
extern u64 enter_user_args(u64, u64, u64, u64, u64);

static __attribute__((noreturn)) void die(u32 me, const char *why, u64 epoch) {
    // No print lock here: a broken/hung peer must not prevent fatal exit.
    puts_("KEEPER_SMP_FAIL cpu="); putu(me); puts_(" epoch="); putu(epoch);
    puts_(" reason="); puts_(why); puts_("\n"); outl(0xF4, 1);
    for (;;) __asm__ volatile("cli; hlt");
}

static void print_begin(u32 me) {
    u64 until = rdtsc() + WAIT_TICKS;
    while (__atomic_exchange_n(&print_lock, 1, __ATOMIC_ACQUIRE)) {
        if (rdtsc() >= until) die(me, "print_timeout", command_epoch);
        pause();
    }
}
static void print_end(void) { puts_("\n"); unlock(&print_lock); }
static void wait_epoch(volatile u64 *p, u64 epoch, u32 me, const char *why) {
    u64 until = rdtsc() + WAIT_TICKS;
    for (;;) {
        u64 value = __atomic_load_n(p, __ATOMIC_ACQUIRE);
        if (value == epoch) return;
        if (value > epoch || rdtsc() >= until) die(me, why, epoch);
        pause();
    }
}
static void wait_flag(volatile u32 *p, u32 me, const char *why) {
    u64 until = rdtsc() + WAIT_TICKS;
    while (!__atomic_load_n(p, __ATOMIC_ACQUIRE)) {
        if (rdtsc() >= until) die(me, why, command_epoch);
        pause();
    }
}
static void code_value(u64 frame, u32 value) {
    // One aligned eight-byte physical store: mov eax,imm32; ret; nop; nop.
    *(volatile u64 *)phys(frame) = 0x9090C300000000B8ULL | (u64)value << 8;
}

void trap(Frame *f) {
    u32 me = this_cpu();
    u64 epoch = __atomic_load_n(&command_epoch, __ATOMIC_ACQUIRE);
    KeeperMail *m = mail[me];
    if (f->vector == V_IPI) {
        u64 requested = __atomic_load_n(&shoot_epoch, __ATOMIC_ACQUIRE);
        if (!me || !entered[me] || !m || requested != epoch || !epoch || epoch > PHASES ||
            m->ready != epoch || (f->cs & 3) != 3 || f->rip < CONTROL_VA ||
            f->rip >= CONTROL_VA + (u64)(keeper_user_blob_end - keeper_user_blob) || ipi_epoch[me] >= epoch)
            die(me, "unexpected_ipi", epoch);
        if (shoot_va) invlpg(shoot_va);
        serialize();
        percpu[me].ipi_count++;
        __atomic_store_n(&ipi_epoch[me], epoch, __ATOMIC_RELEASE);
        apic_wr(0xB0, 0);
        return;
    }
    if (f->vector == 14) {
        u64 cr2; __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        if (!me || !entered[me] || !m || !epoch || epoch > PHASES ||
            !cases[epoch - 1].fault_error || m->epoch != epoch || m->go != epoch ||
            ipi_epoch[me] != epoch || (f->cs & 3) != 3 || f->rip != SHARED_TEST ||
            cr2 != SHARED_TEST || f->err != cases[epoch - 1].fault_error || m->outcome)
            die(me, "unexpected_fetch_fault", epoch);
        fault_rip[me] = f->rip; fault_cr2[me] = cr2; fault_err[me] = f->err;
        m->value = 0; m->outcome = 2;
        __atomic_store_n(&m->done, epoch, __ATOMIC_RELEASE);
        leave_user(percpu[me].enter_rsp);
    }
    if (f->vector == V_LEAVE) {
        if (!entered[me] || !m || (f->cs & 3) != 3 || m->timeout || m->outcome != 1 || m->done != m->epoch)
            die(me, "unexpected_user_return", epoch);
        leave_user(percpu[me].enter_rsp);
    }
    die(me, "unexpected_trap", epoch);
}

static void enter(u32 me) {
    entered[me] = 1;
    enter_user_args(CONTROL_VA, USER_VA + 0x10000 + me * 0x10000 + 0xFF00,
                    (u64)&percpu[me].enter_rsp, MAIL_VA + me * 4096, 0);
    entered[me] = 0;
}

static void make_roots(void) {
    root_a = frame_alloc(); root_b = frame_alloc();
    memcpy(phys(root_a), phys(boot_root), 4096); memcpy(phys(root_b), phys(boot_root), 4096);
    u64 pdpt_a = frame_alloc(), pdpt_b = frame_alloc(); shared_directory = frame_alloc();
    phys(root_a)[(SHARED_TEST >> 39) & 511] = pdpt_a | P | W | U;
    phys(root_b)[(SHARED_TEST >> 39) & 511] = pdpt_b | P | W | U;
    phys(pdpt_a)[(SHARED_TEST >> 30) & 511] = shared_directory | P | W | U;
    phys(pdpt_b)[(SHARED_TEST >> 30) & 511] = shared_directory | P | W | U;
    for (u32 i = 0; i < 3; i++) {
        frames[i] = frame_alloc(); code_value(frames[i], i == 1 ? 33 : i == 2 ? 55 : 17);
    }
    *pte_slot(root_a, SHARED_TEST) = frames[0] | P | U;
    *pte_slot(root_a, SHARED_TEST + 4096) = frames[2] | P | U;
    *pte_slot(root_a, SHARED_TEST + 8192) = frames[0] | P | U;
    shared_slot = pte_slot(root_b, SHARED_TEST);
    burst_slots = (volatile u64 *)((u64)shared_slot & ~0xFFFULL) + 128;
    if (shared_slot != pte_slot(root_a, SHARED_TEST) || pdpt_a == pdpt_b)
        die(0, "root_sharing_shape", 0);
    u64 control = frame_alloc();
    if ((u64)(keeper_user_blob_end - keeper_user_blob) > 4096) die(0, "control_blob_bound", 0);
    memcpy(phys(control), keeper_user_blob, (u64)(keeper_user_blob_end - keeper_user_blob));
    for (u32 r = 0; r < 2; r++) {
        u64 root = r ? root_b : root_a;
        *pte_slot(root, CONTROL_VA) = control | P | U;
        for (u32 cpu = 0; cpu < ncpus; cpu++) {
            if (!r) { mail_frames[cpu] = frame_alloc(); mail[cpu] = (KeeperMail *)phys(mail_frames[cpu]); }
            *pte_slot(root, MAIL_VA + cpu * 4096) = mail_frames[cpu] | P | W | U | NX;
            for (u32 p = 0; p < 16; p++)
                *pte_slot(root, USER_VA + 0x10000 + cpu * 0x10000 + p * 4096) = frame_alloc() | P | W | U | NX;
        }
    }
}

static void establish(void) {
    KeeperMail *m = mail[0];
    m->target = m->warm_target = SHARED_TEST;
    write_cr3(root_a); enter(0);
    if (m->value != 17) die(0, "a_user_warm", 0);
    memset(m, 0, sizeof(*m)); m->target = m->warm_target = SHARED_TEST;
    // Both transitions MUST be CPU0's: B-user replaces CPU0's A-user pin.
    write_cr3(root_b); enter(0);
    if (m->value != 17) die(0, "b_user_warm", 0);
    print_begin(0); puts_("KEEPER_SMP_ESTABLISHED cpu=0 a_then_b=1 shared_pte=1 distinct_upper=1"); print_end();
    __atomic_store_n(&peers_start, 1, __ATOMIC_RELEASE);
}

static void mutate(u64 epoch) {
    switch (epoch) {
        case 1: case 4: case 6: case 8: case 10: *shared_slot = frames[1] | P | U; break;
        case 5: *shared_slot = frames[1] | P | U | NX; break;
        case 7: *shared_slot = frames[1] | P; break;
        case 9: *shared_slot = 0; break;
        case 11:
            // Epoch10 restored a missing mapping and each peer executed it,
            // forcing a fresh keeper mark/write-counter reset. No target walk,
            // allocation or user call occurs between these 65 volatile stores.
            for (u64 n = 0; n < 65; n++) burst_slots[n] = frames[2] | P | U | NX;
            break;
        case 12: code_value(frames[1], 49); break;
        case 13: code_value(frames[0], 50); break;
        default: break;
    }
}

static void result_line(u32 me, u64 epoch) {
    KeeperMail *m = mail[me];
    print_begin(me);
    puts_("KEEPER_SMP_RESULT epoch="); putu(epoch); puts_(" cpu="); putu(me);
    puts_(" warm="); putu(m->warm); puts_(" outcome="); puts_(m->outcome == 2 ? "fault" : "value");
    puts_(" value="); putu(m->value); puts_(" rip="); putu(fault_rip[me]);
    puts_(" cr2="); putu(fault_cr2[me]); puts_(" err="); putu(fault_err[me]); print_end();
}

static void reader(u32 me) {
    wait_flag(&peers_start, me, "start_timeout"); write_cr3(root_b);
    for (u64 epoch = 1; epoch <= PHASES; epoch++) {
        wait_epoch(&command_epoch, epoch, me, "command_timeout");
        const KeeperCase *c = &cases[epoch - 1]; KeeperMail *m = mail[me];
        memset(m, 0, sizeof(*m)); fault_rip[me] = fault_cr2[me] = fault_err[me] = 0;
        m->warm_target = SHARED_TEST + c->warm_page * 4096;
        m->target = SHARED_TEST + c->page * 4096; m->epoch = epoch; m->wait_for_go = 1;
        m->deadline = rdtsc() + WAIT_TICKS;
        enter(me);
        if (m->ready != epoch || ipi_epoch[me] != epoch || m->go != epoch || m->done != epoch ||
            m->warm != c->warm_value || (c->fault_error ? m->outcome != 2 : (m->outcome != 1 || m->value != c->value)))
            die(me, "checked_result", epoch);
        result_line(me, epoch);
        __atomic_store_n(&completed_epoch[me], epoch, __ATOMIC_RELEASE);
    }
    wait_flag(&finish_readers, me, "finish_timeout");
    print_begin(me); puts_("KEEPER_SMP_CPU cpu="); putu(me);
    puts_(" role=reader ready_epoch="); putu(mail[me]->ready); puts_(" ack_epoch="); putu(ipi_epoch[me]);
    puts_(" done_epoch="); putu(completed_epoch[me]); puts_(" parked=1 failures=0"); print_end();
    cli(); __atomic_store_n(&quiescent[me], 1, __ATOMIC_RELEASE);
    outl(0xF4, 0); // Every secondary must have an independently retained successful host outcome.
    for (;;) __asm__ volatile("cli; hlt");
}

static void writer(void) {
    establish();
    for (u64 epoch = 1; epoch <= PHASES; epoch++) {
        const KeeperCase *c = &cases[epoch - 1];
        __atomic_store_n(&command_epoch, epoch, __ATOMIC_RELEASE);
        for (u32 cpu = 1; cpu < ncpus; cpu++) {
            wait_epoch(&mail[cpu]->ready, epoch, 0, "ready_timeout");
            if (mail[cpu]->warm != c->warm_value) die(0, "peer_warm_result", epoch);
        }
        mutate(epoch);
        // SMC-only epochs intentionally do NOT call INVLPG: their translation
        // invalidation must come from the physical code-store path itself.
        shoot_va = (epoch == 1 || (epoch >= 4 && epoch <= 10)) ? SHARED_TEST : 0;
        if (shoot_va) invlpg(shoot_va);
        __atomic_store_n(&shoot_epoch, epoch, __ATOMIC_RELEASE);
        for (u32 cpu = 1; cpu < ncpus; cpu++) send_ipi(cpu, V_IPI);
        for (u32 cpu = 1; cpu < ncpus; cpu++) wait_epoch(&ipi_epoch[cpu], epoch, 0, "shootdown_timeout");
        for (u32 cpu = 1; cpu < ncpus; cpu++) __atomic_store_n(&mail[cpu]->go, epoch, __ATOMIC_RELEASE);
        for (u32 cpu = 1; cpu < ncpus; cpu++) wait_epoch(&completed_epoch[cpu], epoch, 0, "done_timeout");
        if (epoch == 4 && !(*shared_slot & A_BIT)) die(0, "accessed_not_rearmed", epoch);
        print_begin(0); puts_("KEEPER_SMP_PHASE epoch="); putu(epoch); puts_(" case="); puts_(c->name);
        puts_(" peers="); putu(ncpus - 1); puts_(" writes="); putu(c->writes);
        puts_(" completed="); putu(ncpus - 1); print_end();
    }
    __atomic_store_n(&finish_readers, 1, __ATOMIC_RELEASE);
    for (u32 cpu = 1; cpu < ncpus; cpu++) wait_flag(&quiescent[cpu], 0, "quiesce_timeout");
    __atomic_store_n(&quiescent[0], 1, __ATOMIC_RELEASE);
    print_begin(0); puts_("KEEPER_SMP_CPU cpu=0 role=writer ready_epoch=0 ack_epoch=0 done_epoch=15 parked=1 failures=0"); print_end();
    print_begin(0); puts_("KEEPER_SMP_DONE schema=1 cpus="); putu(ncpus);
    puts_(" writer=0 epochs=15 failures=0"); print_end();
    for (u32 n = 0; n < 256; n++) pause();
    outl(0xF4, 0);
    for (;;) __asm__ volatile("cli; hlt");
}

// Exact existing SMP machine setup, without calling any broad-suite workload.
void kmain(u64 ram, u64 me) {
    if (me == 0) {
        ram_size = ram; next_frame = 32ULL << 20; boot_root = read_cr3() & ADDR;
        ncpus = inl(0xF5); tso_on = inl(0xF6) != 0;
        if ((ncpus != 2 && ncpus != 4) || !tso_on) { outl(0xF4, 1); for (;;) __asm__ volatile("cli; hlt"); }
        gdt[1] = 0x00AF9A000000FFFFULL; gdt[2] = 0x00CF92000000FFFFULL;
        gdt[3] = 0x00CFF2000000FFFFULL; gdt[4] = 0x00CFF2000000FFFFULL; gdt[5] = 0x00AFFA000000FFFFULL;
        set_gate(13, isr13); set_gate(14, isr14); set_gate(V_IPI, isr64);
        set_gate(V_PONG, isr65); set_gate(V_LEAVE, isr66); idt[V_LEAVE].attr |= 3 << 5;
        *pte_slot(boot_root, APIC_VA) = 0xFEE00000ULL | P | W | NX;
        make_roots(); __atomic_store_n(&tables_ready, 1, __ATOMIC_RELEASE);
    } else wait_flag(&tables_ready, (u32)me, "tables_timeout");
    u64 t = (u64)&tss[me];
    gdt[6 + 2 * me] = ((t & 0xffff) << 16) | ((t >> 16 & 0xff) << 32) | (0x89ULL << 40) | ((t >> 24 & 0xff) << 56) | (sizeof tss[0] - 1);
    gdt[7 + 2 * me] = t >> 32;
    struct { u16 limit; u64 base; } __attribute__((packed)) d = {sizeof gdt - 1, (u64)gdt}, i = {sizeof idt - 1, (u64)idt};
    __asm__ volatile("lgdt %0" : : "m"(d)); __asm__ volatile("lidt %0" : : "m"(i));
    tss[me].rsp[0] = (u64)kstacks[me] + sizeof kstacks[me]; __asm__ volatile("ltr %%ax" : : "a"(0x30 + 16 * (u32)me));
    wrmsr(0x1B, 0xFEE00000ULL | 1 << 11 | (me ? 0 : 1 << 8)); apic_wr(0xF0, 0x1FF);
    if (this_cpu() != me) die((u32)me, "apic_identity", 0);
    wrmsr(0xC0000080, 0xD01); wrmsr(0xC0000081, 0x0018000800000000ULL);
    wrmsr(0xC0000082, (u64)syscall_entry); wrmsr(0xC0000084, 0x200);
    percpu[me].syscall_rsp = (u64)sstacks[me] + sizeof sstacks[me];
    wrmsr(0xC0000101, (u64)&percpu[me]); wrmsr(0xC0000102, 0);
    if (!me) writer(); else reader((u32)me);
}
