; Madire OS user runtime (0.10): the entry point of every built-in program.
;
; The kernel starts a program here in ring 3 with RDI = its argument (one
; integer, chosen by whoever started it), RSP = the top of its stack
; (16-byte aligned) and every other register 0. main(arg) runs, and its
; return value becomes the exit status.

bits 64
section .text.start progbits alloc exec nowrite align=16
global _start
extern main

_start:
    xor ebp, ebp                    ; the outermost frame
    call main                       ; RSP aligned at the call (System V ABI)
    mov edi, eax                    ; status
    mov eax, 60                     ; exit
    syscall
    ud2                             ; exit never returns

section .note.GNU-stack noalloc noexec nowrite progbits
