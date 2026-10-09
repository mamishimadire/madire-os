#ifndef MADIRE_MEMORY_USPACE_H
#define MADIRE_MEMORY_USPACE_H

/*
 * Address spaces (0.10): one PML4 per process. Its kernel half (entries
 * 256-511) is copied from the kernel's PML4 when it is created, so every
 * kernel mapping is shared (the freeze, paging.h, keeps those entries
 * fixed); entry 510 then points to the new PML4 itself, so the recursive
 * mapping shows its own tables. The lower half belongs to the process
 * (uvm_core.h).
 *
 * The lower half is edited only while the address space is loaded in CR3
 * (through the recursive mapping), by the thread that entered it; a thread
 * switch away and back reloads it (kernel/proc/thread.c), so this needs no
 * preemption guard. Every user frame is filled through the physical-access
 * window before it is mapped: a frame never shows a previous owner's data.
 *
 * Lifecycle: uspace_create, uspace_enter, uspace_map_page..., then
 * uspace_destroy (while entered): every user frame and lower-half table is
 * given back bottom-up, the thread returns to the kernel's address space,
 * and the PML4 frame is freed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/memory/uvm_core.h"

struct uspace {
    uint64_t pml4;                  /* PML4 frame; 0 = none */
    struct uvm uvm;
};

/* A new, empty address space. false: no frame left. */
bool uspace_create(struct uspace *as);

/* The calling thread moves into the address space / back to the kernel's. */
void uspace_enter(struct uspace *as);
void uspace_leave(void);

/* Takes a frame, fills it (len bytes from src, the rest zero; src may be
 * NULL for a zero page) and maps it at va with flags (PTE_WRITE, PTE_NX).
 * The address space must be entered (else panic). *frame_out (optional):
 * the frame. Anything but UVM_OK gives the frame back. */
enum uvm_result uspace_map_page(struct uspace *as, uint64_t va, const void *src, size_t len,
                                uint64_t flags, uint64_t *frame_out);

/* While entered: gives back every user frame and lower-half table, leaves,
 * and frees the PML4 frame. */
void uspace_destroy(struct uspace *as, struct uvm_teardown_stats *out);

/* True if the address space is the one loaded in CR3. */
bool uspace_is_current(const struct uspace *as);

#endif
