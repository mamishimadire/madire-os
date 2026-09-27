#ifndef MADIRE_MEMORY_MEMORY_H
#define MADIRE_MEMORY_MEMORY_H

/*
 * Memory subsystem entry point: validates the Multiboot memory map, reserves
 * the kernel and boot data, and brings up the physical page allocator.
 * Everything is calculated at run time from the bootloader's memory map.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/reserve.h"
#include "kernel/multiboot.h"

struct memory_stats {
    uint64_t ram_bytes;         /* every RAM-type region (types 1, 3, 4, 5), incl. above 4 GiB */
    uint64_t usable_bytes;      /* usable_pages * PAGE_SIZE */
    uint64_t above_4g_bytes;    /* RAM a 32-bit kernel without PAE cannot address */
    uint32_t usable_pages;      /* whole type-1 pages below 4 GiB, minus other map entries */
    uint32_t free_pages;        /* usable pages minus reservations, after set-up */
    uint32_t reserved_pages;    /* usable pages taken by reservations */
    unsigned reservations;      /* number of reserved ranges */
};

/* Sets up the allocator. Any inconsistency (no map, malformed map, kernel
 * not in usable RAM, nothing free) is fatal: it shows the crash screen. */
void memory_init(const struct multiboot_info *mbi, struct memory_stats *out);

/* Lowest free physical page, or 0 when memory is exhausted. */
uint32_t memory_alloc_page(void);

/* Returns a page to the allocator. Freeing an unaligned address, a reserved
 * page or an already-free page is a kernel bug and panics. */
void memory_free_page(uint32_t addr);

/* Independent safety check used by the self-tests: true only if the whole
 * page is inside a usable map region below 4 GiB and touches no other map
 * region and no reservation. */
bool memory_page_is_safe(uint32_t addr);

const struct phys_range *memory_reservations(unsigned *count);

/* Bytes to whole MB (MiB), rounded to nearest. */
uint32_t memory_bytes_to_mb(uint64_t bytes);

#endif
