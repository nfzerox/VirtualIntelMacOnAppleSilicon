// Tiger/Leopard's observed IA-32e ring0 compatibility mode. Four-level
// paging and 64-bit interrupt gates remain active; no guest OS is booted.
.text
.code32
.globl kernel32_start
kernel32_start:
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    xor %eax,%eax
    movw %ax,%fs
    movw $0x58,%ax
    movw %ax,%gs
    cmpl $0xc0def00d,%gs:0
    jne fail
    // Native Engine/C-API fixtures have no TCG: their510 diagnostic port
    // returns a block count or0. QEMU's unassigned port returns all ones.
    // Both plain TCG and real QEMU+hybrid exercise the based-selector path.
    movw $0x510,%dx
    inl %dx,%eax
    cmpl $-1,%eax
    jne flat_only
    movl $1000,%edi
copy_window:
    movw $0x60,%ax
    movw %ax,%es
    movl $0xc0decafe,%es:0
    cmpl $0xc0decafe,%es:0
    jne fail
    movw $0x10,%ax
    movw %ax,%es
    decl %edi
    jnz copy_window
flat_only:
    // Keep the same-ring far-transfer regression under real QEMU, whose
    // precise TCG fallbacks own these forms. Standalone Engine has no TCG.
    movw $0x510,%dx
    inl %dx,%eax
    cmpl $-1,%eax
    jne flat_kernel_work
    movl %esp,%ebp
    movl $41,%ebx
    lcall $0x50,$far_callee
    cmpl $42,%ebx
    jne fail
    lcall *far_call_pointer
    cmpl $43,%ebx
    jne fail
    cmpl %esp,%ebp
    jne fail
    ljmp $0x50,$far_indirect
    jmp fail
far_callee:
    incl %ebx
    lret
far_call_pointer:
    .long far_callee
    .word 0x50
far_jump_pointer:
    .long far_jump
    .word 0x50
far_indirect:
    ljmp *far_jump_pointer
far_jump:
    ljmp $0x08,$long_trampoline
.code64
long_trampoline:
    pushq $0x50
    leaq compat_return(%rip),%rax
    pushq %rax
    lretq
.code32
compat_return:
    cmpl %esp,%ebp
    jne fail
    cmpl $0xc0def00d,%gs:0
    jne fail
flat_kernel_work:
    cli
    pushfl
    popl %eax
    test $0x200,%eax
    jne fail
    sti
    movl %cr3,%eax
    movl %eax,%cr3
    movl $3,%eax
    movl $41,%ebx
    int $0x80
    cmpl $42,%eax
    jne fail
    cmpl $0xc0def00d,%gs:0
    jne fail
    movl $1000000,%ecx
    xorl %eax,%eax
loop:
    addl %ecx,%eax
    decl %ecx
    jnz loop
    cmpl $0x6a5a2920,%eax // sum 1..1000000 modulo 2^32
    jne fail
    movl $1,%eax
    xorl %ebx,%ebx
    int $0x80
fail:
    movl $1,%eax
    movl $1,%ebx
    int $0x80
    ud2
