; Disposable BIOS probe. No installed OS or guest disk is opened.
bits 16
org 0x7c00
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7c00
    mov si, leaves
    mov bp, 6
.leaf:
    mov eax, [si]
    add si, 4
    xor ecx, ecx
    cpuid
    pushad
    call hex32
    mov eax, ebx
    call hex32
    mov eax, ecx
    call hex32
    mov eax, edx
    call hex32
    mov al, 10
    out 0xe9, al
    popad
    dec bp
    jnz .leaf
    mov eax, 16
    out 0xf4, eax
    hlt
hex32:
    pushad
    mov edx, eax
    mov cx, 8
.digit:
    rol edx, 4
    mov al, dl
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    out 0xe9, al
    loop .digit
    popad
    ret
leaves: dd 0x40000000, 0x40000002, 0x40000010, 1, 7, 0x80000001
times 510-($-$$) db 0
dw 0xaa55
