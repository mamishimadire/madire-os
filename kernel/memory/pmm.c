/*
 * Physical page frame allocator: two bitmaps over the 32-bit address space
 * (see pmm.h for the model and phases).
 *
 * Allocation returns the lowest free page. `hint` is the first bitmap word
 * that may contain a free page: every page below it is in use. Allocation
 * scans from there; freeing a page below it moves it back. This keeps
 * allocation fast while staying fully deterministic.
 *
 * Only shifts, additions and comparisons are used on 64-bit values: 64-bit
 * division would need libgcc, which the kernel does not link.
 */
#include "kernel/memory/pmm.h"

#define WORDS (PMM_MAX_PAGES / 32)

enum phase { PHASE_OFF, PHASE_USABLE, PHASE_RESERVE, PHASE_READY };

static uint32_t allocatable[WORDS];
static uint32_t used[WORDS];
static enum phase phase = PHASE_OFF;
static uint32_t free_count;
static uint32_t hint;

static inline bool bit_get(const uint32_t *map, uint32_t page)
{
    return (map[page >> 5] >> (page & 31)) & 1u;
}

static inline void bit_set(uint32_t *map, uint32_t page)
{
    map[page >> 5] |= 1u << (page & 31);
}

static inline void bit_clear(uint32_t *map, uint32_t page)
{
    map[page >> 5] &= ~(1u << (page & 31));
}

/* Counts set bits without __builtin_popcount (which needs libgcc on i386). */
static uint32_t bits_set(uint32_t v)
{
    uint32_t n = 0;
    while (v) {
        v &= v - 1;
        n++;
    }
    return n;
}

void pmm_begin(void)
{
    for (uint32_t i = 0; i < WORDS; i++) {
        allocatable[i] = 0;
        used[i] = 0xFFFFFFFFu;
    }
    free_count = 0;
    hint = 0;
    phase = PHASE_USABLE;
}

enum pmm_result pmm_add_usable(uint64_t base, uint64_t end)
{
    if (phase != PHASE_USABLE)
        return PMM_ERR_PHASE;
    if (end < base)
        return PMM_ERR_RANGE;
    if (base >= PMM_ADDR_LIMIT)
        return PMM_OK;                      /* entirely above 4 GiB */
    if (end > PMM_ADDR_LIMIT)
        end = PMM_ADDR_LIMIT;

    /* Inward: only whole pages inside [base, end). base < 2^32 here, so
     * rounding up cannot overflow. */
    uint64_t first = (base + PAGE_SIZE - 1) >> PAGE_SHIFT;
    uint64_t last = end >> PAGE_SHIFT;      /* exclusive */
    for (uint64_t p = first; p < last; p++)
        bit_set(allocatable, (uint32_t)p);
    return PMM_OK;
}

enum pmm_result pmm_reserve(uint64_t base, uint64_t end)
{
    if (phase == PHASE_USABLE)
        phase = PHASE_RESERVE;
    if (phase != PHASE_RESERVE)
        return PMM_ERR_PHASE;
    if (end < base)
        return PMM_ERR_RANGE;
    if (base >= PMM_ADDR_LIMIT || end == base)
        return PMM_OK;
    if (end > PMM_ADDR_LIMIT)
        end = PMM_ADDR_LIMIT;

    /* Outward: every page touched by [base, end). end <= 2^32 here. */
    uint64_t first = base >> PAGE_SHIFT;
    uint64_t last = (end + PAGE_SIZE - 1) >> PAGE_SHIFT;
    for (uint64_t p = first; p < last; p++)
        bit_clear(allocatable, (uint32_t)p);
    return PMM_OK;
}

void pmm_finish(void)
{
    if (phase != PHASE_USABLE && phase != PHASE_RESERVE)
        return;

    bit_clear(allocatable, 0);              /* page 0: 0 means "no page" */
    free_count = 0;
    for (uint32_t i = 0; i < WORDS; i++) {
        used[i] = ~allocatable[i];
        free_count += bits_set(allocatable[i]);
    }
    hint = 0;
    phase = PHASE_READY;
}

uint32_t pmm_alloc_page(void)
{
    if (phase != PHASE_READY || free_count == 0)
        return 0;

    for (uint32_t w = hint; w < WORDS; w++) {
        uint32_t free_bits = ~used[w];
        if (free_bits) {
            uint32_t page = (w << 5) | (uint32_t)__builtin_ctz(free_bits);
            bit_set(used, page);
            free_count--;
            hint = w;
            return page << PAGE_SHIFT;
        }
    }
    return 0;   /* unreachable while free_count is consistent */
}

enum pmm_result pmm_free_page(uint32_t addr)
{
    if (phase != PHASE_READY)
        return PMM_ERR_PHASE;
    if (addr & (PAGE_SIZE - 1))
        return PMM_ERR_UNALIGNED;

    uint32_t page = addr >> PAGE_SHIFT;
    if (!bit_get(allocatable, page))
        return PMM_ERR_RESERVED;
    if (!bit_get(used, page))
        return PMM_ERR_DOUBLE_FREE;

    bit_clear(used, page);
    free_count++;
    if ((page >> 5) < hint)
        hint = page >> 5;
    return PMM_OK;
}

bool pmm_is_allocatable(uint32_t addr)
{
    return phase != PHASE_OFF && bit_get(allocatable, addr >> PAGE_SHIFT);
}

bool pmm_is_free(uint32_t addr)
{
    return phase == PHASE_READY && pmm_is_allocatable(addr) && !bit_get(used, addr >> PAGE_SHIFT);
}

uint32_t pmm_count_allocatable(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < WORDS; i++)
        n += bits_set(allocatable[i]);
    return n;
}

uint32_t pmm_free_pages(void)
{
    return phase == PHASE_READY ? free_count : 0;
}

const char *pmm_result_str(enum pmm_result result)
{
    switch (result) {
    case PMM_OK:              return "ok";
    case PMM_ERR_PHASE:       return "not allowed in this set-up phase";
    case PMM_ERR_RANGE:       return "invalid range";
    case PMM_ERR_UNALIGNED:   return "address is not page-aligned";
    case PMM_ERR_RESERVED:    return "page is reserved";
    case PMM_ERR_DOUBLE_FREE: return "page is already free";
    }
    return "unknown allocator error";
}
