// Standalone --kernel image: real user-domain keeper, never a synthetic View or monitor-memory write.
// Reuse the established boot/GDT/CR3/fault/return machinery; the broad base suite is not run.
#define kmain keeper_unused_base_kmain
#include "kernel.c"
#undef kmain

#define SHARED_TEST 0x7FFF10000000ULL
#define A_BIT 0x20ULL

static u64 root_a, root_b, directory;
static u64 code_old, code_new, code_neighbor;

static void exit_code(u64 frame, u32 value) {
    // mov imm32,edi; mov $1,eax; syscall; ud2. Exit returns the supplied value through syscall_dispatch.
    u8 *bytes = (u8 *)phys(frame);
    bytes[0] = 0xBF;
    for (u32 i = 0; i < 4; i++) bytes[1 + i] = (u8)(value >> (i * 8));
    bytes[5] = 0xB8; bytes[6] = 1; bytes[7] = bytes[8] = bytes[9] = 0;
    bytes[10] = 0x0F; bytes[11] = 0x05; bytes[12] = 0x0F; bytes[13] = 0x0B;
}

static void share_directory(u64 root) {
    // Own PML4/PDPT paths, physically identical PD and its children. This is what shared_row observes.
    u64 pdpt = frame_alloc();
    phys(root)[(SHARED_TEST >> 39) & 511] = pdpt | P | W | U;
    phys(pdpt)[(SHARED_TEST >> 30) & 511] = directory | P | W | U;
}

static u64 execute(u64 root, u64 va) {
    extern u64 enter_user_args(u64, u64, u64, u64, u64, u64);
    switch_to(root, 0, false);
    user_exit_status = ~0ULL;
    enter_user_args(va, USTACK_TOP - 64, 2, 0, 0, 0);
    return user_exit_status;
}

static void expect_value(u64 va, u64 value, const char *phase) {
    u64 got = execute(root_b, va);
    CHECK(got == value, "%s value %x expected %x", phase, got, value);
}

static void expect_fetch_fault(u64 va, const char *phase) {
    u64 got = execute(root_b, va);
    CHECK(got == 0x10E && (last_err & 0x14) == 0x14 && last_cr2 == va,
          "%s fault status %x err %x cr2 %x", phase, got, last_err, last_cr2);
}

static void establish(void) {
    code_old = frame_alloc(); code_new = frame_alloc(); code_neighbor = frame_alloc();
    exit_code(code_old, 17); exit_code(code_new, 33); exit_code(code_neighbor, 55);
    root_a = new_space(); root_b = new_space();
    map4k(root_a, SHARED_TEST, code_old, P | U);
    map4k(root_a, SHARED_TEST + 4096, code_neighbor, P | U);
    map4k(root_a, SHARED_TEST + 8192, code_old, P | U); // physical synonym for SMC checks
    u64 pdpt = phys(root_a)[(SHARED_TEST >> 39) & 511] & ADDR;
    directory = phys(pdpt)[(SHARED_TEST >> 30) & 511] & ADDR;
    share_directory(root_b);
    CHECK(((*pte_slot(root_a, SHARED_TEST, false)) & ADDR) == code_old, "A mapping changed");
    CHECK(pte_slot(root_a, SHARED_TEST, false) == pte_slot(root_b, SHARED_TEST, false), "roots do not share a real PTE");
    // A translates first; B's translation observes the same PD from a different CR3 and promotes A-user to keeper.
    CHECK(execute(root_a, SHARED_TEST) == 17, "first user-domain execution failed");
    expect_value(SHARED_TEST, 17, "second root shared domain");
    expect_value(SHARED_TEST + 4096, 55, "neighbor initial");
    expect_value(SHARED_TEST + 8192, 17, "synonym initial");
    // B is now the sole user pin. All later operations run as kernel B or user B, leaving A-user inactive.
    puts_("PHASE keeper established via A then B; host sharedDirs/suppression counters must attest path\n");
}

static void mappings_and_permissions(void) {
    volatile u64 *slot = pte_slot(root_b, SHARED_TEST, false);
    *slot = code_new | P | U;
    expect_value(SHARED_TEST, 33, "physical remap");
    expect_value(SHARED_TEST + 4096, 55, "neighbor after remap");
    expect_value(SHARED_TEST + 8192, 17, "old-frame synonym after remap");
    *slot = code_new | P | U; // clear A while preserving the mapping
    expect_value(SHARED_TEST, 33, "A rearm");
    CHECK((*slot & A_BIT) != 0, "instruction fetch did not rearm guest A bit");
    *slot = code_new | P | U | NX;
    expect_fetch_fault(SHARED_TEST, "NX revoke");
    *slot = code_new | P; // supervisor-only at identical PFN
    expect_fetch_fault(SHARED_TEST, "U/S revoke");
    *slot = 0;
    expect_fetch_fault(SHARED_TEST, "present revoke");
    *slot = code_new | P | U;
    expect_value(SHARED_TEST, 33, "permission restore");
    expect_value(SHARED_TEST + 4096, 55, "neighbor after permissions");
    puts_("PASS keeper remap/NX/U-S/present/A-rearm/neighbor checks\n");
}

static void fallback_and_smc(void) {
    // Re-execution refreshes A keeper's table descriptor, resetting its write count.
    expect_value(SHARED_TEST, 33, "fallback warm");
    volatile u64 *table = (volatile u64 *)((u64)pte_slot(root_b, SHARED_TEST, false) & ~0xFFFULL);
    // No user execution or table shadow walk between these stores. Store65 must take the original threshold fallback.
    for (u64 n = 0; n < 65; n++) table[128 + n] = code_neighbor | P | U | NX;
    puts_("PHASE 65-store burst complete; host thresholdFallbacks must be nonzero\n");
    expect_value(SHARED_TEST, 33, "after fallback");
    exit_code(code_new, 49);
    expect_value(SHARED_TEST, 49, "SMC on remapped frame");
    exit_code(code_old, 50);
    expect_value(SHARED_TEST + 8192, 50, "SMC on physical synonym");
    expect_value(SHARED_TEST, 49, "other frame after synonym SMC");
    expect_value(SHARED_TEST + 4096, 55, "neighbor after SMC");
    puts_("PASS keeper threshold-burst and physical-SMC functional checks\n");
}

static void teardown_and_pool_pressure(void) {
    u64 spare = SHARED_TEST + (2ULL << 20), frame = frame_alloc();
    exit_code(frame, 61);
    map4k(root_b, spare, frame, P | U);
    expect_value(spare, 61, "teardown initial");
    u64 old_table = phys(directory)[(spare >> 21) & 511] & ADDR;
    phys(directory)[(spare >> 21) & 511] = 0; // real table unlink: must retire every child mark/mirror
    expect_fetch_fault(spare, "unlinked table");
    for (u64 i = 0; i < 512; i++) ((volatile u64 *)phys(old_table))[i] = 0xA5A5000000000000ULL | i;
    map4k(root_b, spare, frame, P | U); // a newly allocated PT, same VA/frame, independently revalidated
    expect_value(spare, 61, "new table after old table reused as data");
    // With a low ENGINE_POOL_PAGES, real inactive views create reclamation pressure. Default pool need not reclaim.
    for (u64 n = 0; n < 24; n++) {
        u64 other = new_space(); share_directory(other);
        CHECK(execute(other, SHARED_TEST) == 49, "pool-pressure root %d result changed", n);
    }
    expect_value(SHARED_TEST, 49, "return after pool pressure");
    expect_value(spare, 61, "spare after pool pressure");
    puts_("PASS keeper table unlink/reuse and multi-root pool-pressure functional checks\n");
}

void kmain(u64 ram) {
    ram_size = ram; next_frame = 32ULL << 20;
    setup_tables();
    memcpy(kernel_pml4, phys(read_cr3() & ADDR), 4096);
    fault_mode = M_EXIT_ON_FAULT;
    puts_("keeper-inactive standalone fixture; requires ENGINE_SHARE=1 for targeted coverage\n");
    establish();
    mappings_and_permissions();
    fallback_and_smc();
    teardown_and_pool_pressure();
    kprintf("keeper fixture done, %d functional failures; host counter gates still required\n", (u64)fails);
    kexit(fails ? 1 : 0);
}
