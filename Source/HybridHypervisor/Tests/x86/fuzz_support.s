// State loading and saving around one instruction form: every register, flags, MXCSR. Fixed scratch through rip-relative cells.
// Layout of `fuzz_st`: r[16] (+0), xmm[16] (+128), rflags (+384), mxcsr (+392), rsp difference (+400).
.set ST_R, 0
.set ST_X, 128
.set ST_FLAGS, 384
.set ST_MX, 392
.set ST_RSP, 400

.text
.globl _fuzz_load, _fuzz_load_fp, _fuzz_load_x87, _fuzz_save, _fuzz_save_x87, _fuzz_call, _fuzz_st, _fuzz_x87_in, _fuzz_x87_out, buffer_ptr_cell

_fuzz_call: // rdi: form entry
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, saved_rsp(%rip)
    movq %rdi, form_entry(%rip)
    subq $64, %rsp
    jmp *form_entry(%rip)

// A control word and `depth` registers. The last one loaded is ST0.
_fuzz_load_x87:
    leaq _fuzz_x87_in(%rip), %rax
    fninit
    fldcw (%rax)
    movzbl 2(%rax), %ecx
    addq $16, %rax
1:
    testl %ecx, %ecx
    jz 2f
    fldt (%rax)
    addq $16, %rax
    decl %ecx
    jmp 1b
2:
_fuzz_load_fp:
    leaq _fuzz_st(%rip), %rax
    movaps ST_X+0(%rax), %xmm0
    movaps ST_X+16(%rax), %xmm1
    movaps ST_X+32(%rax), %xmm2
    movaps ST_X+48(%rax), %xmm3
    movaps ST_X+64(%rax), %xmm4
    movaps ST_X+144(%rax), %xmm9
    movaps ST_X+192(%rax), %xmm12
    ldmxcsr ST_MX(%rax)
_fuzz_load:
    // On entry the stack top holds the return into the form.
    leaq _fuzz_st(%rip), %rax
    pushq ST_FLAGS(%rax)
    popfq
    movq 8(%rax), %rcx
    movq 16(%rax), %rdx
    movq 24(%rax), %rbx
    movq 40(%rax), %rbp
    movq 48(%rax), %rsi
    movq 56(%rax), %rdi
    movq 64(%rax), %r8
    movq 72(%rax), %r9
    movq 80(%rax), %r10
    movq 88(%rax), %r11
    movq 96(%rax), %r12
    movq 104(%rax), %r13
    movq 112(%rax), %r14
    movq 120(%rax), %r15
    movq (%rax), %rax
    ret

_fuzz_save_x87:
    fxsave64 _fuzz_x87_out(%rip)
    fninit
_fuzz_save:
    pushfq
    movq %rax, _fuzz_st+0(%rip)
    popq %rax
    movq %rax, _fuzz_st+ST_FLAGS(%rip)
    movq %rcx, _fuzz_st+8(%rip)
    movq %rdx, _fuzz_st+16(%rip)
    movq %rbx, _fuzz_st+24(%rip)
    movq %rbp, _fuzz_st+40(%rip)
    movq %rsi, _fuzz_st+48(%rip)
    movq %rdi, _fuzz_st+56(%rip)
    movq %r8, _fuzz_st+64(%rip)
    movq %r9, _fuzz_st+72(%rip)
    movq %r10, _fuzz_st+80(%rip)
    movq %r11, _fuzz_st+88(%rip)
    movq %r12, _fuzz_st+96(%rip)
    movq %r13, _fuzz_st+104(%rip)
    movq %r14, _fuzz_st+112(%rip)
    movq %r15, _fuzz_st+120(%rip)
    leaq _fuzz_st(%rip), %rax
    movaps %xmm0, ST_X+0(%rax)
    movaps %xmm1, ST_X+16(%rax)
    movaps %xmm2, ST_X+32(%rax)
    movaps %xmm3, ST_X+48(%rax)
    movaps %xmm4, ST_X+64(%rax)
    movaps %xmm9, ST_X+144(%rax)
    movaps %xmm12, ST_X+192(%rax)
    stmxcsr ST_MX(%rax)
    movq %rsp, %rcx
    movq saved_rsp(%rip), %rsp
    subq %rcx, %rsp
    movq %rsp, ST_RSP(%rax)
    movq saved_rsp(%rip), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.data
.p2align 3
saved_rsp: .quad 0
form_entry: .quad 0
buffer_ptr_cell: .quad 0x123456789abcdef0
.p2align 6
_fuzz_st: .space 512
.p2align 4
_fuzz_x87_in: .space 16 + 8 * 16
.p2align 4
_fuzz_x87_out: .space 512
