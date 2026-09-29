; Madire OS - load the GDT and reload all segment registers (x86-64).
; void gdt_flush(const struct gdt_ptr *gdtr);   rdi = gdtr

bits 64
section .text
global gdt_flush

gdt_flush:
    lgdt [rdi]

    mov ax, 0x10                    ; GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Reload CS = GDT_KERNEL_CODE with a far return to the next instruction.
    pop rax                         ; our return address
    push qword 0x08
    push rax
    retfq

section .note.GNU-stack noalloc noexec nowrite progbits
