// Interrupt stubs and ring transitions for the compat kernel, x86-64. The return path to 32-bit code loads segments as XNU's does, from the LDT, in 64-bit mode.
.set USER_CS, 0x17
.set USER_DS, 0x1f
.set USER_CTHREAD, 0x37
.text

// rdi entry, rsi low supervisor stack. The exit test uses the same finite
// return_from_user trampoline as ring3, without changing the guest CPL to3.
    .globl _enter_kernel32
_enter_kernel32:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, _resume_rsp(%rip)
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    xor %eax,%eax
    movw %ax,%fs
    movw $0x58,%ax
    movw %ax,%gs
    pushq $0x10
    pushq %rsi
    pushq $0x202
    pushq $0x50
    pushq %rdi
    iretq

    .globl _kernel32_blob, _kernel32_blob_end
_kernel32_blob:
    .incbin "compat_kernel32.bin"
_kernel32_blob_end:

.macro ISR n, has_err
    .globl _isr\n
_isr\n:
    .if \has_err == 0
    pushq $0
    .endif
    pushq $\n
    jmp isr_common
.endm
ISR 0, 0
ISR 3, 0
ISR 6, 0
ISR 8, 1
ISR 11, 1
ISR 12, 1
ISR 13, 1
ISR 14, 1
ISR 32, 0
ISR 128, 0

isr_common: // Frame: r15..rax, vector, error, rip, cs, rflags, rsp, ss
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    cld
    call _trap_dispatch
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

// Reload what a 64-bit kernel changed: ring-3 selectors, and gs by selector, which takes its base from the LDT.
.macro USER_SEGMENTS
    movw $USER_DS, %ax
    movw %ax, %ds
    movw %ax, %es
    xorl %eax, %eax
    movw %ax, %fs
    movw $USER_CTHREAD, %ax
    movw %ax, %gs
.endm

// rdi rip, rsi esp, rdx eax, rcx ebx, r8 edx, r9 ecx. Returns when user code exits or dies.
    .globl _enter_compat
_enter_compat:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, _resume_rsp(%rip)
    pushq %rdx
    USER_SEGMENTS
    popq %rdx
    pushq $USER_DS
    pushq %rsi
    pushq $0x202
    pushq $USER_CS
    pushq %rdi
    movq %rdx, %rax
    movq %rcx, %rbx
    movq %r8, %rdx
    movq %r9, %rcx
    iretq

// The same into 64-bit ring 3.
    .globl _enter_user64
_enter_user64:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, _resume_rsp(%rip)
    pushq $USER_DS
    pushq %rsi
    pushq $0x202
    pushq $0x27
    pushq %rdi
    movq %rdx, %rax
    iretq

// The same, in by sysretl: rdi eip, rsi esp, rdx eax, rcx ebx.
    .globl _enter_sysret
_enter_sysret:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, _resume_rsp(%rip)
    pushq %rdx
    USER_SEGMENTS
    popq %rax
    movq %rcx, %rbx
    movq %rdi, %rcx
    movq $0x202, %r11
    movq %rsi, %rsp
    sysretl

    .globl _return_from_user
_return_from_user:
    movq _resume_rsp(%rip), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

// sysenter, by XNU's convention: edx the return eip, ecx the user's esp, arguments in ebx, esi, edi. Fast path: eax 7 returns at once.
    .globl _sysenter_entry
_sysenter_entry:
    pushq %rcx
    pushq %rdx
    pushfq
    cmpl $7, %eax
    jne 1f
    movl $77, %eax
    popfq
    popq %rdx
    popq %rcx
    sti
    sysexitl
1:  pushq %rbx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    movq %rax, %rdi
    pushq %rsi
    movq %rbx, %rsi
    popq %rdx
    call _sys
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rbx
    pushq %rax
    USER_SEGMENTS
    popq %rax
    popfq
    popq %rdx
    popq %rcx
    sti
    sysexitl

.data
.p2align 3
    .globl _resume_rsp
_resume_rsp: .quad 0
.p2align 4
    .globl _sysenter_stack_top
    .space 16384
_sysenter_stack_top:

.const
.p2align 12
    .globl _user_blob, _user_blob_end
_user_blob:
    .incbin "compat_user.bin"
_user_blob_end:
.p2align 12
