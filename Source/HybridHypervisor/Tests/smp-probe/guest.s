// Routines for smp-probe, run at EL1 with the engine's MMU and EnTSO settings. Arguments in x0-x7, results in x0-x3, hvc #0 ends a routine.
// x6: a stop flag for routines that run until told, x7: a go flag, so vCPUs start together. x25-x28 belong to the vector.
.macro WAITGO
1:  ldr x8, [x7]
    cbz x8, 1b
.endm
.macro DELAY reg
    cbz \reg, 9f
    mov x9, \reg
8:  subs x9, x9, #1
    b.ne 8b
9:
.endm

    .text
    .p2align 12
    .globl _g_start
_g_start:
    .globl _g_vectors
_g_vectors:
    .rept 4
    .p2align 7
    b .
    .endr
    .p2align 7
    b vec_sync
    .p2align 7
    b .
    .p2align 7
    b .
    .p2align 7
    b .
    .rept 8
    .p2align 7
    b .
    .endr

vec_sync:
    mrs x28, esr_el1
    lsr x28, x28, #26
    cmp x28, #0x15
    b.eq on_svc
    cmp x28, #0x25
    b.eq on_abort
    // Anything else is what a torn or stale instruction would look like: report it.
    mov x0, x28
    mrs x1, elr_el1
    mrs x2, far_el1
    hvc #2
    b .
on_svc:
    mrs x28, cntvct_el0
    str x28, [x27] // x27: the patched site's flag
    eret
on_abort:
    add x26, x26, #1
    msr elr_el1, x25
    eret

    .globl _g_nolock
_g_nolock: // x0 n, x2 hold, x3 gap
    WAITGO
1:  DELAY x2
    DELAY x3
    subs x0, x0, #1
    b.ne 1b
    hvc #0

    .globl _g_lock
_g_lock: // x0 n, x1 lock, x2 hold, x3 gap
    WAITGO
    mov x10, #0 // retries
    mov w12, #1
1:  ldr w11, [x1]
    cbnz w11, 2f
    mov w11, #0
    casa w11, w12, [x1]
    cbz w11, 3f
2:  add x10, x10, #1
    b 1b
3:  DELAY x2
    stlr wzr, [x1]
    DELAY x3
    subs x0, x0, #1
    b.ne 1b
    mov x0, x10
    hvc #0

    .globl _g_ticket
_g_ticket: // x0 n, x1 lock (next in the low half, owner in the high half), x2 hold, x3 gap
    WAITGO
    mov w12, #1
1:  ldadda w12, w11, [x1]
2:  ldr w13, [x1, #4]
    cmp w11, w13
    b.ne 2b
    DELAY x2
    add w13, w13, #1
    add x14, x1, #4
    stlr w13, [x14]
    DELAY x3
    subs x0, x0, #1
    b.ne 1b
    hvc #0

    .globl _g_load
_g_load: // x0 n, x1 word to compare, as an exit path would check an epoch
    WAITGO
    mov x10, #0
1:  ldr w11, [x1]
    cmp w11, w10
    b.eq 2f
    mov w10, w11
2:  subs x0, x0, #1
    b.ne 1b
    hvc #0

    .globl _g_load_none
_g_load_none: // the same loop with a register compare, the baseline for the load above
    WAITGO
    mov x10, #0
    mov x11, #0
1:  cmp w11, w10
    b.eq 2f
    mov w10, w11
2:  subs x0, x0, #1
    b.ne 1b
    hvc #0

    .globl _g_ldset
_g_ldset: // x0 n, x1 word: a walker setting an accessed bit atomically
    WAITGO
    mov x12, #0x20
1:  ldset x12, x11, [x1]
    subs x0, x0, #1
    b.ne 1b
    hvc #0

    .globl _g_writer
_g_writer: // x1 word, x2 gap, until stop: another vCPU bumping the epoch
    WAITGO
    mov x10, #1
1:  add x10, x10, #1
    str x10, [x1]
    DELAY x2
    ldr x8, [x6]
    cbz x8, 1b
    hvc #0

    .globl _g_spin
_g_spin: // until stop, no memory traffic beyond the flag
    WAITGO
1:  ldr x8, [x6]
    cbz x8, 1b
    hvc #0

    .globl _g_hammer
_g_hammer: // x1 window, x2 pages: loads through many translations, so the TLB is full of entries an invalidate must search
    WAITGO
    mov x10, #0
1:  add x11, x1, x10, lsl #12
    ldr x12, [x11]
    add x10, x10, #1
    cmp x10, x2
    csel x10, xzr, x10, eq
    ldr x8, [x6]
    cbz x8, 1b
    hvc #0

    .globl _g_tlbi_va
_g_tlbi_va: // x0 n, x1 operand
    WAITGO
1:  dsb ishst
    tlbi vae1is, x1
    dsb ish
    isb
    subs x0, x0, #1
    b.ne 1b
    hvc #0

    .globl _g_tlbi_asid
_g_tlbi_asid:
    WAITGO
1:  dsb ishst
    tlbi aside1is, x1
    dsb ish
    isb
    subs x0, x0, #1
    b.ne 1b
    hvc #0

    .globl _g_tlbread
_g_tlbread: // x1 gen, x2 mapped VA, x3 exit to the host every this many reads or 0, x6 stop. Returns reads, stale, faults.
    WAITGO
    mov x13, #0 // reads
    mov x14, #0 // stale
    mov x26, #0
    adr x25, 2f
1:  ldr x10, [x1]
2:  ldr x11, [x2]
    ldr x15, [x1]
    sub x15, x15, x10 // this vCPU lost its core for long enough that the tag can't be judged: skip this read
    cmp x15, #32
    b.hs 4f
    add x10, x10, #1
    lsr x10, x10, #1
    sub x11, x11, x10
    and x11, x11, #63
    cmp x11, #32
    cinc x14, x14, hs
4:  add x13, x13, #1
    cbz x3, 3f
    udiv x15, x13, x3
    msub x15, x15, x3, x13
    cbnz x15, 3f
    hvc #1
3:  ldr x8, [x6]
    cbz x8, 1b
    mov x0, x13
    mov x1, x14
    mov x2, x26
    hvc #0

    .globl _g_tlbwrite
_g_tlbwrite: // x0 rounds, x1 gen, x2 PTE slot, x3 PTE of frame 0, x4 tlbi operand, x5 0 broadcast 1 local 2 none, x16 delay
    WAITGO
    mov x10, #1 // round
1:  cmp x5, #2
    b.eq 3f
    str xzr, [x2]
    dsb ishst
    cbnz x5, 2f
    tlbi vae1is, x4
    b 21f
2:  tlbi vae1, x4
21: dsb ish
    isb
3:  lsl x11, x10, #1
    sub x12, x11, #1
    str x12, [x1]
    and x12, x10, #63
    add x12, x3, x12, lsl #12
    str x12, [x2]
    dsb ishst
    isb
    str x11, [x1]
    DELAY x16
    add x10, x10, #1
    cmp x10, x0
    b.ne 1b
    hvc #0

    .globl _g_exec
_g_exec: // x1 site, x27 flag, until stop: runs the site in a loop with no context-synchronising event of its own
    WAITGO
    mov x9, #0
1:  blr x1
    ldr x8, [x6]
    cbz x8, 1b
    mov x0, x9
    hvc #0

    .globl _g_turn
_g_turn: // x0 n, x1 lock or 0, x2 turn word, x3 this vCPU's turn, 0 or 1
    WAITGO
    eor x10, x3, #1
    mov w12, #1
1:  ldr x11, [x2]
    cmp x11, x3
    b.ne 1b
    cbz x1, 3f
2:  mov w11, #0
    casa w11, w12, [x1]
    cbnz w11, 2b
    stlr wzr, [x1]
3:  str x10, [x2]
    subs x0, x0, #1
    b.ne 1b
    hvc #0

    .globl _g_ctr
_g_ctr:
    mrs x0, ctr_el0
    hvc #0

    .p2align 12
    .globl _g_end
_g_end:
