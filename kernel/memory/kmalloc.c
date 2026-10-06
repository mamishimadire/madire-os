/* Kernel heap wrapper (see kmalloc.h). */
#include "kernel/arch/x86/irq.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/panic.h"
#include "kernel/proc/preempt.h"

/* 0.9: the heap has no locking. Every operation runs with preemption off
 * (kernel/proc/preempt.h), so another thread never sees it half-changed.
 * Interrupt handlers still never use it (require_normal_context). */
static struct heap heap;
static bool ready;
static struct heap_stats current;

typedef unsigned long long ull;

/* Maps [va, va + bytes) to fresh frames. All or nothing: on failure every
 * page mapped here is unmapped and its frame freed. Page tables created on
 * the way stay (they are never freed). */
static bool map_new_pages(uint64_t va, uint32_t bytes)
{
    uint32_t off;
    for (off = 0; off < bytes; off += VM_PAGE_SIZE) {
        uint32_t frame = memory_alloc_page();
        if (!frame)
            break;
        if (paging_map(va + off, frame, PTE_WRITE | paging_nx_flag()) != VMM_OK) {
            memory_free_page(frame);
            break;
        }
    }
    if (off >= bytes)
        return true;
    for (uint32_t o = 0; o < off; o += VM_PAGE_SIZE) {
        uint64_t phys;
        if (paging_unmap(va + o, &phys) == VMM_OK)
            memory_free_page((uint32_t)phys);
    }
    return false;
}

static bool grow(void *ctx, uint32_t bytes)
{
    (void)ctx;
    return map_new_pages(VM_HEAP_BASE + heap.arena, bytes);
}

bool kheap_init(void)
{
    ready = false;
    if (!paging_enabled()) {
        klogf("Kernel heap: paging is off; heap disabled\n");
        return false;
    }
    if (!map_new_pages(VM_HEAP_BASE, VM_HEAP_INITIAL)) {
        klogf("Kernel heap: no memory for the first %u KiB; heap disabled\n",
              VM_HEAP_INITIAL / 1024);
        return false;
    }
    if (!heap_init(&heap, (uintptr_t)VM_HEAP_BASE, VM_HEAP_INITIAL, VM_HEAP_MAX, VM_HEAP_GROW_MIN,
                   grow, 0)) {
        klogf("Kernel heap: heap_init refused its parameters; heap disabled\n");
        return false;
    }
    ready = true;
    uint64_t flags = 0;
    paging_translate(VM_HEAP_BASE, 0, &flags);
    klogf("Kernel heap: %u KiB at 0x%016llx, max %u KiB, pages %s\n", VM_HEAP_INITIAL / 1024,
          (ull)VM_HEAP_BASE, VM_HEAP_MAX / 1024,
          (flags & PTE_NX) ? "no-execute (NX bit set)" : "executable (no NX bit)");
    return true;
}

static void require_normal_context(const char *fn)
{
    if (!irq_enabled())
        panicf("%s: called with interrupts disabled (IRQ or exception context?).", fn);
}

void *kmalloc(uint32_t size)
{
    require_normal_context("kmalloc");
    preempt_disable();
    void *p = ready ? heap_alloc(&heap, size) : 0;
    preempt_enable();
    return p;
}

void *kcalloc(uint32_t n, uint32_t size)
{
    require_normal_context("kcalloc");
    preempt_disable();
    void *p = ready ? heap_calloc(&heap, n, size) : 0;
    preempt_enable();
    return p;
}

/* The panics below never return, so they may leave preemption disabled. */
void kfree(void *p)
{
    require_normal_context("kfree");
    if (!p)
        return;
    ull a = (uintptr_t)p;
    if (!ready)
        panicf("Heap: invalid free of 0x%016llx (the kernel heap is disabled).", a);
    preempt_disable();
    enum heap_error e = heap_free(&heap, p);
    switch (e) {
    case HEAP_OK:
        preempt_enable();
        return;
    case HEAP_ERR_DOUBLE_FREE:
        panicf("Heap: double free of 0x%016llx.", a);
    case HEAP_ERR_NOT_HEAP:
    case HEAP_ERR_MISALIGNED:
        panicf("Heap: invalid free of 0x%016llx (%s).", a, heap_error_str(e));
    case HEAP_ERR_CORRUPT: {
        uintptr_t bad = 0;
        heap_check(&heap, &bad);
        panicf("Heap: corrupted block at 0x%016llx (freeing 0x%016llx).", (ull)bad, a);
    }
    }
    panicf("Heap: free of 0x%016llx failed (%s).", a, heap_error_str(e));
}

/* The returned snapshot is one static copy: only the console thread
 * (thread 0, for `mem`) asks for it. */
const struct heap_stats *kheap_stats(void)
{
    preempt_disable();
    if (ready) {
        uintptr_t bad = 0;
        if (heap_check(&heap, &bad) != HEAP_OK)
            panicf("Heap: corrupted block at 0x%016llx (found by heap_check).", (ull)bad);
        heap_stats(&heap, &current);
    } else {
        struct heap_stats none = { 0 };
        current = none;
    }
    preempt_enable();
    return &current;
}

/* ---- boot self-test ---- */

#define ST_BLOCKS 200

static struct {
    uint8_t *p;
    uint32_t size;
    uint8_t tag;
} blocks[ST_BLOCKS];

static uint32_t st_rng;
static unsigned st_items, st_failures;

static uint32_t st_rand(void)
{
    st_rng = st_rng * 1103515245u + 12345u;
    return st_rng >> 8;
}

static void st_line(const char *what, const char *detail, bool ok)
{
    st_items++;
    if (!ok)
        st_failures++;
    klogf("Kernel heap self-test: %s: %s: %s\n", what, detail, ok ? "OK" : "MISMATCH");
}

static bool block_intact(unsigned i)
{
    for (uint32_t j = 0; j < blocks[i].size; j++)
        if (blocks[i].p[j] != (uint8_t)(blocks[i].tag + j))
            return false;
    return true;
}

/* One free block covering the whole arena, nothing in use. */
static bool heap_empty(void)
{
    uintptr_t bad;
    struct heap_stats s;
    heap_stats(&heap, &s);
    return heap_check(&heap, &bad) == HEAP_OK && s.used_bytes == 0 && s.live_blocks == 0 &&
           s.free_bytes == s.arena_bytes && s.largest_free == s.arena_bytes - HEAP_HEADER;
}

bool kheap_selftest(void)
{
    char d[96];
    if (!ready)
        return false;
    st_items = st_failures = 0;
    st_rng = 0x6D616469u;                           /* fixed sequence */
    uint32_t free_before = pmm_free_pages();
    uint32_t arena_before = heap.arena;
    uint32_t tables_before = paging_stats()->page_tables;

    /* 1. About 200 mixed allocations, each filled with its own pattern. */
    unsigned got = 0;
    uint32_t bytes = 0;
    bool aligned = true;
    for (unsigned i = 0; i < ST_BLOCKS; i++) {
        uint32_t r = st_rand();
        uint32_t size = (r & 7) == 0 ? r % 8192 + 1 : r % 512 + 1;
        blocks[i].p = kmalloc(size);
        blocks[i].size = size;
        blocks[i].tag = (uint8_t)(i * 37);
        if (!blocks[i].p)
            continue;
        got++;
        bytes += size;
        if ((uintptr_t)blocks[i].p & (HEAP_ALIGN - 1))
            aligned = false;
        for (uint32_t j = 0; j < size; j++)
            blocks[i].p[j] = (uint8_t)(blocks[i].tag + j);
    }
    uintptr_t bad = 0;
    ksnprintf(d, sizeof(d), "%u of %u allocated (%u bytes), 16-byte aligned: %s", got, ST_BLOCKS,
              bytes, aligned ? "yes" : "no");
    st_line("mixed allocations", d, got == ST_BLOCKS && aligned &&
                                    heap_check(&heap, &bad) == HEAP_OK);

    /* 2. Free in a different order (odd ones, then even ones backwards),
     * checking every pattern first. */
    unsigned intact = 0, freed = 0;
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned k = 0; k < ST_BLOCKS; k++) {
            unsigned i = pass == 0 ? k : ST_BLOCKS - 1 - k;
            if ((i & 1) != (pass == 0 ? 1u : 0u) || !blocks[i].p)
                continue;
            if (block_intact(i))
                intact++;
            kfree(blocks[i].p);
            blocks[i].p = 0;
            freed++;
        }
    }
    struct heap_stats s;
    heap_stats(&heap, &s);
    ksnprintf(d, sizeof(d), "%u freed, %u patterns intact; one free block of %u bytes",
              freed, intact, s.largest_free + HEAP_HEADER);
    st_line("free in a different order", d, freed == got && intact == got && heap_empty());

    /* 3. Growth: a request larger than the whole arena. */
    uint32_t arena_now = heap.arena, grows_now = heap.grow_count;
    uint8_t *big = kmalloc(arena_now + 1);
    bool big_ok = big != 0 && heap.arena > arena_now && heap.grow_count > grows_now;
    if (big) {
        big[0] = 0x5A;
        big[arena_now] = 0xA5;
        big_ok = big_ok && big[0] == 0x5A && big[arena_now] == 0xA5;
        kfree(big);
    }
    ksnprintf(d, sizeof(d), "%u bytes: arena %u -> %u KiB", arena_now + 1, arena_now / 1024,
              heap.arena / 1024);
    st_line("growth", d, big_ok && heap_empty());

    /* 4. No page leak: only the pages the heap grew by (and any page table
     * created for them) are gone. */
    uint32_t grown = (heap.arena - arena_before) / VM_PAGE_SIZE;
    uint32_t tables = paging_stats()->page_tables - tables_before;
    uint32_t free_after = pmm_free_pages();
    ksnprintf(d, sizeof(d), "free pages %u -> %u; heap grew by %u pages, %u new page tables",
              free_before, free_after, grown, tables);
    st_line("no page leak", d, free_after == free_before - grown - tables);

    klogf("Kernel heap self-test: %s (%u checks, %u mismatches)\n", st_failures ? "FAIL" : "PASS",
          st_items, st_failures);
    if (st_failures) {
        ready = false;                              /* kmalloc returns NULL from now on */
        return false;
    }
    return true;
}
