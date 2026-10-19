; Disposable BIOS disk. No macOS disk attached. nasm -f bin -o pcid.img this.asm
; Run with -cpu Penryn,+pcid -accel tcg -display none -serial none
; -debugcon stdio -device isa-debug-exit,iobase=0xf4,iosize=4
; -drive file=pcid.img,format=raw,if=ide. Success exits QEMU with status 33.
bits 16
org 0x7c00
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc disk_fail
    jmp 0:0x8000
disk_fail:
    mov al, 'D'
    out 0xe9, al
    hlt
    jmp disk_fail
align 4
dap: db 16, 0
    dw 32, 0x8000, 0
    dq 2
times 510-($-$$) db 0
dw 0xaa55
times 1024-($-$$) db 0
stage2:
    cli
    lgdt [gdtr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 8:pmode
align 8
gdt:
    dq 0, 0x00cf9a000000ffff, 0x00cf92000000ffff, 0x00af9a000000ffff
gdtr: dw $-gdt-1
    dd gdt
bits 32
pmode:
    mov ax, 16
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7000
    xor eax, eax
    mov edi, 0x10000
    mov ecx, 0x11000/4
    rep stosd
    mov dword [0x10000], 0x12003
    mov dword [0x11000], 0x13003
    mov dword [0x12000], 0x14003
    mov dword [0x13000], 0x15003
    mov dword [0x14000], 0x83
    mov dword [0x15000], 0x83
    mov dword [0x14008], 0x16003
    mov dword [0x15008], 0x17003
    mov dword [0x16000], 0x18003
    mov dword [0x17000], 0x19003
    mov dword [0x18000], 0x1234
    mov dword [0x19000], 0x5678
    ; Two different executable bodies at the same virtual address.
    ; mov eax, immediate; ret
    mov dword [0x18100], 0x001234b8
    mov word [0x18104], 0xc300
    mov dword [0x19100], 0x005678b8
    mov word [0x19104], 0xc300
    mov eax, 0x10000
    mov cr3, eax
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov ecx, 0xc0000080
    rdmsr
    or eax, 0x100
    wrmsr
    mov eax, cr0
    or eax, 0x80010000
    mov cr0, eax
    jmp 24:longmode
bits 64
default abs
%macro expect 2
    mov qword [resume], %2
    mov dword [expected], %1
%endmacro
%macro checkfault 0
    cmp dword [expected], 0
    jne fail
%endmacro
longmode:
%ifdef PVG_MEMORY_BENCH
    ; Optional disposable hot-load/store benchmark. No macOS disk attached.
    ; 256 cache-line-spaced nodes: 256,000,000 dependent loads and stores.
    mov rdi, 0x40000
    mov ecx, 256
.init_ring:
    lea rax, [rdi+64]
    mov [rdi], rax
    add rdi, 64
    loop .init_ring
    mov qword [rdi-64], 0x40000
    rdtsc
    shl rdx, 32
    or rax, rdx
    mov r8, rax
    mov rax, 0x40000
    mov ecx, 256000000
.memory_loop:
    mov rax, [rax]
    mov [rax+8], rax
    dec ecx
    jnz .memory_loop
    cmp rax, 0x40000
    jne fail
    rdtsc
    shl rdx, 32
    or rax, rdx
    sub rax, r8
    mov r8, rax
    mov rdi, 0x40000
    mov ecx, 256
.check_ring:
    cmp [rdi+8], rdi
    jne fail
    add rdi, 64
    loop .check_ring
    ; Report TSC delta in hex; compare only with the same default TSC model.
    mov ecx, 16
.hex:
    rol r8, 4
    mov eax, r8d
    and eax, 15
    add al, '0'
    cmp al, '9'
    jbe .digit
    add al, 7
.digit:
    out 0xe9, al
    loop .hex
    mov al, 10
    out 0xe9, al
    mov eax, 16
    out 0xf4, eax
    hlt
%endif
    mov rdi, 0x20000+13*16
    mov rax, gp
    call gate
    mov rdi, 0x20000+14*16
    mov rax, pf
    call gate
    lidt [idtr]
    ; PCIDE enable with nonzero CR3 low bits must fault.
    mov eax, 0x10008
    mov cr3, rax
    mov rax, cr4
    or rax, 0x20000
    expect 13, gp_low
    mov cr4, rax
gp_low:
    checkfault
    mov eax, 0x10000
    mov cr3, rax
    mov rax, cr4
    or rax, 0x20000
    mov cr4, rax
    mov rbx, 0x200000
    mov ecx, 10000
switch_loop:
    mov rax, 0x8000000000010001
    mov cr3, rax
    mov rax, cr3
    cmp rax, 0x10001
    jne fail
    cmp dword [rbx], 0x1234
    jne fail
    ; Same full CR3+PCID preserving write must retain the mapping too.
    ; Exercise indirect dispatch immediately after that write.
    mov rax, 0x8000000000010001
    mov cr3, rax
    lea rdi, [rel same_context_target]
    jmp rdi
same_context_target:
    cmp dword [rbx], 0x1234
    jne fail
    lea rdi, [rbx+0x100]
    call rdi
    cmp eax, 0x1234
    jne fail
    mov rax, 0x8000000000011002
    mov cr3, rax
    cmp dword [rbx], 0x5678
    jne fail
    lea rdi, [rbx+0x100]
    call rdi
    cmp eax, 0x5678
    jne fail
    dec ecx
    jnz switch_loop
    ; Replace a mapping then invalidate the current PCID by flushing CR3.
    mov dword [0x17000], 0x18003
    mov eax, 0x11002
    mov cr3, rax
    cmp dword [rbx], 0x1234
    jne fail
    ; Invalidate a mapping explicitly, and check write protection.
    lea rdi, [rbx+0x100]
    call rdi
    cmp eax, 0x1234
    jne fail
    mov dword [0x17000], 0x19001
    invlpg [rbx]
    lea rdi, [rbx+0x100]
    call rdi
    cmp eax, 0x5678
    jne fail
    cmp dword [rbx], 0x5678
    jne fail
    expect 14, pf_done
    mov dword [rbx], 99
pf_done:
    checkfault
    cmp qword [fault_error], 3
    jne fail
    ; CR0.PG cannot be cleared while PCIDE is set.
    mov rax, cr0
    btr rax, 31
    expect 13, gp_pg
    mov cr0, rax
gp_pg:
    checkfault
    ; Bit 63 is illegal once PCIDE is disabled.
    mov rax, cr4
    btr rax, 17
    mov cr4, rax
    mov rax, 0x8000000000010000
    expect 13, gp_high
    mov cr3, rax
gp_high:
    checkfault
    mov rsi, passed
    call puts
    mov eax, 16
    out 0xf4, eax
    hlt
fail:
    mov rsi, failed
    call puts
    mov eax, 17
    out 0xf4, eax
    hlt
puts:
    lodsb
    test al, al
    jz .done
    out 0xe9, al
    jmp puts
.done: ret
gate:
    mov word [rdi], ax
    mov word [rdi+2], 24
    mov word [rdi+4], 0x8e00
    shr rax, 16
    mov word [rdi+6], ax
    shr rax, 16
    mov dword [rdi+8], eax
    ret
gp:
    cmp dword [expected], 13
    jne fail
    jmp fault
pf:
    cmp dword [expected], 14
    jne fail
fault:
    push rax
    mov rax, [rsp+8]
    mov [fault_error], rax
    mov rax, [resume]
    mov [rsp+16], rax
    mov dword [expected], 0
    pop rax
    add rsp, 8
    iretq
idtr: dw 4095
    dq 0x20000
resume: dq 0
expected: dd 0
fault_error: dq 0
passed: db 'PCID mapping, invalidation, protection and reserved-bit checks PASS', 10, 0
failed: db 'PCID FAIL', 10, 0
times 17408-($-$$) db 0
