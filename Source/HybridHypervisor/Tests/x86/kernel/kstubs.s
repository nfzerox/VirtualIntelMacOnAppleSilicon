// Interrupt stubs and ring transitions for the test kernel. AT&T syntax, x86-64.
.text

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
ISR 12, 1
ISR 13, 1
ISR 14, 1
ISR 32, 0
ISR 128, 0
ISR 33, 0

isr_common:
    // Frame: r15..rax, vector, error, rip, cs, rflags, rsp, ss
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
    testb $3, 18*8(%rsp) // from ring 3?
    jz 1f
    swapgs
1:  movq %rsp, %rdi
    cld
    call _trap_dispatch
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

// syscall entry: rcx = user rip, r11 = user rflags, no stack yet. Per-CPU data through gs.
    .globl _syscall_entry
_syscall_entry:
    swapgs
    movq %rsp, %gs:8
    movq %gs:0, %rsp
    pushq %rcx
    pushq %r11
    // Fast path: rax 0 is getpid-like, returning at once.
    testq %rax, %rax
    jnz 3f
    movq $42, %rax
    popq %r11
    popq %rcx
    movq %gs:8, %rsp
    swapgs
    sysretq
3:  pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r8
    pushq %r9
    pushq %r10
    movq %rax, %rcx
    call _syscall_dispatch
    popq %r10
    popq %r9
    popq %r8
    popq %rdx
    popq %rsi
    popq %rdi
    popq %r11
    popq %rcx
    movq %gs:8, %rsp
    swapgs
    sysretq

// Enters ring 3: rdi rip, rsi rsp, rdx rflags. Returns when user code calls the exit syscall (saved kernel rsp in gs:16).
    .globl _enter_user
_enter_user:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %gs:16
    pushq $0x23 // ss
    pushq %rsi
    pushq %rdx
    pushq $0x2B // cs
    pushq %rdi
    swapgs
    iretq

    .globl _return_from_user // Called from the exit syscall path: unwinds to enter_user's caller.
_return_from_user:
    movq %gs:16, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

// ---- Ring 3 code, copied to user pages. Position independent, with a syscall per action. Ends at user_blob_end. ----
    .globl _user_blob, _user_blob_end
    .globl _u_syscalls, _u_touch, _u_touch_write, _u_spin, _u_cli, _u_ud2, _u_div0, _u_int3, _u_noncanon, _u_kernel_read, _u_int80, _u_flags_at_fault
    .globl _u_sar16_at_fault, _u_sar16_fault_load
    .globl _u_shr16_at_fault, _u_shr16_fault_load
    .globl _u_cl_self_count_at_fault, _u_cl_self_count_fault_load
    .globl _u_table_limits, _u_idt_base, _u_gdt_base
    .globl _u_rmw_fault, _u_store_loop, _u_return_value, _u_rep_copy, _u_store_cross, _u_load_cross
    .p2align 12
_user_blob:

// rdi: count. Each syscall returns 42; a wrong answer exits with status 1.
_u_syscalls:
    movq %rdi, %rbx
1:  xorl %eax, %eax
    syscall
    cmpq $42, %rax
    jne 9f
    decq %rbx
    jnz 1b
    jmp 8f
9:  movq $1, %rdi
    movq $1, %rax
    syscall
8:  xorl %edi, %edi
    movq $1, %rax
    syscall
    ud2

// rdi: base, rsi: page count. Reads one byte per page.
_u_touch:
    xorl %ebx, %ebx
1:  movb (%rdi), %al
    addq $4096, %rdi
    incq %rbx
    cmpq %rsi, %rbx
    jne 1b
    jmp 8b

// rdi: base, rsi: page count. Writes one byte per page.
_u_touch_write:
    xorl %ebx, %ebx
1:  movb %bl, (%rdi)
    addq $4096, %rdi
    incq %rbx
    cmpq %rsi, %rbx
    jne 1b
    jmp 8b

// A loop with registers and flags that interrupts must leave alone. rdi: iterations. Exits 0 if everything still matches.
_u_spin:
    movq %rdi, %rbx
    movq $0x1111111111111111, %r8
    movq $0x2222222222222222, %r9
    movq $0x3333333333333333, %r10
    movq $0x4444444444444444, %r12
    xorl %ecx, %ecx
1:  addq $1, %rcx
    movq %rcx, %rax
    addq %r8, %rax
    subq %r8, %rax
    cmpq %rcx, %rax
    jne 2f
    movq $0x2222222222222222, %rdx
    cmpq %rdx, %r9
    jne 2f
    decq %rbx
    jnz 1b
    jmp 8b
2:  movq $2, %rdi
    movq $1, %rax
    syscall
    ud2

// Without UMIP these read fine from ring 3, and libsystem reads the CPU number that way. Limits and selectors in one word.
_u_table_limits:
    subq $32, %rsp
    sidt (%rsp)
    movzwq (%rsp), %rdi
    sgdt 16(%rsp)
    movzwq 16(%rsp), %rax
    shlq $16, %rax
    orq %rax, %rdi
    strq %rax
    shlq $32, %rax
    orq %rax, %rdi
    sldtq %rax
    shlq $48, %rax
    orq %rax, %rdi
    movq $1, %rax
    syscall
    ud2
_u_idt_base:
    subq $32, %rsp
    sidt (%rsp)
    movq 2(%rsp), %rdi
    movq $1, %rax
    syscall
    ud2
_u_gdt_base:
    subq $32, %rsp
    sgdt (%rsp)
    movq 2(%rsp), %rdi
    movq $1, %rax
    syscall
    ud2

_u_cli:
    cli
    jmp 8b
_u_ud2:
    ud2
    jmp 8b
_u_div0:
    xorl %ecx, %ecx
    movl $5, %eax
    xorl %edx, %edx
    divl %ecx
    jmp 8b
_u_int3:
    int3
    jmp 8b
_u_int80:
    movq $0x55, %rax
    int $0x80
    jmp 8b
_u_noncanon:
    movq $0x0000800000000000, %rax
    movq (%rax), %rbx
    jmp 8b
_u_kernel_read: // rdi: kernel address
    movq (%rdi), %rbx
    jmp 8b

// An arithmetic result right before a faulting load: the saved flags must show it. rdi: unmapped address.
_u_flags_at_fault:
    movq $0x7fffffffffffffff, %rax
    addq $1, %rax // OF SF AF PF set, CF ZF clear
    movq (%rdi), %rbx
    jmp 8b

// rdi: unmapped address; rdx: complete input. TEST is deliberately after the faulting load.
_u_sar16_at_fault:
    pushq $0xad7 // all arithmetic flags set, so SAR must establish its own flags including clearing OF/AF
    popfq
    sarw $9, %dx
    leaq (%rdx,%rdx,2), %r8
_u_sar16_fault_load:
    movq (%rdi), %rbx
    testb %dl, %dl
    jmp 8b

_u_shr16_at_fault:
    pushq $0xad7
    popfq
    shrw $9, %dx
    leaq (%rdx,%rdx,2), %r8
_u_shr16_fault_load:
    movq (%rdi), %rbx
    testb %dl, %dl
    jmp 8b

// rdi: unmapped address; rdx: initial RCX. The following TEST must not hide incorrect flags at the load.
_u_cl_self_count_at_fault:
    movq %rdx, %rcx
    pushq $0x202
    popfq
    shlq %cl, %rcx
    leaq (%rcx,%rcx,2), %r8
_u_cl_self_count_fault_load:
    movq (%rdi), %rbx
    testq %rcx, %rcx
    jmp 8b

// A read-modify-write whose store faults: nothing may change. rdi: read-only page.
_u_rmw_fault:
    movq $5, %rax
    addq $0, %rax // ZF clear, flags known
    stc
    addq %rax, (%rdi)
    jmp 8b

// Stores through a loop, then returns the value. rdi: address, rsi: iterations.
_u_store_loop:
    xorl %ecx, %ecx
1:  movq %rcx, (%rdi)
    incq %rcx
    cmpq %rsi, %rcx
    jne 1b
    jmp 8b

_u_store_cross:
    movq $-1, %rax
    movq %rax, (%rdi)
    jmp 8b
_u_load_cross:
    movq (%rdi), %rax
    jmp 8b

// rdi: destination, rsi: source, rdx: bytes. A rep movsb that may fault part way and must resume with exact registers.
_u_rep_copy:
    movq %rdx, %rcx
    rep movsb
    movq %rcx, %rdi // must be 0
    movq $1, %rax
    syscall
    ud2

// Returns through the exit syscall with status read from rdi.
_u_return_value:
    movq (%rdi), %rdi
    movq $1, %rax
    syscall
    ud2
    .p2align 12
_user_blob_end:
