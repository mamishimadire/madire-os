#ifndef MADIRE_MEMORY_VMM_CORE_H
#define MADIRE_MEMORY_VMM_CORE_H

/*
 * Virtual memory core: map / unmap / translate 4 KiB pages in a 32-bit
 * (non-PAE) page directory.
 *
 * Pure module: all hardware access goes through struct vmm_ops (frame
 * allocation, reaching the directory and tables, TLB flush), so it is
 * unit-tested on the host with fake frames (tests/unit/test_vmm.c). The
 * kernel supplies operations that reach tables physically before paging is
 * enabled and through the recursive mapping afterwards (paging.c).
 *
 * Rules:
 *   - page 0 and the recursive directory slot can never be mapped;
 *   - an existing mapping is never replaced (VMM_ERR_EXISTS);
 *   - only PTE_WRITE may be requested; entries are always kernel-only;
 *   - unmap flushes the TLB entry for that page (exactly once);
 *   - page tables, once created, are never freed (0.6).
 */

#include <stdbool.h>
#include <stdint.h>

struct vmm_ops {
    /* A zero-able physical frame for a new page table; 0 = none left. */
    uint32_t (*alloc_frame)(void *ctx);
    /* The page directory (1024 entries). */
    uint32_t *(*directory)(void *ctx);
    /* Page table number pd_index, whose frame is table_phys. fresh = the
     * directory entry was just created (flush any stale translation). */
    uint32_t *(*table)(void *ctx, uint32_t pd_index, uint32_t table_phys, bool fresh);
    /* Invalidate the TLB entry for va. */
    void (*flush)(void *ctx, uint32_t va);
    void *ctx;
};

struct vmm {
    const struct vmm_ops *ops;
    uint32_t mapped_pages;      /* present page-table entries created through this vmm */
    uint32_t page_tables;       /* page tables created through this vmm */
};

enum vmm_result {
    VMM_OK,
    VMM_ERR_UNALIGNED,      /* va or phys not page-aligned */
    VMM_ERR_EXISTS,         /* va already mapped */
    VMM_ERR_NOT_MAPPED,     /* nothing mapped at va */
    VMM_ERR_NO_FRAME,       /* no frame for a new page table */
    VMM_ERR_RESERVED_VA,    /* page 0 or the recursive slot */
    VMM_ERR_FLAGS,          /* flags other than PTE_WRITE requested */
};

void vmm_init(struct vmm *vm, const struct vmm_ops *ops);

/* Creates the page table covering va if it doesn't exist yet. */
enum vmm_result vmm_ensure_table(struct vmm *vm, uint32_t va);

/* Maps the page at va to the frame phys; flags: 0 (read-only) or PTE_WRITE. */
enum vmm_result vmm_map(struct vmm *vm, uint32_t va, uint32_t phys, uint32_t flags);

/* Removes the mapping at va (flushes it); *phys_out = the frame it used. */
enum vmm_result vmm_unmap(struct vmm *vm, uint32_t va, uint32_t *phys_out);

/* Physical address for va (frame + offset) and its entry flags. */
bool vmm_translate(const struct vmm *vm, uint32_t va, uint32_t *phys_out, uint32_t *flags_out);

const char *vmm_result_str(enum vmm_result result);

#endif
