#ifndef MADIRE_MEMORY_PAGING_H
#define MADIRE_MEMORY_PAGING_H

/*
 * Paging (hardware side, x86-64): builds the kernel's own 4-level tables
 * with the pure vmm_core while the boot stub's temporary tables are still
 * in use, checks them by walking them, then loads CR3. Layout: vm_layout.h.
 *
 * The map/unmap/translate calls and the window are for normal (non-IRQ)
 * context only and need the kernel's tables to be loaded.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/vmm_core.h"

struct paging_stats {
    bool enabled;               /* the kernel's own tables are loaded */
    bool nx_supported;          /* CPUID: the CPU supports no-execute pages */
    bool nx_enabled;            /* EFER.NXE set (and read back) */
    bool nx;                    /* no-execute bits used in the kernel's tables */
    uint64_t cr3;               /* PML4 physical address (0 while not loaded) */
    uint32_t mapped_pages;      /* leaf entries created at build time */
    uint32_t page_tables;       /* PDPTs + PDs + PTs now, incl. later ones (e.g. the
                                 * heap's); the PML4 not included */
    uint32_t kernel_ro_pages;   /* kernel code + read-only data */
};

/* Build + walk-check + load. false = the kernel keeps running on the boot
 * stub's temporary tables (frames released). */
bool paging_init(void);
/* After paging_init() returned true; a failure must panic in the caller. */
bool paging_selftest(void);
bool paging_enabled(void);

/* PTE_NX when no-execute bits are in use, else 0: add it to the flags of
 * every data mapping (e.g. heap pages). */
uint64_t paging_nx_flag(void);

enum vmm_result paging_map(uint64_t va, uint64_t phys, uint64_t flags);
enum vmm_result paging_unmap(uint64_t va, uint64_t *phys_out);
bool paging_translate(uint64_t va, uint64_t *phys_out, uint64_t *flags_out);

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
volatile void *vmm_window_map(uint64_t phys);
void vmm_window_unmap(void);

const struct paging_stats *paging_stats(void);

/* 0.10: the table operations through the recursive slot. They reach the
 * tables of whichever PML4 is loaded in CR3 (each process PML4's slot 510
 * points to itself), so a process's lower half is edited while its CR3 is
 * loaded (kernel/memory/uspace.c). Frames come from and go back to the
 * page allocator. */
const struct vmm_ops *paging_recursive_ops(void);

/* 0.10: the kernel's own PML4 (the CR3 of every kernel thread). */
uint64_t paging_kernel_cr3(void);

/* 0.10: no new kernel-half PML4 entry from now on (vmm_freeze_upper): every
 * process's PML4 will copy entries 256-511 once, so one added later would
 * be missing there. Logs the slots in use. A paging_map that would create
 * one panics. Called once at boot, before the first process. */
void paging_freeze_kernel_half(void);

#endif
