#ifndef MADIRE_MEMORY_PMM_H
#define MADIRE_MEMORY_PMM_H

/*
 * Physical page frame allocator (bitmap).
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_pmm.c). It only tracks page numbers; it never
 * reads or writes the pages themselves.
 *
 * Covers the 32-bit physical address space (1,048,576 pages of 4 KiB) with
 * two bitmaps of 128 KiB each:
 *   allocatable - 1 = the page may ever be handed out (usable, not reserved)
 *   used        - 1 = the page is allocated, or not allocatable
 * A page is free when allocatable = 1 and used = 0.
 *
 * Set-up is phased so that reserved memory always wins, whatever order the
 * caller discovers regions in:
 *   pmm_begin()      everything unavailable
 *   pmm_add_usable() mark usable RAM (rounded inward to whole pages)
 *   pmm_reserve()    remove ranges (rounded outward); after the first
 *                    reservation, pmm_add_usable() is refused
 *   pmm_finish()     page 0 removed, counts fixed, allocation enabled
 *
 * Page 0 is never allocatable, so 0 means "out of memory".
 */

#include <stdbool.h>
#include <stdint.h>

#define PAGE_SIZE       4096u
#define PAGE_SHIFT      12
#define PMM_MAX_PAGES   (1u << 20)          /* 4 GiB / 4 KiB */
#define PMM_ADDR_LIMIT  0x100000000ull      /* no PAE: RAM above 4 GiB is not used */

enum pmm_result {
    PMM_OK,
    PMM_ERR_PHASE,          /* call not allowed in the current set-up phase */
    PMM_ERR_RANGE,          /* end < base */
    PMM_ERR_UNALIGNED,      /* address is not a multiple of PAGE_SIZE */
    PMM_ERR_RESERVED,       /* page was never allocatable */
    PMM_ERR_DOUBLE_FREE,    /* page is already free */
};

void pmm_begin(void);
enum pmm_result pmm_add_usable(uint64_t base, uint64_t end);    /* [base, end) */
enum pmm_result pmm_reserve(uint64_t base, uint64_t end);       /* [base, end) */
void pmm_finish(void);

/* Lowest free page (deterministic), or 0 when memory is exhausted. */
uint32_t pmm_alloc_page(void);
enum pmm_result pmm_free_page(uint32_t addr);

bool pmm_is_allocatable(uint32_t addr);
bool pmm_is_free(uint32_t addr);

/* Pages currently marked allocatable (valid in any phase after begin). */
uint32_t pmm_count_allocatable(void);
/* Free pages right now (valid after finish). */
uint32_t pmm_free_pages(void);

const char *pmm_result_str(enum pmm_result result);

#endif
