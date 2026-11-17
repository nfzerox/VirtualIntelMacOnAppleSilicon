// First instructions: the loader enters in long mode with a stack, rdi = RAM size.
.text
.globl _kstart
_kstart:
    leaq _boot_stack_top(%rip), %rsp
    call _kmain
1:  hlt
    jmp 1b
.data
.p2align 4
.space 65536
_boot_stack_top:
