[bits 32]

extern exception_handler

section .text

; exceptions without a CPU error code push a fake 0 first
%macro EXC_NOERR 1
global exc%1
exc%1:
    push dword 0
    push dword %1
    jmp exc_common
%endmacro

; exceptions where the CPU already pushed an error code
%macro EXC_ERR 1
global exc%1
exc%1:
    push dword %1
    jmp exc_common
%endmacro

EXC_NOERR 0
EXC_NOERR 1
EXC_NOERR 2
EXC_NOERR 3
EXC_NOERR 4
EXC_NOERR 5
EXC_NOERR 6
EXC_NOERR 7
EXC_ERR   8
EXC_NOERR 9
EXC_ERR   10
EXC_ERR   11
EXC_ERR   12
EXC_ERR   13
EXC_ERR   14
EXC_NOERR 15
EXC_NOERR 16
EXC_ERR   17
EXC_NOERR 18
EXC_NOERR 19
EXC_NOERR 20
EXC_ERR   21
EXC_NOERR 22
EXC_NOERR 23
EXC_NOERR 24
EXC_NOERR 25
EXC_NOERR 26
EXC_NOERR 27
EXC_NOERR 28
EXC_ERR   29
EXC_ERR   30
EXC_NOERR 31

exc_common:
    pusha
    push esp                ; pointer to the saved registers
    call exception_handler
    add esp, 4
    popa
    add esp, 8              ; drop vector + error code
    iret

section .data
global exc_table
exc_table:
%assign i 0
%rep 32
    dd exc %+ i
%assign i i+1
%endrep