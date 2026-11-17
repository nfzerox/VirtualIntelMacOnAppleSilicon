// Exact baseline x86_64 VCPHEVC [0xd92bd,0xd92da), with local bounded model tables.
.text
.globl _vcp_cl_case, _vcp_cl_batch, _vcp_cl_case_body, _vcp_cl_case_end, _vcp_cl_batch_body, _vcp_cl_batch_end

// in: r15,r12,rax,rcx,rbx,r10,rdx,r9,lookup-index,output-index. out: eight fields documented in C.
_vcp_cl_case:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsi, %rbp
    movq 0(%rdi), %r15
    movq 8(%rdi), %r12
    movq 16(%rdi), %rax
    movq 24(%rdi), %rcx
    movq 32(%rdi), %rbx
    movq 40(%rdi), %r10
    movq 48(%rdi), %rdx
    movq 56(%rdi), %r9
    movq 64(%rdi), %r14
    movq 72(%rdi), %r13
    leaq cl_lookup(%rip), %rsi
    leaq cl_output(%rip), %r8
    movb $0xa5, 0x7b(%r8,%r13)
_vcp_cl_case_body:
    .byte 0x49,0xd3,0xe7,0x4c,0x8d,0x58,0xff,0x49,0xd3,0xe4,0x4c,0x31,0xd3
    .byte 0x41,0x8a,0x94,0x36,0x00,0x01,0x00,0x00,0x43,0x88,0x54,0x28,0x7b,0x49,0x01,0xc9
_vcp_cl_case_end:
    pushfq
    popq %rdi
    andl $0x8d5, %edi
    movq %r15, 0(%rbp)
    movq %r12, 8(%rbp)
    movq %r11, 16(%rbp)
    movq %rbx, 24(%rbp)
    movq %rdx, 32(%rbp)
    movq %r9, 40(%rbp)
    movq %rdi, 48(%rbp)
    movzbl 0x7b(%r8,%r13), %edi
    movq %rdi, 56(%rbp)
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    retq

// rdi=positive iterations, rsi=seed. No helper call inside the timed loop.
_vcp_cl_batch:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsi, %rax
    movabsq $0xcbf29ce484222325, %rbp
    leaq cl_lookup(%rip), %rsi
    leaq cl_output(%rip), %r8
1:
    leaq 0x13579(%rax,%rax,4), %r15
    leaq -0x2468(%rax,%rax,2), %r12
    movq %rax, %rcx
    andl $255, %ecx
    movq %rax, %rbx
    notq %rbx
    leaq (%rax,%rax,8), %r10
    movabsq $0x9234567800000000, %rdx
    xorq %rax, %rdx
    leaq -1(%rax,%rax,2), %r9
    movq %rax, %r13
    andl $31, %r13d
    movq %rax, %r14
    andl $255, %r14d
    movb $0xa5, 0x7b(%r8,%r13)
_vcp_cl_batch_body:
    .byte 0x49,0xd3,0xe7,0x4c,0x8d,0x58,0xff,0x49,0xd3,0xe4,0x4c,0x31,0xd3
    .byte 0x41,0x8a,0x94,0x36,0x00,0x01,0x00,0x00,0x43,0x88,0x54,0x28,0x7b,0x49,0x01,0xc9
_vcp_cl_batch_end:
    pushfq
    popq %r10
    andl $0x8d5, %r10d
    .macro cl_fold reg
    xorq \reg, %rbp
    imulq $0x1000193, %rbp, %rbp
    .endm
    cl_fold %r15
    cl_fold %r12
    cl_fold %r11
    cl_fold %rbx
    cl_fold %rdx
    cl_fold %r9
    cl_fold %r10
    movzbl 0x7b(%r8,%r13), %r11d
    cl_fold %r11
    leaq 1(%rax), %rax
    decq %rdi
    jnz 1b
    movq %rbp, %rax
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    retq

.globl _vcp_cl_initialize
_vcp_cl_initialize:
    leaq cl_lookup(%rip), %rax
    xorq %rcx, %rcx
2:
    imull $73, %ecx, %edx
    addl $19, %edx
    movb %dl, 0x100(%rax,%rcx)
    incq %rcx
    cmpq $256, %rcx
    jb 2b
    retq
.data
.p2align 3
cl_lookup: .space 512
cl_output: .space 160
