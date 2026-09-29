/* Madire OS kernel layout (x86-64): loaded at physical 1 MiB and, in 0.7,
 * run at the same (identity-mapped) addresses.
 *
 * The image is booted as a flat binary through the Multiboot 1 "a.out
 * kludge" fields in boot/boot.asm:
 *   load_addr = __kernel_start, load_end_addr = __data_end (the file ends
 *   there), bss_end_addr = __kernel_end (the loader zeroes the rest and
 *   places modules and boot data above it).
 *
 * The __*_start / __*_end symbols let the kernel find its own extent at run
 * time, so the memory manager can keep these pages out of the allocator
 * (kernel/memory/reserve.c). __kernel_end is page-aligned and covers all of
 * .bss: the allocator bitmaps, the boot page tables, and the kernel and
 * IST1 stacks with their guard pages. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .text BLOCK(4K) : ALIGN(4K)
    {
        __text_start = .;
        *(.multiboot)       /* must be within the first 8 KiB of the file */
        *(.boot32)          /* 32-bit entry stub: CPU checks, switch to long mode */
        *(.text .text.*)
        __text_end = .;
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    }

    .data BLOCK(4K) : ALIGN(4K)
    {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(4K);
        *(.boot_tables)     /* the stub's temporary PML4, PDPT and 4 PDs */
        . = ALIGN(4K);
        *(.stacks)          /* guard, kernel stack, guard, IST1 stack */
        __bss_end = .;
    }

    . = ALIGN(4K);
    __kernel_end = .;

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }
}
