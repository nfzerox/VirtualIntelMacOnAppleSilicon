// An actual pending LAPIC interrupt must wait for the instruction after STI,
// even if the translator needs TCG to execute that instruction.
#define kmain irq_unused_kmain
#define trap_dispatch irq_base_dispatch
#include "kernel.c"
#undef trap_dispatch
#undef kmain

static volatile u64 irq_rip, irq_cs;
extern void enter_shadow_sysexit(u64 rip, u64 rsp);
__asm__(".text\n.globl _enter_shadow_sysexit\n_enter_shadow_sysexit:\n"
        "pushq %rbx\n pushq %rbp\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
        "movq %rsp, %gs:16\n movl %edi, %edx\n movl %esi, %ecx\n swapgs\n"
        "sti\n sysexitl\n");

void trap_dispatch(Frame *frame) {
    if (frame->vector == 33) {
        irq_rip = frame->rip;
        irq_cs = frame->cs;
    }
    if (frame->vector == 128 && (frame->cs & 3))
        return_from_user();
    irq_base_dispatch(frame);
}

void kmain(u64 ram) {
    ram_size = ram;
    next_frame = 32ULL << 20;
    setup_tables();
    // The ROM entered with CS 0x18, which setup_tables repurposed for user
    // space. Reload the new kernel code selector before taking an IRQ.
    __asm__ volatile("lea 1f(%%rip), %%rax; pushq $8; pushq %%rax; lretq; 1:"
                     : : : "rax", "memory");
    u64 root = read_cr3() & ADDR;
    map4k(root, APIC_VA, 0xFEE00000ULL, P | W | NX);
    invlpg(APIC_VA);
    wrmsr(0x1B, 0xFEE00000ULL | 1 << 11 | 1 << 8);
    apic_wr(0xF0, 0x1FF);
    set_gate(0x22, isr33, 0, 0);
    apic_eoi_needed = 1;

    for (unsigned i = 0; i < 16; i++) {
        u64 after, cr8;
        irq_rip = 0;
        apic_wr(0x300, 1 << 18 | 0x22);
        CHECK(apic_rd(0x210) & (1 << 2), "self IPI not pending");
        __asm__ volatile("lea 1f(%%rip), %0; sti; mov %%cr8, %1; 1: cli"
                         : "=&r"(after), "=&r"(cr8) : : "memory");
        CHECK(irq_rip == after, "IRQ at %x, expected %x after CR8 read", irq_rip, after);
        CHECK(cr8 == 0, "CR8 read %x", cr8);
    }
    // Mirror Tiger's STI/SYSEXIT return to compatibility mode. Repeat
    // with a valid non-flat ES, which deliberately exercises its fallback.
    gdt[3] = 0x00CFFA000000FFFFULL;
    gdt[5] = 0x0040F2000010FFFFULL; // user ES, base 0x10, byte granularity
    wrmsr(0x174, 8);
    u64 user_code = frame_alloc(), user_stack = 0x70000000;
    *(u32 *)phys(user_code) = 0x0b0f80cd; // int 0x80; ud2
    map4k(root, UCODE, user_code, P | U);
    map4k(root, user_stack - 4096, frame_alloc(), P | W | U | NX);
    for (unsigned i = 0; i < 16; i++) {
        __asm__ volatile("mov %0, %%es" : : "r"((u16)(i & 1 ? 0x2b : 0x23)));
        irq_rip = irq_cs = 0;
        apic_wr(0x300, 1 << 18 | 0x22);
        enter_shadow_sysexit(UCODE, user_stack - 16);
        CHECK(irq_rip == UCODE && irq_cs == 0x1b, "SYSEXIT interrupt frame: CS %x RIP %x",
              irq_cs, irq_rip);
    }
    if (!fails)
        puts_("PASS pending IRQ waits for the protected fallback instruction\n");
    kexit(fails ? 1 : 0);
}
