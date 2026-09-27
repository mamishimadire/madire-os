/* Madire OS kernel layout: loaded by GRUB at physical 1 MiB.
 *
 * The __*_start / __*_end symbols let the kernel find its own extent at run
 * time, so the memory manager can keep these pages out of the allocator
 * (kernel/memory/reserve.c). __kernel_end is page-aligned and covers .bss,
 * which includes the boot stack and the allocator bitmaps. */
ENTRY(_start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .text BLOCK(4K) : ALIGN(4K)
    {
        __text_start = .;
        *(.multiboot)       /* must be within the first 8 KiB of the file */
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
        __bss_end = .;
    }

    . = ALIGN(4K);
    __kernel_end = .;

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }
}
