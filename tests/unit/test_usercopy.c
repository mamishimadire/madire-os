/* Unit tests for kernel/memory/usercopy_core.c (user buffer checks, 0.10),
 * with a fake page lookup. */
#include <stdint.h>
#include "kernel/memory/pte.h"
#include "kernel/memory/usercopy_core.h"
#include "kernel/memory/vm_layout.h"
#include "tests/unit/unit.h"

/* Fake address space: a few pages with given flags; everything else
 * unmapped. Counts lookups so the page-by-page walk can be checked. */
struct fake_page { uint64_t va; uint64_t flags; };
static struct fake_page pages[8];
static unsigned npages, lookups;

static bool fake_lookup(void *ctx, uint64_t va, uint64_t *flags_out)
{
    (void)ctx;
    lookups++;
    for (unsigned i = 0; i < npages; i++)
        if (pages[i].va == va) {
            *flags_out = pages[i].flags;
            return true;
        }
    return false;
}

#define RO   (PTE_PRESENT | PTE_USER)
#define RW   (PTE_PRESENT | PTE_USER | PTE_WRITE)
#define KERN (PTE_PRESENT | PTE_WRITE)            /* present, not user */

static void map(uint64_t va, uint64_t flags) { pages[npages].va = va; pages[npages].flags = flags; npages++; }
static void reset(void) { npages = 0; lookups = 0; }

#define DATA VM_USER_DATA

static void test_range_arithmetic(void)
{
    CHECK(ucopy_range_ok(0, 0));                                /* length 0: anything */
    CHECK(ucopy_range_ok(0xFFFFFFFF80100000ull, 0));
    CHECK(ucopy_range_ok(DATA, 1));
    CHECK(ucopy_range_ok(VM_USER_TOP - 1, 1));                  /* last byte below the top */
    CHECK(ucopy_range_ok(VM_USER_TOP - 4096, 4096));
    CHECK(!ucopy_range_ok(VM_USER_TOP, 1));
    CHECK(!ucopy_range_ok(VM_USER_TOP - 1, 2));
    CHECK(!ucopy_range_ok(0xFFFFFFFF80100000ull, 1));           /* kernel address */
    CHECK(!ucopy_range_ok(0xFFFF800000000000ull, 16));
    CHECK(!ucopy_range_ok(DATA, UINT64_MAX));                   /* wraps */
    CHECK(!ucopy_range_ok(DATA, UINT64_MAX - DATA + 1));        /* end wraps to exactly 0 */
    CHECK(!ucopy_range_ok(UINT64_MAX, 1));
    CHECK(!ucopy_range_ok(0x0000800000000000ull, 1));           /* non-canonical */
}

static void test_length_zero_touches_nothing(void)
{
    reset();
    CHECK(ucopy_check(0, 0, false, fake_lookup, 0));
    CHECK(ucopy_check(0xFFFFFFFF80100000ull, 0, true, fake_lookup, 0));
    CHECK_EQ(lookups, 0);
}

static void test_mapped_user_pages(void)
{
    reset();
    map(DATA, RW);
    map(DATA + 4096, RO);
    CHECK(ucopy_check(DATA, 1, false, fake_lookup, 0));
    CHECK(ucopy_check(DATA + 96, 4000, true, fake_lookup, 0));          /* ends at the page end */
    CHECK(!ucopy_check(DATA + 100, 4000, true, fake_lookup, 0));        /* 4 bytes into the RO page */
    CHECK(ucopy_check(DATA + 4000, 200, false, fake_lookup, 0));        /* across two pages */
    CHECK(!ucopy_check(DATA + 4000, 200, true, fake_lookup, 0));        /* second page read-only */
    CHECK(ucopy_check(DATA + 4096, 4096, false, fake_lookup, 0));
    CHECK(!ucopy_check(DATA + 4096, 4097, false, fake_lookup, 0));      /* runs into a hole */
}

static void test_page_by_page(void)
{
    reset();
    map(DATA, RO);
    map(DATA + 4096, RO);
    map(DATA + 8192, RO);
    lookups = 0;
    CHECK(ucopy_check(DATA + 10, 8192, false, fake_lookup, 0));         /* touches 3 pages */
    CHECK_EQ(lookups, 3);
    lookups = 0;
    CHECK(ucopy_check(DATA, 4096, false, fake_lookup, 0));              /* exactly 1 page */
    CHECK_EQ(lookups, 1);
}

static void test_holes_and_kernel_pages(void)
{
    reset();
    map(DATA, RW);
    map(DATA + 8192, RW);                               /* hole at DATA + 4096 */
    CHECK(!ucopy_check(DATA, 12288, false, fake_lookup, 0));
    CHECK(!ucopy_check(DATA + 4096, 1, false, fake_lookup, 0));
    reset();
    map(DATA, KERN);                                    /* present, but not a user page */
    CHECK(!ucopy_check(DATA, 1, false, fake_lookup, 0));
    reset();
    map(DATA, PTE_USER | PTE_WRITE);                    /* not present */
    CHECK(!ucopy_check(DATA, 1, false, fake_lookup, 0));
}

static void test_null_and_top(void)
{
    reset();
    CHECK(!ucopy_check(0, 1, false, fake_lookup, 0));
    CHECK(!ucopy_check(0, 5, true, fake_lookup, 0));
    map(VM_USER_TOP - 4096, RW);
    CHECK(ucopy_check(VM_USER_TOP - 16, 16, true, fake_lookup, 0));
    CHECK(!ucopy_check(VM_USER_TOP - 16, 17, true, fake_lookup, 0));
    lookups = 0;
    CHECK(!ucopy_check(0xFFFFFFFF80100000ull, 8, false, fake_lookup, 0));
    CHECK_EQ(lookups, 0);                               /* refused before any lookup */
}

int main(void)
{
    RUN(test_range_arithmetic);
    RUN(test_length_zero_touches_nothing);
    RUN(test_mapped_user_pages);
    RUN(test_page_by_page);
    RUN(test_holes_and_kernel_pages);
    RUN(test_null_and_top);
    return unit_report("usercopy");
}
