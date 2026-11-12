/* Unit tests for kernel/memory/vmm_core.c (x86-64 4-level paging), on fake
 * physical frames. Entry and address helpers: test_pte.c. */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/pte.h"
#include "kernel/memory/vmm_core.h"
#include "tests/unit/unit.h"

/* Fake physical memory: frame i lives at physical FAKE_BASE + i * 4 KiB. */
#define FAKE_BASE   0x00200000ull
#define FAKE_FRAMES 64u

static uint64_t frames[FAKE_FRAMES][VM_ENTRIES] __attribute__((aligned(4096)));
static uint32_t next_frame, frame_limit, freed_frames, flush_count, fresh_count;
static uint64_t root, last_flush;
static uint64_t freed[8];
static struct vmm vm;

static uint64_t *fake_ptr(uint64_t phys)
{
    return frames[(phys - FAKE_BASE) / 4096];
}


/* Frames given back are reused first, like the real allocator would. */
static uint64_t reuse[8];
static uint32_t reuse_count;

static uint64_t fake_alloc(void *ctx)
{
    (void)ctx;
    uint64_t f;
    if (reuse_count)
        f = reuse[--reuse_count];
    else if (next_frame < frame_limit)
        f = FAKE_BASE + 4096ull * next_frame++;
    else
        return 0;
    memset(fake_ptr(f), 0xAB, 4096);                /* garbage: tables must be cleared */
    return f;
}

static void fake_free(void *ctx, uint64_t frame)
{
    (void)ctx;
    if (freed_frames < 8)
        freed[freed_frames] = frame;
    freed_frames++;
    if (reuse_count < 8)
        reuse[reuse_count++] = frame;
}

static uint64_t *fake_table(void *ctx, int level, uint64_t va, uint64_t phys, bool fresh)
{
    (void)ctx;
    (void)level;
    (void)va;
    if (fresh)
        fresh_count++;
    return fake_ptr(phys);
}

static void fake_flush(void *ctx, uint64_t va)
{
    (void)ctx;
    flush_count++;
    last_flush = va;
}

static const struct vmm_ops ops = { fake_alloc, fake_free, fake_table, fake_flush, 0 };

static void setup(uint32_t limit)
{
    next_frame = 0;
    frame_limit = limit;
    freed_frames = flush_count = fresh_count = reuse_count = 0;
    last_flush = 0;
    root = fake_alloc(0);
    memset(fake_ptr(root), 0, 4096);
    vmm_init(&vm, &ops, root);
}

#define KERNEL_TEXT   0x0000000000100000ull
#define HEAP_BASE     0xFFFFFE8000000000ull
#define WINDOW        0xFFFFFE0000001000ull
#define HIGHER_HALF   0xFFFFFFFF80000000ull

static void test_map_translate_unmap(void)
{
    uint64_t phys = 0, flags = 0;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00400000ull, 0x123456000ull, PTE_WRITE | PTE_NX), VMM_OK);
    CHECK(vmm_translate(&vm, 0x00400ABCull, &phys, &flags));
    CHECK_EQ(phys, 0x123456ABCull);                     /* above 4 GiB: 52-bit frames */
    CHECK_EQ(flags & (PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_NX),
             PTE_PRESENT | PTE_WRITE | PTE_NX);
    CHECK_EQ(vm.mapped_pages, 1);
    CHECK_EQ(vm.page_tables, 3);                        /* PDPT, PD, PT */
    CHECK_EQ(vmm_unmap(&vm, 0x00400000ull, &phys), VMM_OK);
    CHECK_EQ(phys, 0x123456000ull);
    CHECK(!vmm_translate(&vm, 0x00400000ull, &phys, &flags));
    CHECK_EQ(vm.mapped_pages, 0);
}

static void test_tables_per_level(void)
{
    uint64_t phys = 0;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00001000ull, 0x1000, PTE_WRITE), VMM_OK);
    CHECK_EQ(vm.page_tables, 3);
    CHECK_EQ(fresh_count, 3);
    CHECK_EQ(vmm_map(&vm, 0x00002000ull, 0x2000, PTE_WRITE), VMM_OK);   /* same PT */
    CHECK_EQ(vm.page_tables, 3);
    CHECK_EQ(vmm_map(&vm, 0x00200000ull, 0x3000, PTE_WRITE), VMM_OK);   /* next 2 MiB: new PT */
    CHECK_EQ(vm.page_tables, 4);
    CHECK_EQ(vmm_map(&vm, 0x40000000ull, 0x4000, PTE_WRITE), VMM_OK);   /* next 1 GiB: PD + PT */
    CHECK_EQ(vm.page_tables, 6);
    CHECK_EQ(vmm_map(&vm, 0x8000000000ull, 0x5000, PTE_WRITE), VMM_OK); /* next 512 GiB: 3 */
    CHECK_EQ(vm.page_tables, 9);
    /* New tables were filled with garbage by fake_alloc: all must read as empty. */
    for (uint64_t va = 0x00201000ull; va < 0x00400000ull; va += 0x1000)
        if (vmm_translate(&vm, va, &phys, 0))
            CHECK(0 && "stale entry in a new table");
    CHECK(!vmm_translate(&vm, 0x00003000ull, &phys, 0));
    CHECK(!vmm_translate(&vm, 0x80000000ull, &phys, 0));
}

static void test_ensure_table(void)
{
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_ensure_table(&vm, WINDOW), VMM_OK);
    CHECK_EQ(vm.page_tables, 3);
    CHECK_EQ(vmm_ensure_table(&vm, WINDOW + 0x1000), VMM_OK);   /* same PT */
    CHECK_EQ(vm.page_tables, 3);
    CHECK_EQ(vm.mapped_pages, 0);
    CHECK_EQ(vmm_ensure_table(&vm, 0xFFFFFF0000000000ull), VMM_ERR_RESERVED_VA);
}

static void test_out_of_frames_at_each_level(void)
{
    uint64_t phys = 0;
    /* root + 1: the PDPT fits, the PD does not. Nothing may be linked, and
     * the PDPT frame must be given back. */
    setup(2);
    CHECK_EQ(vmm_map(&vm, 0x00400000ull, 0x1000, PTE_WRITE), VMM_ERR_NO_FRAME);
    CHECK_EQ(freed_frames, 1);
    CHECK_EQ(freed[0], FAKE_BASE + 4096);
    CHECK_EQ(fake_ptr(root)[0], 0);
    CHECK_EQ(vm.page_tables, 0);
    CHECK(!vmm_translate(&vm, 0x00400000ull, &phys, 0));

    /* root + 3: one full path, then a new PT (1 frame) fits but a path
     * needing PD + PT does not. */
    setup(5);
    CHECK_EQ(vmm_map(&vm, 0x00001000ull, 0x1000, PTE_WRITE), VMM_OK);     /* 3 tables */
    CHECK_EQ(vmm_map(&vm, 0x40000000ull, 0x2000, PTE_WRITE), VMM_ERR_NO_FRAME);  /* needs 2 */
    CHECK_EQ(freed_frames, 1);
    CHECK_EQ(vm.page_tables, 3);
    CHECK_EQ(vmm_map(&vm, 0x00200000ull, 0x3000, PTE_WRITE), VMM_OK);     /* needs 1 */
    CHECK_EQ(vm.page_tables, 4);
    CHECK_EQ(vmm_map(&vm, 0x00002000ull, 0x4000, PTE_WRITE), VMM_OK);     /* needs 0 */
}

static void test_unaligned_flags_and_frames(void)
{
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00401001ull, 0x1000, PTE_WRITE), VMM_ERR_UNALIGNED);
    CHECK_EQ(vmm_map(&vm, 0x00401000ull, 0x1001, PTE_WRITE), VMM_ERR_UNALIGNED);
    CHECK_EQ(vmm_map(&vm, 0x00401000ull, 1ull << 52, PTE_WRITE), VMM_ERR_UNALIGNED);
    CHECK_EQ(vmm_map(&vm, 0x00401000ull, 0x1000, PTE_USER), VMM_ERR_FLAGS);
    CHECK_EQ(vmm_map(&vm, 0x00401000ull, 0x1000, PTE_PRESENT), VMM_ERR_FLAGS);
    CHECK_EQ(vmm_map(&vm, 0x00401000ull, 0x1000, PTE_LARGE), VMM_ERR_FLAGS);
    CHECK_EQ(vmm_unmap(&vm, 0x00401001ull, 0), VMM_ERR_UNALIGNED);
    CHECK_EQ(vm.page_tables, 0);                        /* refusals create nothing */
}

/* 1.4 stage 4: PCD and PWT may be asked for (a device's registers,
 * uncached), with or without the others; they reach the leaf only. */
static void test_uncached_flags(void)
{
    uint64_t phys = 0, flags = 0;
    setup(FAKE_FRAMES);
    CHECK_EQ(PTE_PWT, 0x008);
    CHECK_EQ(PTE_PCD, 0x010);
    CHECK_EQ(PTE_UNCACHED, 0x018);
    CHECK_EQ(vmm_map(&vm, 0x00401000ull, 0xFEBB1000ull, PTE_WRITE | PTE_UNCACHED | PTE_NX), VMM_OK);
    CHECK(vmm_translate(&vm, 0x00401000ull, &phys, &flags));
    CHECK_EQ(phys, 0xFEBB1000ull);
    CHECK_EQ(flags & (PTE_PRESENT | PTE_WRITE | PTE_PCD | PTE_PWT | PTE_USER | PTE_NX),
             PTE_PRESENT | PTE_WRITE | PTE_PCD | PTE_PWT | PTE_NX);
    CHECK_EQ(vmm_map(&vm, 0x00402000ull, 0x2000, PTE_PCD), VMM_OK);
    CHECK(vmm_translate(&vm, 0x00402000ull, 0, &flags));
    CHECK_EQ(flags & (PTE_WRITE | PTE_PCD | PTE_PWT), PTE_PCD);
    CHECK_EQ(vmm_map(&vm, 0x00403000ull, 0x3000, PTE_UNCACHED | PTE_USER), VMM_ERR_FLAGS);
    CHECK_EQ(vmm_map(&vm, 0x00403000ull, 0x3000, PTE_UNCACHED | PTE_LARGE), VMM_ERR_FLAGS);
    CHECK_EQ(vm.mapped_pages, 2);
}

static void test_reserved_and_noncanonical(void)
{
    uint64_t phys = 0;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0, 0x5000, PTE_WRITE), VMM_ERR_RESERVED_VA);           /* NULL page */
    CHECK_EQ(vmm_map(&vm, 0xFFFFFF0000000000ull, 0x5000, 0), VMM_ERR_RESERVED_VA);  /* recursive */
    CHECK_EQ(vmm_map(&vm, 0xFFFFFF7FBFDFE000ull, 0x5000, 0), VMM_ERR_RESERVED_VA);  /* the PML4 */
    CHECK_EQ(vmm_unmap(&vm, 0, 0), VMM_ERR_RESERVED_VA);
    CHECK(!vmm_translate(&vm, 0x400, &phys, 0));                                    /* BIOS data */
    CHECK_EQ(vmm_map(&vm, 0x0000800000000000ull, 0x5000, 0), VMM_ERR_NONCANONICAL);
    CHECK_EQ(vmm_map(&vm, 0xFFFF7FFFFFFFF000ull, 0x5000, 0), VMM_ERR_NONCANONICAL);
    CHECK_EQ(vmm_unmap(&vm, 0x0000800000000000ull, 0), VMM_ERR_NONCANONICAL);
    CHECK_EQ(vmm_ensure_table(&vm, 0x1234000000000000ull), VMM_ERR_NONCANONICAL);
    CHECK(!vmm_translate(&vm, 0x0000800000000000ull, &phys, 0));
    CHECK_EQ(vm.page_tables, 0);
    CHECK_EQ(vmm_map(&vm, 0x1000, 0x1000, PTE_WRITE), VMM_OK);                   /* page 1 fine */
}

static void test_existing_mapping_not_replaced(void)
{
    uint64_t phys = 0;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, 0x00800000ull, 0x7000, PTE_WRITE), VMM_OK);
    CHECK_EQ(vmm_map(&vm, 0x00800000ull, 0x9000, PTE_WRITE), VMM_ERR_EXISTS);
    CHECK(vmm_translate(&vm, 0x00800000ull, &phys, 0));
    CHECK_EQ(phys, 0x7000);
    CHECK_EQ(vm.mapped_pages, 1);
}

static void test_unmap_not_mapped(void)
{
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_unmap(&vm, 0x00800000ull, 0), VMM_ERR_NOT_MAPPED);   /* no tables */
    vmm_map(&vm, 0x00800000ull, 0x7000, PTE_WRITE);
    CHECK_EQ(vmm_unmap(&vm, 0x00801000ull, 0), VMM_ERR_NOT_MAPPED);   /* table, no entry */
    CHECK_EQ(vmm_unmap(&vm, 0x00800000ull, 0), VMM_OK);
    CHECK_EQ(vmm_unmap(&vm, 0x00800000ull, 0), VMM_ERR_NOT_MAPPED);   /* twice */
    CHECK_EQ(flush_count, 1);
}

static void test_flush_exactly_once_per_unmap(void)
{
    setup(FAKE_FRAMES);
    for (uint64_t i = 0; i < 8; i++)
        vmm_map(&vm, HEAP_BASE + i * 0x1000, 0x10000 + i * 0x1000, PTE_WRITE | PTE_NX);
    CHECK_EQ(flush_count, 0);                           /* new mappings never flush */
    for (uint64_t i = 0; i < 8; i++) {
        uint64_t va = HEAP_BASE + i * 0x1000;
        CHECK_EQ(vmm_unmap(&vm, va, 0), VMM_OK);
        CHECK_EQ(flush_count, i + 1);
        CHECK_EQ(last_flush, va);
    }
}

static void test_remap_after_unmap(void)
{
    uint64_t phys = 0;
    setup(FAKE_FRAMES);
    vmm_map(&vm, WINDOW, 0xA000, PTE_WRITE | PTE_NX);
    vmm_unmap(&vm, WINDOW, 0);
    CHECK_EQ(vmm_map(&vm, WINDOW, 0xB000, PTE_WRITE | PTE_NX), VMM_OK);
    CHECK(vmm_translate(&vm, WINDOW, &phys, 0));
    CHECK_EQ(phys, 0xB000);
    CHECK_EQ(last_flush, WINDOW);
}

static void test_leaf_flags_and_intermediate_entries(void)
{
    uint64_t flags = 0;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, KERNEL_TEXT, KERNEL_TEXT, 0), VMM_OK);          /* code: RO, exec */
    CHECK(vmm_translate(&vm, KERNEL_TEXT, 0, &flags));
    CHECK(flags & PTE_PRESENT);
    CHECK(!(flags & PTE_WRITE));
    CHECK(!(flags & PTE_NX));
    CHECK(!(flags & PTE_USER));
    CHECK_EQ(vmm_map(&vm, KERNEL_TEXT + 0x1000, KERNEL_TEXT + 0x1000, PTE_NX), VMM_OK);  /* rodata */
    CHECK(vmm_translate(&vm, KERNEL_TEXT + 0x1000, 0, &flags));
    CHECK(!(flags & PTE_WRITE) && (flags & PTE_NX));
    /* Intermediate entries: present, writable, never NX or user. */
    uint64_t e4 = fake_ptr(root)[vm_index(KERNEL_TEXT, 4)];
    uint64_t e3 = fake_ptr(pte_frame(e4))[vm_index(KERNEL_TEXT, 3)];
    uint64_t e2 = fake_ptr(pte_frame(e3))[vm_index(KERNEL_TEXT, 2)];
    CHECK_EQ(e4 & ~PTE_ADDR_MASK, PTE_PRESENT | PTE_WRITE);
    CHECK_EQ(e3 & ~PTE_ADDR_MASK, PTE_PRESENT | PTE_WRITE);
    CHECK_EQ(e2 & ~PTE_ADDR_MASK, PTE_PRESENT | PTE_WRITE);
}

static void test_many_mappings_across_boundaries(void)
{
    uint64_t phys = 0;
    setup(FAKE_FRAMES);
    uint64_t start = 0x3FF00000ull;                     /* 1 GiB - 1 MiB: crosses 2 MiB and 1 GiB */
    for (uint64_t i = 0; i < 1024; i++)
        CHECK_EQ(vmm_map(&vm, start + i * 0x1000, start + i * 0x1000, PTE_WRITE), VMM_OK);
    CHECK_EQ(vm.mapped_pages, 1024);
    CHECK_EQ(vm.page_tables, 1 + 2 + 3);                /* PDPT; 2 PDs; PTs at 1G-1M, 1G, 1G+2M */
    for (uint64_t i = 0; i < 1024; i += 97) {
        CHECK(vmm_translate(&vm, start + i * 0x1000 + 5, &phys, 0));
        CHECK_EQ(phys, start + i * 0x1000 + 5);
    }
    CHECK(!vmm_translate(&vm, start - 0x1000, &phys, 0));
    CHECK(!vmm_translate(&vm, start + 1024 * 0x1000, &phys, 0));
}

static void test_upper_half_addresses(void)
{
    uint64_t phys = 0;
    setup(FAKE_FRAMES);
    CHECK_EQ(vmm_map(&vm, HEAP_BASE, 0x20000, PTE_WRITE | PTE_NX), VMM_OK);
    CHECK_EQ(vmm_map(&vm, HIGHER_HALF + 0x100000, 0x100000, 0), VMM_OK);
    CHECK(vmm_translate(&vm, HEAP_BASE + 0x10, &phys, 0) && phys == 0x20010);
    CHECK(vmm_translate(&vm, HIGHER_HALF + 0x100123, &phys, 0) && phys == 0x100123);
    CHECK_EQ(fake_ptr(root)[509] & PTE_PRESENT, PTE_PRESENT);
    CHECK_EQ(fake_ptr(root)[511] & PTE_PRESENT, PTE_PRESENT);
}

static void test_large_pages(void)
{
    uint64_t phys = 0, flags = 0;
    setup(FAKE_FRAMES);
    vmm_ensure_table(&vm, 0x1000);                     /* PML4 -> PDPT -> PD -> PT */
    uint64_t e4 = fake_ptr(root)[0];
    uint64_t e3 = fake_ptr(pte_frame(e4))[0];
    uint64_t *pd = fake_ptr(pte_frame(e3));
    pd[1] = pte_make(0x40000000ull, PTE_PRESENT | PTE_WRITE | PTE_LARGE);   /* 2 MiB at 2 MiB */
    CHECK(vmm_translate(&vm, 0x00212345ull, &phys, &flags));
    CHECK_EQ(phys, 0x40012345ull);
    CHECK(flags & PTE_LARGE);
    CHECK_EQ(vmm_map(&vm, 0x00300000ull, 0x5000, 0), VMM_ERR_EXISTS);
    CHECK_EQ(vmm_unmap(&vm, 0x00200000ull, 0), VMM_ERR_NOT_MAPPED);
    CHECK_EQ(vmm_ensure_table(&vm, 0x00200000ull), VMM_ERR_EXISTS);
}

static void test_recursive_entry_left_alone(void)
{
    setup(FAKE_FRAMES);
    uint64_t recursive = pte_make(root, PTE_PRESENT | PTE_WRITE);
    fake_ptr(root)[VM_RECURSIVE_INDEX] = recursive;
    for (uint64_t i = 0; i < 6; i++)
        CHECK_EQ(vmm_map(&vm, 0x1000 + i * 0x8000000000ull, 0x1000 * (i + 1), PTE_WRITE), VMM_OK);
    CHECK_EQ(vmm_ensure_table(&vm, WINDOW), VMM_OK);
    CHECK_EQ(fake_ptr(root)[VM_RECURSIVE_INDEX], recursive);
    CHECK_EQ(vmm_unmap(&vm, 0xFFFFFF7FBFDFE000ull, 0), VMM_ERR_RESERVED_VA);
}

/* The CPU's own walk over fake memory, following the recursive entry like
 * any other: where does virtual address va really lead? */
static bool cpu_walk(uint64_t va, uint64_t *phys)
{
    uint64_t table = root;
    for (int level = 4; level >= 1; level--) {
        uint64_t e = fake_ptr(table)[vm_index(va, level)];
        if (!pte_present(e))
            return false;
        table = pte_frame(e);
    }
    *phys = table | (va & 0xFFF);
    return true;
}

static void test_recursive_table_addresses(void)
{
    uint64_t phys = 0;
    setup(FAKE_FRAMES);
    fake_ptr(root)[VM_RECURSIVE_INDEX] = pte_make(root, PTE_PRESENT | PTE_WRITE);
    static const uint64_t vas[] = { 0x1000, 0x3FF00000ull, HEAP_BASE + 0x5000, WINDOW,
                                    HIGHER_HALF + 0x100000, 0x00007FFFFFFFF000ull };
    for (unsigned i = 0; i < sizeof(vas) / sizeof(vas[0]); i++)
        vmm_map(&vm, vas[i], 0x1000 * (i + 1), PTE_WRITE);
    for (unsigned i = 0; i < sizeof(vas) / sizeof(vas[0]); i++) {
        uint64_t va = vas[i];
        /* The frame of each table on va's path, from the tables themselves. */
        uint64_t f4 = root;
        uint64_t f3 = pte_frame(fake_ptr(f4)[vm_index(va, 4)]);
        uint64_t f2 = pte_frame(fake_ptr(f3)[vm_index(va, 3)]);
        uint64_t f1 = pte_frame(fake_ptr(f2)[vm_index(va, 2)]);
        const uint64_t want[5] = { 0, f1, f2, f3, f4 };
        for (int level = 1; level <= 4; level++) {
            uint64_t rva = vm_recursive_table(VM_RECURSIVE_INDEX, level, va);
            CHECK(vm_canonical(rva));
            CHECK_EQ(vm_index(rva, 4), VM_RECURSIVE_INDEX);
            CHECK(cpu_walk(rva, &phys));
            CHECK_EQ(phys, want[level]);
        }
    }
    CHECK_EQ(vm_recursive_table(VM_RECURSIVE_INDEX, 4, 0), 0xFFFFFF7FBFDFE000ull);
    CHECK_EQ(vm_recursive_table(VM_RECURSIVE_INDEX, 1, 0), 0xFFFFFF0000000000ull);
}

static void test_result_strings(void)
{
    for (int r = VMM_OK; r <= VMM_ERR_NONCANONICAL; r++)
        CHECK(strcmp(vmm_result_str((enum vmm_result)r), "unknown vmm error") != 0);
    CHECK(strcmp(vmm_result_str(VMM_ERR_NONCANONICAL), "non-canonical address") == 0);
}

int main(void)
{
    printf("vmm unit tests\n");
    RUN(test_map_translate_unmap);
    RUN(test_tables_per_level);
    RUN(test_ensure_table);
    RUN(test_out_of_frames_at_each_level);
    RUN(test_unaligned_flags_and_frames);
    RUN(test_uncached_flags);
    RUN(test_reserved_and_noncanonical);
    RUN(test_existing_mapping_not_replaced);
    RUN(test_unmap_not_mapped);
    RUN(test_flush_exactly_once_per_unmap);
    RUN(test_remap_after_unmap);
    RUN(test_leaf_flags_and_intermediate_entries);
    RUN(test_many_mappings_across_boundaries);
    RUN(test_upper_half_addresses);
    RUN(test_large_pages);
    RUN(test_recursive_entry_left_alone);
    RUN(test_recursive_table_addresses);
    RUN(test_result_strings);
    return unit_report("vmm");
}
