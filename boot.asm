
bits 16
org 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov [boot_drive], dl

    mov si, msg
    call print

    ; load 8 sectors of stage2 at 0000:8000
    xor ax, ax
    mov es, ax
    mov bx, 0x8000

    mov ah, 0x02
    mov al, 8
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Ensure stage 2 receives the original boot drive
    mov dl, [boot_drive]
    jmp 0x0000:0x8000

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

disk_error:
    mov si, err_msg
    call print
.hang:
    cli
    hlt
    jmp .hang

boot_drive db 0
msg db "Yellowfin: loading stage 2...", 13, 10, 0
err_msg db "Disk read failed!", 0

times 510-($-$$) db 0
dw 0xAA55
