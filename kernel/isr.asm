
BITS 32

section .text
global irq0_stub
extern timer_handler

irq0_stub:
    pusha
    cld
    call timer_handler
    mov al, 0x20
    out 0x20, al
    popa
    iretd
