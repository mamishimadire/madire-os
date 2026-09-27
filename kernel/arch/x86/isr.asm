; Madire OS - interrupt and exception entry stubs.
;
; Each of the 48 handled vectors has a tiny stub that makes the stack look the
; same for every interrupt (the CPU pushes an error code for only some
; exceptions, so the others push a dummy 0), pushes its vector number, and
; jumps to isr_common. isr_common saves all registers so the stack matches
; struct interrupt_frame (kernel/arch/x86/interrupts.h), calls the C
; dispatcher, restores everything and returns with iret.

extern interrupt_dispatch

section .text

isr_common:
    pusha                           ; eax, ecx, edx, ebx, esp, ebp, esi, edi
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10                    ; GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld                             ; C code expects the direction flag clear
    push esp                        ; argument: struct interrupt_frame *
    call interrupt_dispatch
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                      ; drop vector number and error code
    iret

; Stubs. Vectors 8, 10-14, 17, 21, 29 and 30 get an error code from the CPU.
%assign i 0
%rep 48
isr_stub_%[i]:
  %if (i == 8) || (i == 10) || (i == 11) || (i == 12) || (i == 13) || (i == 14) || (i == 17) || (i == 21) || (i == 29) || (i == 30)
  %else
    push dword 0                    ; dummy error code
  %endif
    push dword i                    ; vector number
    jmp isr_common
%assign i i+1
%endrep

section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 48
    dd isr_stub_%[i]
%assign i i+1
%endrep
