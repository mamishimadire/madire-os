#ifndef MADIRE_MEMORY_VM_LAYOUT_H
#define MADIRE_MEMORY_VM_LAYOUT_H

/*
 * Madire OS virtual memory layout (x86-64, 4-level paging, 0.7). Every
 * layout constant lives here, so the move to a higher-half kernel (0.8)
 * only has to change this file, the linker script and the boot code.
 *
 * Lower half (PML4 slot 0): the kernel, identity-mapped.
 *   0x0000000000000000-0xFFF  unmapped: NULL guard. Page 0 also holds the
 *                             real-mode IVT and the BIOS data area (0x400,
 *                             incl. the EBDA pointer at 0x40E); nothing in
 *                             Madire OS uses them once paging is set up.
 *                             (The boot stub's temporary tables DO map page
 *                             0, until paging_init() loads these tables.)
 *   0x1000-0xFFFFF            identity, read/write: EBDA, VGA 0xB8000, ROM,
 *                             GRUB data (the rest of the 0.3 low reservation)
 *   __text_start-__data_start identity, read-only: kernel code + rodata
 *   __data_start-__kernel_end identity, read/write: data, bss, stacks
 *   0.3 boot-data reservations above 1 MiB: identity, read/write
 *   everything else: unmapped (free RAM is not mapped)
 *
 * Upper half, one PML4 slot per area:
 *   slot 508  VM_TEST_PAGE (paging self-test), VM_WINDOW (physical access)
 *   slot 509  VM_HEAP_BASE: kernel heap, grows up to VM_HEAP_MAX
 *   slot 510  the recursive mapping: every page table, and the PML4 itself
 *             at VM_PML4
 *   slot 511  reserved for the higher-half kernel image (0.8)
 */

#define VM_NULL_GUARD_END     0x0000000000001000ull
#define VM_LOW_MAP_START      0x0000000000001000ull
#define VM_LOW_MAP_END        0x0000000000100000ull

#define VM_TEST_PAGE          0xFFFFFE0000000000ull
#define VM_WINDOW             0xFFFFFE0000001000ull

#define VM_HEAP_BASE          0xFFFFFE8000000000ull
#define VM_HEAP_INITIAL       (64u * 1024)
#define VM_HEAP_MAX           (16u * 1024 * 1024)
#define VM_HEAP_GROW_MIN      (16u * 1024)

#define VM_RECURSIVE_INDEX    510u
#define VM_RECURSIVE_BASE     0xFFFFFF0000000000ull
#define VM_PML4               0xFFFFFF7FBFDFE000ull

#define VM_KERNEL_HIGH_INDEX  511u                  /* reserved for 0.8 */

#endif
