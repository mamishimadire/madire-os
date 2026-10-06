#ifndef MADIRE_MEMORY_UVM_CORE_H
#define MADIRE_MEMORY_UVM_CORE_H

/*
 * User virtual memory core (0.10): maps, unmaps and tears down the user
 * pages of one process, in the lower half of its PML4. The upper half is
 * the kernel's (shared by every process) and is never touched here.
 *
 * Pure module: every table is reached through struct vmm_ops (vmm_core.h),
 * so it is unit-tested on fake frames (tests/unit/test_uvm.c).
 *
 * Rules:
 *   - user pages only in [VM_USER_MIN, VM_USER_TOP) (vm_layout.h), 4 KiB
 *     pages only; an existing mapping is never replaced;
 *   - only PTE_WRITE and PTE_NX may be requested, PTE_NX only when NX is in
 *     use; with NX in use a page is never writable and executable (W^X);
 *     without NX every user page is executable (a documented limitation),
 *     and code pages are still never writable;
 *   - every lower-half entry, at every level, has PTE_USER; leaf entries
 *     decide the permissions, intermediate ones are present + writable +
 *     user;
 *   - missing tables are created all or nothing (as in vmm_core);
 *   - uvm_teardown() gives back every user page and every lower-half table,
 *     bottom-up, and clears the PML4's lower half. It does not flush the
 *     TLB: the caller reloads CR3 (or switches to another PML4) before the
 *     address space is used again. The PML4 frame itself stays the
 *     caller's.
 *
 * Every frame mapped here must have been fully written (zeroed or filled)
 * by the caller first: freed frames keep their old contents.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/vm_layout.h"
#include "kernel/memory/vmm_core.h"

struct uvm {
    const struct vmm_ops *ops;
    uint64_t root;                  /* the process's PML4 frame */
    bool nx;                        /* no-execute bits in use (EFER.NXE) */
    uint32_t pages;                 /* user pages mapped now */
    uint32_t tables;                /* lower-half PDPTs + PDs + PTs now */
};

enum uvm_result {
    UVM_OK,
    UVM_ERR_RANGE,                  /* outside [VM_USER_MIN, VM_USER_TOP), or unaligned */
    UVM_ERR_EXISTS,                 /* va already mapped */
    UVM_ERR_NOT_MAPPED,             /* nothing mapped at va */
    UVM_ERR_NO_FRAME,               /* no frame for a new table */
    UVM_ERR_FLAGS,                  /* flags other than PTE_WRITE / PTE_NX, or NX without NX support */
    UVM_ERR_WX,                     /* writable and executable, with NX in use */
    UVM_ERR_PHYS,                   /* frame not page-aligned, or beyond 52 bits */
};

struct uvm_teardown_stats {
    uint32_t pages;                 /* user page frames given back */
    uint32_t tables;                /* PDPTs + PDs + PTs given back */
    uint32_t bad_entries;           /* large-page entries found (never made here); left alone */
};

void uvm_init(struct uvm *u, const struct vmm_ops *ops, uint64_t root, bool nx);

/* Maps the user page at va to frame phys; flags: PTE_WRITE and/or PTE_NX. */
enum uvm_result uvm_map(struct uvm *u, uint64_t va, uint64_t phys, uint64_t flags);

/* Removes the mapping at va (flushes it); *phys_out = its frame, which stays
 * the caller's. */
enum uvm_result uvm_unmap(struct uvm *u, uint64_t va, uint64_t *phys_out);

/* Frame of the user page at va and its leaf entry's flag bits. */
bool uvm_translate(const struct uvm *u, uint64_t va, uint64_t *phys_out, uint64_t *flags_out);

/* Gives back every user page frame and lower-half table (ops->free_frame),
 * bottom-up, and clears PML4 entries 0-255. */
void uvm_teardown(struct uvm *u, struct uvm_teardown_stats *out);

/* Upper-half entries (PML4 slots 256-511 except the recursive slot, every
 * level) that have PTE_USER set, in the tables under root. Must be 0. */
uint32_t uvm_upper_user_entries(const struct vmm_ops *ops, uint64_t root);

const char *uvm_result_str(enum uvm_result result);

#endif
