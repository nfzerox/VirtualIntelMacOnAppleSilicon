// Consumers for the cross-modifying-code probe, at EL1 with the MMU on.
// x0 cpu index, x1 PUB (u64: gen << 32 | slot), x2 SITE base, x3 flags, x4 this cpu's counters, x6 STOP, x7 GO
// flags: bit0 isb before the call, bit1 ic ivau of the target by the consumer before the call
// counters (u64): [0] calls, [1] wrong value (stale valid code), [2] unknown exceptions, [3] other exceptions
    .text
    .p2align 12
    .globl _x_start
_x_start:
    .globl _x_consume
_x_consume:
    mov x19, x4
    mov x20, xzr // last seen publication
1:  ldr x9, [x7]
    cbz x9, 1b
2:  ldr x9, [x6]
    cbnz x9, 9f
    ldar x10, [x1]
    cmp x10, x20
    b.eq 2b
    mov x20, x10
    and x11, x10, #0xffffffff
    lsr x12, x10, #32
    add x13, x2, x11, lsl #7 // 128-byte slots
    tbz x3, #1, 3f
    ic ivau, x13
    dsb ish
3:  tbz x3, #0, 4f
    isb
4:  blr x13
    ldr x9, [x19]
    add x9, x9, #1
    str x9, [x19]
    and x12, x12, #0xffffffff
    cmp x0, x12
    b.eq 2b
    ldr x9, [x19, #8]
    add x9, x9, #1
    str x9, [x19, #8]
    b 2b
9:  hvc #0
    b 9b

    .p2align 11
    .globl _x_vectors
_x_vectors:
    .rept 4
    b xbad
    .p2align 7
    .endr
    // current EL, SPx: sync
    b xsync
    .p2align 7
    .rept 11
    b xbad
    .p2align 7
    .endr
xsync:
    mrs x9, esr_el1
    lsr x9, x9, #26
    cbnz x9, xbad
    ldr x9, [x19, #16]
    add x9, x9, #1
    str x9, [x19, #16]
    eret // retry the same instruction, as the engine now does
xbad:
    mrs x0, esr_el1
    mrs x1, elr_el1
    mrs x2, far_el1
    hvc #2
    b xbad
    .globl _x_end
_x_end:
