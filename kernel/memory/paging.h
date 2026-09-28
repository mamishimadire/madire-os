#ifndef MADIRE_MEMORY_PAGING_H
#define MADIRE_MEMORY_PAGING_H

/*
 * Paging (hardware side): builds the kernel's page directory and tables
 * with the pure vmm_core, checks them by walking them, then loads CR3 and
 * sets CR0.WP and CR0.PG. Layout: vm_layout.h.
 *
 * The map/unmap/translate calls and the window are for normal (non-IRQ)
 * context only and need paging to be on.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/vmm_core.h"

struct paging_stats {
    bool enabled;
    uint32_t cr3;               /* directory's physical address (0 while off) */
    uint32_t mapped_pages;      /* page-table entries created at build time */
    uint32_t page_tables;       /* page tables now, incl. later ones (e.g. the heap's);
                                 * the directory not included */
    uint32_t kernel_ro_pages;   /* kernel code + read-only data */
};

/* Build + walk-check + enable. false = paging left OFF (frames released). */
bool paging_init(void);
/* After paging_init() returned true; a failure must panic in the caller. */
bool paging_selftest(void);
bool paging_enabled(void);

enum vmm_result paging_map(uint32_t va, uint32_t phys, uint32_t flags);
enum vmm_result paging_unmap(uint32_t va, uint32_t *phys_out);
bool paging_translate(uint32_t va, uint32_t *phys_out, uint32_t *flags_out);

/*
 * Physical-access window: one page at VM_WINDOW that shows any physical
 * frame (free RAM is otherwise not mapped). Lifecycle:
 *   p = vmm_window_map(phys); ...access p...; vmm_window_unmap();
 * One user at a time, normal context only, not re-entrant: mapping while
 * mapped, or unmapping while not mapped, is a kernel bug and panics.
 * phys must be page-aligned. vmm_window_unmap() clears the entry and
 * flushes its TLB entry; it never frees the frame, which stays the
 * caller's (and may then go back to the allocator).
 */
volatile void *vmm_window_map(uint32_t phys);
void vmm_window_unmap(void);

const struct paging_stats *paging_stats(void);

#endif
