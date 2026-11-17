// Reset vector to long mode, for running the compat kernel under QEMU's TCG as the reference. mkqemu.py puts this at 0xF0000, a jump to it at the reset vector, and the
// kernel's entry and the RAM size at 0xFFFE0 and 0xFFFE8. The registers end up as Engine.swift starts a vCPU: CR0, CR4 and EFER match.
.text
.code16
start:
    cli
    lgdtl %cs:(gdtptr - start)
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $pm32
.code32
pm32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $0x620, %eax
    movl %eax, %cr4
    movl $0x100000, %eax
    movl %eax, %cr3
    movl $0xC0000080, %ecx
    movl $0x901, %eax // SCE, LME, NXE
    xorl %edx, %edx
    wrmsr
    movl $0x80010033, %eax
    movl %eax, %cr0
    ljmpl $0x18, $lm64
.code64
lm64:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movq 0xFFFE0, %rax
    movq 0xFFFE8, %rdi
    xorl %esp, %esp
    jmpq *%rax
.code32
.p2align 3
gdt:
    .long 0, 0
    .long 0x0000FFFF, 0x00CF9A00 // 32-bit code
    .long 0x0000FFFF, 0x00CF9200 // data
    .long 0x0000FFFF, 0x00AF9A00 // 64-bit code
gdtptr:
    .word gdtptr - gdt - 1
    .long gdt
