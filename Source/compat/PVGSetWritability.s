.text
.globl _pvgSetWritabilityConservative
.p2align 4, 0x90
_pvgSetWritabilityConservative:
    # AppleParavirtGPUMetal's private setWritability contract is:
    #
    #   function, buffer bitmap, buffer slot count,
    #             texture bitmap, texture slot count
    #
    # Catalina cannot decode Big Sur's MTLPSBIN reflection payload.  Until the
    # reply translator is installed, conservatively mark every declared slot
    # writable.  The bitmaps use one uint32_t for each 32 resource slots.
    testq %rsi, %rsi
    je 2f
    movl %edx, %eax
    addl $31, %eax
    shrl $5, %eax
    je 2f
    movl %eax, %r9d
    movl $-1, %eax
1:
    movl %eax, (%rsi)
    addq $4, %rsi
    decl %r9d
    jne 1b
2:
    testq %rcx, %rcx
    je 4f
    movl %r8d, %eax
    addl $31, %eax
    shrl $5, %eax
    je 4f
    movl %eax, %r9d
    movl $-1, %eax
3:
    movl %eax, (%rcx)
    addq $4, %rcx
    decl %r9d
    jne 3b
4:
    retq
