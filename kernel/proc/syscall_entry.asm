; Madire OS - the SYSCALL entry (x86-64, 0.10).
;
; SYSCALL (enabled by syscall_init, kernel/proc/syscall.c) arrives here in
; ring 0 with CS = 0x08 and SS = 0x10 (STAR), RCX = the user RIP, R11 = the
; user RFLAGS, and RFLAGS with TF, DF, IF, IOPL, NT and AC cleared
; (SFMASK). RSP is still the program's: it is never used as a stack, only
; saved. With interrupts off and one CPU, one saved word is enough.
;
; The entry builds the same frame as an interrupt from ring 3
; (struct interrupt_frame, kernel/arch/x86/interrupts.h) at the top of the
; thread's kernel stack (the TSS's RSP0): SS 0x33, the user RSP, RFLAGS,
; CS 0x3B, RIP, error code 0, "vector" 256, then the general registers.
; syscall_dispatch handles the call (RAX = number, RDI, RSI, RDX, R10, R8,
; R9 = arguments), sets RAX to the result, RCX and R11 to the return
; address and the cleaned RFLAGS (as SYSRET would), and the return is the
; same IRETQ as for an interrupt (no SYSRET). Every other register comes
; back as it was.

bits 64
extern syscall_dispatch
extern gdt_rsp0_slot
extern cpuprot_smap_active
extern cpuprot_clac_count

section .bss
align 8
syscall_user_rsp: resq 1

section .text
global syscall_entry

syscall_entry:
    mov [rel syscall_user_rsp], rsp
    mov rsp, [rel gdt_rsp0_slot]    ; &tss.rsp[0]
    mov rsp, [rsp]                  ; this thread's kernel stack top
    push qword 0x33                 ; SS (user data, RPL 3)
    push qword [rel syscall_user_rsp]
    push r11                        ; RFLAGS
    push qword 0x3B                 ; CS (user code, RPL 3)
    push rcx                        ; RIP
    push qword 0                    ; error code
    push qword 256                  ; not a CPU vector: a system call
    ; As in isr.asm: with SMAP on, clac on every entry (SFMASK has already
    ; cleared AC; this keeps the rule the same for every kernel entry).
    cmp byte [rel cpuprot_smap_active], 0
    je .smap_done
    clac
    inc qword [rel cpuprot_clac_count]
.smap_done:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld
    mov rdi, rsp                    ; struct interrupt_frame *
    mov rbx, rsp
    and rsp, -16
    call syscall_dispatch           ; returns with interrupts off
    mov rsp, rbx

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16                     ; vector and error code
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
