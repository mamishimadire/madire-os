#ifndef MADIRE_MEMORY_PTE_H
#define MADIRE_MEMORY_PTE_H

/*
 * x86 32-bit (non-PAE) page directory / page table entries, 4 KiB pages.
 * Pure header, unit-tested on the host (tests/unit/test_pte.c).
 *
 *   virtual address = [ directory index : 10 | table index : 10 | offset : 12 ]
 */

#include <stdbool.h>
#include <stdint.h>

#define PTE_PRESENT    0x001u
#define PTE_WRITE      0x002u
#define PTE_USER       0x004u   /* never set in 0.6: kernel-only mappings */
#define PTE_ACCESSED   0x020u   /* set by the CPU */
#define PTE_DIRTY      0x040u   /* set by the CPU */
#define PTE_FRAME_MASK 0xFFFFF000u

#define VM_PAGE_SIZE   4096u
#define VM_ENTRIES     1024u

static inline uint32_t pd_index(uint32_t va) { return va >> 22; }
static inline uint32_t pt_index(uint32_t va) { return (va >> 12) & 0x3FFu; }
static inline bool page_aligned(uint32_t a)   { return (a & 0xFFFu) == 0; }
static inline uint32_t page_floor(uint32_t a) { return a & PTE_FRAME_MASK; }

/* Rounds up to a page; saturates to the last page instead of wrapping. */
static inline uint32_t page_ceil(uint32_t a)
{
    return a > 0xFFFFF000u ? 0xFFFFF000u : (a + 0xFFFu) & PTE_FRAME_MASK;
}

static inline uint32_t pte_make(uint32_t phys, uint32_t flags)
{
    return (phys & PTE_FRAME_MASK) | (flags & 0xFFFu);
}

static inline uint32_t pte_frame(uint32_t entry)   { return entry & PTE_FRAME_MASK; }
static inline bool pte_present(uint32_t entry)     { return entry & PTE_PRESENT; }
static inline bool pte_writable(uint32_t entry)    { return entry & PTE_WRITE; }

#endif
