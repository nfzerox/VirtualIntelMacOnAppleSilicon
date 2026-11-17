// Targeted2CPU same-value64PTE fixture. Compile with smpstubs.s and pte-same64-user.s.
// Includes only the unchanged broad-suite machine definitions; its workloads never run.
// Real first-use user data faults drive unlocked x86_walk(set_ad=true) A/D setters.
// Ordinary aligned guest PTE stores race those walkers; no guest atomic OR/CAS emulates A/D.
#define kmain same64_unused_broad_kmain
#define trap same64_unused_broad_trap
#include "smp.c"
#undef trap
#undef kmain
#include <stddef.h>

#define SHARED_TEST 0x7FFF10000000ULL
#define CONTROL_VA USER_VA
#define MAIL_VA (USER_VA + 0x2000)
#define A_BIT 0x20ULL
#define PHASES 19
#define AD_ROUNDS 128
#define AD_ATTEMPTS 64
#define DATA_FIRST_INDEX 64
#define PT_ALIAS_INDEX 63
#define PT_CODE_INDEX 400
#define ZERO_INDEX 511
#define DATA_VALUE 0x12345678ULL
#define D_BIT 0x40ULL
#define WAIT_TICKS 5000000000ULL // engine-run's existing 1 GHz guest TSC

typedef struct {
    u64 warm_target, target, epoch, wait_for_go;
    volatile u64 ready, go, warm, value, outcome, done;
    u64 deadline;
    volatile u64 timeout;
    u64 mode, iterations;
    volatile u64 race_started, loop_done, final_go;
} SameMail;
_Static_assert(sizeof(SameMail) == 136, "user mailbox size");
_Static_assert(offsetof(SameMail, ready) == 32 && offsetof(SameMail, go) == 40, "user gate offsets");
_Static_assert(offsetof(SameMail, warm) == 48 && offsetof(SameMail, value) == 56, "user result offsets");
_Static_assert(offsetof(SameMail, outcome) == 64 && offsetof(SameMail, done) == 72, "user outcome offsets");
_Static_assert(offsetof(SameMail, deadline) == 80 && offsetof(SameMail, timeout) == 88, "user watchdog offsets");
_Static_assert(offsetof(SameMail, mode) == 96 && offsetof(SameMail, iterations) == 104, "user data mode offsets");
_Static_assert(offsetof(SameMail, race_started) == 112 && offsetof(SameMail, loop_done) == 120 && offsetof(SameMail, final_go) == 128, "user race gate offsets");

typedef struct {
    const char *name;
    u32 warm_page, page, warm_value, value, fault_error, writes;
} SameCase;
static const SameCase cases[PHASES] = {
    {"same_value_preserve",   0, 0, 17, 17, 0, 1},
    {"remap",                 0, 0, 17, 33, 0, 1},
    {"neighbor_after_remap",  1, 1, 55, 55, 0, 0},
    {"synonym_after_remap",   2, 2, 17, 17, 0, 0},
    {"accessed_rearm",         0, 0, 33, 33, 0, 1},
    {"nx_revoke",              0, 0, 33, 0, 0x15, 1},
    {"nx_restore",             1, 0, 55, 33, 0, 1},
    {"us_revoke",              0, 0, 33, 0, 0x15, 1},
    {"us_restore",             1, 0, 55, 33, 0, 1},
    {"present_revoke",         0, 0, 33, 0, 0x14, 1},
    {"present_restore",        1, 0, 55, 33, 0, 1},
    {"identical_zero_flood",  0, 0, 33, 33, 0, 65},
    {"smc_remapped",           0, 0, 33, 49, 0, 1},
    {"smc_synonym",            2, 2, 17, 50, 0, 1},
    {"remapped_final",         0, 0, 49, 49, 0, 0},
    {"neighbor_final",         1, 1, 55, 55, 0, 0},
    {"inactive_zero_root",     0, 0, 49, 49, 0, 3},
    {"ad_walk_store_race",     1, 0, 55, DATA_VALUE, 0, AD_ROUNDS * (AD_ATTEMPTS + 1)},
    {"pt_code_alias_smc",      PT_ALIAS_INDEX, PT_ALIAS_INDEX, 71, 73, 0, 1},
};

static u64 root_a, root_b, shared_directory, frames[3], mail_frames[MAX_CPUS];
static u64 data_frame, tested_pt_frame, global_deadline;
static volatile u64 *data_slots, *inactive_root_slot;
static volatile u64 race_kernel_ready, race_shoot_ticket, race_ipi_ticket, race_peer_completed, race_release;
static volatile u64 *shared_slot, *burst_slots;
static SameMail *mail[MAX_CPUS];
static volatile u64 command_epoch, shoot_epoch, ipi_epoch[MAX_CPUS], completed_epoch[MAX_CPUS];
static volatile u32 peers_start, finish_readers, quiescent[MAX_CPUS];
static u64 fault_rip[MAX_CPUS], fault_cr2[MAX_CPUS], fault_err[MAX_CPUS];
static volatile u32 entered[MAX_CPUS];
extern char pte_same64_user_blob[], pte_same64_user_blob_end[];
extern u64 enter_user_args(u64, u64, u64, u64, u64);

static __attribute__((noreturn)) void die(u32 me, const char *why, u64 epoch) {
    // No print lock here: a broken/hung peer must not prevent fatal exit.
    puts_("PTE_SAME64_FAIL cpu="); putu(me); puts_(" epoch="); putu(epoch);
    puts_(" reason="); puts_(why); puts_("\n"); outl(0xF4, 1);
    for (;;) __asm__ volatile("cli; hlt");
}

static void print_begin(u32 me) {
    u64 until = rdtsc() + WAIT_TICKS;
    while (__atomic_exchange_n(&print_lock, 1, __ATOMIC_ACQUIRE)) {
        if ((rdtsc() >= until || rdtsc() >= global_deadline)) die(me, "print_timeout", command_epoch);
        pause();
    }
}
static void print_end(void) { puts_("\n"); unlock(&print_lock); }
static void wait_epoch(volatile u64 *p, u64 epoch, u32 me, const char *why) {
    u64 until = rdtsc() + WAIT_TICKS;
    for (;;) {
        u64 value = __atomic_load_n(p, __ATOMIC_ACQUIRE);
        if (value == epoch) return;
        if (value > epoch || (rdtsc() >= until || rdtsc() >= global_deadline)) die(me, why, epoch);
        pause();
    }
}
static void wait_flag(volatile u32 *p, u32 me, const char *why) {
    u64 until = rdtsc() + WAIT_TICKS;
    while (!__atomic_load_n(p, __ATOMIC_ACQUIRE)) {
        if ((rdtsc() >= until || rdtsc() >= global_deadline)) die(me, why, command_epoch);
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
    SameMail *m = mail[me];
    if (f->vector == V_IPI) {
        if (epoch == 18) {
            u64 ticket = __atomic_load_n(&race_shoot_ticket, __ATOMIC_ACQUIRE);
            u64 round = (ticket + 1) / 2;
            bool final = !(ticket & 1);
            u64 target = SHARED_TEST + (DATA_FIRST_INDEX + round - 1) * 4096;
            if (me != 1 || !entered[me] || !m || !ticket || ticket > 2 * AD_ROUNDS ||
                m->epoch != round || m->ready != round || m->target != target ||
                (f->cs & 3) != 3 || f->rip < CONTROL_VA ||
                f->rip >= CONTROL_VA + (u64)(pte_same64_user_blob_end - pte_same64_user_blob) ||
                race_ipi_ticket >= ticket || (final ?
                    (m->go != round || m->race_started != round || m->loop_done != round || m->final_go) :
                    (m->go || m->race_started || m->loop_done || m->final_go)))
                die(me, "race_ipi_identity", ticket);
            if (final) invlpg(target); // architectural A/D-clear shootdown, not a race-window walk substitute
            serialize(); percpu[me].ipi_count++; apic_wr(0xB0, 0);
            __atomic_store_n(&ipi_epoch[me], epoch, __ATOMIC_RELEASE);
            __atomic_store_n(&race_ipi_ticket, ticket, __ATOMIC_RELEASE);
            return;
        }
        u64 requested = __atomic_load_n(&shoot_epoch, __ATOMIC_ACQUIRE);
        if (!me || !entered[me] || !m || requested != epoch || !epoch || epoch > PHASES ||
            m->ready != epoch || (f->cs & 3) != 3 || f->rip < CONTROL_VA ||
            f->rip >= CONTROL_VA + (u64)(pte_same64_user_blob_end - pte_same64_user_blob) || ipi_epoch[me] >= epoch)
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
    burst_slots = (volatile u64 *)((u64)shared_slot & ~0xFFFULL) + ZERO_INDEX;
    tested_pt_frame = ((u64)shared_slot - PHYSMAP) & ~0xFFFULL;
    data_slots = (volatile u64 *)((u64)shared_slot & ~0xFFFULL) + DATA_FIRST_INDEX;
    inactive_root_slot = phys(root_a) + ((SHARED_TEST >> 39) & 511);
    data_frame = frame_alloc(); *phys(data_frame) = DATA_VALUE;
    for (u32 n = 0; n < AD_ROUNDS; n++) data_slots[n] = data_frame | P | W | U | NX;
    *pte_slot(root_a, SHARED_TEST + PT_ALIAS_INDEX * 4096) = tested_pt_frame | P | U;
    code_value(tested_pt_frame + PT_CODE_INDEX * 8, 71);
    if (DATA_FIRST_INDEX + AD_ROUNDS > PT_CODE_INDEX || PT_CODE_INDEX >= ZERO_INDEX ||
        ((u64)data_slots & 7) || ((u64)shared_slot & 7)) die(0, "disjoint_pt_layout", 0);
    if (shared_slot != pte_slot(root_a, SHARED_TEST) || pdpt_a == pdpt_b)
        die(0, "root_sharing_shape", 0);
    u64 control = frame_alloc();
    if ((u64)(pte_same64_user_blob_end - pte_same64_user_blob) > 4096) die(0, "control_blob_bound", 0);
    memcpy(phys(control), pte_same64_user_blob, (u64)(pte_same64_user_blob_end - pte_same64_user_blob));
    for (u32 r = 0; r < 2; r++) {
        u64 root = r ? root_b : root_a;
        *pte_slot(root, CONTROL_VA) = control | P | U;
        for (u32 cpu = 0; cpu < ncpus; cpu++) {
            if (!r) { mail_frames[cpu] = frame_alloc(); mail[cpu] = (SameMail *)phys(mail_frames[cpu]); }
            *pte_slot(root, MAIL_VA + cpu * 4096) = mail_frames[cpu] | P | W | U | NX;
            for (u32 p = 0; p < 16; p++)
                *pte_slot(root, USER_VA + 0x10000 + cpu * 0x10000 + p * 4096) = frame_alloc() | P | W | U | NX;
        }
    }
}

static void establish(void) {
    SameMail *m = mail[0];
    m->target = m->warm_target = SHARED_TEST;
    write_cr3(root_a); enter(0);
    if (m->value != 17) die(0, "a_user_warm", 0);
    memset(m, 0, sizeof(*m)); m->target = m->warm_target = SHARED_TEST;
    // Both transitions MUST be CPU0's: B-user replaces CPU0's A-user pin.
    write_cr3(root_b); enter(0);
    if (m->value != 17) die(0, "b_user_warm", 0);
    // A's code marks are keeper marks. Repin A-user after promotion, then
    // switch only to B-kernel: B-user would remove CPU0's A-user pin and let
    // inactive-keeper flood bypass the level0 skip we are trying to exercise.
    memset(m, 0, sizeof(*m)); m->target = m->warm_target = SHARED_TEST;
    write_cr3(root_a); enter(0);
    if (m->value != 17) die(0, "a_keeper_repin", 0);
    write_cr3(root_b);
    print_begin(0); puts_("PTE_SAME64_ESTABLISHED cpu=0 a_then_b=1 a_reentered=1 b_kernel_only=1 shared_pte=1 distinct_upper=1"); print_end();
    __atomic_store_n(&peers_start, 1, __ATOMIC_RELEASE);
}

#ifdef PTE_ATOMIC_FORMS
// The peer has warmed the mapping and is parked outside this PT. Exercise
// actual x86 atomics on the mirrored frame, including failed CAS and the
// upper32-bit NX field. Do not replace the separate ordinary-store A/D race.
static void atomic_pte_store(u64 value, u64 epoch) {
    volatile u32 *halves = (volatile u32 *)shared_slot;
    u64 old = *shared_slot;
    if (epoch & 1) {
        for (u32 i = 0; i < 2; i++) {
            u32 before = halves[i], expected = ~before;
            bool changed = __atomic_compare_exchange_n(&halves[i], &expected,
                (u32)(value >> (32 * i)), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            if (changed || expected != before || halves[i] != before)
                die(0, "failed_cas32_modified_pte_or_result", epoch);
            expected = before;
            if (!__atomic_compare_exchange_n(&halves[i], &expected,
                    (u32)(value >> (32 * i)), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || expected != before)
                die(0, "cas32_result", epoch);
        }
    } else if (epoch & 2) {
        u64 expected = ~old;
        if (__atomic_compare_exchange_n(shared_slot, &expected, value, false,
                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || expected != old || *shared_slot != old)
            die(0, "failed_cas64_modified_pte_or_result", epoch);
        if (!__atomic_compare_exchange_n(shared_slot, &expected, value, false,
                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || expected != old)
            die(0, "cas64_result", epoch);
    } else {
        for (u32 i = 0; i < 2; i++) {
            u32 before = halves[i];
            u32 result = __atomic_exchange_n(&halves[i], (u32)(value >> (32 * i)), __ATOMIC_SEQ_CST);
            if (result != before) die(0, "swap32_result", epoch);
        }
        if (__atomic_exchange_n(shared_slot, value, __ATOMIC_SEQ_CST) != value)
            die(0, "swap64_result", epoch);
    }
    if (*shared_slot != value) die(0, "atomic_final_pte", epoch);
    // Bit9 is architecturally available to software. Exercise each emitted
    // LSE RMW width while retaining PFN, presence and access permissions.
    if (value & 0x200) die(0, "test_software_bit_already_set", epoch);
#define CHECK_PTE_RMW(type, slot) do { \
    __atomic_fetch_or((slot), (type)0x200, __ATOMIC_SEQ_CST); \
    if (*shared_slot != (value | 0x200)) die(0, "or_pte", epoch); \
    __atomic_fetch_and((slot), (type)~0x200ULL, __ATOMIC_SEQ_CST); \
    if (*shared_slot != value) die(0, "and_pte", epoch); \
    __atomic_fetch_xor((slot), (type)0x200, __ATOMIC_SEQ_CST); \
    if (*shared_slot != (value | 0x200)) die(0, "xor_pte", epoch); \
    __atomic_fetch_xor((slot), (type)0x200, __ATOMIC_SEQ_CST); \
    __atomic_fetch_add((slot), (type)0x200, __ATOMIC_SEQ_CST); \
    if (*shared_slot != (value | 0x200)) die(0, "add_pte", epoch); \
    __atomic_fetch_add((slot), (type)-0x200, __ATOMIC_SEQ_CST); \
    if (*shared_slot != value) die(0, "rmw_final_pte", epoch); \
} while (0)
    CHECK_PTE_RMW(u32, halves);
    CHECK_PTE_RMW(u64, shared_slot);
#undef CHECK_PTE_RMW
}
#else
static void atomic_pte_store(u64 value, u64 epoch) { (void)epoch; *shared_slot = value; }
#endif

static void mutate(u64 epoch) {
    switch (epoch) {
        case 1: { u64 current = *shared_slot; atomic_pte_store(current, epoch); break; }
        case 2: case 5: case 7: case 9: case 11: atomic_pte_store(frames[1] | P | U, epoch); break;
        case 6: atomic_pte_store(frames[1] | P | U | NX, epoch); break;
        case 8: atomic_pte_store(frames[1] | P, epoch); break;
        case 10: atomic_pte_store(0, epoch); break;
        case 12:
            // SAME empty PTE,65ordinary stores. Flood accounting must not be skipped.
            if (*burst_slots) die(0, "zero_slot_not_empty", epoch);
            for (u64 n = 0; n < 65; n++) *burst_slots = 0;
            break;
        case 13: code_value(frames[1], 49); break;
        case 14: code_value(frames[0], 50); break;
        case 17: {
            // CPU0 is in B. A's USER upper slot is inactive; equal-zero must not
            // bypass root-view/keeper teardown. Both clear and restore are real stores.
            SameMail *m = mail[0]; memset(m, 0, sizeof(*m));
            m->target = m->warm_target = SHARED_TEST;
            // Enter B-user now (not merely B-kernel), explicitly unpinning A.
            write_cr3(root_b); enter(0);
            if (m->value != 49) die(0, "b_user_before_teardown", epoch);
            u64 saved = *inactive_root_slot;
            if (!(saved & P) || read_cr3() != root_b) die(0, "inactive_root_identity", epoch);
            *inactive_root_slot = 0; *inactive_root_slot = 0; *inactive_root_slot = saved;
            memset(m, 0, sizeof(*m)); m->target = m->warm_target = SHARED_TEST;
            write_cr3(root_a); enter(0);
            if (m->value != 49 || m->outcome != 1) die(0, "inactive_root_restore", epoch);
            write_cr3(root_b);
            break;
        }
        case 19:
            // The physical page IS the mirrored guest PT and has just been
            // executed through a user code alias. PI_CODE must win over PI_PT.
            code_value(tested_pt_frame + PT_CODE_INDEX * 8, 73);
            break;
        default: break;
    }
}

static void ad_reader(void) {
    for (u64 round = 1; round <= AD_ROUNDS; round++) {
        SameMail *m = mail[1]; memset(m, 0, sizeof(*m));
        m->warm_target = SHARED_TEST + 4096;
        m->target = SHARED_TEST + (DATA_FIRST_INDEX + round - 1) * 4096;
        m->epoch = round; m->wait_for_go = 1; m->mode = 2; m->iterations = AD_ATTEMPTS;
        m->deadline = rdtsc() + WAIT_TICKS;
        if (m->deadline > global_deadline) m->deadline = global_deadline;
        __atomic_store_n(&race_kernel_ready, round, __ATOMIC_RELEASE);
        enter(1);
        if (m->ready != round || m->go != round || race_ipi_ticket != 2 * round ||
            m->race_started != round || m->loop_done != round || m->final_go != round ||
            m->done != round || m->warm != 55 || m->value != DATA_VALUE || m->outcome != 1)
            die(1, "race_user_result", round);
        __atomic_store_n(&race_peer_completed, round, __ATOMIC_RELEASE);
        wait_epoch(&race_release, round, 1, "race_release_timeout");
    }
}

static void ad_writer(void) {
    const u64 base = data_frame | P | W | U | NX;
    for (u64 round = 1; round <= AD_ROUNDS; round++) {
        SameMail *m = mail[1];
        wait_epoch(&race_kernel_ready, round, 0, "race_kernel_ready_timeout");
        wait_epoch(&m->ready, round, 0, "race_user_ready_timeout");
        __atomic_store_n(&race_shoot_ticket, 2 * round - 1, __ATOMIC_RELEASE);
        send_ipi(1, V_IPI);
        wait_epoch(&race_ipi_ticket, 2 * round - 1, 0, "race_ipi_timeout");
        if (data_slots[round - 1] != base) die(0, "first_use_pte_not_pristine", round);
        __atomic_store_n(&m->go, round, __ATOMIC_RELEASE);
        wait_epoch(&m->race_started, round, 0, "race_start_timeout");
        // The normal fixture uses MOV64. The atomic variant races32/64-bit
        // exchanges against the same real, lock-free A/D walker, not a guest
        // atomic OR/CAS standing in for that walker.
        // A scheduler may serialize some/all attempts; no overlap hit is claimed.
        for (u64 n = 0; n < AD_ATTEMPTS; n++) {
#ifdef PTE_ATOMIC_FORMS
            if (n & 1) __atomic_exchange_n((volatile u32 *)&data_slots[round - 1], (u32)base, __ATOMIC_SEQ_CST);
            else __atomic_exchange_n(&data_slots[round - 1], base, __ATOMIC_SEQ_CST);
#else
            data_slots[round - 1] = base;
#endif
        }
        wait_epoch(&m->loop_done, round, 0, "race_loop_timeout");
        // Peer is parked outside the tested PT. Clear then perform the x86
        // architectural shootdown before requiring the quiescent access to rearm A/D.
#ifdef PTE_ATOMIC_FORMS
        __atomic_exchange_n(&data_slots[round - 1], base, __ATOMIC_SEQ_CST);
#else
        data_slots[round - 1] = base;
#endif
        __atomic_store_n(&race_shoot_ticket, 2 * round, __ATOMIC_RELEASE);
        send_ipi(1, V_IPI);
        wait_epoch(&race_ipi_ticket, 2 * round, 0, "race_final_ipi_timeout");
        __atomic_store_n(&m->final_go, round, __ATOMIC_RELEASE);
        wait_epoch(&race_peer_completed, round, 0, "race_final_timeout");
        u64 observed = data_slots[round - 1];
        if (observed != (base | A_BIT | D_BIT) || *phys(data_frame) != DATA_VALUE)
            die(0, "quiescent_ad_pfn_permissions_data", round);
        print_begin(0); puts_("PTE_SAME64_AD round="); putu(round);
        puts_(" cpu=1 first_use=1 read_write_attempts=65 pte_clears=65 final_shootdown=1 final_accessed=1 final_dirty=1 pfn_permissions=1 value="); putu(DATA_VALUE);
        puts_(" failures=0"); print_end();
        __atomic_store_n(&race_release, round, __ATOMIC_RELEASE);
    }
}

static void result_line(u32 me, u64 epoch) {
    SameMail *m = mail[me];
    print_begin(me);
    puts_("PTE_SAME64_RESULT epoch="); putu(epoch); puts_(" cpu="); putu(me);
    puts_(" warm="); putu(m->warm); puts_(" outcome="); puts_(m->outcome == 2 ? "fault" : "value");
    puts_(" value="); putu(m->value); puts_(" rip="); putu(fault_rip[me]);
    puts_(" cr2="); putu(fault_cr2[me]); puts_(" err="); putu(fault_err[me]); print_end();
}

static void reader(u32 me) {
    wait_flag(&peers_start, me, "start_timeout"); write_cr3(root_b);
    for (u64 epoch = 1; epoch <= PHASES; epoch++) {
        wait_epoch(&command_epoch, epoch, me, "command_timeout");
        if (epoch == 18) {
            ad_reader();
            // Round128 READY must not masquerade as a future ordinary phase.
            __atomic_store_n(&mail[me]->ready, epoch, __ATOMIC_RELEASE);
            __atomic_store_n(&completed_epoch[me], epoch, __ATOMIC_RELEASE); continue;
        }
        const SameCase *c = &cases[epoch - 1]; SameMail *m = mail[me];
        memset(m, 0, sizeof(*m)); fault_rip[me] = fault_cr2[me] = fault_err[me] = 0;
        m->warm_target = SHARED_TEST + c->warm_page * 4096;
        m->target = SHARED_TEST + c->page * 4096;
        if (epoch == 19) m->warm_target += PT_CODE_INDEX * 8, m->target += PT_CODE_INDEX * 8;
        m->epoch = epoch; m->wait_for_go = 1;
        m->deadline = rdtsc() + WAIT_TICKS;
        if (m->deadline > global_deadline) m->deadline = global_deadline;
        enter(me);
        if (m->ready != epoch || ipi_epoch[me] != epoch || m->go != epoch || m->done != epoch ||
            m->warm != c->warm_value || (c->fault_error ? m->outcome != 2 : (m->outcome != 1 || m->value != c->value)))
            die(me, "checked_result", epoch);
        result_line(me, epoch);
        __atomic_store_n(&completed_epoch[me], epoch, __ATOMIC_RELEASE);
    }
    wait_flag(&finish_readers, me, "finish_timeout");
    print_begin(me); puts_("PTE_SAME64_CPU cpu="); putu(me);
    puts_(" role=reader ready_epoch="); putu(mail[me]->ready); puts_(" ack_epoch="); putu(ipi_epoch[me]);
    puts_(" done_epoch="); putu(completed_epoch[me]); puts_(" parked=1 failures=0"); print_end();
    cli(); __atomic_store_n(&quiescent[me], 1, __ATOMIC_RELEASE);
    outl(0xF4, 0); // Every secondary must have an independently retained successful host outcome.
    for (;;) __asm__ volatile("cli; hlt");
}

static void writer(void) {
    establish();
    for (u64 epoch = 1; epoch <= PHASES; epoch++) {
        const SameCase *c = &cases[epoch - 1];
        __atomic_store_n(&command_epoch, epoch, __ATOMIC_RELEASE);
        if (epoch == 18) {
            ad_writer(); wait_epoch(&completed_epoch[1], epoch, 0, "race_phase_done_timeout");
            print_begin(0); puts_("PTE_SAME64_PHASE epoch=18 case=ad_walk_store_race peers=1 writes=8320 completed=1"); print_end();
            continue;
        }
        for (u32 cpu = 1; cpu < ncpus; cpu++) {
            wait_epoch(&mail[cpu]->ready, epoch, 0, "ready_timeout");
            if (mail[cpu]->warm != c->warm_value) die(0, "peer_warm_result", epoch);
        }
        mutate(epoch);
        // SMC-only epochs intentionally do NOT call INVLPG: their translation
        // invalidation must come from the physical code-store path itself.
        shoot_va = (epoch == 2 || (epoch >= 5 && epoch <= 11)) ? SHARED_TEST : 0;
        if (shoot_va) invlpg(shoot_va);
        __atomic_store_n(&shoot_epoch, epoch, __ATOMIC_RELEASE);
        for (u32 cpu = 1; cpu < ncpus; cpu++) send_ipi(cpu, V_IPI);
        for (u32 cpu = 1; cpu < ncpus; cpu++) wait_epoch(&ipi_epoch[cpu], epoch, 0, "shootdown_timeout");
        for (u32 cpu = 1; cpu < ncpus; cpu++) __atomic_store_n(&mail[cpu]->go, epoch, __ATOMIC_RELEASE);
        for (u32 cpu = 1; cpu < ncpus; cpu++) wait_epoch(&completed_epoch[cpu], epoch, 0, "done_timeout");
        if (epoch == 5 && !(*shared_slot & A_BIT)) die(0, "accessed_not_rearmed", epoch);
        print_begin(0); puts_("PTE_SAME64_PHASE epoch="); putu(epoch); puts_(" case="); puts_(c->name);
        puts_(" peers="); putu(ncpus - 1); puts_(" writes="); putu(c->writes);
        puts_(" completed="); putu(ncpus - 1); print_end();
    }
    __atomic_store_n(&finish_readers, 1, __ATOMIC_RELEASE);
    for (u32 cpu = 1; cpu < ncpus; cpu++) wait_flag(&quiescent[cpu], 0, "quiesce_timeout");
    __atomic_store_n(&quiescent[0], 1, __ATOMIC_RELEASE);
    print_begin(0); puts_("PTE_SAME64_CPU cpu=0 role=writer ready_epoch=0 ack_epoch=0 done_epoch=19 parked=1 failures=0"); print_end();
    print_begin(0); puts_("PTE_SAME64_DONE schema=1 cpus="); putu(ncpus);
    puts_(" writer=0 epochs=19 failures=0"); print_end();
#ifdef PTE_ATOMIC_FORMS
    print_begin(0); puts_("PTE_ATOMIC_FORMS widths=32,64 cas_success=1 cas_failure=1 swap=1 ad_walk_rounds=128 failures=0"); print_end();
#endif
    for (u32 n = 0; n < 256; n++) pause();
    outl(0xF4, 0);
    for (;;) __asm__ volatile("cli; hlt");
}

// Exact existing SMP machine setup, without calling any broad-suite workload.
void kmain(u64 ram, u64 me) {
    if (me == 0) {
        global_deadline = rdtsc() + 20000000000ULL;
        ram_size = ram; next_frame = 32ULL << 20; boot_root = read_cr3() & ADDR;
        ncpus = inl(0xF5); tso_on = inl(0xF6) != 0;
        if ((ncpus != 2) || !tso_on) { outl(0xF4, 1); for (;;) __asm__ volatile("cli; hlt"); }
        gdt[1] = 0x00AF9A000000FFFFULL; gdt[2] = 0x00CF92000000FFFFULL;
        gdt[3] = 0x00CFF2000000FFFFULL; gdt[4] = 0x00CFF2000000FFFFULL; gdt[5] = 0x00AFFA000000FFFFULL;
        set_gate(13, isr13); set_gate(14, isr14); set_gate(V_IPI, isr64);
        set_gate(V_PONG, isr65); set_gate(V_LEAVE, isr66); idt[V_LEAVE].attr |= 3 << 5;
        *pte_slot(boot_root, APIC_VA) = 0xFEE00000ULL | P | W | NX;
        make_roots(); __atomic_store_n(&tables_ready, 1, __ATOMIC_RELEASE);
    } else {
        // Before the release/acquire publication, global_deadline is not yet
        // initialized. This boot wait uses only its own5s clock bound.
        u64 until = rdtsc() + WAIT_TICKS;
        while (!__atomic_load_n(&tables_ready, __ATOMIC_ACQUIRE)) {
            if (rdtsc() >= until) die((u32)me, "tables_timeout", 0);
            pause();
        }
    }
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
