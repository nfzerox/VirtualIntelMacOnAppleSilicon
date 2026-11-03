; Disposable BIOS boot sector: measure RDTSC against the emulated PIT.
; No macOS disk or guest services. See check-tcg-tsc-frequency.py.
bits 16
org 0x7c00
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7c00
    mov bp, 5
again:
    in al, 0x61
    and al, 0xfc             ; gate channel 2 off, speaker off
    out 0x61, al
    mov al, 0xb0             ; channel 2, low/high count, mode 0
    out 0x43, al
    mov ax, 60000
    out 0x42, al
    mov al, ah
    out 0x42, al
    rdtsc
    mov esi, eax
    mov edi, edx
    in al, 0x61
    or al, 1
    out 0x61, al
wait_pit:
    in al, 0x61
    test al, 0x20
    jz wait_pit
    rdtsc
    sub eax, esi
    sbb edx, edi
    ; At ordinary frequencies this 50-ms delta fits 32 bits. Print high too.
    mov ebx, eax
    mov eax, edx
    call hex32
    mov eax, ebx
    call hex32
    mov al, 10
    out 0xe9, al
    dec bp
    jnz again
    mov eax, 16
    out 0xf4, eax
    hlt
hex32:
    push cx
    push edx
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
    pop edx
    pop cx
    ret
times 510-($-$$) db 0
dw 0xaa55
