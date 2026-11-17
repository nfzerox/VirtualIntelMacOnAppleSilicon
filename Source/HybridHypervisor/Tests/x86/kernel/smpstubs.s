// Entry, interrupt stubs and ring transitions for the two-or-more vCPU test kernel. AT&T syntax, x86-64.
.text
// The host starts every vCPU here in long mode: rdi RAM size, rsi vCPU index.
.globl _kstart
_kstart:
    movq %rsi, %rax
    incq %rax
    shlq $16, %rax
    leaq _boot_stacks(%rip), %rsp
    addq %rax, %rsp
    call _kmain
1:  hlt
    jmp 1b

.macro ISR n, has_err
    .globl _isr\n
_isr\n:
    .if \has_err == 0
    pushq $0
    .endif
    pushq $\n
    jmp isr_common
.endm

ISR 13, 1
ISR 14, 1
ISR 64, 0
ISR 65, 0
ISR 66, 0

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
    testb $3, 18*8(%rsp)
    jz 1f
    swapgs
1:  movq %rsp, %rdi
    cld
    call _trap
    testb $3, 18*8(%rsp)
    jz 2f
    swapgs
2:  popq %r15
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

// syscall: the shortest round trip with a stack swap, on this vCPU's own stack through gs.
.globl _syscall_entry
_syscall_entry:
    swapgs
    movq %rsp, %gs:8
    movq %gs:0, %rsp
    pushq %rcx
    popq %rcx
    movq %gs:8, %rsp
    swapgs
    sysretq

// enter_user_args(rip, rsp, &saved, a, b): to ring 3 until `int $66` comes back through leave_user, with a and b in rdi and rsi there.
.globl _enter_user_args
_enter_user_args:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdx)
    pushq $0x23
    pushq %rsi
    pushq $0x202
    pushq $0x2B
    pushq %rdi
    movq %rcx, %rdi
    movq %r8, %rsi
    swapgs
    iretq
.globl _leave_user
_leave_user: // rdi: the saved stack
    movq %rdi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

// Ring 3: rdi calls of a syscall loop, then rsi rounds of recursion, depth 40 and back. Position independent, copied to a user page.
.globl _user_blob
_user_blob:
    movq %rdi, %r12
    testq %r12, %r12
    jz 2f
1:  syscall
    decq %r12
    jnz 1b
2:  movq %rsi, %r12
    testq %r12, %r12
    jz 4f
3:  movq $40, %rdi
    call 5f
    decq %r12
    jnz 3b
4:  int $66
5:  decq %rdi
    jz 6f
    call 5b
6:  ret
.globl _user_blob_end
_user_blob_end:

// A load that a fault can skip: the #PF handler resumes at load_resume with rax all ones.
.globl _load_or_fault
_load_or_fault:
    movq (%rdi), %rax
.globl _load_resume
_load_resume:
    ret

.data
.p2align 4
.globl _boot_stacks
_boot_stacks:
.space 65536 * 9
