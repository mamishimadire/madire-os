; Madire OS - kernel thread context switch (x86-64, 0.9).
;
; switch_to(uint64_t *save_rsp, uint64_t new_rsp)
;   Saves RFLAGS and the callee-saved registers (rbx, rbp, r12-r15) on the
;   current stack, stores RSP in *save_rsp, loads new_rsp, restores that
;   thread's callee-saved registers and RFLAGS, and returns on its stack.
;   The System V ABI lets a called function clobber every other general
;   register, so nothing else needs saving. RFLAGS is switched so that no
;   flag (DF, AC, ...) leaks from one thread to the next on any path; every
;   caller switches with interrupts disabled, so the saved IF is 0, and each
;   thread then restores its own IF (a yield through irq_restore, a
;   preempted thread through iretq, a new thread with sti in thread_start).
;
; A new thread's stack is built by thread_create() so that switch_to
; "returns" into thread_trampoline with r12 = entry function, r13 = its
; argument, RFLAGS = 0x2 (every flag clear) and RSP 16-byte aligned:
;   top-8, top-16   0 (padding; keeps the call below aligned)
;   top-24          thread_trampoline (the return address)
;   top-32          RFLAGS 0x2
;   top-40 .. -80   rbp, rbx, r12 = fn, r13 = arg, r14, r15

bits 64
section .text

global switch_to
global thread_trampoline
global switch_test_regs
extern thread_start
extern thread_yield

switch_to:
    pushfq
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    popfq
    ret

thread_trampoline:
    mov rdi, r12                    ; fn
    mov rsi, r13                    ; arg
    call thread_start               ; enables interrupts, runs fn, exits
    ud2                             ; thread_start never returns

; uint32_t switch_test_regs(uint64_t seed)  (test=threads)
;   Puts seed+1 .. seed+6 in rbx, rbp, r12, r13, r14, r15, yields, and
;   returns a bit mask of the registers that no longer hold their value
;   (bit 0 rbx, 1 rbp, 2 r12, 3 r13, 4 r14, 5 r15). 0 means the switch
;   away and back preserved every callee-saved register.
switch_test_regs:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 8                      ; [rsp] = seed; also aligns the call
    mov [rsp], rdi
    lea rbx, [rdi + 1]
    lea rbp, [rdi + 2]
    lea r12, [rdi + 3]
    lea r13, [rdi + 4]
    lea r14, [rdi + 5]
    lea r15, [rdi + 6]
    call thread_yield
    xor eax, eax
    mov rdx, [rsp]
    lea rcx, [rdx + 1]
    cmp rbx, rcx
    je .rbp
    or eax, 1
.rbp:
    lea rcx, [rdx + 2]
    cmp rbp, rcx
    je .r12
    or eax, 2
.r12:
    lea rcx, [rdx + 3]
    cmp r12, rcx
    je .r13
    or eax, 4
.r13:
    lea rcx, [rdx + 4]
    cmp r13, rcx
    je .r14
    or eax, 8
.r14:
    lea rcx, [rdx + 5]
    cmp r14, rcx
    je .r15
    or eax, 16
.r15:
    lea rcx, [rdx + 6]
    cmp r15, rcx
    je .done
    or eax, 32
.done:
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
