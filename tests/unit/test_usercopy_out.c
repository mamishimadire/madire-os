/* Unit tests for copy_to_user's check (1.0 stage 4): usercopy_core.c's
 * ucopy_check with write = true, in the shapes the descriptor calls use
 * (kernel/proc/fd.c): a read's destination, one chunk of at most 4096
 * bytes at a time, must be present, user and writable on every page it
 * touches; read-only program data, kernel pages, holes and anything at or
 * above VM_USER_TOP are refused before a byte is written. */
#include <stdint.h>
#include "kernel/memory/pte.h"
#include "kernel/memory/usercopy_core.h"
#include "kernel/memory/vm_layout.h"
#include "tests/unit/unit.h"

/* Fake address space: the pages listed, everything else unmapped. */
struct fake_page { uint64_t va; uint64_t flags; };
static struct fake_page pages[24];
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
#define KERN (PTE_PRESENT | PTE_WRITE)

static void map(uint64_t va, uint64_t flags) { pages[npages].va = va; pages[npages].flags = flags; npages++; }
static void reset(void) { npages = 0; lookups = 0; }
static bool out(uint64_t addr, uint64_t len) { return ucopy_check(addr, len, true, fake_lookup, 0); }

#define TEXT VM_USER_TEXT
#define DATA VM_USER_DATA
#define PG   VM_PAGE_SIZE

/* A program like fdops: text read-only, 12 data pages (48 KiB) writable. */
static void program(void)
{
    reset();
    map(TEXT, RO);
    map(TEXT + PG, RO);
    for (unsigned i = 0; i < 12; i++)
        map(DATA + i * PG, RW);
}

static void test_chunks_into_data(void)
{
    program();
    for (uint64_t off = 0; off < 12 * PG; off += PG)
        CHECK(out(DATA + off, PG));                         /* each 4096-byte chunk */
    CHECK(out(DATA + 100, PG));                             /* across two pages */
    CHECK(out(DATA + 12 * PG - 8, 8));                      /* the last bytes */
    CHECK(!out(DATA + 12 * PG - 8, 9));                     /* one byte into the hole */
    CHECK(!out(DATA + 12 * PG, 1));
}

static void test_refused_before_writing(void)
{
    program();
    CHECK(!out(TEXT + 16, 100));                            /* read-only data (fdops 14) */
    CHECK(out(TEXT + 16, 0));                               /* length 0 touches nothing */
    CHECK(!out(0x10000000ull, 100));                        /* unmapped (fdops 14) */
    CHECK(!out(0xFFFFFFFF80100000ull, 100));                /* kernel text (fdops 14) */
    CHECK(!out(0, 1));                                      /* NULL */
    map(DATA + 20 * PG, KERN);
    CHECK(!out(DATA + 20 * PG, 8));                         /* present, writable, not user */
    CHECK(!out(TEXT + PG - 4, 8));                          /* read-only across read-only */
    CHECK(!out(DATA - 4, 8));                               /* hole into the data */
}

static void test_one_bad_page_inside(void)
{
    program();
    pages[2 + 5].flags = RO;                                /* data page 5 read-only */
    CHECK(out(DATA, 5 * PG));
    CHECK(!out(DATA, 6 * PG));
    CHECK(!out(DATA + 5 * PG + 4095, 1));
    CHECK(out(DATA + 6 * PG, PG));
    lookups = 0;
    CHECK(!out(DATA + 4 * PG, 2 * PG));
    CHECK_EQ(lookups, 2);                                   /* stops at the bad page */
}

static void test_top_and_wrap(void)
{
    reset();
    map(VM_USER_TOP - PG, RW);
    CHECK(out(VM_USER_TOP - PG, PG));
    CHECK(!out(VM_USER_TOP - PG, PG + 1));                  /* past the user top */
    CHECK(!out(VM_USER_TOP - 8, UINT64_MAX));               /* wraps */
    CHECK(!out(VM_USER_TOP, 1));
    CHECK(!out(0x0000800000000000ull, 1));                  /* non-canonical */
}

int main(void)
{
    RUN(test_chunks_into_data);
    RUN(test_refused_before_writing);
    RUN(test_one_bad_page_inside);
    RUN(test_top_and_wrap);
    return unit_report("usercopy_out");
}
