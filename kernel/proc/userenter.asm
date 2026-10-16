; Madire OS - entering user mode (x86-64, 0.10).
;
; enter_user(const struct user_iret *frame)   never returns
;   Starts a program in ring 3: pushes the five-word IRETQ frame (SS, RSP,
;   RFLAGS, CS, RIP; built and checked by kernel/proc/userentry.h), clears
;   every general register (1.0 stage 7: System V start-up as Linux does it;
;   RSP is the start block, argc there, kernel/proc/loadplan.h; until then
;   RDI held one integer argument), so nothing of the kernel's leaks into
;   the program, and executes iretq. The thread's address space must
;   already hold the program (CR3), and the TSS's RSP0 is its own kernel
;   stack top (set on every switch), so the first interrupt from ring 3
;   lands there; the kernel frames below this call are never returned to.
;
;   Interrupts are disabled while the frame is built; iretq loads the
;   program's RFLAGS (IF=1), so the first instruction in ring 3 already
;   runs with interrupts on. The data segment registers are left alone:
;   iretq to ring 3 loads null into any of DS, ES, FS, GS that holds a
;   ring-0 segment, and 64-bit code doesn't use them.

bits 64
section .text

global enter_user

enter_user:
    cli
    push qword [rdi + 32]           ; SS
    push qword [rdi + 24]           ; RSP
    push qword [rdi + 16]           ; RFLAGS
    push qword [rdi + 8]            ; CS
    push qword [rdi]                ; RIP
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
