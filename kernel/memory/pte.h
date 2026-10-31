#ifndef MADIRE_MEMORY_PTE_H
#define MADIRE_MEMORY_PTE_H

/*
 * x86-64 4-level page-table entries and address helpers (pure).
 *
 * Levels: 4 = PML4, 3 = PDPT, 2 = PD, 1 = PT. 512 entries of 8 bytes each.
 */

#include <stdbool.h>
#include <stdint.h>

#define PTE_PRESENT   0x001ull
#define PTE_WRITE     0x002ull
#define PTE_USER      0x004ull              /* never set in 0.7 */
/* 1.4 stage 4: write-through and cache-disable, both set for a device's
 * registers (docs/plans/1.4.md, D4): with the boot-time PAT (never changed)
 * PCD=1 PWT=1 selects its entry 3, uncacheable (UC). */
#define PTE_PWT       0x008ull
#define PTE_PCD       0x010ull
#define PTE_UNCACHED  (PTE_PCD | PTE_PWT)
#define PTE_ACCESSED  0x020ull
#define PTE_DIRTY     0x040ull
#define PTE_LARGE     0x080ull              /* PS: 2 MiB / 1 GiB page (boot tables only) */
#define PTE_NX        (1ull << 63)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ull /* bits 12-51: frame address */

#define VM_PAGE_SIZE  4096ull
#define VM_ENTRIES    512u
#define VM_LEVELS     4

/* Index of va's entry in its table at level 1..4. */
static inline unsigned vm_index(uint64_t va, int level)
{
    return (unsigned)(va >> (12 + 9 * (level - 1))) & (VM_ENTRIES - 1);
}

/* Bits 63..47 all equal (48-bit virtual addresses). */
static inline bool vm_canonical(uint64_t va)
{
    uint64_t top = va >> 47;
    return top == 0 || top == 0x1FFFFull;
}

/* Bit 47 copied into bits 48..63. */
static inline uint64_t vm_sign_extend(uint64_t va)
{
    return (va & (1ull << 47)) ? (va | 0xFFFF000000000000ull) : (va & 0x0000FFFFFFFFFFFFull);
}

static inline bool vm_aligned(uint64_t a) { return (a & (VM_PAGE_SIZE - 1)) == 0; }
static inline uint64_t vm_floor(uint64_t a) { return a & ~(VM_PAGE_SIZE - 1); }
static inline uint64_t vm_ceil(uint64_t a)
{
    return a > 0xFFFFFFFFFFFFF000ull ? 0xFFFFFFFFFFFFF000ull : vm_floor(a + VM_PAGE_SIZE - 1);
}

static inline uint64_t pte_make(uint64_t phys, uint64_t flags) { return (phys & PTE_ADDR_MASK) | flags; }
static inline uint64_t pte_frame(uint64_t e) { return e & PTE_ADDR_MASK; }
static inline bool pte_present(uint64_t e) { return (e & PTE_PRESENT) != 0; }
static inline bool pte_writable(uint64_t e) { return (e & PTE_WRITE) != 0; }
static inline bool pte_nx(uint64_t e) { return (e & PTE_NX) != 0; }

/* The no-execute flag to put in a data page's entry: PTE_NX only while NX
 * is enabled (EFER.NXE). Without it bit 63 is reserved, and setting it would
 * make every access to the page fault. */
static inline uint64_t pte_nx_flag(bool nx_enabled) { return nx_enabled ? PTE_NX : 0; }

/*
 * Recursive mapping: with PML4[r] pointing to the PML4 itself, the table at
 * `level` that covers va is visible at this virtual address. Level 1 gives
 * the PT, 2 the PD, 3 the PDPT, 4 the PML4 itself.
 */
static inline uint64_t vm_recursive_table(unsigned r, int level, uint64_t va)
{
    uint64_t addr = 0;
    int k;
    /* The top `level` index slots are r; below them come va's own indexes,
     * shifted up by `level` levels; the page offset is 0. */
    for (k = 0; k < level; k++)
        addr |= (uint64_t)r << (39 - 9 * k);
    addr |= ((va & 0x0000FFFFFFFFFFFFull) >> (9 * level)) & ~0xFFFull &
            ((1ull << (48 - 9 * level)) - 1);
    return vm_sign_extend(addr);
}

#endif
