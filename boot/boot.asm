; Madire OS - boot entry point (x86-64)
;
; The boot loader (Limine since 0.13, or QEMU's own Multiboot loader) loads the kernel image, finds the
; Multiboot header below, and jumps to _start in 32-bit protected mode:
;   eax = 0x2BADB002 (Multiboot magic), ebx = physical address of multiboot info
;   interrupts disabled, paging off, no usable stack.
;
; This stub (still 32-bit code) checks that the CPU can run a 64-bit kernel,
; builds temporary page tables, switches to 64-bit long mode, jumps to the
; kernel's higher-half address and calls kernel_main(magic, info). If the
; CPU can't run it, a message is shown and the CPU halts: no mode switch is
; attempted, so there is no triple fault.
;
; The stub runs at its load address (linker/linker.ld: .boot is linked at
; 1 MiB); the rest of the kernel is linked at 0xFFFFFFFF80000000 + physical
; (0.8). 32-bit code therefore uses PHYSICAL addresses for everything it
; touches outside .boot: `symbol - KERNEL_VIRT_OFFSET`.
;
; Temporary boot tables, 2 MiB pages, read/write:
;   PML4[0]    -> PDPT -> 4 page directories: identity 0-4 GiB, INCLUDING
;                 page 0 (the stub, memory set-up and the boot self-test)
;   PML4[256]  -> the same PDPT: the physical view, 0xFFFF800000000000 +
;                 physical, 0-4 GiB (VGA and boot data from the first C line)
;   PML4[511]  -> PDPT_high, entry 510 -> the first page directory: the
;                 kernel, 0xFFFFFFFF80000000 + physical, 0-1 GiB
; They stay in use until paging_init() loads the kernel's own 4-level
; tables, which map only what is needed (and leave page 0 unmapped).

KERNEL_VIRT_OFFSET equ 0xFFFFFFFF80000000   ; = VM_KERNEL_VIRT_OFFSET

MB_ALIGN    equ 1 << 0              ; align loaded modules on page boundaries
MB_MEMINFO  equ 1 << 1              ; ask the loader for a memory map
MB_AOUT     equ 1 << 16             ; the load addresses below are valid
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO | MB_AOUT
MB_MAGIC    equ 0x1BADB002
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

COM1        equ 0x3F8
EFER_MSR    equ 0xC0000080
EFER_LME    equ 1 << 8
CR0_PG      equ 1 << 31
CR0_WP      equ 1 << 16
CR4_PAE     equ 1 << 5

extern __kernel_phys_start, __data_end_phys, __kernel_end_phys, __bss_start_phys, kernel_main

; ---- Multiboot 1 header, with the a.out kludge address fields (physical) ----
section .multiboot
align 4
mb_header:
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    dd mb_header                    ; header_addr (.boot: virtual = physical)
    dd __kernel_phys_start          ; load_addr: the image starts here
    dd __data_end_phys              ; load_end_addr: the file ends here
    dd __kernel_end_phys            ; bss_end_addr: ALL of .bss (tables, stacks)
    dd _start                       ; entry_addr

; ---- 32-bit stub ----
section .boot32
bits 32
global _start
_start:
    cli
    mov esp, stack_top - KERNEL_VIRT_OFFSET     ; nothing is pushed before .bss is zeroed
    mov [boot_magic - KERNEL_VIRT_OFFSET], eax
    mov [boot_info - KERNEL_VIRT_OFFSET], ebx

    ; Zero .bss (tables and stacks included). The loader already does, but
    ; the stub must not depend on it.
    mov edi, __bss_start_phys
    mov ecx, __kernel_end_phys
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    rep stosd

    call serial_init
    mov esi, msg_start
    call serial_puts

    ; CPUID present? Only CPUs with CPUID let EFLAGS.ID (bit 21) change.
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    test eax, 1 << 21
    jz .no_cpuid

    mov eax, 0x80000000             ; highest extended leaf
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29               ; LM: long mode
    jz .no_long_mode
    mov eax, 1
    cpuid
    test edx, 1 << 6                ; PAE
    jz .no_pae

    mov esi, msg_cpu_ok
    call serial_puts

    ; Boot tables (all zeroed above).
    mov eax, boot_pdpt - KERNEL_VIRT_OFFSET
    or eax, 3                       ; present, writable
    mov [boot_pml4 - KERNEL_VIRT_OFFSET], eax               ; PML4[0]: identity
    mov [boot_pml4 - KERNEL_VIRT_OFFSET + 256 * 8], eax     ; PML4[256]: physical view
    mov eax, boot_pdpt_high - KERNEL_VIRT_OFFSET
    or eax, 3
    mov [boot_pml4 - KERNEL_VIRT_OFFSET + 511 * 8], eax     ; PML4[511]: kernel
    mov eax, boot_pds - KERNEL_VIRT_OFFSET
    or eax, 3
    mov [boot_pdpt_high - KERNEL_VIRT_OFFSET + 510 * 8], eax ; 0-1 GiB at -2 GiB
    xor ecx, ecx
.pdpt:
    mov eax, ecx
    shl eax, 12
    add eax, boot_pds - KERNEL_VIRT_OFFSET
    or eax, 3
    mov [boot_pdpt - KERNEL_VIRT_OFFSET + ecx * 8], eax
    inc ecx
    cmp ecx, 4
    jb .pdpt
    xor ecx, ecx                    ; 2048 entries x 2 MiB = 4 GiB
.pd:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83                    ; present, writable, 2 MiB page
    mov [boot_pds - KERNEL_VIRT_OFFSET + ecx * 8], eax
    mov eax, ecx
    shr eax, 11                     ; bits 32+ of the address (0 below 4 GiB)
    mov [boot_pds - KERNEL_VIRT_OFFSET + ecx * 8 + 4], eax
    inc ecx
    cmp ecx, 2048
    jb .pd

    mov eax, boot_pml4 - KERNEL_VIRT_OFFSET
    mov cr3, eax
    mov eax, cr4
    or eax, CR4_PAE
    mov cr4, eax
    mov ecx, EFER_MSR
    rdmsr
    or eax, EFER_LME                ; NX (EFER.NXE) is enabled later, by paging_init()
    wrmsr
    mov eax, cr0
    or eax, CR0_PG | CR0_WP         ; paging on -> long mode active (EFER.LMA)
    mov cr0, eax

    lgdt [boot_gdt_ptr]
    jmp 0x08:long_low               ; into a 64-bit code segment, still low

.no_cpuid:
    mov esi, msg_no_cpuid
    jmp halt_with_message
.no_long_mode:
    mov esi, msg_no_long_mode
    jmp halt_with_message
.no_pae:
    mov esi, msg_no_pae

; esi = message: to serial, and white on red on screen row 0; then halt.
halt_with_message:
    push esi
    call serial_puts
    pop esi
    mov edi, 0xB8000
.vga:
    lodsb
    cmp al, 10
    je .halt
    test al, al
    jz .halt
    mov ah, 0x4F
    stosw
    jmp .vga
.halt:
    cli
    hlt
    jmp .halt

; COM1: 38400 baud, 8N1, FIFO on, no interrupts (as drivers/serial/serial.c).
serial_init:
    mov dx, COM1 + 1
    xor al, al
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al
    mov dx, COM1
    mov al, 3
    out dx, al
    mov dx, COM1 + 1
    xor al, al
    out dx, al
    mov dx, COM1 + 3
    mov al, 3
    out dx, al
    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al
    ret

; esi = NUL-terminated string.
serial_puts:
.next:
    lodsb
    test al, al
    jz .done
    mov ah, al
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    mov al, ah
    mov dx, COM1
    out dx, al
    jmp .next
.done:
    ret

; 64-bit, still at the load address: jump to the kernel's high address.
bits 64
long_low:
    mov rax, long_entry             ; 64-bit absolute (higher half)
    jmp rax

; Boot GDT: null, 64-bit code (0x08, L=1), data (0x10). gdt_init() replaces it.
align 8
boot_gdt:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
boot_gdt_ptr:
    dw boot_gdt_ptr - boot_gdt - 1
    dd boot_gdt

msg_start:        db "boot32: start", 10, 0
msg_cpu_ok:       db "boot32: CPUID ok, long mode and PAE present", 10, 0
msg_no_cpuid:     db "Madire OS needs a 64-bit (x86-64) CPU: CPUID not supported. Halted.", 10, 0
msg_no_long_mode: db "Madire OS needs a 64-bit (x86-64) CPU: long mode not supported. Halted.", 10, 0
msg_no_pae:       db "Madire OS needs a 64-bit (x86-64) CPU: PAE not supported. Halted.", 10, 0

; ---- 64-bit entry, in the higher half ----
section .text
bits 64
long_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    lea rsp, [rel stack_top]        ; 16-byte aligned at the call below
    push 0
    popfq                           ; clean RFLAGS (IF off, DF clear)

    lea rsi, [rel msg_boot64]
    call puts64
    lea rsi, [rel msg_high]
    call puts64
    lea rax, [rel long_entry]       ; this code's own (high) address
    call puthex64
    mov al, 10
    call putc64

    mov edi, [rel boot_magic]       ; arg 1: Multiboot magic
    mov esi, [rel boot_info]        ; arg 2: Multiboot info address (physical)
    call kernel_main

    ; kernel_main should never return; if it does, halt forever.
    cli
.hang:
    hlt
    jmp .hang

; al = character, to COM1.
putc64:
    push rdx
    mov ah, al
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    mov al, ah
    mov dx, COM1
    out dx, al
    pop rdx
    ret

; rsi = NUL-terminated string.
puts64:
    lodsb
    test al, al
    jz .done
    call putc64
    jmp puts64
.done:
    ret

; rax = value: "0x" and 16 hex digits.
puthex64:
    push rbx
    push rcx
    mov rbx, rax
    mov al, '0'
    call putc64
    mov al, 'x'
    call putc64
    mov ecx, 16
.digit:
    rol rbx, 4
    mov eax, ebx
    and eax, 15
    cmp al, 10
    jb .num
    add al, 'a' - 10 - '0'
.num:
    add al, '0'
    call putc64
    dec ecx
    jnz .digit
    pop rcx
    pop rbx
    ret

section .rodata
msg_boot64: db "boot64: entered 64-bit long mode", 10, 0
msg_high:   db "boot64: running at the higher half, RIP=", 0

section .data
boot_magic:   dd 0
boot_info:    dd 0

; ---- temporary boot tables (inside .bss, below __kernel_end) ----
section .boot_tables nobits alloc write align=4096
boot_pml4:      resb 4096
boot_pdpt:      resb 4096
boot_pdpt_high: resb 4096
boot_pds:       resb 4096 * 4

; ---- stacks, each with an unmapped guard page below it once the kernel's
; own page tables are in use ----
section .stacks nobits alloc write align=4096
global stack_bottom, stack_top      ; visible to C: the memory manager checks
global kstack_guard                 ; the stack lies inside .bss
global ist1_guard, ist1_stack_bottom, ist1_stack_top
kstack_guard:
    resb 4096
stack_bottom:
    resb 16384                      ; 16 KiB kernel stack
stack_top:
ist1_guard:
    resb 4096
ist1_stack_bottom:
    resb 8192                       ; 8 KiB stack for double faults (TSS IST1)
ist1_stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
