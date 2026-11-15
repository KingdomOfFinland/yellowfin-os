bits 16
org 0x8000

; stage2 is 8 sectors (4096 bytes) at 0x8000, loaded by boot.asm (mov al, 8).
; The kernel starts right after it, at LBA 9.
;
; The kernel is read in chunks into a small bounce buffer (0x10000) and each
; chunk is copied straight up to 1 MiB using "unreal mode" (32-bit addressing
; from real mode), so the kernel size is no longer limited by real-mode memory.
KERNEL_LBA     equ 9
KERNEL_LOAD    equ 0x10000        ; bounce buffer (real mode, segment 0x1000)
KERNEL_DEST    equ 0x100000       ; where the kernel ends up (1 MiB)

; Number of kernel sectors to read. Best passed in by the build so it matches
; kernel.bin exactly:  nasm -f bin -dKERNEL_SECTORS=<n> stage2.asm
; This default is only used when the build does not pass it.
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 144
%endif

; 64 sectors = 32 KB per chunk: safe on every BIOS and never crosses a 64 KB boundary.
CHUNK          equ 64

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld
    sti

    mov [boot_drive], dl

    mov si, msg
    call print

    ; A20 on early: the kernel is copied above 1 MiB while loading
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al

    ; BIOS Extended Disk Drive (EDD) reads if available, otherwise CHS
    mov byte [use_edd], 0
    mov bx, 0x55AA
    mov ah, 0x41
    mov dl, [boot_drive]
    int 0x13
    jc .no_edd
    cmp bx, 0xAA55
    jne .no_edd
    test cx, 1
    jz .no_edd
    mov byte [use_edd], 1
.no_edd:

    mov word [cur_lba], KERNEL_LBA
    mov word [cur_lba + 2], 0
    mov word [sectors_left], KERNEL_SECTORS
    mov dword [dest], KERNEL_DEST

.chunk_loop:
    mov ax, [sectors_left]
    cmp ax, CHUNK
    jbe .have_count
    mov ax, CHUNK
.have_count:
    mov [chunk_cnt], ax

    call read_chunk              ; sectors -> bounce buffer (or disk_error)
    call copy_chunk              ; bounce buffer -> [dest], dest advances

    mov ax, [chunk_cnt]
    add word [cur_lba], ax
    adc word [cur_lba + 2], 0
    sub [sectors_left], ax
    jnz .chunk_loop

    ; Get BIOS 8x16 font in real mode
    mov ax, 0x1130
    mov bh, 0x06
    int 0x10

    push ds
    push es
    push si
    push di

    mov si, bp
    mov ax, es
    mov ds, ax

    xor ax, ax
    mov es, ax
    mov di, 0x9400
    mov cx, 4096
    cld
    rep movsb

    pop di
    pop si
    pop es
    pop ds

find_mode:
    xor ax, ax
    mov es, ax
    mov di, 0x9200
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .fallback

    mov si, [0x920E]
    mov ax, [0x9210]
    mov fs, ax

.next:
    mov cx, [fs:si]
    add si, 2
    cmp cx, 0xFFFF
    je .fallback

    push si
    xor ax, ax
    mov es, ax
    mov di, 0x9000
    mov ax, 0x4F01
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .next

    test word [0x9000], 0x80
    jz .next
    cmp word [0x9012], 1280
    jne .next
    cmp word [0x9014], 720
    jne .next
    cmp byte [0x9019], 32
    jne .next

    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .fallback
    mov byte [0x8FF0], 1
    jmp .graphics_done

.fallback:
    mov ax, 0x4F01
    mov cx, 0x0112
    xor di, di
    mov es, di
    mov di, 0x9000
    int 0x10

    cmp ax, 0x004F
    jne .no_vbe
    test word [0x9000], 0x0080
    jz .no_vbe
    cmp byte [0x9019], 24
    jne .no_vbe

    mov ax, 0x4F02
    mov bx, 0x4112
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe

    mov byte [0x8FF0], 1
    jmp .graphics_done

.no_vbe:
    mov byte [0x8FF0], 0

.graphics_done:
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

; ---------------------------------------------------------------------------
; read_chunk: read [chunk_cnt] sectors starting at [cur_lba] to 0x1000:0000.
; Uses EDD when available (falls back to CHS if EDD keeps failing).
read_chunk:
    cmp byte [use_edd], 0
    je .chs

    mov ax, [chunk_cnt]
    mov [dap_count], ax
    mov ax, [cur_lba]
    mov [dap_lba], ax
    mov ax, [cur_lba + 2]
    mov [dap_lba + 2], ax
    mov byte [retries], 3

.edd_try:
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .done

    mov [edd_error], ah
    xor ah, ah                   ; reset the disk, then retry
    mov dl, [boot_drive]
    int 0x13
    dec byte [retries]
    jnz .edd_try
    mov byte [use_edd], 0        ; EDD failed: use CHS from now on

.chs:
    cmp byte [geo_ok], 0
    jne .have_geo
    call get_geometry
.have_geo:
    push es
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov ax, [cur_lba]            ; CHS path handles LBAs below 65536
    mov [tmp_lba], ax
    mov si, [chunk_cnt]

.chs_sector_loop:
    mov ax, [tmp_lba]
    xor dx, dx
    movzx cx, byte [geo_spt]
    div cx                       ; ax = lba / spt, dx = lba % spt
    inc dl
    mov [chs_sector], dl
    xor dx, dx
    movzx cx, byte [geo_heads]
    div cx                       ; ax = cylinder, dx = head
    mov [chs_head], dl
    mov ch, al                   ; cylinder low 8 bits
    mov cl, ah
    shl cl, 6                    ; cylinder bits 8-9 go in cl bits 6-7
    or cl, [chs_sector]
    mov dh, [chs_head]
    mov dl, [boot_drive]
    mov ax, 0x0201
    int 0x13
    jc disk_error

    add bx, 512
    inc word [tmp_lba]
    dec si
    jnz .chs_sector_loop
    pop es
.done:
    ret

; get_geometry: ask the BIOS for the drive geometry (default 18 sectors, 2 heads)
get_geometry:
    mov byte [geo_spt], 18
    mov byte [geo_heads], 2
    mov byte [geo_ok], 1
    push es
    xor ax, ax
    mov es, ax
    xor di, di
    mov ah, 0x08
    mov dl, [boot_drive]
    int 0x13
    pop es
    jc .gg_done
    mov al, cl
    and al, 0x3F
    jz .gg_done
    mov [geo_spt], al
    inc dh
    mov [geo_heads], dh
.gg_done:
    ret

; copy_chunk: copy chunk_cnt*512 bytes from the bounce buffer to [dest] (above 1 MiB)
; using unreal mode, then advance [dest].
copy_chunk:
    call enter_unreal            ; fs now has a flat 4 GB limit
    movzx ecx, word [chunk_cnt]
    shl ecx, 7                   ; sectors * 512 / 4 dwords
    mov esi, KERNEL_LOAD
    mov edi, [dest]
.copy:
    mov eax, [fs:esi]
    mov [fs:edi], eax
    add esi, 4
    add edi, 4
    dec ecx
    jnz .copy
    mov [dest], edi
    ret

; enter_unreal: briefly enter protected mode to load a flat 4 GB limit into fs,
; then return to real mode. fs keeps the big limit, so [fs:esi] reaches above 1 MiB.
enter_unreal:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short .pm
.pm:
    mov bx, 0x10                 ; flat data descriptor
    mov fs, bx
    and al, 0xFE
    mov cr0, eax
    sti
    ret

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
    mov [chs_error_code], ah

    mov si, err_msg
    call print

    mov si, edd_msg
    call print
    mov al, [edd_error]
    call print_hex

    mov si, chs_msg
    call print
    mov al, [chs_error_code]
    call print_hex

.hang:
    cli
    hlt
    jmp .hang

print_hex:
    push ax
    shr al, 4
    call .nibble
    pop ax
    and al, 0x0F
    call .nibble
    ret

.nibble:
    cmp al, 9
    jbe .digit
    add al, 7

.digit:
    add al, '0'
    mov ah, 0x0E
    int 0x10
    ret

boot_drive db 0
use_edd db 0
geo_ok db 0
geo_spt db 18
geo_heads db 2
chs_sector db 0
chs_head db 0
retries db 3
edd_error db 0xFF
chs_error_code db 0
chunk_cnt dw 0
sectors_left dw 0
tmp_lba dw 0
cur_lba dd 0
dest dd 0

msg db "Yellowfin: loading kernel...", 13, 10, 0
err_msg db "Kernel disk read failed!", 0
edd_msg db " EDD AH=", 0
chs_msg db " CHS AH=", 0

align 4
dap:
    db 0x10, 0
dap_count:
    dw 0
    dw 0                         ; offset 0
    dw KERNEL_LOAD >> 4          ; segment 0x1000
dap_lba:
    dq 0

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

bits 32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov esp, 0x90000
    cld

    jmp 0x08:0x00100010          ; the kernel is already at 1 MiB

%if ($ - $$) > 4080
%error "stage2 code runs into the VBE data at 0x8FF0"
%endif

times 4096-($-$$) db 0