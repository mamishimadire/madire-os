; Madire OS - boot entry point
;
; GRUB loads the kernel ELF, finds the Multiboot header below, switches the
; CPU to 32-bit protected mode and jumps to _start. At that point:
;   eax = 0x2BADB002 (Multiboot magic), ebx = physical address of multiboot info
;   interrupts disabled, paging off, no usable stack.
; Our job here is only to set up a stack and hand control to C.

MB_ALIGN    equ 1 << 0              ; align loaded modules on page boundaries
MB_MEMINFO  equ 1 << 1              ; ask GRUB for a memory map
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO
MB_MAGIC    equ 0x1BADB002
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 16
global stack_bottom                 ; visible to C so the memory manager can
global stack_top                    ; verify the stack lies inside .bss
stack_bottom:
    resb 16384                      ; 16 KiB kernel stack
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top
    push ebx                        ; arg 2: multiboot info pointer
    push eax                        ; arg 1: multiboot magic
    call kernel_main

    ; kernel_main should never return; if it does, halt forever.
    cli
.hang:
    hlt
    jmp .hang
