/* Unit tests for kernel/memory/pte.h and vm_layout.h (x86-64 4-level entries
 * and address helpers). */
#include <stdint.h>
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "tests/unit/unit.h"

#define HIGHER_HALF 0xFFFFFFFF80000000ull

static void test_index_split(void)
{
    CHECK_EQ(vm_index(0x0000000000000000ull, 1), 0);
    CHECK_EQ(vm_index(0x00000000001FF000ull, 1), 511);        /* last PT entry */
    CHECK_EQ(vm_index(0x00000000001FFFFFull, 2), 0);
    CHECK_EQ(vm_index(0x0000000000200000ull, 2), 1);          /* 2 MiB boundary */
    CHECK_EQ(vm_index(0x0000000000200000ull, 1), 0);
    CHECK_EQ(vm_index(0x0000000040000000ull, 3), 1);          /* 1 GiB boundary */
    CHECK_EQ(vm_index(0x0000008000000000ull, 4), 1);          /* 512 GiB boundary */
    CHECK_EQ(vm_index(0x00000000000B8000ull, 1), 0xB8);       /* VGA */
    CHECK_EQ(vm_index(0x00000000000B8000ull, 2), 0);
    CHECK_EQ(vm_index(VM_HEAP_BASE, 4), 509);
    CHECK_EQ(vm_index(VM_RECURSIVE_BASE, 4), 510);
    CHECK_EQ(vm_index(HIGHER_HALF, 4), 511);
    CHECK_EQ(vm_index(HIGHER_HALF, 3), 510);
    CHECK_EQ(vm_index(0xFFFFFFFFFFFFF000ull, 1), 511);
}

static void test_canonical(void)
{
    CHECK(vm_canonical(0));
    CHECK(vm_canonical(0x00007FFFFFFFFFFFull));
    CHECK(!vm_canonical(0x0000800000000000ull));
    CHECK(!vm_canonical(0xFFFF7FFFFFFFFFFFull));
    CHECK(vm_canonical(0xFFFF800000000000ull));
    CHECK(vm_canonical(0xFFFFFFFFFFFFFFFFull));
    CHECK_EQ(vm_sign_extend(0x0000FF8000000000ull), 0xFFFFFF8000000000ull);
    CHECK_EQ(vm_sign_extend(0x00007F8000000000ull), 0x00007F8000000000ull);
}

static void test_entry_encode_decode(void)
{
    uint64_t e = pte_make(0x12345000ull, PTE_PRESENT | PTE_WRITE);
    CHECK_EQ(e, 0x12345003ull);
    CHECK_EQ(pte_frame(e), 0x12345000ull);
    CHECK(pte_present(e) && pte_writable(e) && !pte_nx(e));
    CHECK(!pte_present(0));
    e = pte_make(0x000FFFFFFFFFF000ull, PTE_PRESENT | PTE_WRITE | PTE_NX);   /* 52-bit frame */
    CHECK_EQ(pte_frame(e), 0x000FFFFFFFFFF000ull);
    CHECK(pte_nx(e));
    e = pte_make(0x12345ABCull, PTE_PRESENT);        /* offset bits never leak into the frame */
    CHECK_EQ(e, 0x12345001ull);
    CHECK(!pte_writable(e));
    CHECK_EQ(pte_frame(0xFFFFFFFFFFFFFFFFull), PTE_ADDR_MASK);   /* NX and flags stripped */
}

static void test_alignment_helpers(void)
{
    CHECK(vm_aligned(0));
    CHECK(vm_aligned(0x1000));
    CHECK(!vm_aligned(0x1001));
    CHECK_EQ(vm_floor(0x14a026ull), 0x14a000ull);
    CHECK_EQ(vm_ceil(0x14a026ull), 0x14b000ull);
    CHECK_EQ(vm_ceil(0x14a000ull), 0x14a000ull);
    CHECK_EQ(vm_ceil(0xFFFFFFFFFFFFFFFFull), 0xFFFFFFFFFFFFF000ull);   /* no wrap to 0 */
    CHECK_EQ(vm_ceil(0xFFFFFFFFFFFFF001ull), 0xFFFFFFFFFFFFF000ull);
}

static void test_layout_consistency(void)
{
    /* One PML4 slot per area; all canonical and page-aligned; the recursive
     * slot's formula gives VM_RECURSIVE_BASE and VM_PML4; the heap at its
     * maximum stays inside its own slot. */
    CHECK_EQ(vm_index(VM_TEST_PAGE, 4), 508);
    CHECK_EQ(vm_index(VM_WINDOW, 4), 508);
    CHECK_EQ(vm_index(VM_WINDOW, 1), vm_index(VM_TEST_PAGE, 1) + 1);   /* share one PT */
    CHECK_EQ(vm_recursive_table(VM_RECURSIVE_INDEX, 1, 0), VM_RECURSIVE_BASE);
    CHECK_EQ(vm_recursive_table(VM_RECURSIVE_INDEX, 4, 0), VM_PML4);
    CHECK(vm_canonical(VM_TEST_PAGE) && vm_canonical(VM_WINDOW) && vm_canonical(VM_HEAP_BASE) &&
          vm_canonical(VM_RECURSIVE_BASE) && vm_canonical(VM_PML4));
    CHECK(vm_aligned(VM_WINDOW) && vm_aligned(VM_TEST_PAGE) && vm_aligned(VM_HEAP_BASE));
    CHECK_EQ(vm_index(VM_HEAP_BASE + VM_HEAP_MAX + VM_PAGE_SIZE, 4), 509);
    CHECK(VM_HEAP_INITIAL % VM_PAGE_SIZE == 0 && VM_HEAP_GROW_MIN % VM_PAGE_SIZE == 0);
    CHECK(VM_LOW_MAP_START == VM_NULL_GUARD_END);
    CHECK_EQ(VM_KERNEL_HIGH_INDEX, vm_index(HIGHER_HALF, 4));
}

/* 0.8: the kernel slot and the physical view, and their helpers. */
static void test_higher_half_helpers(void)
{
    CHECK_EQ(VM_KERNEL_VIRT_OFFSET, HIGHER_HALF);
    CHECK_EQ(vm_index(VM_KERNEL_VIRT_OFFSET, 4), VM_KERNEL_HIGH_INDEX);
    CHECK_EQ(vm_index(VM_KERNEL_VIRT_OFFSET + 0x100000, 3), 510);   /* kernel: PDPT slot 510 */
    CHECK_EQ(kernel_phys(VM_KERNEL_VIRT_OFFSET + 0x100000), 0x100000);
    CHECK_EQ(kernel_phys(0xFFFFFFFF80163000ull), 0x163000);
    /* -mcmodel=kernel needs the kernel in the top 2 GiB: anything loaded
     * below 1 GiB physical lands there, canonical. */
    CHECK(vm_canonical(VM_KERNEL_VIRT_OFFSET + 0x3FFFFFFFull));

    CHECK_EQ(vm_index(VM_PHYS_VIEW_BASE, 4), VM_PHYS_VIEW_INDEX);
    CHECK_EQ(VM_PHYS_VIEW_INDEX, 256);
    CHECK(vm_canonical(VM_PHYS_VIEW_BASE) && vm_aligned(VM_PHYS_VIEW_BASE));
    CHECK_EQ((uint64_t)(uintptr_t)phys_view(0), VM_PHYS_VIEW_BASE);
    CHECK_EQ((uint64_t)(uintptr_t)phys_view(0xB8000), 0xFFFF8000000B8000ull);
    /* All of 0-4 GiB stays inside slot 256. */
    CHECK_EQ(vm_index((uint64_t)(uintptr_t)phys_view(0xFFFFF000ull), 4), 256);
    /* Slot 256 is apart from every other area. */
    CHECK(VM_PHYS_VIEW_INDEX != vm_index(VM_TEST_PAGE, 4) &&
          VM_PHYS_VIEW_INDEX != vm_index(VM_HEAP_BASE, 4) &&
          VM_PHYS_VIEW_INDEX != VM_RECURSIVE_INDEX && VM_PHYS_VIEW_INDEX != VM_KERNEL_HIGH_INDEX);
}

static void test_page_zero_is_the_null_guard(void)
{
    CHECK_EQ(vm_floor(0x400ull), 0);      /* BIOS data area lives in page 0 */
    CHECK_EQ(vm_floor(0x40Eull), 0);      /* EBDA pointer too */
    CHECK(VM_LOW_MAP_START > 0x40Eull);
    CHECK_EQ(VM_NULL_GUARD_END, VM_PAGE_SIZE);
}

int main(void)
{
    printf("pte unit tests\n");
    RUN(test_index_split);
    RUN(test_canonical);
    RUN(test_entry_encode_decode);
    RUN(test_alignment_helpers);
    RUN(test_layout_consistency);
    RUN(test_higher_half_helpers);
    RUN(test_page_zero_is_the_null_guard);
    return unit_report("pte");
}
