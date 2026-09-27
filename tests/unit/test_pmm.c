/* Unit tests for kernel/memory/pmm.c (bitmap page allocator). */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/pmm.h"
#include "tests/unit/unit.h"

#define KiB 1024ull
#define MiB (1024ull * KiB)
#define GiB (1024ull * MiB)

/* Allocates until exhaustion; checks every page is aligned, allocatable,
 * strictly increasing (so never duplicated) and outside [avoid_lo, avoid_hi).
 * Returns the number of pages allocated. */
static uint32_t drain(uint64_t avoid_lo, uint64_t avoid_hi)
{
    uint32_t n = 0, prev = 0, p;
    while ((p = pmm_alloc_page()) != 0) {
        CHECK_EQ(p & (PAGE_SIZE - 1), 0);
        CHECK(p > prev);
        CHECK(!(p >= avoid_lo && p < avoid_hi));
        CHECK(!pmm_is_free(p));
        prev = p;
        n++;
    }
    return n;
}

/* 16 MiB machine shaped like QEMU: low 639 KiB, then 1 MiB..16 MiB-128 KiB. */
static void setup_small_machine(void)
{
    pmm_begin();
    CHECK_EQ(pmm_add_usable(0x0, 0x9fc00), PMM_OK);
    CHECK_EQ(pmm_add_usable(0x100000, 0xfe0000), PMM_OK);
    CHECK_EQ(pmm_reserve(0x0, 0x100000), PMM_OK);           /* low 1 MiB */
    CHECK_EQ(pmm_reserve(0x100000, 0x149000), PMM_OK);      /* "kernel" */
    pmm_finish();
}

static void test_first_allocation_is_lowest_free_page(void)
{
    setup_small_machine();
    CHECK_EQ(pmm_free_pages(), (0xfe0000 - 0x149000) / PAGE_SIZE);
    CHECK_EQ(pmm_alloc_page(), 0x149000);
    CHECK_EQ(pmm_alloc_page(), 0x14a000);
}

static void test_multiple_allocations_are_distinct(void)
{
    setup_small_machine();
    uint32_t pages[64];
    for (int i = 0; i < 64; i++) {
        pages[i] = pmm_alloc_page();
        CHECK(pages[i] != 0);
        for (int j = 0; j < i; j++)
            CHECK(pages[i] != pages[j]);
    }
}

static void test_exhaustion(void)
{
    setup_small_machine();
    uint32_t before = pmm_free_pages();
    CHECK_EQ(drain(0, 0x149000), before);
    CHECK_EQ(pmm_free_pages(), 0);
    CHECK_EQ(pmm_alloc_page(), 0);          /* stays exhausted, no crash */
    CHECK_EQ(pmm_alloc_page(), 0);
}

static void test_free_and_reallocate(void)
{
    setup_small_machine();
    uint32_t a = pmm_alloc_page(), b = pmm_alloc_page(), c = pmm_alloc_page();
    uint32_t before = pmm_free_pages();
    CHECK_EQ(pmm_free_page(b), PMM_OK);
    CHECK_EQ(pmm_free_pages(), before + 1);
    CHECK(pmm_is_free(b));
    CHECK_EQ(pmm_alloc_page(), b);          /* lowest free page again */
    CHECK_EQ(pmm_free_page(a), PMM_OK);
    CHECK_EQ(pmm_free_page(c), PMM_OK);
    CHECK_EQ(pmm_alloc_page(), a);
}

static void test_free_after_exhaustion(void)
{
    setup_small_machine();
    uint32_t first = pmm_alloc_page();
    drain(0, 0x149000);
    CHECK_EQ(pmm_free_page(first), PMM_OK);
    CHECK_EQ(pmm_alloc_page(), first);
    CHECK_EQ(pmm_alloc_page(), 0);
}

static void test_double_free_rejected(void)
{
    setup_small_machine();
    uint32_t p = pmm_alloc_page();
    CHECK_EQ(pmm_free_page(p), PMM_OK);
    uint32_t before = pmm_free_pages();
    CHECK_EQ(pmm_free_page(p), PMM_ERR_DOUBLE_FREE);
    CHECK_EQ(pmm_free_pages(), before);     /* count not corrupted */
    CHECK_EQ(pmm_free_page(0x800000), PMM_ERR_DOUBLE_FREE);  /* never allocated */
}

static void test_reserved_and_kernel_pages_rejected(void)
{
    setup_small_machine();
    CHECK_EQ(pmm_free_page(0x100000), PMM_ERR_RESERVED);     /* kernel */
    CHECK_EQ(pmm_free_page(0x148000), PMM_ERR_RESERVED);     /* last kernel page */
    CHECK_EQ(pmm_free_page(0xb8000), PMM_ERR_RESERVED);      /* VGA */
    CHECK_EQ(pmm_free_page(0x0), PMM_ERR_RESERVED);          /* page 0 */
    CHECK_EQ(pmm_free_page(0xfe0000), PMM_ERR_RESERVED);     /* beyond usable RAM */
    CHECK_EQ(pmm_free_page(0xfffff000u), PMM_ERR_RESERVED);  /* top of 4 GiB */
    CHECK(!pmm_is_allocatable(0x100000));
    CHECK(!pmm_is_allocatable(0x0));
    CHECK(!pmm_is_allocatable(0xb8000));
}

static void test_unaligned_rejected(void)
{
    setup_small_machine();
    uint32_t p = pmm_alloc_page();
    CHECK_EQ(pmm_free_page(p + 1), PMM_ERR_UNALIGNED);
    CHECK_EQ(pmm_free_page(p + 0x800), PMM_ERR_UNALIGNED);
    CHECK_EQ(pmm_free_page(p), PMM_OK);
}

static void test_page_zero_never_allocated(void)
{
    pmm_begin();
    pmm_add_usable(0x0, 0x3000);            /* pages 0, 1, 2 usable; no reservations */
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), 2);
    CHECK_EQ(pmm_alloc_page(), 0x1000);
    CHECK_EQ(pmm_alloc_page(), 0x2000);
    CHECK_EQ(pmm_alloc_page(), 0);
}

static void test_sub_page_region_ignored(void)
{
    pmm_begin();
    pmm_add_usable(0x201234, 0x201fff);     /* smaller than a page */
    pmm_add_usable(0x300000, 0x300fff);     /* 1 byte short of a page */
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), 0);
    CHECK_EQ(pmm_alloc_page(), 0);
}

static void test_region_crossing_page_boundaries(void)
{
    pmm_begin();
    pmm_add_usable(0x201800, 0x203800);     /* only page 0x202000 is whole */
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), 1);
    CHECK_EQ(pmm_alloc_page(), 0x202000);
    CHECK_EQ(pmm_alloc_page(), 0);
}

static void test_reservation_rounds_outward(void)
{
    pmm_begin();
    pmm_add_usable(0x200000, 0x205000);     /* 5 pages */
    pmm_reserve(0x202001, 0x202002);        /* 1 byte inside page 0x202000 */
    pmm_reserve(0x203fff, 0x204001);        /* touches pages 0x203000 and 0x204000 */
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), 2);
    CHECK_EQ(pmm_alloc_page(), 0x200000);
    CHECK_EQ(pmm_alloc_page(), 0x201000);
    CHECK_EQ(pmm_alloc_page(), 0);
}

static void test_reserved_wins_over_overlapping_usable(void)
{
    pmm_begin();
    pmm_add_usable(0x200000, 0x400000);
    pmm_add_usable(0x300000, 0x500000);     /* overlapping usable entries */
    pmm_reserve(0x250000, 0x260000);        /* reserved hole inside */
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), (0x500000 - 0x200000 - 0x10000) / PAGE_SIZE);
    CHECK(!pmm_is_allocatable(0x250000));
    CHECK(!pmm_is_allocatable(0x25f000));
    CHECK(pmm_is_allocatable(0x260000));
    CHECK_EQ(drain(0x250000, 0x260000), (0x500000 - 0x200000 - 0x10000) / PAGE_SIZE);
}

static void test_phase_enforced(void)
{
    pmm_begin();
    pmm_add_usable(0x200000, 0x300000);
    CHECK_EQ(pmm_reserve(0x200000, 0x201000), PMM_OK);
    /* After a reservation, usable memory can no longer be added, so a
     * reservation can never be undone by a later add. */
    CHECK_EQ(pmm_add_usable(0x200000, 0x201000), PMM_ERR_PHASE);
    CHECK_EQ(pmm_free_page(0x202000), PMM_ERR_PHASE);   /* not finished yet */
    CHECK_EQ(pmm_alloc_page(), 0);
    pmm_finish();
    CHECK_EQ(pmm_reserve(0x0, 0x1000), PMM_ERR_PHASE);
    CHECK_EQ(pmm_add_usable(0x0, 0x1000), PMM_ERR_PHASE);
    CHECK(!pmm_is_allocatable(0x200000));
    CHECK_EQ(pmm_alloc_page(), 0x201000);
}

static void test_invalid_range(void)
{
    pmm_begin();
    CHECK_EQ(pmm_add_usable(0x300000, 0x200000), PMM_ERR_RANGE);
    CHECK_EQ(pmm_add_usable(0x300000, 0x300000), PMM_OK);   /* empty is fine */
    CHECK_EQ(pmm_reserve(0x300000, 0x200000), PMM_ERR_RANGE);
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), 0);
}

static void test_above_4g_ignored_and_clipped(void)
{
    pmm_begin();
    pmm_add_usable(4 * GiB, 5 * GiB);                        /* entirely above */
    CHECK_EQ(pmm_count_allocatable(), 0);
    pmm_add_usable(0xfffff000ull, 0x100001000ull);           /* straddles 4 GiB */
    CHECK_EQ(pmm_count_allocatable(), 1);
    pmm_add_usable(0xffffffffffffe000ull, 0xfffffffffffff000ull);  /* near 2^64 */
    CHECK_EQ(pmm_count_allocatable(), 1);
    pmm_reserve(5 * GiB, 6 * GiB);                           /* no effect, no wrap */
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), 1);
    CHECK_EQ(pmm_alloc_page(), 0xfffff000u);                 /* highest 32-bit page */
    CHECK_EQ(pmm_free_page(0xfffff000u), PMM_OK);
}

static void test_no_usable_memory(void)
{
    pmm_begin();
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), 0);
    CHECK_EQ(pmm_alloc_page(), 0);
}

static void test_single_usable_page(void)
{
    pmm_begin();
    pmm_add_usable(0x100000, 0x200000);
    pmm_reserve(0x100000, 0x1ff000);
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), 1);
    CHECK_EQ(pmm_alloc_page(), 0x1ff000);
    CHECK_EQ(pmm_alloc_page(), 0);
    CHECK_EQ(pmm_free_page(0x1ff000), PMM_OK);
    CHECK_EQ(pmm_alloc_page(), 0x1ff000);
}

static void test_multiple_usable_regions(void)
{
    pmm_begin();
    pmm_add_usable(0x100000, 0x102000);     /* 2 pages */
    pmm_add_usable(0x800000, 0x803000);     /* 3 pages */
    pmm_add_usable(0x10000000, 0x10001000); /* 1 page */
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), 6);
    uint32_t expect[] = { 0x100000, 0x101000, 0x800000, 0x801000, 0x802000, 0x10000000 };
    for (int i = 0; i < 6; i++)
        CHECK_EQ(pmm_alloc_page(), expect[i]);
    CHECK_EQ(pmm_alloc_page(), 0);
}

static void test_full_32bit_space(void)
{
    /* 4 GiB of usable RAM minus the low 1 MiB: the largest possible map. */
    pmm_begin();
    pmm_add_usable(0, PMM_ADDR_LIMIT);
    pmm_reserve(0, 0x100000);
    pmm_finish();
    CHECK_EQ(pmm_free_pages(), PMM_MAX_PAGES - 256);
    CHECK_EQ(drain(0, 0x100000), PMM_MAX_PAGES - 256);
}

int main(void)
{
    printf("pmm unit tests\n");
    RUN(test_first_allocation_is_lowest_free_page);
    RUN(test_multiple_allocations_are_distinct);
    RUN(test_exhaustion);
    RUN(test_free_and_reallocate);
    RUN(test_free_after_exhaustion);
    RUN(test_double_free_rejected);
    RUN(test_reserved_and_kernel_pages_rejected);
    RUN(test_unaligned_rejected);
    RUN(test_page_zero_never_allocated);
    RUN(test_sub_page_region_ignored);
    RUN(test_region_crossing_page_boundaries);
    RUN(test_reservation_rounds_outward);
    RUN(test_reserved_wins_over_overlapping_usable);
    RUN(test_phase_enforced);
    RUN(test_invalid_range);
    RUN(test_above_4g_ignored_and_clipped);
    RUN(test_no_usable_memory);
    RUN(test_single_usable_page);
    RUN(test_multiple_usable_regions);
    RUN(test_full_32bit_space);
    return unit_report("pmm");
}
