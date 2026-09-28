#ifndef MADIRE_MEMORY_VM_LAYOUT_H
#define MADIRE_MEMORY_VM_LAYOUT_H

/*
 * Madire OS virtual memory layout (0.6). Every layout constant lives here,
 * so a later move to a higher-half kernel only has to change this file,
 * the linker script and the boot code.
 *
 *   0x00000000-0x00000FFF  unmapped: NULL guard. Page 0 also holds the
 *                          real-mode IVT and the BIOS data area (0x400,
 *                          incl. the EBDA pointer at 0x40E); nothing in
 *                          Madire OS uses them once paging is on.
 *   0x00001000-0x000FFFFF  identity, read/write: EBDA, VGA 0xB8000, ROM,
 *                          GRUB data (the rest of the 0.3 low reservation)
 *   __text_start-__data_start   identity, read-only: kernel code + rodata
 *   __data_start-__kernel_end   identity, read/write: data, bss, stack
 *   0.3 boot-data reservations above 1 MiB: identity, read/write
 *   everything else below VM_HEAP_BASE: unmapped (free RAM is not mapped)
 *   VM_HEAP_BASE ..         kernel heap, grows on demand up to VM_HEAP_MAX
 *   VM_TEST_PAGE            one page used by the paging self-test
 *   VM_WINDOW               one-page physical-access window
 *   VM_RECURSIVE_BASE ..    page tables through the recursive mapping,
 *                           VM_PAGE_DIRECTORY = the directory itself
 */

#define VM_NULL_GUARD_END     0x00001000u
#define VM_LOW_MAP_START      0x00001000u
#define VM_LOW_MAP_END        0x00100000u

#define VM_HEAP_BASE          0xD0000000u
#define VM_HEAP_INITIAL       (64u * 1024)
#define VM_HEAP_MAX           (16u * 1024 * 1024)
#define VM_HEAP_GROW_MIN      (16u * 1024)

#define VM_TEST_PAGE          0xFFBFE000u
#define VM_WINDOW             0xFFBFF000u

#define VM_RECURSIVE_INDEX    1023u
#define VM_RECURSIVE_BASE     0xFFC00000u
#define VM_PAGE_DIRECTORY     0xFFFFF000u

#endif
