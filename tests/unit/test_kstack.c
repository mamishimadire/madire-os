/* Unit tests for the kernel thread stack slots in kernel/proc/sched_core.c
 * (0.9): allocation, and the guard/stack layout in PML4 slot 507. */
#include <stdint.h>
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/sched_core.h"
#include "tests/unit/unit.h"

static void test_alloc_and_free(void)
{
    struct kstack_pool p;
    kstack_init(&p);
    CHECK_EQ(kstack_in_use(&p), 0);
    for (int i = 0; i < KSTACK_SLOTS; i++)
        CHECK_EQ(kstack_alloc(&p), i);      /* lowest free slot first */
    CHECK_EQ(kstack_in_use(&p), KSTACK_SLOTS);
    CHECK_EQ(kstack_alloc(&p), -1);         /* all in use */
    CHECK_EQ(kstack_alloc(&p), -1);

    CHECK(kstack_free(&p, 17));
    CHECK(kstack_free(&p, 63));
    CHECK_EQ(kstack_in_use(&p), KSTACK_SLOTS - 2);
    CHECK_EQ(kstack_alloc(&p), 17);
    CHECK_EQ(kstack_alloc(&p), 63);
    CHECK_EQ(kstack_alloc(&p), -1);

    CHECK(kstack_free(&p, 0));
    CHECK(!kstack_free(&p, 0));             /* double free refused */
    CHECK(!kstack_free(&p, -1));
    CHECK(!kstack_free(&p, KSTACK_SLOTS));
    CHECK(!kstack_free(&p, 1000));
    CHECK_EQ(kstack_in_use(&p), KSTACK_SLOTS - 1);

    /* Free everything, in a scattered order. */
    for (int i = 1; i < KSTACK_SLOTS; i += 2)
        CHECK(kstack_free(&p, i));
    for (int i = 2; i < KSTACK_SLOTS; i += 2)
        CHECK(kstack_free(&p, i));
    CHECK_EQ(kstack_in_use(&p), 0);
    CHECK_EQ(p.used, 0);
}

static void test_sizes(void)
{
    CHECK_EQ(KSTACK_SLOTS, 64);
    CHECK_EQ(KSTACK_SLOT_SIZE, 32 * 1024);
    CHECK_EQ(KSTACK_SIZE, 16 * 1024);       /* as the boot stack */
    CHECK_EQ(KSTACK_GUARD_SIZE, 16 * 1024);
    CHECK_EQ(KSTACK_SIZE % 4096, 0);
    CHECK_EQ(KSTACK_GUARD_SIZE % 4096, 0);
    CHECK(KSTACK_SLOTS >= SCHED_MAX_THREADS - 1);   /* every thread but 0 */
}

static void test_layout(void)
{
    CHECK_EQ(VM_KSTACK_INDEX, 507);
    CHECK_EQ(VM_KSTACK_BASE, 0xFFFFFD8000000000ull);
    CHECK_EQ(kstack_slot_base(0), VM_KSTACK_BASE);
    for (int i = 0; i < KSTACK_SLOTS; i++) {
        uint64_t base = kstack_slot_base(i), bottom = kstack_bottom(i), top = kstack_top(i);
        CHECK_EQ(bottom - base, KSTACK_GUARD_SIZE);     /* guard below the stack */
        CHECK_EQ(top - bottom, KSTACK_SIZE);
        CHECK(vm_aligned(base) && vm_aligned(bottom) && vm_aligned(top));
        CHECK(vm_canonical(base) && vm_canonical(top - 1));
        CHECK_EQ(vm_index(base, 4), VM_KSTACK_INDEX);
        CHECK_EQ(vm_index(top - 1, 4), VM_KSTACK_INDEX);
        if (i + 1 < KSTACK_SLOTS)
            CHECK_EQ(top, kstack_slot_base(i + 1));     /* no gap, no overlap */
    }
    /* The whole area is 2 MiB: one page table, inside slot 507, apart from
     * the test page and window (slot 508) and the heap (slot 509). */
    uint64_t end = kstack_top(KSTACK_SLOTS - 1);
    CHECK_EQ(end - VM_KSTACK_BASE, 2u * 1024 * 1024);
    CHECK_EQ(vm_index(VM_KSTACK_BASE, 1), 0);
    CHECK_EQ(vm_index(end - 1, 2), vm_index(VM_KSTACK_BASE, 2));  /* one PD entry */
    CHECK(end <= VM_TEST_PAGE);
    CHECK(VM_KSTACK_INDEX != vm_index(VM_TEST_PAGE, 4) &&
          VM_KSTACK_INDEX != vm_index(VM_HEAP_BASE, 4) &&
          VM_KSTACK_INDEX != VM_RECURSIVE_INDEX && VM_KSTACK_INDEX != VM_KERNEL_HIGH_INDEX &&
          VM_KSTACK_INDEX != VM_PHYS_VIEW_INDEX);
}

static void test_guard_and_stack_lookup(void)
{
    for (int i = 0; i < KSTACK_SLOTS; i++) {
        uint64_t base = kstack_slot_base(i), bottom = kstack_bottom(i), top = kstack_top(i);
        CHECK_EQ(kstack_guard_slot(base), i);
        CHECK_EQ(kstack_guard_slot(bottom - 1), i);
        CHECK_EQ(kstack_guard_slot(bottom - 8), i);     /* a push just past the bottom */
        CHECK_EQ(kstack_guard_slot(bottom), -1);
        CHECK_EQ(kstack_guard_slot(top - 1), -1);
        CHECK_EQ(kstack_stack_slot(bottom), i);
        CHECK_EQ(kstack_stack_slot(top - 1), i);
        CHECK_EQ(kstack_stack_slot(top - 16), i);
        CHECK_EQ(kstack_stack_slot(bottom - 1), -1);
    }
    uint64_t end = kstack_top(KSTACK_SLOTS - 1);
    CHECK_EQ(kstack_guard_slot(VM_KSTACK_BASE - 1), -1);
    CHECK_EQ(kstack_stack_slot(VM_KSTACK_BASE - 1), -1);
    CHECK_EQ(kstack_guard_slot(end), -1);
    CHECK_EQ(kstack_stack_slot(end), -1);
    CHECK_EQ(kstack_guard_slot(0), -1);
    CHECK_EQ(kstack_stack_slot(VM_TEST_PAGE), -1);
    CHECK_EQ(kstack_stack_slot(UINT64_MAX), -1);
}

int main(void)
{
    printf("kstack unit tests\n");
    RUN(test_alloc_and_free);
    RUN(test_sizes);
    RUN(test_layout);
    RUN(test_guard_and_stack_lookup);
    return unit_report("kstack");
}
