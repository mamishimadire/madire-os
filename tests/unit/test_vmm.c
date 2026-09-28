/* Unit tests for kernel/memory/vmm_core.c, on fake physical frames. */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/memory/vmm_core.h"
#include "tests/unit/unit.h"

/* Fake physical memory: frame i lives at physical FAKE_BASE + i * 4 KiB. */
#define FAKE_BASE   0x00200000u
#define FAKE_FRAMES 16u

static uint32_t frames[FAKE_FRAMES][VM_ENTRIES] __attribute__((aligned(4096)));
static uint32_t next_frame;         /* frames handed out so far */
static uint32_t frame_limit;        /* stop handing out frames after this many */
static uint32_t dir_phys;
static uint32_t flush_count;
static uint32_t last_flush;
static uint32_t fresh_count;

static uint32_t *fake_ptr(uint32_t phys)
{
    return frames[(phys - FAKE_BASE) / 4096];
}

static uint32_t fake_alloc(void *ctx)
{
    (void)ctx;
    if (next_frame >= frame_limit)
        return 0;
    memset(frames[next_frame], 0xAB, 4096);         /* garbage: table must be cleared */
    return FAKE_BASE + 4096 * next_frame++;
}

static uint32_t *fake_directory(void *ctx)
{
    (void)ctx;
    return fake_ptr(dir_phys);
}

static uint32_t *fake_table(void *ctx, uint32_t pdi, uint32_t phys, bool fresh)
{
    (void)ctx;
    (void)pdi;
    if (fresh)
        fresh_count++;
    return fake_ptr(phys);
}

static void fake_flush(void *ctx, uint32_t va)
{
    (void)ctx;
    flush_count++;
    last_flush = va;
}

static const struct vmm_ops ops = { fake_alloc, fake_directory, fake_table, fake_flush, 0 };
static struct vmm vm;

static void setup(uint32_t limit)
{
    next_frame = 0;
    frame_limit = limit;
    flush_count = fresh_count = last_flush = 0;
    dir_phys = fake_alloc(0);
    memset(fake_ptr(dir_phys), 0, 4096);
    vmm_init(&vm, &ops);
}

static void test_map_translate_unmap(void)
{
    uint32_t phys, flags;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00400000u, 0x12345000u, PTE_WRITE), VMM_OK);
    CHECK(vmm_translate(&vm, 0x00400ABCu, &phys, &flags));
    CHECK_EQ(phys, 0x12345ABCu);                            /* frame + offset */
    CHECK_EQ(flags & (PTE_PRESENT | PTE_WRITE | PTE_USER), PTE_PRESENT | PTE_WRITE);
    CHECK_EQ(vm.mapped_pages, 1);
    CHECK_EQ(vm.page_tables, 1);
    CHECK_EQ(vmm_unmap(&vm, 0x00400000u, &phys), VMM_OK);
    CHECK_EQ(phys, 0x12345000u);                            /* frame returned to caller */
    CHECK(!vmm_translate(&vm, 0x00400000u, &phys, &flags));
    CHECK_EQ(vm.mapped_pages, 0);
}

static void test_tables_created_on_demand_and_cleared(void)
{
    uint32_t phys;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00001000u, 0x1000u, PTE_WRITE), VMM_OK);
    CHECK_EQ(vm.page_tables, 1);
    CHECK_EQ(fresh_count, 1);
    CHECK_EQ(vmm_map(&vm, 0x00002000u, 0x2000u, PTE_WRITE), VMM_OK);   /* same table */
    CHECK_EQ(vm.page_tables, 1);
    CHECK_EQ(vmm_map(&vm, 0x00400000u, 0x3000u, PTE_WRITE), VMM_OK);   /* next 4 MiB */
    CHECK_EQ(vm.page_tables, 2);
    /* The new table was filled with garbage by fake_alloc: every other
     * entry must read as not present. */
    for (uint32_t va = 0x00401000u; va < 0x00800000u; va += 0x1000u)
        if (vmm_translate(&vm, va, &phys, 0))
            CHECK(0 && "stale entry in a new table");
    CHECK(!vmm_translate(&vm, 0x00003000u, &phys, 0));
}

static void test_ensure_table(void)
{
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_ensure_table(&vm, VM_WINDOW), VMM_OK);
    CHECK_EQ(vm.page_tables, 1);
    CHECK_EQ(vmm_ensure_table(&vm, VM_TEST_PAGE), VMM_OK);          /* same table */
    CHECK_EQ(vm.page_tables, 1);
    CHECK_EQ(vm.mapped_pages, 0);
    CHECK_EQ(vmm_ensure_table(&vm, VM_RECURSIVE_BASE), VMM_ERR_RESERVED_VA);
}

static void test_out_of_frames(void)
{
    uint32_t phys;
    setup(2);                                       /* directory + one table */
    CHECK_EQ(vmm_map(&vm, 0x00001000u, 0x1000u, PTE_WRITE), VMM_OK);
    CHECK_EQ(vmm_map(&vm, 0x00400000u, 0x2000u, PTE_WRITE), VMM_ERR_NO_FRAME);
    CHECK_EQ(vm.page_tables, 1);
    CHECK_EQ(vm.mapped_pages, 1);
    CHECK(!vmm_translate(&vm, 0x00400000u, &phys, 0));   /* nothing half-done */
    CHECK_EQ(fake_directory(0)[1], 0);
    CHECK_EQ(vmm_map(&vm, 0x00002000u, 0x2000u, PTE_WRITE), VMM_OK);   /* existing table still fine */
}

static void test_unaligned_and_flags(void)
{
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00401001u, 0x1000u, PTE_WRITE), VMM_ERR_UNALIGNED);
    CHECK_EQ(vmm_map(&vm, 0x00401000u, 0x1001u, PTE_WRITE), VMM_ERR_UNALIGNED);
    CHECK_EQ(vmm_map(&vm, 0x00401000u, 0x1000u, PTE_USER), VMM_ERR_FLAGS);   /* no user pages */
    CHECK_EQ(vmm_map(&vm, 0x00401000u, 0x1000u, PTE_PRESENT), VMM_ERR_FLAGS);
    CHECK_EQ(vmm_unmap(&vm, 0x00401001u, 0), VMM_ERR_UNALIGNED);
    CHECK_EQ(vm.page_tables, 0);                    /* refusals create nothing */
}

static void test_reserved_addresses(void)
{
    uint32_t phys;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00000000u, 0x5000u, PTE_WRITE), VMM_ERR_RESERVED_VA);   /* NULL page */
    CHECK_EQ(vmm_map(&vm, VM_RECURSIVE_BASE, 0x5000u, PTE_WRITE), VMM_ERR_RESERVED_VA);
    CHECK_EQ(vmm_map(&vm, VM_PAGE_DIRECTORY, 0x5000u, PTE_WRITE), VMM_ERR_RESERVED_VA);
    CHECK_EQ(vmm_unmap(&vm, 0x00000000u, 0), VMM_ERR_RESERVED_VA);
    CHECK(!vmm_translate(&vm, 0x00000400u, &phys, 0));   /* BIOS data area: never */
    CHECK_EQ(vm.page_tables, 0);
    CHECK_EQ(vmm_map(&vm, 0x00001000u, 0x1000u, PTE_WRITE), VMM_OK);                /* page 1 is fine */
}

static void test_existing_mapping_not_replaced(void)
{
    uint32_t phys;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00800000u, 0x7000u, PTE_WRITE), VMM_OK);
    CHECK_EQ(vmm_map(&vm, 0x00800000u, 0x9000u, PTE_WRITE), VMM_ERR_EXISTS);
    CHECK(vmm_translate(&vm, 0x00800000u, &phys, 0));
    CHECK_EQ(phys, 0x7000u);
    CHECK_EQ(vm.mapped_pages, 1);
}

static void test_unmap_not_mapped(void)
{
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_unmap(&vm, 0x00800000u, 0), VMM_ERR_NOT_MAPPED);   /* no table */
    vmm_map(&vm, 0x00800000u, 0x7000u, PTE_WRITE);
    CHECK_EQ(vmm_unmap(&vm, 0x00801000u, 0), VMM_ERR_NOT_MAPPED);   /* table, no entry */
    CHECK_EQ(vmm_unmap(&vm, 0x00800000u, 0), VMM_OK);
    CHECK_EQ(vmm_unmap(&vm, 0x00800000u, 0), VMM_ERR_NOT_MAPPED);   /* twice */
    CHECK_EQ(flush_count, 1);
}

static void test_flush_exactly_once_per_unmap(void)
{
    setup(FAKE_FRAMES);
    for (uint32_t i = 0; i < 8; i++)
        vmm_map(&vm, 0x00C00000u + i * 0x1000u, 0x10000u + i * 0x1000u, PTE_WRITE);
    CHECK_EQ(flush_count, 0);                       /* mapping new pages never flushes */
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t va = 0x00C00000u + i * 0x1000u;
        CHECK_EQ(vmm_unmap(&vm, va, 0), VMM_OK);
        CHECK_EQ(flush_count, i + 1);
        CHECK_EQ(last_flush, va);
    }
}

static void test_remap_after_unmap(void)
{
    uint32_t phys;
    setup(FAKE_FRAMES);
    vmm_map(&vm, VM_TEST_PAGE, 0xA000u, PTE_WRITE);
    vmm_unmap(&vm, VM_TEST_PAGE, 0);
    CHECK_EQ(vmm_map(&vm, VM_TEST_PAGE, 0xB000u, PTE_WRITE), VMM_OK);
    CHECK(vmm_translate(&vm, VM_TEST_PAGE, &phys, 0));
    CHECK_EQ(phys, 0xB000u);
    CHECK_EQ(last_flush, VM_TEST_PAGE);
}

static void test_read_only_flag_kept(void)
{
    uint32_t flags;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00100000u, 0x00100000u, 0), VMM_OK);    /* kernel code: read-only */
    CHECK(vmm_translate(&vm, 0x00100000u, 0, &flags));
    CHECK(flags & PTE_PRESENT);
    CHECK(!(flags & PTE_WRITE));
    CHECK(!(flags & PTE_USER));
}

static void test_many_mappings_across_a_4mib_boundary(void)
{
    uint32_t phys;
    setup(FAKE_FRAMES);
    uint32_t start = 0x00300000u;                   /* 3 MiB .. 7 MiB: crosses 4 MiB */
    for (uint32_t i = 0; i < 1024; i++)
        CHECK_EQ(vmm_map(&vm, start + i * 0x1000u, start + i * 0x1000u, PTE_WRITE), VMM_OK);
    CHECK_EQ(vm.page_tables, 2);
    CHECK_EQ(vm.mapped_pages, 1024);
    for (uint32_t i = 0; i < 1024; i += 97) {
        CHECK(vmm_translate(&vm, start + i * 0x1000u + 5, &phys, 0));
        CHECK_EQ(phys, start + i * 0x1000u + 5);    /* identity */
    }
    CHECK(!vmm_translate(&vm, start - 0x1000u, &phys, 0));
    CHECK(!vmm_translate(&vm, start + 1024 * 0x1000u, &phys, 0));
}

static void test_directory_entries_are_kernel_writable(void)
{
    setup(FAKE_FRAMES);
    vmm_map(&vm, 0xD0000000u, 0x5000u, 0);          /* even for a read-only page */
    uint32_t pde = fake_directory(0)[pd_index(0xD0000000u)];
    CHECK(pte_present(pde));
    CHECK(pte_writable(pde));                       /* the page entry decides */
    CHECK(!(pde & PTE_USER));
}

/* paging.c sets PD[1023] to the directory itself; the core must never touch
 * that entry and must refuse addresses resolved through it. */
static void test_recursive_entry_left_alone(void)
{
    uint32_t phys;
    setup(FAKE_FRAMES);
    uint32_t recursive = pte_make(dir_phys, PTE_PRESENT | PTE_WRITE);
    fake_directory(0)[VM_RECURSIVE_INDEX] = recursive;
    for (uint32_t i = 0; i < 8; i++)                /* one new table each */
        CHECK_EQ(vmm_map(&vm, 0x00400000u + i * 0x00400000u, 0x1000u * (i + 1), PTE_WRITE), VMM_OK);
    CHECK_EQ(vmm_ensure_table(&vm, VM_WINDOW), VMM_OK);   /* table in slot 1022, next to it */
    CHECK_EQ(vm.page_tables, 9);
    CHECK_EQ(fake_directory(0)[VM_RECURSIVE_INDEX], recursive);
    CHECK_EQ(pte_frame(fake_directory(0)[VM_RECURSIVE_INDEX]), dir_phys);   /* points to the directory */
    CHECK(!vmm_translate(&vm, VM_PAGE_DIRECTORY, &phys, 0));
    CHECK(!vmm_translate(&vm, VM_RECURSIVE_BASE + 0x1000u, &phys, 0));
    CHECK_EQ(vmm_unmap(&vm, VM_PAGE_DIRECTORY, 0), VMM_ERR_RESERVED_VA);
    CHECK_EQ(fake_directory(0)[VM_RECURSIVE_INDEX], recursive);
}

static void test_result_strings(void)
{
    for (int r = VMM_OK; r <= VMM_ERR_FLAGS; r++)
        CHECK(strcmp(vmm_result_str((enum vmm_result)r), "unknown vmm error") != 0);
    CHECK(strcmp(vmm_result_str(VMM_ERR_EXISTS), "address already mapped") == 0);
}

int main(void)
{
    printf("vmm unit tests\n");
    RUN(test_map_translate_unmap);
    RUN(test_tables_created_on_demand_and_cleared);
    RUN(test_ensure_table);
    RUN(test_out_of_frames);
    RUN(test_unaligned_and_flags);
    RUN(test_reserved_addresses);
    RUN(test_existing_mapping_not_replaced);
    RUN(test_unmap_not_mapped);
    RUN(test_flush_exactly_once_per_unmap);
    RUN(test_remap_after_unmap);
    RUN(test_read_only_flag_kept);
    RUN(test_many_mappings_across_a_4mib_boundary);
    RUN(test_directory_entries_are_kernel_writable);
    RUN(test_recursive_entry_left_alone);
    RUN(test_result_strings);
    return unit_report("vmm");
}
