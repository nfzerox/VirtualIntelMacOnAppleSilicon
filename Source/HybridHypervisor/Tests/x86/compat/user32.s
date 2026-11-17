// 32-bit ring 3 checks for the compat kernel. Entered with eax the test number. Exit status 0 is a pass; any other value names the check. Every status is compared by
// the kernel, under the engine and under QEMU's TCG, so what these claim is held to TCG as well as to the SDM.
.set SYS_EXIT, 1
.set SYS_ECHO, 3
.set MAILBOX, 0x0F100000
.set GS_AREA, 0x0F000000
.set RO_PAGE, 0x0F200000
.set UNMAPPED, 0x0F300000
.set PAGE_LAST, 0x0F500000 // writable, with nothing mapped after it
.set USER_CS, 0x17
.set USER_DS, 0x1f
.set USER64_CS, 0x27
.set RO_DATA, 0x2f
.set USER_CTHREAD, 0x37
.set KERNEL_DATA3, 0x3f
.set NOT_PRESENT, 0x47

.text
.globl _start
_start:
    jmp 2f
    .p2align 3
    .long t64_entry // the kernel reads this: UCODE + 8
2:  cmpl $100, %eax
    jae 1f
    jmp *tests(,%eax,4)
1:  pushl %eax
    call c_main // never returns
    ud2

.macro EXIT status
    movl \status, %ebx
    jmp do_exit
.endm
.macro EQ a, b, status
    cmpl \b, \a
    je 77f
    movl $\status, %ebx
    jmp do_exit
77:
.endm
do_exit:
    movl $SYS_EXIT, %eax
    int $0x80
    ud2
pass:
    EXIT $0

// ---- what differs from 64-bit mode ----
t_basic:
    movl $1, %eax
    .byte 0x40 // inc eax
    EQ %eax, $2, 1
    .byte 0x4B // dec ebx
    movl $0x7fffffff, %ecx
    .byte 0x41 // inc ecx: OF, and CF kept
    jo 1f
    EXIT $2
1:  movl %esp, %ebp
    movl $0x11, %eax
    movl $0x22, %ebx
    movl $0x33, %ecx
    movl $0x44, %edx
    movl $0x55, %esi
    movl $0x66, %edi
    pushal
    EQ 12(%esp), %ebp, 3 // the old esp
    EQ 28(%esp), $0x11, 4
    EQ (%esp), $0x66, 5
    movl $0, %eax
    movl $0, %edi
    movl $0xdead, 12(%esp) // dropped by popa
    popal
    EQ %esp, %ebp, 6
    EQ %eax, $0x11, 7
    EQ %edi, $0x66, 8
    movl $0x12345678, %ecx
    pushw %cx
    popw %ax
    cmpw $0x5678, %ax
    je 91f
    EXIT $9
91:
    EQ %esp, %ebp, 10
    pushw $0x7777
    pushaw
    addl $18, %esp
    EQ %esp, %ebp, 111
    movl $0xAABBCCDD, MAILBOX + 0x40 // moffs through a32 forms below
    movl MAILBOX + 0x40, %eax
    EQ %eax, $0xAABBCCDD, 11
    .byte 0xA1
    .long MAILBOX + 0x40 // mov moffs, eax
    EQ %eax, $0xAABBCCDD, 12
    movb $0x42, %al
    .byte 0xA2
    .long MAILBOX + 0x41 // mov al, moffs
    EQ MAILBOX + 0x40, $0xAABB42DD, 13
    // call, ret, ret imm
    pushl $0x99
    call 2f
12: jmp 3f
2:  EQ (%esp), $12b, 14
    ret
3:  popl %ecx
    EQ %esp, %ebp, 15
    pushl $1
    pushl $2
    call 4f
    jmp 5f
4:  ret $8
5:  EQ %esp, %ebp, 16
    movl $6f, %eax
    pushl %eax
    movl %esp, %esi
    calll *(%esi)
    jmp 7f
6:  ret
7:  popl %ecx
    EQ %esp, %ebp, 17
    pushfl
    popl %eax
    testl $0x202, %eax
    jnz 8f
    EXIT $18
8:  // enter, leave
    enter $16, $0
    leal -16(%ebp), %ecx
    EQ %esp, %ecx, 19
    leave
    // cbw family, xlat
    movl $0xFFFF8080, %eax
    cwtl
    EQ %eax, $0xFFFF8080, 20
    cbtw
    EQ %eax, $0xFFFFFF80, 21
    cltd
    EQ %edx, $0xFFFFFFFF, 22
    movb $3, %al
    movl $xlat_table, %ebx
    xlatb
    cmpb $0x44, %al
    je 92f
    EXIT $23
92:
    // jecxz, loop, on ecx and not rcx
    movl $2, %ecx
    xorl %eax, %eax
9:  incl %eax
    loop 9b
    EQ %eax, $2, 24
    EQ %ecx, $0, 25
    jecxz 10f
    EXIT $26
10: // sidt and sgdt store six bytes
    movl $-1, MAILBOX + 0x60
    movl $-1, MAILBOX + 0x64
    sidtl MAILBOX + 0x60
    EQ MAILBOX + 0x66, $0xFFFF, 27 // the word after the 32-bit base is untouched
    cmpw $0xFFFF, MAILBOX + 0x60
    jne 11f
    EXIT $28
11: jmp pass

// ---- wraps at 4 GB and 64 KB ----
t_wrap:
    movl $0x1234, MAILBOX
    movl $0xFFFFFFF0, %ebx
    movl $MAILBOX + 16, %ecx
    EQ (%ebx,%ecx), $0x1234, 1
    EQ -16(%ecx), $0x1234, 2
    movl $MAILBOX, %ecx
    movl $-1, %edx
    EQ 1(%ecx,%edx), $0x1234, 3
    movl $0x40000000, %edx
    EQ (%ecx,%edx,4), $0x1234, 4 // the index shifts out
    leal (%ebx,%ecx), %eax
    EQ %eax, $MAILBOX - 16, 5
    leal 0x7fffffff(%ebx), %eax
    EQ %eax, $0x7FFFFFEF, 6
    leal (%bx,%si), %eax
    movzwl %bx, %edx
    movzwl %si, %edi
    addl %edi, %edx
    movzwl %dx, %edx
    EQ %eax, %edx, 7
    // 16-bit addressing to memory: page 1 of low memory is mapped
    movl $0x1234567, 0x1000
    movl $0x1FFFF, %ebx
    movl $0x1001, %esi
    movl (%bx,%si), %eax // bx+si wraps at 64 KB to 0x1000
    EQ %eax, $0x1234567, 77
    // bytes that end at 4 GB, then go on at 0
    movl $0xFFFFFFF0, %esi
    movl $16, %ecx
1:  movb %cl, (%esi)
    incl %esi
    loop 1b
    EQ %esi, $0, 8
    movl $0x11223344, 0
    movl $0xFFFFFFF0, %esi
    movl $MAILBOX + 0x100, %edi
    movl $32, %ecx
    cld
    rep movsb
    EQ %esi, $16, 9
    EQ %edi, $MAILBOX + 0x120, 10
    EQ MAILBOX + 0x110, $0x11223344, 11
    movl $0xFFFFFFF8, %edi
    movl $4, %ecx
    movl $0x5a5a5a5a, %eax
    rep stosl
    EQ %edi, $8, 12
    EQ 4, $0x5a5a5a5a, 13
    jmp pass

// ---- the kernel's ways in and out ----
t_int80:
    movl $0x10, %ecx
    movl $0x41, %ebx
    movl $SYS_ECHO, %eax
    stc
    int $0x80
    jc 1f
    EXIT $1
1:  EQ %eax, $0x42, 2
    EQ %ecx, $0x10, 3
    jmp pass

t_sysenter:
    movl $0x41, %ebx
    movl $0x5, %esi
    movl $0x6, %edi
    movl $0x12345678, %ebp
    movl $SYS_ECHO, %eax
    movl %esp, %ecx
    movl $1f, %edx
    stc
    sysenter
1:  jc 2f
    EXIT $1
2:  EQ %eax, $0x42, 2
    EQ %esi, $5, 3
    EQ %edi, $6, 4
    EQ %ebp, $0x12345678, 5
    EQ %edx, $1b, 6
    movl $7, %eax
    movl %esp, %ecx
    movl $3f, %edx
    sysenter
3:  EQ %eax, $77, 7
    movl %ss, %eax
    EQ %eax, $USER_DS, 8
    jmp pass

t_sysenter_loop:
    movl %ebx, %ebp
1:  movl $7, %eax
    movl %esp, %ecx
    movl $2f, %edx
    sysenter
2:  cmpl $77, %eax
    jne 3f
    decl %ebp
    jnz 1b
    jmp pass
3:  EXIT $1

t_syscall: // #UD in 32-bit code on Intel's parts, and on TCG with Intel's vendor
    movl $1f, MAILBOX
1:  syscall
    EXIT $1

// ---- faults: the kernel reads the frame and the mailbox ----
.macro FAULT_AT
    movl $77f, MAILBOX
77:
.endm
t_pf_read:
    movl $0x7fffffff, %eax
    addl $1, %eax // OF SF AF PF set, CF ZF clear
    movl $UNMAPPED, %esi
    movl $1f, MAILBOX
1:  movl (%esi), %ebx
    EXIT $1
t_pf_write:
    movl $0x77, %ebx
    movl $RO_PAGE, %esi
    movl $1f, MAILBOX
1:  addl %ebx, (%esi)
    EXIT $1
t_pf_push: // esp stays, CR2 is below it
    movl $0x5000, %eax
    movl $UNMAPPED + 4, %esp
    movl $1f, MAILBOX
1:  pushl %eax
    EXIT $1
t_pf_call:
    movl $UNMAPPED + 4, %esp
    movl $1f, MAILBOX
1:  call 2f
2:  EXIT $1
t_pf_ret: // a load: esp stays
    movl $UNMAPPED, %esp
    movl $1f, MAILBOX
1:  ret
t_pf_pusha: // the store into the second page faults: esp and the registers stay
    movl $UNMAPPED + 8, %esp
    movl $1f, MAILBOX
1:  pushal
    EXIT $1
t_pf_popa: // the far end is unmapped, and no register may change
    movl $0x11111111, %eax
    movl $0x22222222, %ecx
    movl $0x33333333, %edx
    movl $0x44444444, %ebx
    movl $0x55555555, %ebp
    movl $0x66666666, %esi
    movl $0x77777777, %edi
    movl $PAGE_LAST + 4096 - 16, %esp
    movl $1f, MAILBOX
1:  popal
    EXIT $1
t_pf_rep: // rep movsl stops at the unmapped page with counts exact
    movl $PAGE_LAST + 4096 - 16, %edi
    movl $MAILBOX, %esi
    movl $8, %ecx
    movl $1f, MAILBOX
1:  rep movsl
    EXIT $1
t_gp_cli:
    movl $1f, MAILBOX
1:  cli
    EXIT $1
t_gp_cr:
    movl $1f, MAILBOX
1:  movl %cr3, %eax
    EXIT $1
t_gp_kernel_ds: // DPL 0 data from ring 3
    movl $KERNEL_DATA3, %eax
    movl $1f, MAILBOX
1:  movl %eax, %ds
    EXIT $1
t_np_ds:
    movl $NOT_PRESENT, %eax
    movl $1f, MAILBOX
1:  movl %eax, %es
    EXIT $1
t_ss_np:
    movl $NOT_PRESENT, %eax
    movl $1f, MAILBOX
1:  movl %eax, %ss
    EXIT $1
t_gp_ro_ss:
    movl $RO_DATA, %eax
    movl $1f, MAILBOX
1:  movl %eax, %ss
    EXIT $1
t_gp_null_ss:
    xorl %eax, %eax
    movl $1f, MAILBOX
1:  movl %eax, %ss
    EXIT $1
t_gp_far:
    movl $1f, MAILBOX
1:  ljmp $RO_DATA, $2f
2:  EXIT $1
t_gp_gate: // an absent gate, from software
    movl $1f, MAILBOX
1:  int $0x90
    EXIT $1
t_ud:
    movl $1f, MAILBOX
1:  ud2
    EXIT $1
t_int3: // the frame's rip is the next instruction
    movl $1f, MAILBOX
    int3
1:  EXIT $1
t_de:
    xorl %ecx, %ecx
    movl $5, %eax
    xorl %edx, %edx
    movl $1f, MAILBOX
1:  divl %ecx
    EXIT $1
t_ud_rex: // 0F 05's 64-bit relatives do not exist here. 0xC4 with a register operand is VEX, which isn't offered.
    movl $1f, MAILBOX
1:  .byte 0xC4, 0xE2, 0x79, 0x18, 0xC0
    EXIT $1

// ---- far transfers ----
t_far:
    movl %esp, %ebp
    ljmp $USER_CS, $1f
    EXIT $1
1:  movl $0, %eax
    lcall $USER_CS, $far_sub
    EQ %eax, $1, 2
    EQ %esp, %ebp, 3
    pushl $7
    pushl $7
    lcall $USER_CS, $far_sub_imm
    EQ %eax, $2, 4
    EQ %esp, %ebp, 5
    movl $far_sub, MAILBOX + 0x20
    movw $USER_CS, MAILBOX + 0x24
    xorl %eax, %eax
    lcall *MAILBOX + 0x20
    EQ %eax, $1, 6
    movl $2f, MAILBOX + 0x20
    ljmp *MAILBOX + 0x20
    EXIT $7
2:  // into 64-bit mode and back, with a far return
    movl $0x11, %eax
    lcall $USER64_CS, $code64_sub
    EQ %eax, $0x55, 8
    movl %cs, %eax
    EQ %eax, $USER_CS, 9
    EQ %esp, %ebp, 10
    // the same bytes are different instructions in each mode, and each mode must see its own
    movl $5, %eax
    movl $9, %ebx
    call both_modes // dec eax; mov ebx, eax
    EQ %eax, $4, 11
    EQ %ebx, $4, 12
    movl $5, %eax
    movl $9, %ebx
    lcall $USER64_CS, $call_both_64 // mov rbx, rax
    EQ %eax, $5, 13
    EQ %ebx, $5, 14
    call both_modes
    EQ %eax, $4, 15
    // a jump out and a jump back
    movl $0x22, %eax
    ljmp $USER64_CS, $code64_jump
    EXIT $16
back_from_64:
    EQ %eax, $0x77, 17
    EQ %esp, %ebp, 18
    jmp pass
far_sub:
    incl %eax
    lret
far_sub_imm:
    movl $2, %eax
    lret $8
both_modes:
    .byte 0x48, 0x89, 0xC3
    ret
.code64
code64_sub: // called with lcall: return eip, then cs, four bytes each
    movabsq $0x100000000, %r8
    movq %rax, %rcx
    addq %r8, %rcx
    shrq $32, %rcx
    addl $0x43, %eax
    addl %ecx, %eax
    lretl
call_both_64: // the same both_modes, from 64-bit mode: a 64-bit call pushes 8 bytes, and its ret pops 8.
    callq both_modes
    lretl
code64_jump:
    movabsq $0x100000000, %r9
    addq %r9, %rax
    movl %eax, %eax
    addl $0x55, %eax
    pushq $USER_CS
    pushq $back_from_64
    lretq
.code32

// A 64-bit process at ring 3: entered by iret, leaving by a far call to 32-bit code.
t64_entry:
.code64
    movq $0x123, %r8
    movabsq $0x200000000, %rbx
    addq %r8, %rbx
    pushq $USER_CS
    pushq $from_64_entry
    lretq
.code32
from_64_entry:
    movl $SYS_EXIT, %eax
    int $0x80
    ud2

// ---- segments ----
t_segments:
    movl %gs:0, %eax
    EQ %eax, $0xC0DE0000, 1
    movl $0xFFFFFFF8, %ebx
    movl %gs:12(%ebx), %eax
    EQ %eax, $0xC0DE0001, 2 // gs:4, after the sum wraps
    movl %gs, %eax
    EQ %eax, $USER_CTHREAD, 3
    movl %eax, %fs
    movl %fs:4, %eax
    EQ %eax, $0xC0DE0001, 4
    pushl %gs
    EQ (%esp), $USER_CTHREAD, 5
    popl %fs
    pushl %fs
    popl %eax
    EQ %eax, $USER_CTHREAD, 6
    xorl %eax, %eax
    movl %eax, %fs
    movl %fs, %eax
    EQ %eax, $0, 7
    pushl %ds
    popl %es
    movl %es, %eax
    EQ %eax, $USER_DS, 8
    pushl %cs
    popl %eax
    EQ %eax, $USER_CS, 9
    pushl %ss
    popl %ss
    movl %ss, %eax
    EQ %eax, $USER_DS, 10
    movl $USER_DS, %eax
    movl %eax, %ss // the shadow: the kernel's tick can't land on the next instruction
    nop
    addl $1, %gs:8
    EQ %gs:8, $0xC0DE0003, 11
    lock incl %gs:8
    EQ %gs:8, $0xC0DE0004, 12
    jmp pass

// ---- code that changes ----
t_smc:
    movl $smc_site, %esi
    movl $1, 1(%esi)
    call smc_site
    EQ %eax, $1, 1
    movl $2, 1(%esi)
    call smc_site
    EQ %eax, $2, 2
    movl $1f + 1, %esi
    movl $0xAB, (%esi) // the very next instruction
1:  movl $0, %eax
    EQ %eax, $0xAB, 3
    jmp pass

t_touch: // ebx: base, ecx: pages. Demand paging from 32-bit code.
1:  movb (%ebx), %al
    addl $4096, %ebx
    loop 1b
    jmp pass

t_rep_resume: // ebx: destination, ecx: bytes. A copy faulting page after page.
    movl %ebx, %edi
    movl $0x100000, %esi
    rep movsb
    EQ %ecx, $0, 1
    jmp pass

t_spin: // ebx: iterations. Registers and flags that a tick must leave alone.
    movl $0x11111111, %ebp
    movl $0x22222222, %esi
    movl $0x33333333, %edi
    movl %ebx, %edx
    xorl %ecx, %ecx
1:  incl %ecx
    movl %ecx, %eax
    addl %ebp, %eax
    subl %ebp, %eax
    cmpl %ecx, %eax
    jne 2f
    cmpl $0x22222222, %esi
    jne 2f
    cmpl $0x33333333, %edi
    jne 2f
    decl %edx
    jnz 1b
    jmp pass
2:  EXIT $2

.data
.p2align 2
tests:
    .long t_basic, t_wrap, t_int80, t_sysenter, t_sysenter_loop, t_syscall       // 0..5
    .long t_pf_read, t_pf_write, t_pf_push, t_pf_call, t_pf_ret, t_pf_pusha, t_pf_popa, t_pf_rep // 6..13
    .long t_gp_cli, t_gp_cr, t_gp_kernel_ds, t_np_ds, t_ss_np, t_gp_ro_ss, t_gp_null_ss, t_gp_far, t_gp_gate // 14..22
    .long t_ud, t_int3, t_de, t_ud_rex // 23..26
    .long t_far, t_segments, t_smc, t_touch, t_rep_resume, t_spin // 27..32
    .long t64_entry // 33: the 64-bit entry point, read by the kernel
xlat_table: .byte 0x11, 0x22, 0x33, 0x44
smc_site:
    movl $0, %eax
    ret
