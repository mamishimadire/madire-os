; Madire OS - interrupt and exception entry stubs (x86-64).
;
; Each of the 48 handled vectors has a tiny stub that makes the stack look the
; same for every interrupt (the CPU pushes an error code for only some
; exceptions, so the others push a dummy 0), pushes its vector number, and
; jumps to isr_common. isr_common saves all general registers so the stack
; matches struct interrupt_frame (kernel/arch/x86/interrupts.h), calls the C
; dispatcher with RSP 16-byte aligned (System V ABI), restores everything
; and returns with iretq. In long mode the CPU always pushes SS:RSP.

bits 64
extern interrupt_dispatch

section .text

isr_common:
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

    cld                             ; C code expects the direction flag clear
    mov rdi, rsp                    ; argument: struct interrupt_frame *
    mov rbx, rsp                    ; rbx is preserved across the call
    and rsp, -16                    ; ABI: 16-byte aligned at the call
    call interrupt_dispatch
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
    add rsp, 16                     ; drop vector number and error code
    iretq

; Stubs. Vectors 8, 10-14, 17, 21, 29 and 30 get an error code from the CPU.
%assign i 0
%rep 48
isr_stub_%[i]:
  %if (i == 8) || (i == 10) || (i == 11) || (i == 12) || (i == 13) || (i == 14) || (i == 17) || (i == 21) || (i == 29) || (i == 30)
  %else
    push qword 0                    ; dummy error code
  %endif
    push qword i                    ; vector number
    jmp isr_common
%assign i i+1
%endrep

section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dq isr_stub_%[i]
%assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
