/* Virtual-memory diagnostics (see vmtest.h). */
#include <stdint.h>
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/memory/vmtest.h"
#include "kernel/panic.h"

extern char __text_start[];

#define PF_UNMAPPED_ADDR 0x40000000u

/* The target address goes through a volatile variable so the compiler
 * cannot see (and reject or optimise away) the deliberate bad access. */
static void report_target(const char *what, uint32_t addr)
{
    uint32_t phys = 0, flags = 0;
    bool mapped = paging_translate(addr, &phys, &flags);
    klogf("Page-fault test: %s at 0x%08x (paging %s; %s)\n", what, addr,
          paging_enabled() ? "on" : "off",
          !mapped ? "not mapped" : (flags & 0x2u) ? "mapped read/write" : "mapped read-only");
}

void vmtest_pf_null(void)
{
    static volatile uintptr_t target = 0;
    report_target("read", (uint32_t)target);
    volatile uint32_t v = *(volatile uint32_t *)target;
    (void)v;
    panic("Page-fault test: read from NULL was not detected.");
}

void vmtest_pf_unmapped(void)
{
    static volatile uintptr_t target = PF_UNMAPPED_ADDR;
    report_target("read", (uint32_t)target);
    volatile uint32_t v = *(volatile uint32_t *)target;
    (void)v;
    panic("Page-fault test: read from an unmapped address was not detected.");
}

void vmtest_pf_readonly(void)
{
    static volatile uintptr_t target;
    target = (uintptr_t)__text_start;
    report_target("write", (uint32_t)target);
    *(volatile uint32_t *)target = 0xDEADBEEFu;
    panic("Page-fault test: write to read-only kernel code was not detected.");
}

/* ---- kernel heap ---- */

void vmtest_heap_double(void)
{
    void *p = kmalloc(64);
    if (!p)
        panic("Heap test: kmalloc(64) returned NULL (heap disabled?).");
    klogf("Heap test: freeing 0x%08x twice\n", (uint32_t)(uintptr_t)p);
    kfree(p);
    kfree(p);                                       /* must panic */
    panic("Heap test: double free was not detected.");
}

void vmtest_heap_invalid(void)
{
    uint32_t local[4] __attribute__((aligned(16))) = { 0 };
    klogf("Heap test: freeing stack address 0x%08x\n", (uint32_t)(uintptr_t)local);
    kfree(local);                                   /* must panic */
    panic("Heap test: invalid free was not detected.");
}

void vmtest_heap_corrupt(void)
{
    uint32_t *p = kmalloc(64);
    if (!p)
        panic("Heap test: kmalloc(64) returned NULL (heap disabled?).");
    volatile uint32_t *header = p - HEAP_HEADER / sizeof(uint32_t);
    klogf("Heap test: overwriting the header at 0x%08x, then freeing 0x%08x\n",
          (uint32_t)(uintptr_t)header, (uint32_t)(uintptr_t)p);
    header[0] = 0x0BADC0DEu;                        /* the magic */
    kfree(p);                                       /* must panic */
    panic("Heap test: corrupted header was not detected.");
}

bool vmtest_heap_exhaust(void)
{
    const struct heap_stats *s = kheap_stats();
    if (s->arena_bytes == 0) {
        klogf("Heap exhaustion test: FAIL (heap disabled)\n");
        return false;
    }
    uint32_t free_before = pmm_free_pages();
    uint32_t arena_before = s->arena_bytes;
    uint32_t tables_before = paging_stats()->page_tables;

    /* Big blocks first, then smaller ones, until even 16 bytes fail. The
     * blocks are chained through their first word, so nothing else needs
     * memory. */
    static const uint32_t sizes[] = { 65536 - HEAP_HEADER, 4096 - HEAP_HEADER, 16 };
    void *head = 0;
    uint32_t count = 0;
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        void *p;
        while ((p = kmalloc(sizes[k])) != 0) {
            *(void **)p = head;
            head = p;
            count++;
        }
    }
    s = kheap_stats();
    uint32_t arena_full = s->arena_bytes, pmm_left = pmm_free_pages();
    bool at_cap = arena_full == VM_HEAP_MAX;
    bool still_null = kmalloc(16) == 0 && kmalloc(1) == 0;
    klogf("Heap exhaustion test: %u blocks, arena %u KiB (%s), %u free pages left, "
          "further kmalloc %s\n", count, arena_full / 1024,
          at_cap ? "16 MiB cap reached" : "stopped by physical memory: less than one growth step left",
          pmm_left,
          still_null ? "still NULL" : "SUCCEEDED");

    while (head) {
        void *next = *(void **)head;
        kfree(head);
        head = next;
    }
    s = kheap_stats();
    bool empty = s->used_bytes == 0 && s->live_blocks == 0 && s->free_bytes == s->arena_bytes;
    uint32_t grown = (arena_full - arena_before) / 4096;
    uint32_t tables = paging_stats()->page_tables - tables_before;
    uint32_t free_after = pmm_free_pages();
    bool no_leak = free_after == free_before - grown - tables;
    /* Without the cap, growth stopped because a minimum growth step (plus
     * possibly a new page table) no longer fits in the free pages. */
    bool out_of_frames = pmm_left < VM_HEAP_GROW_MIN / 4096 + 1;
    bool ok = count > 0 && (at_cap || out_of_frames) && still_null && empty && no_leak;
    klogf("Heap exhaustion test: %s (%u blocks freed; heap check %s; free pages %u -> %u = "
          "%u heap pages + %u page tables)\n", ok ? "PASS" : "FAIL", count,
          empty ? "OK" : "MISMATCH", free_before, free_after, grown, tables);
    return ok;
}
