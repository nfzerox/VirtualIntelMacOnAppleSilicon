// Standalone --kernel image: each address space exits and leaves its page directory to a new one, as an OS recycles a dead process's tables.
#define kmain shared_table_reuse_unused_base_kmain
#include "kernel.c"
#undef kmain

#define SHARED_TEST 0x7FFF10000000ULL
#define ELSEWHERE 0x40000000ULL
#define GENERATIONS 400 // more than MAX_VIEWS

static void exit_code(u64 frame, u32 value) {
    // mov imm32,edi; mov $1,eax; syscall; ud2
    u8 *bytes = (u8 *)phys(frame);
    bytes[0] = 0xBF;
    for (u32 i = 0; i < 4; i++) bytes[1 + i] = (u8)(value >> (i * 8));
    bytes[5] = 0xB8; bytes[6] = 1; bytes[7] = bytes[8] = bytes[9] = 0;
    bytes[10] = 0x0F; bytes[11] = 0x05; bytes[12] = 0x0F; bytes[13] = 0x0B;
}

static u64 execute(u64 root, u64 va) {
    extern u64 enter_user_args(u64, u64, u64, u64, u64, u64);
    switch_to(root, 0, false);
    user_exit_status = ~0ULL;
    enter_user_args(va, USTACK_TOP - 64, 2, 0, 0, 0);
    return user_exit_status;
}

static volatile u64 *directory_slot(u64 root) {
    u64 pdpt = phys(root)[(SHARED_TEST >> 39) & 511] & ADDR;
    return &phys(pdpt)[(SHARED_TEST >> 30) & 511];
}

static void inherit_directory(u64 root, u64 directory) {
    u64 pdpt = frame_alloc();
    phys(root)[(SHARED_TEST >> 39) & 511] = pdpt | P | W | U;
    phys(pdpt)[(SHARED_TEST >> 30) & 511] = directory | P | W | U;
}

void kmain(u64 ram) {
    ram_size = ram; next_frame = 32ULL << 20;
    setup_tables();
    memcpy(kernel_pml4, phys(read_cr3() & ADDR), 4096);
    fault_mode = M_EXIT_ON_FAULT;
    puts_("shared-table-reuse standalone fixture; requires ENGINE_SHARE=1\n");
    for (u64 n = 0; n < GENERATIONS; n++) {
        u64 old_code = frame_alloc(), new_code = frame_alloc(), elsewhere = frame_alloc();
        exit_code(old_code, (u32)n);
        exit_code(new_code, (u32)n + 1000);
        exit_code(elsewhere, 7);
        u64 exiting = new_space(), heir = new_space();
        map4k(exiting, SHARED_TEST, old_code, P | U);
        CHECK(execute(exiting, SHARED_TEST) == n, "generation %d: first owner", n);
        map4k(heir, ELSEWHERE, elsewhere, P | U);
        CHECK(execute(heir, ELSEWHERE) == 7, "generation %d: next owner elsewhere", n);
        u64 directory = *directory_slot(exiting) & ADDR;
        memset(phys(directory), 0, 4096); // the exiting tables still point at it, as an OS frees them
        inherit_directory(heir, directory);
        map4k(heir, SHARED_TEST, new_code, P | U);
        CHECK(execute(heir, SHARED_TEST) == n + 1000, "generation %d: next owner", n);
    }
    kprintf("shared-table-reuse done, %d generations, %d failures\n", (u64)GENERATIONS, (u64)fails);
    kexit(fails ? 1 : 0);
}
