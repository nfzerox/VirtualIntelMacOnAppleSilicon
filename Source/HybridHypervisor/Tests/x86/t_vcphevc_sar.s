// Standalone fixture only. Both marked bodies are the exact x86_64 VCPHEVC bytes [0xd9033,0xd904c).
// Context is local data, stack is supplied by these wrappers; no guest addresses, callbacks or TLS.
.text
.globl _vcp_case, _vcp_batch, _vcp_case_body, _vcp_case_end, _vcp_batch_body, _vcp_batch_end

// rdi=in {rdx,rcx,rax,rsi}, rsi=out {rdx,rcx,r8,r11,rax,rsi,defined_TEST_flags,store}.
_vcp_case:
    pushq %rbp
    pushq %r12
    subq $128, %rsp
    leaq 96(%rsp), %rbp
    movq %rsi, %r12
    movq %rdi, %r10
    movq 0(%r10), %rdx
    movq 8(%r10), %rcx
    movq 16(%r10), %rax
    movq 24(%r10), %rsi
    leaq vcp_context(%rip), %rdi
_vcp_case_body:
    .byte 0x66,0xc1,0xfa,0x09,0x44,0x8d,0x04,0x81,0x4c,0x8b,0x5f,0x20,0x48
    .byte 0x8b,0x47,0x28,0x48,0x89,0x45,0xa8,0x0f,0xbe,0xc2,0x85,0xf6
_vcp_case_end:
    pushfq
    popq %r9
    andl $0x8c5, %r9d // TEST defines CF/PF/ZF/SF/OF; AF and system flags are not compared.
    movq %rdx, 0(%r12)
    movq %rcx, 8(%r12)
    movq %r8, 16(%r12)
    movq %r11, 24(%r12)
    movq %rax, 32(%r12)
    movq %rsi, 40(%r12)
    movq %r9, 48(%r12)
    movq -0x58(%rbp), %r10
    movq %r10, 56(%r12)
    addq $128, %rsp
    popq %r12
    popq %rbp
    retq

// rdi=iterations (positive), rsi=seed. Hash every output field for every deterministic input.
_vcp_batch:
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $128, %rsp
    leaq 96(%rsp), %rbp
    movq %rdi, %r12
    movq %rsi, %r13
    movabsq $0xcbf29ce484222325, %r14
    leaq vcp_context(%rip), %r15
1:
    movq %r13, %rdx
    movq %r13, %rcx
    notq %rcx
    leaq 0x13579(%r13,%r13,4), %rax
    leal (%r13,%r13,2), %esi
    movq %r15, %rdi
_vcp_batch_body:
    .byte 0x66,0xc1,0xfa,0x09,0x44,0x8d,0x04,0x81,0x4c,0x8b,0x5f,0x20,0x48
    .byte 0x8b,0x47,0x28,0x48,0x89,0x45,0xa8,0x0f,0xbe,0xc2,0x85,0xf6
_vcp_batch_end:
    pushfq
    popq %r9
    andl $0x8c5, %r9d
    .macro fold reg
    xorq \reg, %r14
    imulq $0x1000193, %r14, %r14
    .endm
    fold %rdx
    fold %rcx
    fold %r8
    fold %r11
    fold %rax
    fold %rsi
    fold %r9
    movq -0x58(%rbp), %r10
    fold %r10
    leaq 1(%r13), %r13
    decq %r12
    jnz 1b
    movq %r14, %rax
    addq $128, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    retq

.data
.p2align 3
vcp_context:
    .space 32
    .quad 0x5a5a1234abcdef01
    .quad 0xa5a5fedc98765432
