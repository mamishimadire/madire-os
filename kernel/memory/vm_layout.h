#ifndef MADIRE_MEMORY_VM_LAYOUT_H
#define MADIRE_MEMORY_VM_LAYOUT_H

/*
 * Madire OS virtual memory layout (x86-64, 4-level paging, higher-half
 * kernel since 0.8). Every layout constant lives here.
 *
 * Lower half: nothing is mapped (PML4 slot 0 is empty). Every address in
 * 0-4 GiB faults, including page 0 (NULL, the real-mode IVT and the BIOS
 * data area). The boot stub's temporary tables DO map 0-4 GiB here, until
 * paging_init() loads the kernel's own tables. Free RAM is mapped nowhere.
 *
 * Upper half, one PML4 slot per area:
 *   slot 256  (0.8) the physical view: VM_PHYS_VIEW_BASE + physical address
 *             for the low 1 MiB and the boot data only (never free RAM,
 *             page 0 or the kernel image); phys_view() below
 *   slot 506  (1.2) the display, only on boots with a usable framebuffer
 *             (kernel/display/display.h): the framebuffer at VM_FB_BASE
 *             (plus its offset in its first page) and the back buffer,
 *             frames of free RAM, at VM_BACKBUF_BASE; each area up to
 *             1 GiB, so each has its own page directory. 1.3 stage 3: a
 *             third area at VM_DESKTOP_BASE, the desktop's (the
 *             background cache, frames of free RAM), mapped when the
 *             desktop starts (kernel/display/desktop.h), never unmapped
 *   slot 507  (0.9) kernel thread stacks: VM_KSTACK_BASE, one 32 KiB slot
 *             per thread (16 KiB unmapped guard, then the 16 KiB stack);
 *             kernel/proc/sched_core.h
 *   slot 508  VM_TEST_PAGE (paging self-test), VM_WINDOW (physical access);
 *             1.4 stage 4, only on boots that find an AHCI controller
 *             (docs/plans/1.4.md, D4, C3): its registers at VM_DEVREG_BASE
 *             (uncached, PTE_PCD|PTE_PWT), and the storage area at
 *             VM_STORAGE_BASE: the controller's DMA structures, then the
 *             block cache's pages from VM_BLK_CACHE_BASE (frames of free
 *             RAM, write-back); each area in its own 1 GiB (its own page
 *             directory), mapped after the freeze (slot 508's PDPT exists
 *             from paging_init(), so no PML4 entry is added)
 *   slot 509  VM_HEAP_BASE: kernel heap, grows up to VM_HEAP_MAX
 *   slot 510  the recursive mapping: every page table, and the PML4 itself
 *             at VM_PML4
 *   slot 511  (0.8) the higher-half kernel image: VM_KERNEL_VIRT_OFFSET +
 *             physical address; kernel_phys() below
 *             (code read-only + executable, rodata read-only, the rest
 *             read/write, the two stack guard pages unmapped)
 */

#include <stdint.h>

#define VM_NULL_GUARD_END     0x0000000000001000ull
#define VM_LOW_MAP_START      0x0000000000001000ull
#define VM_LOW_MAP_END        0x0000000000100000ull

#define VM_DISPLAY_INDEX      506u
#define VM_FB_BASE            0xFFFFFD0000000000ull
#define VM_BACKBUF_BASE       0xFFFFFD0040000000ull /* VM_FB_BASE + 1 GiB */
#define VM_DESKTOP_BASE       0xFFFFFD0080000000ull /* VM_FB_BASE + 2 GiB (1.3) */
#define VM_DISPLAY_AREA       0x0000000040000000ull /* each area's size */

#define VM_KSTACK_INDEX       507u
#define VM_KSTACK_BASE        0xFFFFFD8000000000ull

#define VM_TEST_PAGE          0xFFFFFE0000000000ull
#define VM_WINDOW             0xFFFFFE0000001000ull
#define VM_STORAGE_INDEX      508u                   /* 1.4 stage 4: slot 508's new areas */
#define VM_DEVREG_BASE        0xFFFFFE0040000000ull  /* VM_TEST_PAGE + 1 GiB */
#define VM_DEVREG_PAGES       4u                     /* the registers' pages, at most */
#define VM_STORAGE_BASE       0xFFFFFE0080000000ull  /* VM_TEST_PAGE + 2 GiB */
#define VM_STORAGE_DMA_PAGES  64u                    /* 2 per port, 32 ports */
#define VM_BLK_CACHE_BASE     0xFFFFFE0080200000ull  /* VM_STORAGE_BASE + 2 MiB */
#define VM_BLK_CACHE_PAGES    512u                   /* 2 MiB, the cache's most */

#define VM_HEAP_BASE          0xFFFFFE8000000000ull
#define VM_HEAP_INITIAL       (64u * 1024)
#define VM_HEAP_MAX           (16u * 1024 * 1024)
#define VM_HEAP_GROW_MIN      (16u * 1024)

#define VM_RECURSIVE_INDEX    510u
#define VM_RECURSIVE_BASE     0xFFFFFF0000000000ull
#define VM_PML4               0xFFFFFF7FBFDFE000ull

#define VM_KERNEL_HIGH_INDEX  511u
#define VM_KERNEL_VIRT_OFFSET 0xFFFFFFFF80000000ull /* virtual = this + physical */

#define VM_PHYS_VIEW_INDEX    256u
#define VM_PHYS_VIEW_BASE     0xFFFF800000000000ull /* virtual = this + physical */

/*
 * 0.10: a process's lower half (its own tables; the upper half is the
 * kernel's, shared). User pages lie in [VM_USER_MIN, VM_USER_TOP): nothing
 * below 4 MiB (NULL and small offsets from it always fault), and the last
 * canonical page below the hole is never mapped. A program's code and
 * read-only data are at VM_USER_TEXT (read + execute), its data and bss at
 * VM_USER_DATA (read + write, no-execute), and its stack ends at
 * VM_USER_STACK_TOP, with nothing mapped below it.
 */
#define VM_USER_MIN           0x0000000000400000ull
#define VM_USER_TOP           0x00007FFFFFFFF000ull /* exclusive */
#define VM_USER_TEXT          0x0000000000400000ull
#define VM_USER_DATA          0x0000000000500000ull
#define VM_USER_STACK_TOP     VM_USER_TOP
#define VM_USER_STACK_SIZE    (32u * 1024)

/* Where the kernel reads a physical address that lies in the physical view
 * (low 1 MiB, boot data). Anything else is not mapped there. */
static inline void *phys_view(uint64_t phys)
{
    return (void *)(uintptr_t)(VM_PHYS_VIEW_BASE + phys);
}

/* The physical address of a kernel-image virtual address (slot 511). */
static inline uint64_t kernel_phys(uint64_t virt)
{
    return virt - VM_KERNEL_VIRT_OFFSET;
}

#endif
