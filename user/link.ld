/*
 * Madire OS built-in user programs (0.10): fixed addresses (vm_layout.h).
 *   0x400000  code + read-only data   (read + execute)
 *   0x500000  data, then bss          (read + write, no-execute)
 * scripts/user-images.sh turns .text and .data into raw blobs and refuses
 * any other loaded section.
 */
ENTRY(_start)

SECTIONS
{
    . = 0x400000;
    .text : {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    . = 0x500000;
    .data : {
        *(.data .data.*)
    }
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
    }
}
