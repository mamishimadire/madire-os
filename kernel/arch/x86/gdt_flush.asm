; Madire OS - load the GDT and reload all segment registers.
; void gdt_flush(const struct gdt_ptr *gdtr);

section .text
global gdt_flush

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]

    mov ax, 0x10                    ; GDT_KERNEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.reload_cs             ; far jump loads CS = GDT_KERNEL_CODE
.reload_cs:
    ret
