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
 *   slot 507  (0.9) kernel thread stacks: VM_KSTACK_BASE, one 32 KiB slot
 *             per thread (16 KiB unmapped guard, then the 16 KiB stack);
 *             kernel/proc/sched_core.h
 *   slot 508  VM_TEST_PAGE (paging self-test), VM_WINDOW (physical access)
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

#define VM_KSTACK_INDEX       507u
#define VM_KSTACK_BASE        0xFFFFFD8000000000ull

#define VM_TEST_PAGE          0xFFFFFE0000000000ull
#define VM_WINDOW             0xFFFFFE0000001000ull

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
