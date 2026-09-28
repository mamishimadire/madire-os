/* Unit tests for kernel/memory/pte.h and vm_layout.h (entry and address helpers). */
#include <stdint.h>
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "tests/unit/unit.h"

static void test_index_split(void)
{
    CHECK_EQ(pd_index(0x00000000u), 0);
    CHECK_EQ(pt_index(0x00000000u), 0);
    CHECK_EQ(pd_index(0x003FFFFFu), 0);
    CHECK_EQ(pt_index(0x003FFFFFu), 1023);
    CHECK_EQ(pd_index(0x00400000u), 1);
    CHECK_EQ(pt_index(0x00400000u), 0);
    CHECK_EQ(pd_index(0x000B8000u), 0);
    CHECK_EQ(pt_index(0x000B8000u), 0xB8);
    CHECK_EQ(pd_index(0xFFFFF000u), 1023);
    CHECK_EQ(pt_index(0xFFFFF000u), 1023);
}

static void test_entry_encode_decode(void)
{
    uint32_t e = pte_make(0x12345000u, PTE_PRESENT | PTE_WRITE);
    CHECK_EQ(e, 0x12345003u);
    CHECK_EQ(pte_frame(e), 0x12345000u);
    CHECK(pte_present(e));
    CHECK(pte_writable(e));
    CHECK(!pte_present(0));
    e = pte_make(0x12345ABCu, PTE_PRESENT);       /* offset bits never leak into the frame */
    CHECK_EQ(e, 0x12345001u);
    CHECK(!pte_writable(e));
    CHECK_EQ(pte_frame(0xFFFFFFFFu), 0xFFFFF000u);
}

static void test_alignment_helpers(void)
{
    CHECK(page_aligned(0));
    CHECK(page_aligned(0x1000));
    CHECK(!page_aligned(0x1001));
    CHECK_EQ(page_floor(0x14a026u), 0x14a000u);
    CHECK_EQ(page_ceil(0x14a026u), 0x14b000u);
    CHECK_EQ(page_ceil(0x14a000u), 0x14a000u);
    CHECK_EQ(page_ceil(0xFFFFFFFFu), 0xFFFFF000u);  /* no wrap to 0 */
    CHECK_EQ(page_ceil(0xFFFFF001u), 0xFFFFF000u);
}

static void test_layout_consistency(void)
{
    /* The recursive slot is the last directory entry; the window, the
     * self-test page and the heap all sit below it, page-aligned, and the
     * heap (at its maximum) cannot reach them. */
    CHECK_EQ(pd_index(VM_RECURSIVE_BASE), VM_RECURSIVE_INDEX);
    CHECK_EQ(VM_PAGE_DIRECTORY, VM_RECURSIVE_BASE + VM_RECURSIVE_INDEX * VM_PAGE_SIZE);
    CHECK(page_aligned(VM_WINDOW) && page_aligned(VM_TEST_PAGE) && page_aligned(VM_HEAP_BASE));
    CHECK(pd_index(VM_WINDOW) != VM_RECURSIVE_INDEX);
    CHECK_EQ(pd_index(VM_WINDOW), pd_index(VM_TEST_PAGE));        /* share one table */
    CHECK((uint64_t)VM_HEAP_BASE + VM_HEAP_MAX + VM_PAGE_SIZE <= VM_TEST_PAGE);
    CHECK(VM_HEAP_INITIAL % VM_PAGE_SIZE == 0 && VM_HEAP_GROW_MIN % VM_PAGE_SIZE == 0);
    CHECK(VM_LOW_MAP_START == VM_NULL_GUARD_END);
}

static void test_page_zero_is_the_null_guard(void)
{
    CHECK_EQ(page_floor(0x400u), 0);      /* BIOS data area lives in page 0 */
    CHECK_EQ(page_floor(0x40Eu), 0);      /* EBDA pointer too */
    CHECK(VM_LOW_MAP_START > 0x40Eu);
}

int main(void)
{
    printf("pte unit tests\n");
    RUN(test_index_split);
    RUN(test_entry_encode_decode);
    RUN(test_alignment_helpers);
    RUN(test_layout_consistency);
    RUN(test_page_zero_is_the_null_guard);
    return unit_report("pte");
}
