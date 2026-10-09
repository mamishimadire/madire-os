/* Unit tests for kernel/memory/uvm_core.c (user address spaces, 0.10), on
 * fake physical frames. Every frame's allocations and frees are counted, so
 * the tests can require that each one comes back exactly once. */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/pte.h"
#include "kernel/memory/uvm_core.h"
#include "kernel/memory/vmm_core.h"
#include "tests/unit/unit.h"

#define FAKE_BASE   0x00200000ull
#define FAKE_FRAMES 96u
#define FOREIGN_BASE 0x10000000ull     /* leaf frames outside the fake memory */

static uint64_t frames[FAKE_FRAMES][VM_ENTRIES] __attribute__((aligned(4096)));
static uint8_t in_use[FAKE_FRAMES];         /* 1 = allocated and not freed */
static uint32_t limit, allocs, frees, double_frees, foreign_frees, flushes;
static uint64_t last_flush;
static struct uvm u;
static uint64_t root;

static uint64_t *fake_ptr(uint64_t phys) { return frames[(phys - FAKE_BASE) / 4096]; }
static uint32_t fake_index(uint64_t phys) { return (uint32_t)((phys - FAKE_BASE) / 4096); }

static uint64_t fake_alloc(void *ctx)
{
    (void)ctx;
    if (allocs - frees >= limit)
        return 0;
    for (uint32_t i = 0; i < FAKE_FRAMES; i++) {
        if (in_use[i])
            continue;
        in_use[i] = 1;
        allocs++;
        memset(frames[i], 0xAB, 4096);          /* garbage: tables must be cleared */
        return FAKE_BASE + 4096ull * i;
    }
    return 0;
}

static void fake_free(void *ctx, uint64_t frame)
{
    (void)ctx;
    if (frame >= FOREIGN_BASE) {                /* a leaf frame not from this allocator */
        foreign_frees++;
        return;
    }
    uint32_t i = fake_index(frame);
    if (i >= FAKE_FRAMES || !in_use[i]) {
        double_frees++;
        return;
    }
    in_use[i] = 0;
    frees++;
}

static uint64_t *fake_table(void *ctx, int level, uint64_t va, uint64_t phys, bool fresh)
{
    (void)ctx; (void)level; (void)va; (void)fresh;
    return fake_ptr(phys);
}

static void fake_flush(void *ctx, uint64_t va) { (void)ctx; flushes++; last_flush = va; }

static const struct vmm_ops ops = { fake_alloc, fake_free, fake_table, fake_flush, 0 };

static uint32_t frames_in_use(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < FAKE_FRAMES; i++)
        n += in_use[i];
    return n;
}

/* A user page frame from the same allocator (as the kernel will do). */
static uint64_t page_frame(void)
{
    uint64_t f = fake_alloc(0);
    memset(fake_ptr(f), 0, 4096);
    return f;
}

static void setup(bool nx)
{
    memset(in_use, 0, sizeof(in_use));
    limit = FAKE_FRAMES;
    allocs = frees = double_frees = foreign_frees = flushes = 0;
    last_flush = 0;
    root = fake_alloc(0);
    memset(fake_ptr(root), 0, 4096);
    uvm_init(&u, &ops, root, nx);
}

#define TEXT  VM_USER_TEXT
#define DATA  VM_USER_DATA
#define STACK (VM_USER_STACK_TOP - 4096)

static void test_map_translate_unmap(void)
{
    setup(true);
    uint64_t f = page_frame(), phys = 0, flags = 0;
    CHECK_EQ(uvm_map(&u, TEXT, f, 0), UVM_OK);
    CHECK_EQ(u.pages, 1);
    CHECK_EQ(u.tables, 3);                      /* PDPT, PD, PT */
    CHECK(uvm_translate(&u, TEXT + 0x123, &phys, &flags));
    CHECK_EQ(phys, f + 0x123);
    CHECK_EQ(flags, PTE_PRESENT | PTE_USER);    /* read + execute */
    uint64_t back = 0;
    CHECK_EQ(uvm_unmap(&u, TEXT, &back), UVM_OK);
    CHECK_EQ(back, f);
    CHECK_EQ(flushes, 1);
    CHECK_EQ(last_flush, TEXT);
    CHECK_EQ(u.pages, 0);
    CHECK(!uvm_translate(&u, TEXT, 0, 0));
    CHECK_EQ(uvm_unmap(&u, TEXT, 0), UVM_ERR_NOT_MAPPED);
}

static void test_user_bit_at_every_level(void)
{
    setup(true);
    CHECK_EQ(uvm_map(&u, DATA, page_frame(), PTE_WRITE | PTE_NX), UVM_OK);
    uint64_t e4 = fake_ptr(root)[vm_index(DATA, 4)];
    uint64_t e3 = fake_ptr(pte_frame(e4))[vm_index(DATA, 3)];
    uint64_t e2 = fake_ptr(pte_frame(e3))[vm_index(DATA, 2)];
    uint64_t e1 = fake_ptr(pte_frame(e2))[vm_index(DATA, 1)];
    CHECK_EQ(e4 & 0xFFF, PTE_PRESENT | PTE_WRITE | PTE_USER);
    CHECK_EQ(e3 & 0xFFF, PTE_PRESENT | PTE_WRITE | PTE_USER);
    CHECK_EQ(e2 & 0xFFF, PTE_PRESENT | PTE_WRITE | PTE_USER);
    CHECK(!pte_nx(e4) && !pte_nx(e3) && !pte_nx(e2));   /* NX decided at the leaf */
    CHECK_EQ(e1 & 0xFFF, PTE_PRESENT | PTE_WRITE | PTE_USER);
    CHECK(pte_nx(e1));
    /* Fresh tables were cleared: only the one entry on each path is set. */
    uint32_t set = 0;
    for (unsigned i = 0; i < VM_ENTRIES; i++)
        set += fake_ptr(pte_frame(e2))[i] != 0;
    CHECK_EQ(set, 1);
}

static void test_user_range_only(void)
{
    setup(true);
    uint64_t f = page_frame();
    CHECK_EQ(uvm_map(&u, 0, f, 0), UVM_ERR_RANGE);                      /* NULL */
    CHECK_EQ(uvm_map(&u, 0x1000, f, 0), UVM_ERR_RANGE);
    CHECK_EQ(uvm_map(&u, VM_USER_MIN - 4096, f, 0), UVM_ERR_RANGE);     /* below 4 MiB */
    CHECK_EQ(uvm_map(&u, VM_USER_TOP, f, 0), UVM_ERR_RANGE);            /* the last canonical page */
    CHECK_EQ(uvm_map(&u, 0x0000800000000000ull, f, 0), UVM_ERR_RANGE);  /* non-canonical */
    CHECK_EQ(uvm_map(&u, 0xFFFF800000000000ull, f, 0), UVM_ERR_RANGE);  /* kernel half */
    CHECK_EQ(uvm_map(&u, 0xFFFFFFFF80100000ull, f, 0), UVM_ERR_RANGE);  /* kernel image */
    CHECK_EQ(uvm_map(&u, TEXT + 8, f, 0), UVM_ERR_RANGE);               /* unaligned */
    CHECK_EQ(u.tables, 0);                                              /* nothing made */
    CHECK_EQ(uvm_map(&u, VM_USER_MIN, f, 0), UVM_OK);
    CHECK_EQ(uvm_map(&u, VM_USER_TOP - 4096, page_frame(), PTE_WRITE | PTE_NX), UVM_OK);
    CHECK(!uvm_translate(&u, VM_USER_TOP, 0, 0));
    CHECK(!uvm_translate(&u, 0xFFFFFFFF80100000ull, 0, 0));
    CHECK_EQ(uvm_unmap(&u, 0xFFFFFFFF80100000ull, 0), UVM_ERR_RANGE);
}

static void test_flags_and_wx(void)
{
    setup(true);
    uint64_t f = page_frame();
    CHECK_EQ(uvm_map(&u, TEXT, f, PTE_USER), UVM_ERR_FLAGS);
    CHECK_EQ(uvm_map(&u, TEXT, f, PTE_PRESENT), UVM_ERR_FLAGS);
    CHECK_EQ(uvm_map(&u, TEXT, f, PTE_LARGE), UVM_ERR_FLAGS);
    CHECK_EQ(uvm_map(&u, TEXT, f, PTE_WRITE), UVM_ERR_WX);              /* writable + executable */
    CHECK_EQ(u.tables, 0);
    CHECK_EQ(uvm_map(&u, TEXT, f, PTE_NX), UVM_OK);                      /* read-only data */
    CHECK_EQ(uvm_map(&u, TEXT + 4096, page_frame(), PTE_WRITE | PTE_NX), UVM_OK);

    setup(false);                               /* no NX: bit 63 is reserved */
    f = page_frame();
    CHECK_EQ(uvm_map(&u, TEXT, f, PTE_NX), UVM_ERR_FLAGS);
    CHECK_EQ(uvm_map(&u, TEXT, f, PTE_WRITE | PTE_NX), UVM_ERR_FLAGS);
    CHECK_EQ(uvm_map(&u, TEXT, f, PTE_WRITE), UVM_OK);                   /* documented limitation */
    uint64_t flags = 0;
    CHECK(uvm_translate(&u, TEXT, 0, &flags));
    CHECK(!pte_nx(flags));
}

static void test_bad_frames(void)
{
    setup(true);
    CHECK_EQ(uvm_map(&u, TEXT, 0, 0), UVM_ERR_PHYS);
    CHECK_EQ(uvm_map(&u, TEXT, 0x00300010ull, 0), UVM_ERR_PHYS);
    CHECK_EQ(uvm_map(&u, TEXT, 1ull << 52, 0), UVM_ERR_PHYS);
    CHECK_EQ(u.tables, 0);
}

static void test_existing_mapping_not_replaced(void)
{
    setup(true);
    uint64_t f1 = page_frame(), f2 = page_frame(), phys = 0;
    CHECK_EQ(uvm_map(&u, DATA, f1, PTE_WRITE | PTE_NX), UVM_OK);
    CHECK_EQ(uvm_map(&u, DATA, f2, PTE_WRITE | PTE_NX), UVM_ERR_EXISTS);
    CHECK(uvm_translate(&u, DATA, &phys, 0));
    CHECK_EQ(phys, f1);
    CHECK_EQ(u.pages, 1);
}

static void test_out_of_frames_at_each_level(void)
{
    for (uint32_t avail = 0; avail < 3; avail++) {
        setup(true);
        uint64_t f = page_frame();
        uint32_t used = frames_in_use();
        limit = used + avail;                   /* room for `avail` tables only */
        CHECK_EQ(uvm_map(&u, TEXT, f, 0), UVM_ERR_NO_FRAME);
        CHECK_EQ(frames_in_use(), used);        /* every frame taken was given back */
        CHECK_EQ(double_frees, 0);
        CHECK_EQ(fake_ptr(root)[0], 0);         /* nothing changed */
        CHECK_EQ(u.tables, 0);
        CHECK_EQ(u.pages, 0);
    }
}

/* A process-like address space: text, data + bss, stack. */
static uint32_t build_process(void)
{
    uint32_t pages = 0;
    for (int i = 0; i < 3; i++, pages++)
        CHECK_EQ(uvm_map(&u, TEXT + 4096ull * i, page_frame(), 0), UVM_OK);
    for (int i = 0; i < 2; i++, pages++)
        CHECK_EQ(uvm_map(&u, DATA + 4096ull * i, page_frame(), PTE_WRITE | PTE_NX), UVM_OK);
    for (uint64_t va = VM_USER_STACK_TOP - VM_USER_STACK_SIZE; va < VM_USER_STACK_TOP; va += 4096, pages++)
        CHECK_EQ(uvm_map(&u, va, page_frame(), PTE_WRITE | PTE_NX), UVM_OK);
    return pages;
}

static void test_teardown_gives_back_everything(void)
{
    setup(true);
    /* A shared kernel-half entry, as every process PML4 has: never freed. */
    uint64_t kernel_pdpt = fake_alloc(0);
    memset(fake_ptr(kernel_pdpt), 0, 4096);
    fake_ptr(root)[511] = pte_make(kernel_pdpt, PTE_PRESENT | PTE_WRITE);
    uint32_t before = frames_in_use();          /* root + the kernel PDPT */

    uint32_t pages = build_process();
    CHECK_EQ(pages, 3 + 2 + VM_USER_STACK_SIZE / 4096);
    CHECK_EQ(u.pages, pages);
    CHECK_EQ(u.tables, 6);                      /* slot 0: PDPT+PD+PT; slot 255: PDPT+PD+PT */
    CHECK_EQ(frames_in_use(), before + pages + 6);

    struct uvm_teardown_stats st = { 1, 1, 1 };
    uvm_teardown(&u, &st);
    CHECK_EQ(st.pages, pages);
    CHECK_EQ(st.tables, 6);
    CHECK_EQ(st.bad_entries, 0);
    CHECK_EQ(frames_in_use(), before);          /* exactly restored */
    CHECK_EQ(double_frees, 0);
    CHECK_EQ(u.pages, 0);
    CHECK_EQ(u.tables, 0);
    uint32_t lower = 0;
    for (unsigned i = 0; i < 256; i++)
        lower += fake_ptr(root)[i] != 0;
    CHECK_EQ(lower, 0);
    CHECK_EQ(fake_ptr(root)[511], pte_make(kernel_pdpt, PTE_PRESENT | PTE_WRITE));
    CHECK(in_use[fake_index(kernel_pdpt)]);
    CHECK(in_use[fake_index(root)]);            /* the PML4 stays the caller's */
}

static void test_teardown_empty_and_reuse(void)
{
    setup(true);
    struct uvm_teardown_stats st;
    uvm_teardown(&u, &st);
    CHECK_EQ(st.pages, 0);
    CHECK_EQ(st.tables, 0);
    uint32_t before = frames_in_use();
    for (int round = 0; round < 20; round++) {
        uint32_t pages = build_process();
        uvm_teardown(&u, &st);
        CHECK_EQ(st.pages, pages);
        CHECK_EQ(st.tables, 6);
    }
    CHECK_EQ(frames_in_use(), before);
    CHECK_EQ(double_frees, 0);
}

static void test_teardown_across_table_boundaries(void)
{
    setup(true);
    uint32_t before = frames_in_use();
    /* 520 pages from a 2 MiB boundary: one PT full (512), a second PT with
     * 8, under one PD and one PDPT. The leaves are frames outside the fake
     * memory, counted separately when given back. */
    uint64_t base = 0x3FC00000ull;
    for (uint32_t i = 0; i < 520; i++)
        CHECK_EQ(uvm_map(&u, base + 4096ull * i, FOREIGN_BASE + 4096ull * i, PTE_NX), UVM_OK);
    CHECK_EQ(u.tables, 4);
    CHECK_EQ(u.pages, 520);
    struct uvm_teardown_stats st;
    uvm_teardown(&u, &st);
    CHECK_EQ(st.pages, 520);
    CHECK_EQ(st.tables, 4);
    CHECK_EQ(foreign_frees, 520);
    CHECK_EQ(double_frees, 0);
    CHECK_EQ(frames_in_use(), before);
}

static void test_large_entry_left_alone(void)
{
    setup(true);
    CHECK_EQ(uvm_map(&u, TEXT, page_frame(), 0), UVM_OK);
    /* A large page in the lower half is never made by uvm; one planted
     * there must be reported, not walked or freed. */
    uint64_t e4 = fake_ptr(root)[0];
    uint64_t *pd = fake_ptr(pte_frame(fake_ptr(pte_frame(e4))[0]));
    pd[5] = pte_make(0x40000000ull, PTE_PRESENT | PTE_LARGE | PTE_USER);
    uint64_t f = page_frame();
    CHECK_EQ(uvm_map(&u, 5ull << 21, f, 0), UVM_ERR_EXISTS);
    CHECK(!uvm_translate(&u, 5ull << 21, 0, 0));
    struct uvm_teardown_stats st;
    uvm_teardown(&u, &st);
    CHECK_EQ(st.bad_entries, 1);
    CHECK_EQ(st.pages, 1);
    CHECK_EQ(st.tables, 3);
    CHECK_EQ(double_frees, 0);
}

static void test_upper_user_entries(void)
{
    setup(true);
    /* Kernel-half tables made by vmm_core, as paging_init does. */
    struct vmm vm;
    vmm_init(&vm, &ops, root);
    CHECK_EQ(vmm_map(&vm, 0xFFFFFFFF80100000ull, 0x100000ull, 0), VMM_OK);
    CHECK_EQ(vmm_map(&vm, 0xFFFFFE8000000000ull, 0x200000ull, PTE_WRITE | PTE_NX), VMM_OK);
    /* The recursive slot (its own frame, writable) is skipped. */
    fake_ptr(root)[VM_RECURSIVE_INDEX] = pte_make(root, PTE_PRESENT | PTE_WRITE | PTE_USER);
    build_process();                            /* user pages: lower half, not counted */
    CHECK_EQ(uvm_upper_user_entries(&ops, root), 0);

    uint64_t va = 0xFFFFFFFF80100000ull;
    uint64_t *pdpt = fake_ptr(pte_frame(fake_ptr(root)[511]));
    uint64_t *pd = fake_ptr(pte_frame(pdpt[vm_index(va, 3)]));
    uint64_t *pt = fake_ptr(pte_frame(pd[vm_index(va, 2)]));
    pt[vm_index(va, 1)] |= PTE_USER;            /* a kernel page made user-accessible */
    CHECK_EQ(uvm_upper_user_entries(&ops, root), 1);
    pd[vm_index(va, 2)] |= PTE_USER;            /* and its PD entry */
    CHECK_EQ(uvm_upper_user_entries(&ops, root), 2);
    fake_ptr(root)[511] |= PTE_USER;
    CHECK_EQ(uvm_upper_user_entries(&ops, root), 3);
}

static void test_result_strings(void)
{
    for (int r = UVM_OK; r <= UVM_ERR_PHYS; r++)
        CHECK(strcmp(uvm_result_str((enum uvm_result)r), "unknown uvm error") != 0);
    CHECK(strcmp(uvm_result_str(UVM_ERR_WX), "writable and executable") == 0);
}

int main(void)
{
    RUN(test_map_translate_unmap);
    RUN(test_user_bit_at_every_level);
    RUN(test_user_range_only);
    RUN(test_flags_and_wx);
    RUN(test_bad_frames);
    RUN(test_existing_mapping_not_replaced);
    RUN(test_out_of_frames_at_each_level);
    RUN(test_teardown_gives_back_everything);
    RUN(test_teardown_empty_and_reuse);
    RUN(test_teardown_across_table_boundaries);
    RUN(test_large_entry_left_alone);
    RUN(test_upper_user_entries);
    RUN(test_result_strings);
    return unit_report("uvm");
}
