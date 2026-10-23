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
%ifdef PROBE
; 1.2 stage 3: the probe kernel (make probe) also asks for a video mode.
MB_VIDEO    equ 1 << 2
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO | MB_VIDEO | MB_AOUT
%else
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO | MB_AOUT
%endif
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
%ifdef PROBE
    dd 0                            ; mode_type: linear framebuffer
    dd 1280                         ; width (tests/probe_fb.py patch rewrites
    dd 800                          ; height  these three for the probe's
    dd 32                           ; depth   other modes)
%endif

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

%ifdef PROBE
    call probe_delay                ; before the first serial byte
%endif
    call serial_init
    mov esi, msg_start
    call serial_puts
%ifdef PROBE
    call probe_report
%endif

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

%ifdef PROBE
; ---- 1.2 stage 3: the probe (test-only; docs/plans/1.2.md, stage 3) ----
; Assembled only with -DPROBE, into its own BUILD directory (make probe):
; the real kernel contains none of it. The header asks for a linear
; framebuffer (flag bit 2). With "probedelay" on the command line the stub
; waits 4-5 s before its first serial byte, so the screen Limine leaves
; between its mode set and the kernel's first byte can be captured. After
; "boot32: start" it reports the header's request and the Multiboot
; information's video fields; with "probeaux" it also enables the i8042's
; aux port (the PS/2 mouse), reports every byte it reads for about 10 s,
; and disables the port again. Every line starts with "probe: ".

; Prints the text (NUL added), then carries on after it.
%macro PROBE_SAY 1+
    jmp %%skip
%%text: db %1, 0
%%skip:
    mov esi, %%text
    call serial_puts
%endmacro

probe_w_delay: db "probedelay", 0
probe_w_aux:   db "probeaux", 0

; With "probedelay": five changes of the RTC's seconds (4-5 s).
probe_delay:
    mov esi, probe_w_delay
    call probe_has
    test eax, eax
    jz .done
    mov ecx, 5
    call probe_rtc_wait
.done:
    ret

; esi = word; eax = 1 if the Multiboot command line contains it, else 0.
probe_has:
    push ebx
    push ecx
    push edi
    xor eax, eax
    mov ebx, [boot_info - KERNEL_VIRT_OFFSET]
    test dword [ebx], 1 << 2        ; information flag 2: cmdline valid
    jz .done
    mov edi, [ebx + 16]
.scan:
    xor ecx, ecx
.cmp:
    mov al, [esi + ecx]
    test al, al
    jz .found
    cmp al, [edi + ecx]
    jne .next
    inc ecx
    jmp .cmp
.next:
    cmp byte [edi], 0
    je .none
    inc edi
    jmp .scan
.found:
    mov eax, 1
    jmp .done
.none:
    xor eax, eax
.done:
    pop edi
    pop ecx
    pop ebx
    ret

; al = the RTC's seconds (CMOS register 0; the NMI bit left clear).
probe_rtc_sec:
    mov al, 0
    out 0x70, al
    in al, 0x71
    ret

; ecx = changes of the RTC's seconds to wait for; at most 0x4000000 reads
; in all, so a stopped RTC can't hang the probe.
probe_rtc_wait:
    push ebx
    push edi
    mov edi, 0x4000000
    call probe_rtc_sec
    mov bl, al
.poll:
    dec edi
    jz .out
    call probe_rtc_sec
    cmp al, bl
    je .poll
    mov bl, al
    dec ecx
    jnz .poll
.out:
    pop edi
    pop ebx
    ret

; al = character, to COM1.
probe_putc:
    push eax
    push edx
    mov ah, al
.wait:
    mov dx, COM1 + 5
    in al, dx
    test al, 0x20
    jz .wait
    mov al, ah
    mov dx, COM1
    out dx, al
    pop edx
    pop eax
    ret

; eax = value, ecx = number of hex digits (the low ones): "0x" and the
; digits (probe_digits: the digits only).
probe_hex:
    push eax
    mov al, '0'
    call probe_putc
    mov al, 'x'
    call probe_putc
    pop eax
probe_digits:
    pushad
    mov ebx, eax
    lea eax, [ecx * 4]
    neg eax
    add eax, 32
    xchg eax, ecx                   ; ecx = 32 - 4 x digits, eax = digits
    rol ebx, cl                     ; the first digit wanted at the top
    mov ecx, eax
.digit:
    rol ebx, 4
    mov eax, ebx
    and eax, 15
    cmp al, 10
    jb .num
    add al, 'a' - 10 - '0'
.num:
    add al, '0'
    call probe_putc
    loop .digit
    popad
    ret

; eax = value, in decimal.
probe_dec:
    pushad
    mov ebx, 10
    xor ecx, ecx
.div:
    xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .div
.out:
    pop eax
    add al, '0'
    call probe_putc
    loop .out
    popad
    ret

; The header's request and the Multiboot information's video fields, then
; the aux port (with "probeaux"), then "probe: done".
probe_report:
    pushad
    PROBE_SAY "probe: header requests mode_type "
    mov eax, [mb_header + 32]
    call probe_dec
    PROBE_SAY ", width "
    mov eax, [mb_header + 36]
    call probe_dec
    PROBE_SAY ", height "
    mov eax, [mb_header + 40]
    call probe_dec
    PROBE_SAY ", depth "
    mov eax, [mb_header + 44]
    call probe_dec
    PROBE_SAY 10
    mov ebx, [boot_info - KERNEL_VIRT_OFFSET]
    PROBE_SAY "probe: Multiboot information at "
    mov eax, ebx
    mov ecx, 8
    call probe_hex
    PROBE_SAY ", flags "
    mov eax, [ebx]
    mov ecx, 8
    call probe_hex
    PROBE_SAY 10
    test dword [ebx], 1 << 9        ; boot_loader_name
    jz .vbe
    PROBE_SAY "probe: boot loader name: "
    mov esi, [ebx + 64]
    call serial_puts
    PROBE_SAY 10
.vbe:
    test dword [ebx], 1 << 11       ; the VBE fields
    jnz .vbe_fields
    PROBE_SAY "probe: VBE fields: none (flags bit 11 clear)", 10
    jmp .fb
.vbe_fields:
    PROBE_SAY "probe: VBE fields (flags bit 11): control info "
    mov eax, [ebx + 72]
    mov ecx, 8
    call probe_hex
    PROBE_SAY ", mode info "
    mov eax, [ebx + 76]
    mov ecx, 8
    call probe_hex
    PROBE_SAY ", mode "
    movzx eax, word [ebx + 80]
    mov ecx, 4
    call probe_hex
    PROBE_SAY ", interface segment "
    movzx eax, word [ebx + 82]
    mov ecx, 4
    call probe_hex
    PROBE_SAY " offset "
    movzx eax, word [ebx + 84]
    mov ecx, 4
    call probe_hex
    PROBE_SAY " length "
    movzx eax, word [ebx + 86]
    mov ecx, 4
    call probe_hex
    PROBE_SAY 10
.fb:
    ; The raw bytes 108-119 (bpp, type, then the colour fields), whatever
    ; the flags say: where the colour fields start is read from these.
    PROBE_SAY "probe: raw information bytes 108-119:"
    xor edi, edi
.raw:
    PROBE_SAY " "
    movzx eax, byte [ebx + 108 + edi]
    mov ecx, 2
    call probe_digits
    inc edi
    cmp edi, 12
    jb .raw
    PROBE_SAY 10
    test dword [ebx], 1 << 12       ; the framebuffer fields
    jnz .fb_fields
    PROBE_SAY "probe: framebuffer fields: none (flags bit 12 clear)", 10
    jmp .aux
.fb_fields:
    PROBE_SAY "probe: framebuffer (flags bit 12): address "
    mov eax, [ebx + 92]
    mov ecx, 8
    call probe_hex
    mov eax, [ebx + 88]
    mov ecx, 8
    call probe_digits
    PROBE_SAY ", pitch "
    mov eax, [ebx + 96]
    call probe_dec
    PROBE_SAY ", width "
    mov eax, [ebx + 100]
    call probe_dec
    PROBE_SAY ", height "
    mov eax, [ebx + 104]
    call probe_dec
    PROBE_SAY ", bpp "
    movzx eax, byte [ebx + 108]
    call probe_dec
    PROBE_SAY ", type "
    movzx eax, byte [ebx + 109]
    call probe_dec
    PROBE_SAY 10
    cmp byte [ebx + 109], 1         ; direct RGB: the colour fields
    jne .palette
    ; Both readings: at byte 110 (the Multiboot 1 spec's table) and at
    ; byte 112 (the C structure's union aligned to 4 bytes).
    PROBE_SAY "probe: colour fields read at byte 110 (the spec's table): "
    mov edi, 110
    call probe_colours
    PROBE_SAY "probe: colour fields read at byte 112 (aligned to 4): "
    mov edi, 112
    call probe_colours
    jmp .aux
.palette:
    cmp byte [ebx + 109], 0         ; indexed: the palette
    jne .aux
    PROBE_SAY "probe: palette at "
    mov eax, [ebx + 110]
    mov ecx, 8
    call probe_hex
    PROBE_SAY ", "
    movzx eax, word [ebx + 114]
    call probe_dec
    PROBE_SAY " colours", 10
.aux:
    call probe_aux
    PROBE_SAY "probe: done", 10
    popad
    ret

; ebx = the Multiboot information, edi = the offset read: "red at bit R
; size S, green at bit ..., blue at bit ..." and a newline.
probe_colours:
    pushad
    PROBE_SAY "red at bit "
    movzx eax, byte [ebx + edi]
    call probe_dec
    PROBE_SAY " size "
    movzx eax, byte [ebx + edi + 1]
    call probe_dec
    PROBE_SAY ", green at bit "
    movzx eax, byte [ebx + edi + 2]
    call probe_dec
    PROBE_SAY " size "
    movzx eax, byte [ebx + edi + 3]
    call probe_dec
    PROBE_SAY ", blue at bit "
    movzx eax, byte [ebx + edi + 4]
    call probe_dec
    PROBE_SAY " size "
    movzx eax, byte [ebx + edi + 5]
    call probe_dec
    PROBE_SAY 10
    popad
    ret

; dx = port (0x60 or 0x64), al = byte: written once the controller's
; input buffer is empty (a bounded wait).
probe_kbc_write:
    push ecx
    push eax
    mov ecx, 0x100000
.wait:
    in al, 0x64
    test al, 2
    jz .ready
    loop .wait
.ready:
    pop eax
    out dx, al
    pop ecx
    ret

; eax = the next byte from the controller (bits 0-7) and the status it
; came with (bits 8-15); bit 31 set if none came within a bounded wait.
probe_kbc_read:
    push ecx
    mov ecx, 0x400000
.wait:
    in al, 0x64
    test al, 1
    jnz .full
    loop .wait
    mov eax, 0x80000000
    pop ecx
    ret
.full:
    movzx eax, al
    shl eax, 8
    in al, 0x60
    pop ecx
    ret

; eax as probe_kbc_read returns it: "0xBB (status 0xSS, aux)" or
; "... keyboard)" by status bit 5, or "none".
probe_kbc_show:
    pushad
    test eax, eax
    jns .byte
    PROBE_SAY "none"
    jmp .done
.byte:
    mov ebx, eax
    mov ecx, 2
    call probe_hex
    PROBE_SAY " (status "
    mov eax, ebx
    shr eax, 8
    mov ecx, 2
    call probe_hex
    test bh, 0x20
    jz .kbd
    PROBE_SAY ", aux)"
    jmp .done
.kbd:
    PROBE_SAY ", keyboard)"
.done:
    popad
    ret

; Drops what the controller holds (at most 32 bytes) and says how many.
probe_kbc_flush:
    push ecx
    push edi
    xor edi, edi
    mov ecx, 32
.next:
    in al, 0x64
    test al, 1
    jz .done
    in al, 0x60
    inc edi
    loop .next
.done:
    PROBE_SAY "probe: aux: flushed "
    mov eax, edi
    call probe_dec
    PROBE_SAY " bytes", 10
    pop edi
    pop ecx
    ret

; With "probeaux": the aux port enabled, the mouse's data reporting on,
; every byte read for 10 changes of the RTC's seconds (the first 96
; printed; a line each second, so the stall watch never sees silence),
; then reporting off and the port disabled again.
probe_aux:
    mov esi, probe_w_aux
    call probe_has
    test eax, eax
    jnz .run
    ret
.run:
    call probe_kbc_flush
    mov dx, 0x64
    mov al, 0x20                    ; read the configuration byte
    call probe_kbc_write
    call probe_kbc_read
    push eax
    PROBE_SAY "probe: aux: configuration byte "
    pop eax
    call probe_kbc_show
    PROBE_SAY 10
    mov dx, 0x64
    mov al, 0xA8                    ; enable the aux port
    call probe_kbc_write
    mov dx, 0x64
    mov al, 0xD4                    ; the next data byte goes to the aux device
    call probe_kbc_write
    mov dx, 0x60
    mov al, 0xF4                    ; mouse: enable data reporting
    call probe_kbc_write
    call probe_kbc_read
    push eax
    PROBE_SAY "probe: aux: reply to 0xF4: "
    pop eax
    call probe_kbc_show
    PROBE_SAY 10
    PROBE_SAY "probe: aux ready: polling the controller for 10 s (move the mouse now)", 10
    xor edi, edi                    ; bytes read
    xor ebp, ebp                    ; of them with the aux bit
    push dword 0x4000000            ; the bound on status reads
    call probe_rtc_sec
    mov bl, al
    mov ecx, 10                     ; changes of the RTC's seconds to go
.poll:
    in al, 0x64
    test al, 1
    jz .idle
    mov ah, al
    in al, 0x60
    movzx eax, ax
    inc edi
    test ah, 0x20
    jz .count
    inc ebp
.count:
    cmp edi, 96
    ja .idle
    push eax
    PROBE_SAY "probe: aux: byte "
    pop eax
    call probe_kbc_show
    PROBE_SAY 10
.idle:
    call probe_rtc_sec
    cmp al, bl
    je .next
    mov bl, al
    PROBE_SAY "probe: aux: polling, "
    mov eax, edi
    call probe_dec
    PROBE_SAY " bytes so far", 10
    dec ecx
    jz .end
.next:
    dec dword [esp]
    jnz .poll
.end:
    add esp, 4
    PROBE_SAY "probe: aux: "
    mov eax, edi
    call probe_dec
    PROBE_SAY " bytes in 10 s, "
    mov eax, ebp
    call probe_dec
    PROBE_SAY " with the aux bit (status bit 5)", 10
    mov dx, 0x64
    mov al, 0xD4
    call probe_kbc_write
    mov dx, 0x60
    mov al, 0xF5                    ; mouse: disable data reporting
    call probe_kbc_write
    call probe_kbc_read
    push eax
    PROBE_SAY "probe: aux: reply to 0xF5: "
    pop eax
    call probe_kbc_show
    PROBE_SAY 10
    mov dx, 0x64
    mov al, 0xA7                    ; disable the aux port
    call probe_kbc_write
    call probe_kbc_flush
    PROBE_SAY "probe: aux: port disabled again (0xA7)", 10
    ret
%endif

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
