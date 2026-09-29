#ifndef MADIRE_MEMORY_VMM_CORE_H
#define MADIRE_MEMORY_VMM_CORE_H

/*
 * Virtual memory core for x86-64 4-level paging: map / unmap / translate
 * 4 KiB pages.
 *
 * Pure module: every table is reached through struct vmm_ops (physically
 * while the boot tables are active, through the recursive mapping once the
 * kernel's own tables are loaded), so it is unit-tested on fake frames
 * (tests/unit/test_vmm.c).
 *
 * Rules:
 *   - page 0, the recursive PML4 slot and non-canonical addresses are refused;
 *   - an existing mapping (also one made by a large page) is never replaced;
 *   - only PTE_WRITE and PTE_NX may be requested; never user pages;
 *   - intermediate entries are present + writable, never NX (NX is decided
 *     per page, at the leaf);
 *   - missing tables are created all or nothing: if a frame for a deeper
 *     table can't be had, the ones just taken are given back;
 *   - unmap flushes the TLB entry for that page, exactly once;
 *   - tables, once created, are never freed.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/vm_layout.h"        /* VM_NULL_GUARD_END, VM_RECURSIVE_INDEX */

struct vmm_ops {
    /* A physical frame for a new table (cleared by the core); 0 = none left. */
    uint64_t (*alloc_frame)(void *ctx);
    /* Gives back a frame from alloc_frame that ended up unused. */
    void (*free_frame)(void *ctx, uint64_t frame);
    /* The table at `level` (4 = PML4 ... 1 = PT) that covers va; its frame is
     * table_phys. fresh = its entry in the parent was just created. */
    uint64_t *(*table)(void *ctx, int level, uint64_t va, uint64_t table_phys, bool fresh);
    /* Invalidate the TLB entry for va. */
    void (*flush)(void *ctx, uint64_t va);
    void *ctx;
};

struct vmm {
    const struct vmm_ops *ops;
    uint64_t root;                  /* PML4 frame */
    uint32_t mapped_pages;          /* leaf entries created through this vmm */
    uint32_t page_tables;           /* PDPTs + PDs + PTs created through this vmm */
};

enum vmm_result {
    VMM_OK,
    VMM_ERR_UNALIGNED,             /* va or phys not page-aligned, or phys beyond 52 bits */
    VMM_ERR_EXISTS,                /* va already mapped */
    VMM_ERR_NOT_MAPPED,            /* nothing mapped at va */
    VMM_ERR_NO_FRAME,              /* no frame for a new table */
    VMM_ERR_RESERVED_VA,           /* page 0 or the recursive slot */
    VMM_ERR_FLAGS,                 /* flags other than PTE_WRITE / PTE_NX */
    VMM_ERR_NONCANONICAL,          /* bits 63..47 not all equal */
};

void vmm_init(struct vmm *vm, const struct vmm_ops *ops, uint64_t root);

/* Creates the PDPT, PD and PT covering va where missing. */
enum vmm_result vmm_ensure_table(struct vmm *vm, uint64_t va);

/* Maps the page at va to the frame phys; flags: PTE_WRITE and/or PTE_NX. */
enum vmm_result vmm_map(struct vmm *vm, uint64_t va, uint64_t phys, uint64_t flags);

/* Removes the mapping at va (flushes it); *phys_out = the frame it used. */
enum vmm_result vmm_unmap(struct vmm *vm, uint64_t va, uint64_t *phys_out);

/* Physical address for va (frame + offset; large pages included) and the
 * leaf entry's flag bits (incl. PTE_NX). */
bool vmm_translate(const struct vmm *vm, uint64_t va, uint64_t *phys_out, uint64_t *flags_out);

const char *vmm_result_str(enum vmm_result result);

#endif
