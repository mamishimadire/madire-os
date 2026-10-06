/* Unit tests for the 0.10 freeze rule of kernel/memory/vmm_core.c, on fake
 * physical frames (the 0.7 tests in test_vmm.c are unchanged). */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/pte.h"
#include "kernel/memory/vmm_core.h"
#include "tests/unit/unit.h"

#define FAKE_BASE   0x00200000ull
#define FAKE_FRAMES 32u

static uint64_t frames[FAKE_FRAMES][VM_ENTRIES] __attribute__((aligned(4096)));
static uint32_t next_frame, allocs, frees;
static struct vmm vm;
static uint64_t root;

static uint64_t *fake_ptr(uint64_t phys) { return frames[(phys - FAKE_BASE) / 4096]; }

static uint64_t fake_alloc(void *ctx)
{
    (void)ctx;
    if (next_frame >= FAKE_FRAMES)
        return 0;
    allocs++;
    uint64_t f = FAKE_BASE + 4096ull * next_frame++;
    memset(fake_ptr(f), 0xAB, 4096);
    return f;
}

static void fake_free(void *ctx, uint64_t frame) { (void)ctx; (void)frame; frees++; }

static uint64_t *fake_table(void *ctx, int level, uint64_t va, uint64_t phys, bool fresh)
{
    (void)ctx; (void)level; (void)va; (void)fresh;
    return fake_ptr(phys);
}

static void fake_flush(void *ctx, uint64_t va) { (void)ctx; (void)va; }

static const struct vmm_ops ops = { fake_alloc, fake_free, fake_table, fake_flush, 0 };

static void setup(void)
{
    next_frame = allocs = frees = 0;
    root = fake_alloc(0);
    memset(fake_ptr(root), 0, 4096);
    vmm_init(&vm, &ops, root);
}

#define HEAP_BASE   0xFFFFFE8000000000ull       /* PML4 slot 509 */
#define NEW_SLOT    0xFFFF808000000000ull       /* PML4 slot 257: empty in the kernel */
#define FRAME_A     0x00900000ull

static void test_not_frozen_by_default(void)
{
    setup();
    CHECK(!vm.upper_frozen);
    CHECK_EQ(vmm_map(&vm, NEW_SLOT, FRAME_A, PTE_WRITE), VMM_OK);
    CHECK(pte_present(fake_ptr(root)[257]));
}

static void test_frozen_refuses_new_upper_pml4_entry(void)
{
    setup();
    vmm_freeze_upper(&vm);
    CHECK(vm.upper_frozen);
    uint32_t before = allocs;
    CHECK_EQ(vmm_map(&vm, NEW_SLOT, FRAME_A, PTE_WRITE), VMM_ERR_FROZEN);
    CHECK_EQ(vmm_ensure_table(&vm, NEW_SLOT), VMM_ERR_FROZEN);
    CHECK_EQ(allocs, before);                       /* nothing taken */
    CHECK_EQ(frees, 0);
    CHECK_EQ(fake_ptr(root)[257], 0);               /* nothing changed */
    CHECK_EQ(vm.page_tables, 0);
    CHECK_EQ(vm.mapped_pages, 0);
}

static void test_frozen_still_grows_below_existing_entries(void)
{
    setup();
    CHECK_EQ(vmm_map(&vm, HEAP_BASE, FRAME_A, PTE_WRITE), VMM_OK);     /* slot 509 made before */
    vmm_freeze_upper(&vm);
    /* A new PDPT entry, PD entry and PT below slot 509: allowed, shared. */
    CHECK_EQ(vmm_map(&vm, HEAP_BASE + 0x40000000ull, FRAME_A + 0x1000, PTE_WRITE), VMM_OK);
    CHECK_EQ(vmm_map(&vm, HEAP_BASE + 0x200000ull, FRAME_A + 0x2000, PTE_WRITE), VMM_OK);
    CHECK_EQ(vmm_map(&vm, HEAP_BASE + 0x1000ull, FRAME_A + 0x3000, PTE_WRITE), VMM_OK);
    CHECK_EQ(vm.mapped_pages, 4);
    uint64_t phys = 0;
    CHECK(vmm_translate(&vm, HEAP_BASE + 0x40000000ull, &phys, 0));
    CHECK_EQ(phys, FRAME_A + 0x1000);
}

static void test_frozen_leaves_lower_half_alone(void)
{
    /* The freeze is about the kernel half; vmm_core never maps user pages,
     * and lower-half kernel mappings (the boot tables' era) are unaffected. */
    setup();
    vmm_freeze_upper(&vm);
    CHECK_EQ(vmm_map(&vm, 0x0000000000400000ull, FRAME_A, 0), VMM_OK);
    CHECK(pte_present(fake_ptr(root)[0]));
}

static void test_frozen_result_string(void)
{
    CHECK(strcmp(vmm_result_str(VMM_ERR_FROZEN), "new kernel-half PML4 entry after the freeze") == 0);
    CHECK(strcmp(vmm_result_str(VMM_ERR_NONCANONICAL), "non-canonical address") == 0);
}

int main(void)
{
    RUN(test_not_frozen_by_default);
    RUN(test_frozen_refuses_new_upper_pml4_entry);
    RUN(test_frozen_still_grows_below_existing_entries);
    RUN(test_frozen_leaves_lower_half_alone);
    RUN(test_frozen_result_string);
    return unit_report("vmm010 (freeze)");
}
