// EL1 exception vectors and entry and exit paths for the monitor, then the stubs translated code calls at EL0.
// Every entry comes from EL0, where x18 always holds EngineCtx. An exception taken at EL1 is a monitor bug.
.set CTX_ELR, 248
.set CTX_SPSR, 256
.set CTX_ESR, 264
.set CTX_FAR, 272
.set CTX_DF, 280
.set CTX_RS_TOP, 296
.set CTX_RFLAGS_SYS, 304
.set SVC_LOOKUP_MISS, 4
.set SVC_REP_PAGE, 71
.set HASH_MASK, 0xFFFF

.macro SAVE_ALL
    stp x0, x1, [x18, #0]
    stp x2, x3, [x18, #16]
    stp x4, x5, [x18, #32]
    stp x6, x7, [x18, #48]
    stp x8, x9, [x18, #64]
    stp x10, x11, [x18, #80]
    stp x12, x13, [x18, #96]
    stp x14, x15, [x18, #112]
    stp x16, x17, [x18, #128]
    str x19, [x18, #152]
    stp x20, x21, [x18, #160]
    stp x22, x23, [x18, #176]
    stp x24, x25, [x18, #192]
    stp x26, x27, [x18, #208]
    stp x28, x29, [x18, #224]
    str x30, [x18, #240]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [x18, #CTX_ELR]
    mrs x0, esr_el1
    mrs x1, far_el1
    stp x0, x1, [x18, #CTX_ESR]
.endm

.macro ENT target
    .p2align 7
    b \target
.endm

    .section __TEXT,__text,regular,pure_instructions
    .globl _mon_vectors
    .p2align 11
_mon_vectors:
    // Current EL with SP0: never used.
    ENT Lbug
    ENT Lbug
    ENT Lbug
    ENT Lbug
    // Current EL with SPx: a monitor fault.
    ENT Lbug
    ENT Lbug
    ENT Lbug
    ENT Lbug
    // Lower EL, AArch64.
    ENT Lsync
    ENT Lirq
    ENT Lfiq
    ENT Lserror
    // Lower EL, AArch32: not reachable.
    ENT Lbug
    ENT Lbug
    ENT Lbug
    ENT Lbug

Lbug:
    SAVE_ALL
    mrs x0, elr_el1
    bl _mon_bug
1:  b 1b

Lsync:
    SAVE_ALL
    bl _mon_sync
    b _mon_enter_guest

Lirq:
    SAVE_ALL
    bl _mon_irq
    b _mon_enter_guest

Lfiq:
    SAVE_ALL
    bl _mon_fiq
    b _mon_enter_guest

Lserror:
    SAVE_ALL
    bl _mon_bug
1:  b 1b

// The host starts every vCPU here, and every monitor path leaves here.
    .globl _mon_enter_guest
_mon_enter_guest:
    ldp x0, x1, [x18, #CTX_ELR]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldp x0, x1, [x18, #0]
    ldp x2, x3, [x18, #16]
    ldp x4, x5, [x18, #32]
    ldp x6, x7, [x18, #48]
    ldp x8, x9, [x18, #64]
    ldp x10, x11, [x18, #80]
    ldp x12, x13, [x18, #96]
    ldp x14, x15, [x18, #112]
    ldp x16, x17, [x18, #128]
    ldr x19, [x18, #152]
    ldp x20, x21, [x18, #160]
    ldp x22, x23, [x18, #176]
    ldp x24, x25, [x18, #192]
    ldp x26, x27, [x18, #208]
    ldp x28, x29, [x18, #224]
    ldr x30, [x18, #240]
    eret

// A stop that came back: start over on an empty stack, whatever depth the monitor stopped at.
    .globl _mon_restart
_mon_restart:
    mrs x0, tpidr_el1 // the host keeps this vCPU's stack top here
    mov sp, x0
    bl _mon_after_stop
    b _mon_enter_guest

// Host call: hvc #code. The host runs with the vCPU stopped here and resumes at the next instruction.
    .globl _mon_hvc
_mon_hvc:
    hvc #0
    ret

    .globl _mon_fp_save // x0: 256 bytes
_mon_fp_save:
    stp q0, q1, [x0, #0]
    stp q2, q3, [x0, #32]
    stp q4, q5, [x0, #64]
    stp q6, q7, [x0, #96]
    stp q8, q9, [x0, #128]
    stp q10, q11, [x0, #160]
    stp q12, q13, [x0, #192]
    stp q14, q15, [x0, #224]
    ret
    .globl _mon_fp_restore
_mon_fp_restore:
    ldp q0, q1, [x0, #0]
    ldp q2, q3, [x0, #32]
    ldp q4, q5, [x0, #64]
    ldp q6, q7, [x0, #96]
    ldp q8, q9, [x0, #128]
    ldp q10, q11, [x0, #160]
    ldp q12, q13, [x0, #192]
    ldp q14, q15, [x0, #224]
    ret

// ---- EL0 stubs. Fixed offsets, the translator calls them by address; see EngineLayout. ----
    .section __EL0,__stubs,regular,pure_instructions
    .p2align 12
    .globl _stub_base
_stub_base:
// x16: x86 target. Returns the translation in x22. Keeps NZCV. Clobbers x17, x20, x23.
    .globl _stub_lookup
_stub_lookup:
    crc32cx w17, wzr, x16
    and w17, w17, #HASH_MASK
1:  add x23, x19, x17, lsl #4
    ldp x20, x22, [x23]
    cbz x20, 2f
    eor x20, x20, x16
    cbnz x20, 3f
    ret
3:  add w17, w17, #1
    and w17, w17, #HASH_MASK
    b 1b
2:  svc #SVC_LOOKUP_MISS
    // The monitor resumes at x30 with x22 set, so nothing follows.
    brk #0

    .org _stub_base + 0x80
// A return whose shadow entry did not match: drop the shadow stack, the guest stack stays authoritative.
    .globl _stub_ret_miss
_stub_ret_miss:
    ldr x26, [x18, #CTX_RS_TOP]
    bl _stub_lookup
    br x22

    .org _stub_base + 0x100
// x16 = RFLAGS. Keeps NZCV. Clobbers x17, x20.
    .globl _stub_pushf
_stub_pushf:
    mrs x17, nzcv
    ldr x16, [x18, #CTX_RFLAGS_SYS]
    ubfx x20, x17, #29, #1
    eor x20, x20, #1
    orr x16, x16, x20
    ubfx x20, x17, #30, #1
    orr x16, x16, x20, lsl #6
    ubfx x20, x17, #31, #1
    orr x16, x16, x20, lsl #7
    ubfx x20, x17, #28, #1
    orr x16, x16, x20, lsl #11
    eor w20, w21, w21, lsr #4
    and w20, w20, #0xf
    mov w17, #0x9669
    lsr w17, w17, w20
    and w17, w17, #1
    orr x16, x16, x17, lsl #2
    eor x20, x25, x21
    and x20, x20, #0x10
    orr x16, x16, x20
    ldr x20, [x18, #CTX_DF]
    tbz x20, #63, 1f
    orr x16, x16, #0x400
1:  ret

    .org _stub_base + 0x180
// x16: the byte for sahf. Keeps V. Clobbers x17, x20.
    .globl _stub_sahf
_stub_sahf:
    mrs x17, nzcv
    and x17, x17, #(1 << 28)
    ubfx x20, x16, #7, #1
    orr x17, x17, x20, lsl #31
    ubfx x20, x16, #6, #1
    orr x17, x17, x20, lsl #30
    ubfx x20, x16, #0, #1
    eor x20, x20, #1
    orr x17, x17, x20, lsl #29
    msr nzcv, x17
    ubfx x20, x16, #2, #1
    eor x21, x20, #1
    and x20, x16, #0x10
    eor x25, x21, x20
    ret
    .org _stub_base + 0x200
// rep movs, 32 bytes at a time while the copy runs forward and cannot see its own output inside a chunk. x17: elements per chunk.
// Registers move only after a chunk is stored, so a fault restarts the x86 instruction with exact state, and redoing a chunk is harmless
// because source and destination are at least a chunk apart both ways. Leaves the tail, and anything else, to the plain loop at the call
// site. No NEON: under CR0.TS it would trap for every element, and nothing in a plain copy would clear TS. Keeps NZCV. Clobbers x16, x20, x22, x23.
    .globl _stub_rep_movs
_stub_rep_movs:
    ldr x16, [x18, #CTX_DF]
    tbnz x16, #63, 2f
    sub x16, x7, x6
    lsr x20, x16, #5
    cbz x20, 2f
    sub x16, x6, x7
    lsr x20, x16, #5
    cbz x20, 2f
    b 1f
    .org _stub_base + 0x240 // the loop head is an x86 boundary: the monitor delivers interrupts here
1:  sub x20, x1, x17
    tbnz x20, #63, 2f
    ldp x22, x23, [x6]
    stp x22, x23, [x7]
    ldp x22, x23, [x6, #16]
    stp x22, x23, [x7, #16]
    add x6, x6, #32
    add x7, x7, #32
    mov x1, x20
    b 1b
2:  ret


    .org _stub_base + 0x280
// rep stos with the pattern already in x22. No NEON, as for movs: a kernel zeroes pages with CR0.TS set.
    .globl _stub_rep_stos
_stub_rep_stos:
    ldr x16, [x18, #CTX_DF]
    tbnz x16, #63, 2f
    b 1f
    .org _stub_base + 0x2C0 // boundary, as above
1:  sub x20, x1, x17
    tbnz x20, #63, 2f
    stp x22, x22, [x7]
    stp x22, x22, [x7, #16]
    add x7, x7, #32
    mov x1, x20
    b 1b
2:  ret

    .org _stub_base + 0x300
// Experimental page REP: the service either commits a page prefix or returns to
// the emitter's scalar loop. It never enters the original bulk stubs above.
    .globl _stub_rep_page_movs
_stub_rep_page_movs:
    ldr x16, [x18, #CTX_DF]
    tbnz x16, #63, 2f
    sub x16, x7, x6
    lsr x20, x16, #5
    cbz x20, 2f
    sub x16, x6, x7
    lsr x20, x16, #5
    cbz x20, 2f
    sub x20, x1, x17
    tbnz x20, #63, 2f
    b 1f
2:  ret
    .org _stub_base + 0x340
1:  svc #SVC_REP_PAGE
    ret

    .org _stub_base + 0x380
    .globl _stub_rep_page_stos
_stub_rep_page_stos:
    ldr x16, [x18, #CTX_DF]
    tbnz x16, #63, 2f
    sub x20, x1, x17
    tbnz x20, #63, 2f
    b 1f
2:  ret
    .org _stub_base + 0x3C0
1:  svc #SVC_REP_PAGE
    ret
    .org _stub_base + 0x400
