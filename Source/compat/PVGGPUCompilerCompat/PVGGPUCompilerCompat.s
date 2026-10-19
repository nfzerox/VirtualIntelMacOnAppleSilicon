.text
.p2align 4, 0x90

/* GPUCompiler 31001 inserted an optional function-name argument before the
 * Catalina error/output pair:
 *
 *   3902: (..., r9=diagnostics, stack[0]=moduleOut)
 *  31001: (..., r9=functionName, stack[0]=diagnostics,
 *           stack[1]=moduleOut)
 *
 * Preserve Catalina's seven-argument ABI, supply a null function name (which
 * 31001 explicitly accepts), and shift the final two arguments into their
 * Big Sur positions.  Retarget the input LLVM Module to Catalina before
 * specialization as GPUCompiler otherwise propagates its Big Sur deployment
 * target into the specialized GPUPass function.  MTLCompiler binds this
 * wrapper through an equal-length symbol rename so the call to the real 31001
 * export below cannot recurse. */
.globl _PVGGPUCompilerSpecializeFunction
_PVGGPUCompilerSpecializeFunction:
    pushq %rbp
    movq %rsp, %rbp
    subq $64, %rsp
    movq %rdi, 16(%rsp)
    movq %rsi, 24(%rsp)
    movq %rdx, 32(%rsp)
    movq %rcx, 40(%rsp)
    movq %r8, 48(%rsp)
    movq %r9, 56(%rsp)
    movq %rdx, %rdi
    callq _PVGDownlevelModuleTarget
    movq 16(%rsp), %rdi
    movq 24(%rsp), %rsi
    movq 32(%rsp), %rdx
    movq 40(%rsp), %rcx
    movq 48(%rsp), %r8
    movq 56(%rsp), %r9
    movq %r9, (%rsp)
    movq 16(%rbp), %rax
    movq %rax, 8(%rsp)
    xorl %r9d, %r9d
    callq _MTLGPUCompilerSpecializeFunction
    movl %eax, 12(%rsp)
    testl %eax, %eax
    je 1f
    movq 16(%rbp), %rax
    testq %rax, %rax
    je 1f
    movq (%rax), %rdi
    testq %rdi, %rdi
    je 1f
    callq _MTLObjectGetAsModule
    movq %rax, %rdi
    callq _PVGDownlevelModuleTarget
1:
    movl 12(%rsp), %eax
    leave
    retq

/* GPUCompiler 31001 generalized the single-module LLVM 5 entry point into an
 * array-based entry point.  Convert the one Module* to a one-element array and
 * preserve the two output pointers' stack ordering. */
.globl _MTLGPUCompilerLoadModule
_MTLGPUCompilerLoadModule:
    pushq %rbp
    movq %rsp, %rbp
    subq $32, %rsp
    movq %r9, (%rsp)
    movq 16(%rbp), %rax
    movq %rax, 8(%rsp)
    movq %rdx, 16(%rsp)
    leaq 16(%rsp), %rdx
    movl $1, %ecx
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    callq _MTLGPUCompilerLoadModules
    movl %eax, 24(%rsp)
    movq 16(%rbp), %rax
    movq (%rax), %rdi
    callq _MTLObjectGetAsModule
    movq %rax, %rdi
    callq _PVGDownlevelModuleTarget
    movl 24(%rsp), %eax
    leave
    retq

/* Catalina calls the monolithic source path for newLibraryWithSource:.  Keep
 * its ten-argument ABI intact, then downlevel the tagged AIR module returned
 * through the final output pointer before MTLCompiler serializes its reply. */
.globl _PVGGPUCompilerBuildFromSource
_PVGGPUCompilerBuildFromSource:
    pushq %rbp
    movq %rsp, %rbp
    subq $80, %rsp
    movq %rdi, 32(%rsp)
    movq %rsi, 40(%rsp)
    movq %rdx, 48(%rsp)
    movq %rcx, 56(%rsp)
    movq %r8, 64(%rsp)
    movq %r9, 72(%rsp)
    movq 16(%rbp), %rax
    movq %rax, (%rsp)
    movq 24(%rbp), %rax
    movq %rax, 8(%rsp)
    movq 32(%rbp), %rax
    movq %rax, 16(%rsp)
    callq _MTLGPUCompilerBuildFromSource
    movl %eax, 24(%rsp)
    movq 32(%rbp), %rax
    movq (%rax), %rdi
    callq _MTLObjectGetAsModule
    movq %rax, %rdi
    callq _PVGDownlevelModuleTarget
    movl 24(%rsp), %eax
    leave
    retq
