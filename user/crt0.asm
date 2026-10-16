; Madire OS user runtime (0.10): the entry point of every built-in program.
;
; 1.0 stage 7: System V start-up (x86-64 ABI). The kernel starts a program
; here in ring 3 with RSP at its start block (16-byte aligned): argc, then
; argv[0..argc-1] and NULL, envp[] and NULL, then the auxiliary vector
; (kernel/proc/loadplan.h); every general register is 0. main(argc, argv,
; envp) runs, and its return value becomes the exit status.

bits 64
section .text.start progbits alloc exec nowrite align=16
global _start
extern main

_start:
    xor ebp, ebp                    ; the outermost frame
    mov rdi, [rsp]                  ; argc
    lea rsi, [rsp + 8]              ; argv
    lea rdx, [rsi + rdi * 8 + 8]    ; envp: after argv's NULL
    call main                       ; RSP aligned at the call (System V ABI)
    mov edi, eax                    ; status
    mov eax, 60                     ; exit
    syscall
    ud2                             ; exit never returns

section .note.GNU-stack noalloc noexec nowrite progbits
