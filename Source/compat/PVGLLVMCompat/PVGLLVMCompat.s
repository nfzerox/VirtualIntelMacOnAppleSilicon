.text
.p2align 4, 0x90

/* LLVM 5: createLoopUnrollPass(int, int, int, int, int, int)
 * LLVM 7 adds AllowPeeling as the seventh argument. */
.globl __ZN4llvm20createLoopUnrollPassEiiiiii
__ZN4llvm20createLoopUnrollPassEiiiiii:
    pushq %rbp
    movq %rsp, %rbp
    subq $16, %rsp
    movq $-1, (%rsp)
    callq __ZN4llvm20createLoopUnrollPassEiiiiiii
    leave
    retq

/* LLVM 5 accepted Threshold and a std::function.  Preserve the non-trivial
 * std::function pointer and insert LLVM 7's default boolean arguments. */
.globl __ZN4llvm27createCFGSimplificationPassEiNSt3__18functionIFbRKNS_8FunctionEEEE
__ZN4llvm27createCFGSimplificationPassEiNSt3__18functionIFbRKNS_8FunctionEEEE:
    movq %rsi, %r9
    xorl %esi, %esi
    xorl %edx, %edx
    movl $1, %ecx
    xorl %r8d, %r8d
    jmp __ZN4llvm27createCFGSimplificationPassEjbbbbNSt3__18functionIFbRKNS_8FunctionEEEE

/* LLVM 7 added an optional DeferredDominance argument. */
.globl __ZN4llvm25MergeBlockIntoPredecessorEPNS_10BasicBlockEPNS_13DominatorTreeEPNS_8LoopInfoEPNS_23MemoryDependenceResultsE
__ZN4llvm25MergeBlockIntoPredecessorEPNS_10BasicBlockEPNS_13DominatorTreeEPNS_8LoopInfoEPNS_23MemoryDependenceResultsE:
    xorl %r8d, %r8d
    jmp __ZN4llvm25MergeBlockIntoPredecessorEPNS_10BasicBlockEPNS_13DominatorTreeEPNS_8LoopInfoEPNS_23MemoryDependenceResultsEPNS_17DeferredDominanceE

/* The legacy instruction-simplification pass was renamed. */
.globl __ZN4llvm31createInstructionSimplifierPassEv
__ZN4llvm31createInstructionSimplifierPassEv:
    jmp __ZN4llvm28createInstSimplifyLegacyPassEv

/* CallInst::addAttribute was inlined into CallBase in LLVM 7.  Recreate the
 * LLVM 5 out-of-line body against the LLVM 7 object and AttributeList API. */
.globl __ZN4llvm8CallInst12addAttributeEjNS_9Attribute8AttrKindE
__ZN4llvm8CallInst12addAttributeEjNS_9Attribute8AttrKindE:
    pushq %rbp
    movq %rsp, %rbp
    pushq %rbx
    pushq %rax
    movl %edx, %ecx
    movl %esi, %edx
    movq %rdi, %rbx
    movq 0x38(%rdi), %rax
    leaq -0x10(%rbp), %rdi
    movq %rax, (%rdi)
    movq (%rbx), %rax
    movq (%rax), %rsi
    callq __ZNK4llvm13AttributeList12addAttributeERNS_11LLVMContextEjNS_9Attribute8AttrKindE
    movq %rax, 0x38(%rbx)
    addq $8, %rsp
    popq %rbx
    popq %rbp
    retq
