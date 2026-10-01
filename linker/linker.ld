/* Madire OS kernel layout (x86-64): loaded at physical 1 MiB and, since
 * 0.8, run in the higher half at 0xFFFFFFFF80000000 + physical address
 * (PML4 slot 511). kernel/memory/vm_layout.h has the same offset.
 *
 * The image is booted as a flat binary through the Multiboot 1 "a.out
 * kludge" fields in boot/boot.asm. Those fields and the 32-bit boot stub
 * need PHYSICAL addresses, so:
 *   - .boot (the Multiboot header and the 32-bit stub) is linked where it
 *     is loaded: virtual = physical = 1 MiB. It runs only before the switch
 *     to the higher half; afterwards the same bytes are also reachable at
 *     their high address, as the first page of kernel code;
 *   - everything else is linked high and placed low with AT().
 * The loader copies the file up to __data_end_phys and zeroes up to
 * __kernel_end_phys (all of .bss: bitmaps, boot tables, stacks, guards).
 *
 * The __*_start / __*_end symbols are VIRTUAL (high) addresses; the kernel
 * converts them with kernel_phys() where it needs physical ones (the memory
 * manager keeps these pages out of the allocator, kernel/memory/reserve.c).
 * __kernel_start / __text_start are the high address of 1 MiB, so the
 * image's first page (the header and stub) belongs to the kernel code range.
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VIRT_OFFSET = 0xFFFFFFFF80000000;

SECTIONS
{
    . = 1M;
    __kernel_phys_start = .;
    __kernel_start = . + KERNEL_VIRT_OFFSET;
    __text_start = __kernel_start;

    .boot : ALIGN(4K)
    {
        *(.multiboot)       /* must be within the first 8 KiB of the file */
        *(.boot32)          /* 32-bit entry stub: CPU checks, switch to long mode */
    }

    . = ALIGN(4K) + KERNEL_VIRT_OFFSET;

    .text : AT(ADDR(.text) - KERNEL_VIRT_OFFSET)
    {
        *(.text .text.*)
        __text_end = .;
    }

    .rodata BLOCK(4K) : AT(ADDR(.rodata) - KERNEL_VIRT_OFFSET) ALIGN(4K)
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    }

    .data BLOCK(4K) : AT(ADDR(.data) - KERNEL_VIRT_OFFSET) ALIGN(4K)
    {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    }
    __data_end_phys = __data_end - KERNEL_VIRT_OFFSET;

    .bss BLOCK(4K) : AT(ADDR(.bss) - KERNEL_VIRT_OFFSET) ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(4K);
        *(.boot_tables)     /* the stub's temporary PML4, PDPTs and PDs */
        . = ALIGN(4K);
        *(.stacks)          /* guard, kernel stack, guard, IST1 stack */
        __bss_end = .;
    }

    . = ALIGN(4K);
    __kernel_end = .;
    __kernel_end_phys = __kernel_end - KERNEL_VIRT_OFFSET;
    __bss_start_phys = __bss_start - KERNEL_VIRT_OFFSET;

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }
}
