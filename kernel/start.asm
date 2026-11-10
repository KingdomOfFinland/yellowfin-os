
MBALIGN  equ 1<<0
MEMINFO  equ 1<<1
FLAGS    equ MBALIGN | MEMINFO
MAGIC    equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)

%macro BEEP 0
    mov al, 0xB6
    out 0x43, al
    mov al, 0xA9            ; divisor 1193 = ~1 kHz
    out 0x42, al
    mov al, 0x04
    out 0x42, al
    in  al, 0x61
    or  al, 3
    out 0x61, al
    mov ecx, 0x18000000     ; beep length, tune to taste
%%on:
    dec ecx
    jnz %%on
    in  al, 0x61
    and al, 0xFC
    out 0x61, al
    mov ecx, 0x18000000     ; silence between beeps
%%off:
    dec ecx
    jnz %%off
%endmacro

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kernel_main
extern __bss_start
extern __bss_end

_start:
	cli
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb
	
    ; Write START directly to VGA text memory
    mov word [0xB8000], 0x0F53
    mov word [0xB8002], 0x0F54
    mov word [0xB8004], 0x0F41
    mov word [0xB8006], 0x0F52
    mov word [0xB8008], 0x0F54

    mov esp, stack_top
    xor ebp, ebp

    ; Sharkloader provides no Multiboot information
    push dword 0
    push dword 0
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang