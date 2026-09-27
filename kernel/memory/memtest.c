/*
 * Memory self-tests (see memtest.h).
 *
 * Every allocated page is checked with memory_page_is_safe(), which re-reads
 * the memory map and reservation list independently of the allocator's
 * bitmaps, so an allocator bug cannot hide behind its own bookkeeping.
 */
#include "kernel/kprintf.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/memtest.h"
#include "kernel/memory/pmm.h"
#include "kernel/panic.h"

extern char __kernel_start[], __kernel_end[];

#define SYM(s) ((uint32_t)(uintptr_t)(s))
#define WORDS_PER_PAGE (PAGE_SIZE / sizeof(uint32_t))
#define BOOT_TEST_PAGES 3

static uint32_t checks;

#define EXPECT(cond, ...)                                        \
    do {                                                         \
        if (!(cond))                                             \
            panicf("Memory self-test failed: " __VA_ARGS__);     \
        checks++;                                                \
    } while (0)

static volatile uint32_t *page_ptr(uint32_t addr)
{
    return (volatile uint32_t *)(uintptr_t)addr;
}

static uint32_t pattern(uint32_t addr, uint32_t i)
{
    return addr ^ (i * 0x9E3779B9u);
}

static void check_allocated(uint32_t p)
{
    EXPECT(p != 0, "allocator returned no page");
    EXPECT((p & (PAGE_SIZE - 1)) == 0, "page 0x%08x is not aligned", p);
    EXPECT(memory_page_is_safe(p), "page 0x%08x is reserved or outside usable RAM", p);
    EXPECT(!pmm_is_free(p), "page 0x%08x still marked free after allocation", p);
}

uint32_t memtest_boot(void)
{
    uint32_t before = pmm_free_pages();
    uint32_t pages[BOOT_TEST_PAGES];
    checks = 0;

    /* 1. Allocate: aligned, safe, marked used, strictly increasing. */
    for (int i = 0; i < BOOT_TEST_PAGES; i++) {
        pages[i] = memory_alloc_page();
        check_allocated(pages[i]);
        if (i > 0)
            EXPECT(pages[i] > pages[i - 1], "pages not in increasing order");
    }
    EXPECT(pmm_free_pages() == before - BOOT_TEST_PAGES, "free count did not drop by %u",
           BOOT_TEST_PAGES);

    /* 2. The pages are real RAM: write every word, then read it back. */
    for (int i = 0; i < BOOT_TEST_PAGES; i++)
        for (uint32_t w = 0; w < WORDS_PER_PAGE; w++)
            page_ptr(pages[i])[w] = pattern(pages[i], w);
    for (int i = 0; i < BOOT_TEST_PAGES; i++)
        for (uint32_t w = 0; w < WORDS_PER_PAGE; w++)
            EXPECT(page_ptr(pages[i])[w] == pattern(pages[i], w),
                   "RAM at 0x%08x did not hold its value", pages[i] + w * 4);

    /* 3. Free the middle page; the next allocation must return it. */
    memory_free_page(pages[1]);
    EXPECT(pmm_is_free(pages[1]), "freed page 0x%08x not marked free", pages[1]);
    uint32_t again = memory_alloc_page();
    EXPECT(again == pages[1], "reallocation returned 0x%08x, expected 0x%08x", again, pages[1]);

    /* 4. Free everything: the count must be restored exactly. */
    for (int i = 0; i < BOOT_TEST_PAGES; i++)
        memory_free_page(pages[i]);
    EXPECT(pmm_free_pages() == before, "free count not restored after freeing");

    /* 5. Pages that must never be allocatable. */
    unsigned n;
    const struct phys_range *res = memory_reservations(&n);
    EXPECT(!pmm_is_allocatable(0), "page 0 is allocatable");
    EXPECT(!pmm_is_allocatable(0xB8000), "VGA text buffer is allocatable");
    EXPECT(!pmm_is_allocatable(SYM(__kernel_start)), "first kernel page is allocatable");
    EXPECT(!pmm_is_allocatable(SYM(__kernel_end) - PAGE_SIZE), "last kernel page is allocatable");
    for (unsigned i = 0; i < n; i++) {
        EXPECT(!pmm_is_allocatable((uint32_t)res[i].start & ~(PAGE_SIZE - 1)),
               "reserved %s is allocatable", res[i].what);
        EXPECT(!pmm_is_allocatable((uint32_t)(res[i].end - 1) & ~(PAGE_SIZE - 1)),
               "end of reserved %s is allocatable", res[i].what);
    }

    klogf("Memory self-test: PASS (%u checks)\n", checks);
    return checks;
}

uint32_t memtest_exhaust(void)
{
    uint32_t before = pmm_free_pages();
    uint32_t count = 0, prev = 0, p;
    checks = 0;

    /* Allocate everything. Strictly increasing addresses prove no page is
     * handed out twice. Each page is tagged with its own address. */
    while ((p = memory_alloc_page()) != 0) {
        EXPECT((p & (PAGE_SIZE - 1)) == 0, "page 0x%08x is not aligned", p);
        EXPECT(p > prev, "page 0x%08x returned out of order (duplicate?)", p);
        EXPECT(memory_page_is_safe(p), "page 0x%08x is reserved or outside usable RAM", p);
        page_ptr(p)[0] = p;
        page_ptr(p)[WORDS_PER_PAGE - 1] = ~p;
        prev = p;
        count++;
    }
    EXPECT(count == before, "allocated %u pages, expected %u", count, before);
    EXPECT(pmm_free_pages() == 0, "free count not zero when exhausted");
    EXPECT(memory_alloc_page() == 0, "allocation succeeded after exhaustion");
    EXPECT(memory_alloc_page() == 0, "allocation succeeded after exhaustion (repeat)");

    /* Every allocatable page is now in use: check its tag, then free it. */
    uint32_t freed = 0;
    for (uint32_t page = 0; page < PMM_MAX_PAGES; page++) {
        uint32_t addr = page << PAGE_SHIFT;
        if (!pmm_is_allocatable(addr))
            continue;
        EXPECT(page_ptr(addr)[0] == addr && page_ptr(addr)[WORDS_PER_PAGE - 1] == ~addr,
               "page 0x%08x lost its contents (overlapping allocation?)", addr);
        memory_free_page(addr);
        freed++;
    }
    EXPECT(freed == count, "freed %u pages, allocated %u", freed, count);
    EXPECT(pmm_free_pages() == before, "free count not restored");

    klogf("Memory exhaustion test: PASS (%u pages allocated, verified and freed; %u checks)\n",
          count, checks);
    return count;
}

void memtest_double_free(void)
{
    uint32_t p = memory_alloc_page();
    klogf("Double-free test: freeing page 0x%08x twice\n", p);
    memory_free_page(p);
    memory_free_page(p);        /* must panic */
    panic("Double free was not detected.");
}

void memtest_free_reserved(void)
{
    klogf("Free-reserved test: freeing kernel page 0x%08x\n", SYM(__kernel_start));
    memory_free_page(SYM(__kernel_start));     /* must panic */
    panic("Freeing a reserved page was not detected.");
}
