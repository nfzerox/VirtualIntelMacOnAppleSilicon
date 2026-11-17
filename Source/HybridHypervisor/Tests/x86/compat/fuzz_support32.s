// fuzz_support.s for 32-bit code: state loading and saving around one instruction form. The layout of fuzz_st is fuzz_driver.c's: r[16] (+0), xmm[16] (+128), rflags (+384),
// mxcsr (+392), esp difference (+400). r[8] on are unused here and hash as they were left.
.set ST_X, 128
.set ST_FLAGS, 384
.set ST_MX, 392
.set ST_RSP, 400

.text
.globl fuzz_load, fuzz_load_fp, fuzz_save, fuzz_call, fuzz_st

fuzz_call: // 4(%esp): form entry
    movl 4(%esp), %eax
    pushl %ebx
    pushl %ebp
    pushl %esi
    pushl %edi
    movl %esp, saved_esp
    movl %eax, form_entry
    subl $64, %esp
    jmp *form_entry

fuzz_load_fp:
    movaps fuzz_st+ST_X+0, %xmm0
    movaps fuzz_st+ST_X+16, %xmm1
    movaps fuzz_st+ST_X+32, %xmm2
    movaps fuzz_st+ST_X+48, %xmm3
    movaps fuzz_st+ST_X+64, %xmm4
    ldmxcsr fuzz_st+ST_MX
fuzz_load:
    pushl fuzz_st+ST_FLAGS
    popfl
    movl fuzz_st+8, %ecx
    movl fuzz_st+16, %edx
    movl fuzz_st+24, %ebx
    movl fuzz_st+40, %ebp
    movl fuzz_st+48, %esi
    movl fuzz_st+56, %edi
    movl fuzz_st+0, %eax
    ret

fuzz_save:
    pushfl
    movl %eax, fuzz_st+0
    popl %eax
    movl %eax, fuzz_st+ST_FLAGS
    movl %ecx, fuzz_st+8
    movl %edx, fuzz_st+16
    movl %ebx, fuzz_st+24
    movl %ebp, fuzz_st+40
    movl %esi, fuzz_st+48
    movl %edi, fuzz_st+56
    xorl %eax, %eax
    movl %eax, fuzz_st+4
    movl %eax, fuzz_st+12
    movl %eax, fuzz_st+20
    movl %eax, fuzz_st+28
    movl %eax, fuzz_st+44
    movl %eax, fuzz_st+52
    movl %eax, fuzz_st+60
    movaps %xmm0, fuzz_st+ST_X+0
    movaps %xmm1, fuzz_st+ST_X+16
    movaps %xmm2, fuzz_st+ST_X+32
    movaps %xmm3, fuzz_st+ST_X+48
    movaps %xmm4, fuzz_st+ST_X+64
    stmxcsr fuzz_st+ST_MX
    movl %esp, %ecx
    movl saved_esp, %esp
    subl %ecx, %esp
    movl %esp, fuzz_st+ST_RSP
    movl %eax, fuzz_st+ST_RSP+4
    movl saved_esp, %esp
    popl %edi
    popl %esi
    popl %ebp
    popl %ebx
    ret

.data
.p2align 2
saved_esp: .long 0
form_entry: .long 0
.p2align 6
fuzz_st: .space 512
.globl fuzz_x87_in, fuzz_x87_out // fuzz_driver.c names them; no form here uses them
fuzz_x87_in: .space 1024
fuzz_x87_out: .space 512
