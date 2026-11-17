// Narrow keeper fixture only. Position independent, copied to a private user page.
// Mailbox offsets are asserted against C in keeper-smp.c. No external references.
.text
.globl _keeper_user_blob
_keeper_user_blob:
    movq %rdi, %r12
    movq 0(%r12), %r13              // warm_target
    call *%r13
    movq %rax, 48(%r12)            // warm result
    cmpq $0, 24(%r12)              // wait_for_go
    je 4f
    movq 16(%r12), %r14            // epoch
    movq %r14, 32(%r12)            // READY: after warm call, before mutation
1:  cmpq %r14, 40(%r12)            // GO only after this CPU's shootdown ack
    je 2f
    rdtsc
    shlq $32, %rdx
    orq %rdx, %rax
    cmpq 80(%r12), %rax
    jae 5f
    pause
    jmp 1b
2:  xorl %eax, %eax
    xorl %ecx, %ecx
    cpuid                          // x86 SMC serialization, also used without INVLPG
    movq 8(%r12), %r13             // checked target, possibly expected to #PF
    call *%r13
    movq %rax, 56(%r12)
    movq $1, 64(%r12)              // value outcome
    movq %r14, 72(%r12)            // DONE
    int $66
    ud2
4:  movq %rax, 56(%r12)            // CPU0 A/B establishment: one call only
    movq $1, 64(%r12)
    movq 16(%r12), %r14
    movq %r14, 72(%r12)
    int $66
    ud2
5:  movq $1, 88(%r12)              // timeout is never an accepted outcome
    int $66
    ud2
.globl _keeper_user_blob_end
_keeper_user_blob_end:
