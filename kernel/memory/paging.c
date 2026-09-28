/*
 * Paging (see paging.h).
 *
 * The tables are built while paging is off, so vmm_core reaches the
 * directory and every table through its physical address. Before anything
 * is loaded, the result is walk-checked: everything the kernel is running
 * on right now must translate to itself with the right access, and page 0
 * and free RAM must not be mapped. Every check is logged on serial as
 * "Paging check: <item>: expected <...>, actual <...>: OK|MISMATCH".
 *
 * If the check passes, CR3 is loaded and CR0.WP then CR0.PG are set; if it
 * fails, the frames are freed and paging stays off. paging_selftest() then
 * proves the live address space (map, read back, TLB flush, no page leak).
 */
#include "kernel/arch/x86/cpuid.h"
#include "kernel/kprintf.h"
#include "kernel/panic.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/memory/vmm_core.h"

extern char __text_start[], __rodata_end[], __data_start[], __bss_start[], __bss_end[];
extern char __kernel_end[];
extern char stack_bottom[], stack_top[];

#define SYM(s) ((uint32_t)(uintptr_t)(s))

#define VGA_TEXT_START   0x000B8000u
#define VGA_TEXT_END     (VGA_TEXT_START + 80u * 25 * 2)
#define MAX_TABLE_FRAMES 32u

static uint32_t dir_phys;
static uint32_t table_frames[MAX_TABLE_FRAMES];
static unsigned table_frame_count;
static struct vmm vm;
static struct paging_stats stats;
static unsigned check_items, check_failures;

/* ---- vmm_core operations while paging is off (physical addresses) ---- */

static uint32_t phys_alloc_frame(void *ctx)
{
    (void)ctx;
    if (table_frame_count >= MAX_TABLE_FRAMES)
        return 0;
    uint32_t frame = memory_alloc_page();
    if (frame)
        table_frames[table_frame_count++] = frame;
    return frame;
}

static uint32_t *phys_directory(void *ctx)
{
    (void)ctx;
    return (uint32_t *)(uintptr_t)dir_phys;
}

static uint32_t *phys_table(void *ctx, uint32_t pd_index, uint32_t table_phys, bool fresh)
{
    (void)ctx;
    (void)pd_index;
    (void)fresh;
    return (uint32_t *)(uintptr_t)table_phys;
}

static void phys_flush(void *ctx, uint32_t va)
{
    (void)ctx;
    (void)va;                   /* paging is off: there is no TLB to flush */
}

static const struct vmm_ops phys_ops = {
    phys_alloc_frame, phys_directory, phys_table, phys_flush, 0
};

/* ---- building ---- */

/* Identity-maps [start, end); pages already mapped (by an earlier, more
 * specific range) are left as they are when allow_existing is set. */
static bool map_identity(const char *what, uint32_t start, uint32_t end, uint32_t flags,
                         bool allow_existing)
{
    for (uint32_t va = page_floor(start); va < end; va += VM_PAGE_SIZE) {
        enum vmm_result r = vmm_map(&vm, va, va, flags);
        if (r == VMM_OK || (r == VMM_ERR_EXISTS && allow_existing))
            continue;
        klogf("Paging build: %s: page 0x%08x: %s\n", what, va, vmm_result_str(r));
        return false;
    }
    return true;
}

static bool build_tables(void)
{
    dir_phys = memory_alloc_page();
    if (!dir_phys) {
        klogf("Paging build: no free page for the page directory\n");
        return false;
    }
    uint32_t *dir = (uint32_t *)(uintptr_t)dir_phys;
    for (uint32_t i = 0; i < VM_ENTRIES; i++)
        dir[i] = 0;
    dir[VM_RECURSIVE_INDEX] = pte_make(dir_phys, PTE_PRESENT | PTE_WRITE);
    vmm_init(&vm, &phys_ops);

    /* Most specific first: the kernel's read-only part must not be mapped
     * writable by the (overlapping) kernel reservation afterwards. */
    if (!map_identity("kernel code+rodata", SYM(__text_start), SYM(__data_start), 0, false) ||
        !map_identity("kernel data+bss", SYM(__data_start), SYM(__kernel_end), PTE_WRITE, false) ||
        !map_identity("low memory", VM_LOW_MAP_START, VM_LOW_MAP_END, PTE_WRITE, false))
        return false;

    unsigned count;
    const struct phys_range *r = memory_reservations(&count);
    for (unsigned i = 0; i < count; i++) {
        if (r[i].start >= r[i].end || r[i].start >= 0x100000000ull)
            continue;
        uint64_t end64 = r[i].end;
        if (end64 > VM_HEAP_BASE) {                 /* would collide with heap/window/tables */
            klogf("Paging build: reservation %s ends at 0x%08x%08x, above 0x%08x\n",
                  r[i].what, (uint32_t)(end64 >> 32), (uint32_t)end64, VM_HEAP_BASE);
            return false;
        }
        uint32_t start = (uint32_t)r[i].start, end = (uint32_t)end64;
        if (start < VM_NULL_GUARD_END)
            start = VM_NULL_GUARD_END;              /* page 0 stays the NULL guard */
        if (start < end && !map_identity(r[i].what, start, end, PTE_WRITE, true))
            return false;
    }

    enum vmm_result w = vmm_ensure_table(&vm, VM_WINDOW);
    if (w != VMM_OK) {
        klogf("Paging build: window page table: %s\n", vmm_result_str(w));
        return false;
    }

    stats.cr3 = 0;
    stats.mapped_pages = vm.mapped_pages;
    stats.page_tables = vm.page_tables;
    stats.kernel_ro_pages = (SYM(__data_start) - SYM(__text_start)) / VM_PAGE_SIZE;
    return true;
}

static void release_tables(void)
{
    for (unsigned i = 0; i < table_frame_count; i++)
        memory_free_page(table_frames[i]);
    table_frame_count = 0;
    if (dir_phys)
        memory_free_page(dir_phys);
    dir_phys = 0;
}

/* ---- vmm_core operations once paging is on (recursive mapping) ---- */

static inline void invlpg(uint32_t va)
{
    __asm__ volatile ("invlpg (%0)" :: "r"(va) : "memory");
}

static inline uint32_t read_cr3(void)
{
    uint32_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static uint32_t rec_alloc_frame(void *ctx)
{
    (void)ctx;
    return memory_alloc_page();         /* page tables live for the whole run */
}

static uint32_t *rec_directory(void *ctx)
{
    (void)ctx;
    return (uint32_t *)VM_PAGE_DIRECTORY;
}

static uint32_t *rec_table(void *ctx, uint32_t pd_index, uint32_t table_phys, bool fresh)
{
    (void)ctx;
    (void)table_phys;
    uint32_t va = VM_RECURSIVE_BASE + pd_index * VM_PAGE_SIZE;
    if (fresh)
        invlpg(va);                     /* the directory entry behind va just changed */
    return (uint32_t *)va;
}

static void rec_flush(void *ctx, uint32_t va)
{
    (void)ctx;
    invlpg(va);
}

static const struct vmm_ops recursive_ops = {
    rec_alloc_frame, rec_directory, rec_table, rec_flush, 0
};

#define CR0_WP 0x00010000u
#define CR0_PG 0x80000000u

/* Identity mapping: execution continues at the same addresses. From here
 * on the tables are reached only through the recursive mapping (their
 * frames are free RAM, which is not mapped). */
static void enable(void)
{
    __asm__ volatile ("mov %0, %%cr3" :: "r"(dir_phys) : "memory");
    uint32_t cr0 = cpu_read_cr0() | CR0_WP;
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
    cr0 |= CR0_PG;
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
    vm.ops = &recursive_ops;
    stats.enabled = true;
    stats.cr3 = dir_phys;
}

/* ---- walk-check ---- */

static const char *access_str(bool present, uint32_t flags)
{
    return !present ? "unmapped" : (flags & PTE_WRITE) ? "rw" : "ro";
}

static bool in_kernel_ro(uint32_t va)
{
    return va >= SYM(__text_start) && va < SYM(__data_start);
}

enum expect { EXPECT_RW, EXPECT_RO, EXPECT_KERNEL_AWARE, EXPECT_UNMAPPED };

static void report(const char *what, uint32_t start, uint32_t end, const char *expected,
                   const char *actual, bool ok)
{
    check_items++;
    if (!ok)
        check_failures++;
    klogf("Paging check: %s 0x%08x-0x%08x: expected %s, actual %s: %s\n",
          what, start, end, expected, actual, ok ? "OK" : "MISMATCH");
}

/* Every page of [start, end) must match. Logs one line for the range; on a
 * mismatch the actual value shown is the first bad page. */
static bool check_range(const char *what, uint32_t start, uint32_t end, enum expect e)
{
    static const char *const expected_str[] = {
        "identity rw", "identity ro", "identity, ro in kernel code else rw", "unmapped"
    };
    char actual[64];
    uint32_t pages = 0;
    bool ok = true;
    for (uint32_t va = page_floor(start); va < end && ok; va += VM_PAGE_SIZE) {
        uint32_t phys = 0, flags = 0;
        bool present = vmm_translate(&vm, va, &phys, &flags);
        bool writable = (flags & PTE_WRITE) != 0;
        bool want_rw = e == EXPECT_RW || (e == EXPECT_KERNEL_AWARE && !in_kernel_ro(va));
        if (e == EXPECT_UNMAPPED)
            ok = !present;
        else
            ok = present && phys == va && writable == want_rw && !(flags & PTE_USER);
        if (!ok)
            ksnprintf(actual, sizeof(actual), "page 0x%08x -> %s 0x%08x", va,
                      access_str(present, flags), phys);
        pages++;
        if (va + VM_PAGE_SIZE < va)
            break;
    }
    if (ok) {
        if (e == EXPECT_UNMAPPED)
            ksnprintf(actual, sizeof(actual), "unmapped (%u pages)", pages);
        else if (e == EXPECT_KERNEL_AWARE)
            ksnprintf(actual, sizeof(actual), "identity as expected (%u pages)", pages);
        else
            ksnprintf(actual, sizeof(actual), "identity %s (%u pages)",
                      e == EXPECT_RW ? "rw" : "ro", pages);
    }
    report(what, start, end, expected_str[e], actual, ok);
    return ok;
}

/* A fact about the layout (not a translation); logged the same way. */
static void check_fact(const char *what, uint32_t start, uint32_t end, const char *expected,
                       bool ok)
{
    report(what, start, end, expected, ok ? "as expected" : "different", ok);
}

struct __attribute__((packed)) descriptor_table {
    uint16_t limit;
    uint32_t base;
};

static bool in_kernel_rw(uint32_t start, uint32_t end)
{
    return start >= SYM(__data_start) && end <= SYM(__kernel_end) && start < end;
}

static void walk_check(void)
{
    uint32_t eip, esp;
    __asm__ volatile ("call 1f\n1: pop %0" : "=r"(eip));
    __asm__ volatile ("mov %%esp, %0" : "=r"(esp));
    struct descriptor_table gdtr, idtr;
    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    __asm__ volatile ("sidt %0" : "=m"(idtr));
    uint32_t gdt_end = gdtr.base + gdtr.limit + 1, idt_end = idtr.base + idtr.limit + 1;

    check_items = check_failures = 0;

    /* What the CPU is using at this moment. */
    check_range("EIP (running code)", eip, eip + 1, EXPECT_RO);
    check_range("ESP (current stack)", esp, esp + 1, EXPECT_RW);
    check_range("boot stack", SYM(stack_bottom), SYM(stack_top), EXPECT_RW);
    check_range("kernel code+rodata", SYM(__text_start), SYM(__data_start), EXPECT_RO);
    check_range("kernel data+bss", SYM(__data_start), SYM(__kernel_end), EXPECT_RW);
    check_range("VGA text buffer", VGA_TEXT_START, VGA_TEXT_END, EXPECT_RW);
    check_range("low memory pages 1-255", VM_LOW_MAP_START, VM_LOW_MAP_END, EXPECT_RW);

    /* Check C: the CPU's own tables, found where the CPU says they are. */
    check_range("GDT (sgdt)", gdtr.base, gdt_end, EXPECT_RW);
    check_range("IDT (sidt)", idtr.base, idt_end, EXPECT_RW);
    check_fact("GDT inside kernel .data/.bss", gdtr.base, gdt_end, "writable kernel data",
               in_kernel_rw(gdtr.base, gdt_end));
    check_fact("IDT inside kernel .data/.bss", idtr.base, idt_end, "writable kernel data",
               in_kernel_rw(idtr.base, idt_end));

    /* The pmm bitmaps are static in pmm.c, in .bss: covered page by page by
     * "kernel data+bss" above; the QEMU test places them with nm. */
    check_range("pmm bitmaps (.bss)", SYM(__bss_start), SYM(__bss_end), EXPECT_RW);

    /* Every boot-data reservation (page 0 excluded: see the next check). */
    unsigned count;
    const struct phys_range *r = memory_reservations(&count);
    for (unsigned i = 0; i < count; i++) {
        if (r[i].start >= r[i].end || r[i].start >= 0x100000000ull)
            continue;
        uint32_t start = (uint32_t)r[i].start, end = (uint32_t)r[i].end;
        if (start < VM_NULL_GUARD_END)
            start = VM_NULL_GUARD_END;
        char what[80];
        ksnprintf(what, sizeof(what), "reservation %s", r[i].what);
        check_range(what, start, end, EXPECT_KERNEL_AWARE);
    }

    /* Check A: page 0 (NULL, real-mode IVT, BIOS data area 0x400, EBDA
     * pointer 0x40E) is not mapped. */
    check_range("page 0 (NULL, IVT, BDA 0x400, EBDA ptr 0x40E)", 0, VM_NULL_GUARD_END,
                EXPECT_UNMAPPED);

    /* Check B: the read-only range holds only .text/.rodata, and everything
     * the kernel writes lies outside it. */
    check_fact("section starts page-aligned", SYM(__text_start), SYM(__kernel_end),
               "text/data/bss/end 4 KiB aligned",
               page_aligned(SYM(__text_start)) && page_aligned(SYM(__data_start)) &&
               page_aligned(SYM(__bss_start)) && page_aligned(SYM(__kernel_end)));
    check_fact("rodata ends before writable data", SYM(__rodata_end), SYM(__data_start),
               "__rodata_end <= __data_start", SYM(__rodata_end) <= SYM(__data_start));
    check_fact("writable objects outside read-only", SYM(__text_start), SYM(__data_start),
               "stack, GDT, IDT, .data, .bss not read-only",
               !in_kernel_ro(SYM(stack_bottom)) && !in_kernel_ro(SYM(stack_top) - 1) &&
               !in_kernel_ro(gdtr.base) && !in_kernel_ro(idtr.base) &&
               !in_kernel_ro(SYM(__data_start)) && !in_kernel_ro(SYM(__bss_start)));

    /* Free RAM is not mapped: the table frames themselves came from it. */
    check_range("page directory frame (free RAM)", dir_phys, dir_phys + 1, EXPECT_UNMAPPED);
    for (unsigned i = 0; i < table_frame_count; i++)
        check_range("page table frame (free RAM)", table_frames[i], table_frames[i] + 1,
                    EXPECT_UNMAPPED);

    /* Addresses reserved for later use are empty. */
    check_range("heap area (mapped by kmalloc)", VM_HEAP_BASE, VM_HEAP_BASE + VM_HEAP_INITIAL,
                EXPECT_UNMAPPED);
    check_range("paging self-test page", VM_TEST_PAGE, VM_TEST_PAGE + 1, EXPECT_UNMAPPED);
    check_range("physical-access window", VM_WINDOW, VM_WINDOW + 1, EXPECT_UNMAPPED);

    const uint32_t *dir = (const uint32_t *)(uintptr_t)dir_phys;
    check_fact("window page table present", VM_WINDOW, VM_WINDOW + VM_PAGE_SIZE,
               "directory entry present", pte_present(dir[pd_index(VM_WINDOW)]));
    uint32_t rec = dir[VM_RECURSIVE_INDEX];
    check_fact("recursive entry (PD[1023])", VM_RECURSIVE_BASE, 0xFFFFFFFFu,
               "points to the directory, rw",
               pte_present(rec) && pte_writable(rec) && pte_frame(rec) == dir_phys &&
               !(rec & PTE_USER));

    /* Counts, recomputed independently of vmm_core's counters. */
    uint32_t tables = 0, pages = 0;
    for (uint32_t pdi = 0; pdi < VM_RECURSIVE_INDEX; pdi++) {
        if (!pte_present(dir[pdi]))
            continue;
        tables++;
        const uint32_t *t = (const uint32_t *)(uintptr_t)pte_frame(dir[pdi]);
        for (uint32_t i = 0; i < VM_ENTRIES; i++)
            if (pte_present(t[i]))
                pages++;
    }
    char expected[48];
    ksnprintf(expected, sizeof(expected), "%u tables, %u pages", vm.page_tables, vm.mapped_pages);
    check_fact("table walk counts", 0, 0xFFBFFFFFu, expected,
               tables == vm.page_tables && pages == vm.mapped_pages);
}

bool paging_init(void)
{
    stats.enabled = false;
    if (!cpuid_supported()) {                   /* invlpg and CR0.WP need a 486+ */
        klogf("Paging: CPU without CPUID (486 or later needed); paging stays off\n");
        return false;
    }

    uint32_t free_before = pmm_free_pages();
    table_frame_count = 0;
    bool built = build_tables();
    if (built)
        walk_check();
    bool ok = built && check_failures == 0;
    if (built)
        klogf("Paging walk-check: %s (%u items, %u mismatches)\n", ok ? "PASS" : "FAIL",
              check_items, check_failures);
    klogf("Paging tables: %u page tables + directory, %u mapped pages, %u read-only kernel pages\n",
          stats.page_tables, stats.mapped_pages, stats.kernel_ro_pages);

    if (!ok) {
        /* Never loaded: give the frames back and prove nothing leaked. */
        uint32_t frames = table_frame_count + (dir_phys ? 1 : 0);
        release_tables();
        uint32_t free_after = pmm_free_pages();
        klogf("Paging: stays OFF; %u table frames released, free pages %u -> %u\n",
              frames, free_before, free_after);
        if (free_after != free_before)
            klogf("Paging: page leak: %u pages not returned\n", free_before - free_after);
        return false;
    }

    enable();
    klogf("Paging: enabled; CR0=0x%08x CR3=0x%08x\n", cpu_read_cr0(), read_cr3());
    return true;
}

/* ---- after enabling ---- */

static void st_line(const char *what, const char *detail, bool ok)
{
    check_items++;
    if (!ok)
        check_failures++;
    klogf("Paging self-test: %s: %s: %s\n", what, detail, ok ? "OK" : "MISMATCH");
}

static uint32_t test_pattern(uint32_t frame, uint32_t i)
{
    return frame ^ (i * 0x9E3779B9u);
}

/* Fills (write) or checks (!write) the page at p with seed's pattern. */
static bool pattern_page(volatile uint32_t *p, uint32_t seed, bool write)
{
    for (uint32_t i = 0; i < VM_ENTRIES; i++) {
        if (write)
            p[i] = test_pattern(seed, i);
        else if (p[i] != test_pattern(seed, i))
            return false;
    }
    return true;
}

/* The same on the self-test page, with frame's own pattern. */
static bool test_page(uint32_t frame, bool write)
{
    return pattern_page((volatile uint32_t *)VM_TEST_PAGE, frame, write);
}

static bool test_map(uint32_t frame)
{
    return vmm_map(&vm, VM_TEST_PAGE, frame, PTE_WRITE) == VMM_OK;
}

static bool test_unmap(uint32_t frame)
{
    uint32_t phys = 0;
    return vmm_unmap(&vm, VM_TEST_PAGE, &phys) == VMM_OK && phys == frame;
}

bool paging_selftest(void)
{
    char d[96];
    if (!stats.enabled)
        return false;
    check_items = check_failures = 0;

    uint32_t cr0 = cpu_read_cr0(), cr3 = read_cr3();
    ksnprintf(d, sizeof(d), "CR0=0x%08x, PG %s, WP %s", cr0, (cr0 & CR0_PG) ? "set" : "clear",
              (cr0 & CR0_WP) ? "set" : "clear");
    st_line("control register CR0", d, (cr0 & CR0_PG) && (cr0 & CR0_WP));
    ksnprintf(d, sizeof(d), "CR3=0x%08x, directory at 0x%08x", cr3, dir_phys);
    st_line("control register CR3", d, (cr3 & PTE_FRAME_MASK) == dir_phys);
    uint32_t rec = ((const volatile uint32_t *)VM_PAGE_DIRECTORY)[VM_RECURSIVE_INDEX];
    ksnprintf(d, sizeof(d), "PD[1023] read at 0x%08x = 0x%08x", VM_PAGE_DIRECTORY, rec);
    st_line("recursive mapping", d, pte_present(rec) && pte_frame(rec) == dir_phys);

    /* The address space still holds what the kernel runs on (read through
     * the recursive mapping now), and page 0 is not mapped. */
    uint32_t esp;
    __asm__ volatile ("mov %%esp, %0" : "=r"(esp));
    check_range("kernel code+rodata", SYM(__text_start), SYM(__data_start), EXPECT_RO);
    check_range("kernel data+bss", SYM(__data_start), SYM(__kernel_end), EXPECT_RW);
    check_range("ESP (current stack)", esp, esp + 1, EXPECT_RW);
    check_range("VGA text buffer", VGA_TEXT_START, VGA_TEXT_END, EXPECT_RW);
    unsigned count;
    const struct phys_range *r = memory_reservations(&count);
    for (unsigned i = 0; i < count; i++) {
        if (r[i].start >= r[i].end || r[i].start >= 0x100000000ull || r[i].start < 0x100000u)
            continue;                               /* low 1 MiB: covered by the build check */
        char what[80];
        ksnprintf(what, sizeof(what), "reservation %s", r[i].what);
        check_range(what, (uint32_t)r[i].start, (uint32_t)r[i].end, EXPECT_KERNEL_AWARE);
    }
    check_range("page 0 (NULL, IVT, BDA 0x400, EBDA ptr 0x40E)", 0, VM_NULL_GUARD_END,
                EXPECT_UNMAPPED);

    /* Map, write, read back, translate. */
    uint32_t free_before = pmm_free_pages();
    uint32_t a = memory_alloc_page(), b = memory_alloc_page();
    ksnprintf(d, sizeof(d), "frames A=0x%08x B=0x%08x", a, b);
    st_line("test frames allocated", d, a && b);
    if (a && b) {
        uint32_t phys = 0;
        bool mapped = test_map(a);
        bool ok = mapped && (test_page(a, true), test_page(a, false)) &&
                  vmm_translate(&vm, VM_TEST_PAGE + 0x123, &phys, 0) && phys == a + 0x123;
        ksnprintf(d, sizeof(d), "0x%08x -> A, 4 KiB written and read back, translates to 0x%08x",
                  VM_TEST_PAGE, phys);
        st_line("map and read back", d, ok);

        ok = mapped && test_unmap(a) && !vmm_translate(&vm, VM_TEST_PAGE, 0, 0);
        st_line("unmap", "test page no longer translates; frame A returned", ok);

        /* TLB flush: the same virtual page now goes to B. If the old
         * translation were still cached, B's pattern would land in A. */
        mapped = test_map(b);
        ok = mapped && (test_page(b, true), test_page(b, false)) && test_unmap(b);
        mapped = test_map(a);
        bool a_intact = mapped && test_page(a, false);
        ok = ok && a_intact && test_unmap(a);
        mapped = test_map(b);
        bool b_intact = mapped && test_page(b, false);
        ok = ok && b_intact && test_unmap(b);
        ksnprintf(d, sizeof(d), "remapped A -> B -> A -> B: A kept %s, B kept %s",
                  a_intact ? "its pattern" : "B's pattern (stale TLB)",
                  b_intact ? "its pattern" : "a wrong pattern");
        st_line("TLB flush on remap", d, ok);

        /* Physical-access window: map -> access -> unmap -> not mapped.
         * It must reach frame A itself: it sees the pattern written through
         * the test page, and what it writes is then found in A. */
        volatile uint32_t *w = vmm_window_map(a);
        uint32_t wphys = 0;
        bool w_ok = paging_translate(VM_WINDOW, &wphys, 0) && wphys == a &&
                    pattern_page(w, a, false);
        pattern_page(w, ~a, true);
        w_ok = w_ok && pattern_page(w, ~a, false);
        vmm_window_unmap();
        bool w_gone = !vmm_translate(&vm, VM_WINDOW, 0, 0);
        mapped = test_map(a);
        bool landed = mapped && pattern_page((volatile uint32_t *)VM_TEST_PAGE, ~a, false);
        ok = w_ok && w_gone && landed && test_unmap(a);
        ksnprintf(d, sizeof(d), "0x%08x -> A read and written; after unmap %s; data in A: %s",
                  VM_WINDOW, w_gone ? "not mapped" : "STILL MAPPED", landed ? "yes" : "no");
        st_line("physical-access window", d, ok);
    }
    if (a)
        memory_free_page(a);
    if (b)
        memory_free_page(b);
    uint32_t free_after = pmm_free_pages();
    ksnprintf(d, sizeof(d), "free pages %u -> %u", free_before, free_after);
    st_line("no page leak", d, free_after == free_before && !vmm_translate(&vm, VM_TEST_PAGE, 0, 0) &&
                               !vmm_translate(&vm, VM_WINDOW, 0, 0));

    klogf("Paging self-test: %s (%u checks, %u mismatches)\n", check_failures ? "FAIL" : "PASS",
          check_items, check_failures);
    return check_failures == 0;
}

bool paging_enabled(void)
{
    return stats.enabled;
}

static void require_enabled(const char *what)
{
    if (!stats.enabled)
        panicf("%s called while paging is off.", what);
}

enum vmm_result paging_map(uint32_t va, uint32_t phys, uint32_t flags)
{
    require_enabled("paging_map");
    enum vmm_result r = vmm_map(&vm, va, phys, flags);
    stats.page_tables = vm.page_tables;
    return r;
}

enum vmm_result paging_unmap(uint32_t va, uint32_t *phys_out)
{
    require_enabled("paging_unmap");
    return vmm_unmap(&vm, va, phys_out);
}

bool paging_translate(uint32_t va, uint32_t *phys_out, uint32_t *flags_out)
{
    return stats.enabled && vmm_translate(&vm, va, phys_out, flags_out);
}

/* The window's page table was created at build time, so mapping it never
 * needs a frame (it works even when physical memory is exhausted). */
static bool window_mapped;

volatile void *vmm_window_map(uint32_t phys)
{
    require_enabled("vmm_window_map");
    if (window_mapped)
        panic("vmm_window_map: window already in use (missing vmm_window_unmap).");
    enum vmm_result r = vmm_map(&vm, VM_WINDOW, phys, PTE_WRITE);
    if (r != VMM_OK)
        panicf("vmm_window_map(0x%08x): %s.", phys, vmm_result_str(r));
    window_mapped = true;
    return (volatile void *)VM_WINDOW;
}

void vmm_window_unmap(void)
{
    require_enabled("vmm_window_unmap");
    if (!window_mapped)
        panic("vmm_window_unmap: window not mapped.");
    /* Clears the entry and invalidates the TLB entry (vmm_core flushes on
     * unmap). The frame is not freed: it belongs to the caller. */
    enum vmm_result r = vmm_unmap(&vm, VM_WINDOW, 0);
    if (r != VMM_OK || vmm_translate(&vm, VM_WINDOW, 0, 0))
        panicf("vmm_window_unmap: %s.", r != VMM_OK ? vmm_result_str(r) : "window still mapped");
    window_mapped = false;
}

const struct paging_stats *paging_stats(void)
{
    return &stats;
}
