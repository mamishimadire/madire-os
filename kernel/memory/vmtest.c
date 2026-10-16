/* Virtual-memory diagnostics (see vmtest.h). */
#include <stdint.h>
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/memory/vmtest.h"
#include "kernel/panic.h"

extern char __text_start[];

#define PF_UNMAPPED_ADDR 0x40000000u

/* The target address goes through a volatile variable so the compiler
 * cannot see (and reject or optimise away) the deliberate bad access. */
typedef unsigned long long ull;

static void report_target(const char *what, uint64_t addr)
{
    uint64_t phys = 0, flags = 0;
    bool mapped = paging_translate(addr, &phys, &flags);
    klogf("Page-fault test: %s at 0x%016llx (paging %s; %s)\n", what, (ull)addr,
          paging_enabled() ? "on" : "off",
          !mapped ? "not mapped" : (flags & 0x2u) ? "mapped read/write" : "mapped read-only");
}

void vmtest_pf_null(void)
{
    static volatile uintptr_t target = 0;
    report_target("read", target);
    volatile uint32_t v = *(volatile uint32_t *)target;
    (void)v;
    panic("Page-fault test: read from NULL was not detected.");
}

void vmtest_pf_unmapped(void)
{
    static volatile uintptr_t target = PF_UNMAPPED_ADDR;
    report_target("read", target);
    volatile uint32_t v = *(volatile uint32_t *)target;
    (void)v;
    panic("Page-fault test: read from an unmapped address was not detected.");
}

void vmtest_pf_readonly(void)
{
    static volatile uintptr_t target;
    target = (uintptr_t)__text_start;
    report_target("write", target);
    *(volatile uint32_t *)target = 0xDEADBEEFu;
    panic("Page-fault test: write to read-only kernel code was not detected.");
}

void vmtest_pf_low(void)
{
    /* The kernel's physical load address: where 0.7 ran it (identity). */
    static volatile uintptr_t target = 0x100000u;
    report_target("read", target);
    volatile uint32_t v = *(volatile uint32_t *)target;
    (void)v;
    panic("Page-fault test: read from the old low address of the kernel was not detected.");
}

/* ---- no-execute (NX) ---- */

/* A page used only by the NX test: .bss is data, so it is mapped
 * no-execute when NX is in use. It holds a single `ret`. */
static uint8_t nx_test_page[4096] __attribute__((aligned(4096)));

/* Kernel code (.text): must run whether or not NX is in use. */
static __attribute__((noinline)) uint32_t nx_exec_probe(void)
{
    return 0x5A5A5A5Au;
}

static const char *nx_bit_str(uint64_t va)
{
    uint64_t flags = 0;
    if (!paging_translate(va, 0, &flags))
        return "not mapped";
    return (flags & PTE_NX) ? "NX bit set" : "no NX bit";
}

void vmtest_pf_nx(void)
{
    const struct paging_stats *ps = paging_stats();
    /* Called through volatile pointers: the compiler can neither inline the
     * code-page call nor see (and reject) the deliberate data-page call. */
    uint32_t (*volatile exec_probe)(void) = nx_exec_probe;
    uint64_t code = (uint64_t)(uintptr_t)nx_exec_probe;
    uint32_t result = exec_probe();
    klogf("NX test: executable page: kernel code at 0x%016llx (%s) ran, returned 0x%08x (%s)\n",
          (ull)code, nx_bit_str(code), result, result == 0x5A5A5A5Au ? "correct" : "WRONG");
    if (result != 0x5A5A5A5Au)
        panic("NX test: kernel code did not run correctly.");

    nx_test_page[0] = 0xC3;                         /* ret */
    uint64_t data = (uint64_t)(uintptr_t)nx_test_page;
    klogf("NX test: data page at 0x%016llx (%s); NX %s; calling it\n", (ull)data,
          nx_bit_str(data), ps->nx ? "in use: this must fault" : "not in use: it will run");
    void (*volatile data_code)(void) = (void (*)(void))(uintptr_t)data;
    data_code();

    /* Only reached if the instruction fetch was allowed. */
    if (ps->nx)
        panic("NX test: an instruction fetch from a no-execute page was not detected.");
    klogf("NX test: NOT APPLICABLE: NX not supported by this CPU; the data page executed "
          "without a fault, so no-execute protection could not be tested\n");
}

/* ---- kernel stack guard page ---- */

extern char kstack_guard[], stack_bottom[], stack_top[];

/* Each level keeps a 512-byte buffer live across the next call, so every
 * level really uses the stack. The depth limit is never reached (it only
 * keeps the compiler from treating this as infinite recursion). */
static __attribute__((noinline)) uint32_t stack_eater(uint32_t depth)
{
    volatile uint8_t buf[512];
    buf[0] = (uint8_t)depth;
    buf[sizeof(buf) - 1] = (uint8_t)depth;
    if (depth >= 0x100000u)
        return depth;
    return stack_eater(depth + 1) + buf[0] + buf[sizeof(buf) - 1];
}

void vmtest_stack_overflow(void)
{
    uint64_t rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
    uint64_t guard = (uint64_t)(uintptr_t)kstack_guard;
    if (!paging_enabled()) {
        klogf("Stack test: NOT APPLICABLE: paging is off, so there is no guard page to hit\n");
        return;
    }
    klogf("Stack test: kernel stack 0x%016llx-0x%016llx, RSP 0x%016llx, guard page "
          "0x%016llx (%s); recursing until the stack overflows\n",
          (ull)(uintptr_t)stack_bottom, (ull)(uintptr_t)stack_top, (ull)rsp, (ull)guard,
          nx_bit_str(guard));
    uint32_t (*volatile eat)(uint32_t) = stack_eater;
    uint32_t r = eat(0);
    klogf("Stack test: recursion returned %u\n", r);
    panic("Stack test: kernel stack overflow was not detected.");
}

/* ---- kernel heap ---- */

void vmtest_heap_double(void)
{
    void *p = kmalloc(64);
    if (!p)
        panic("Heap test: kmalloc(64) returned NULL (heap disabled?).");
    klogf("Heap test: freeing 0x%016llx twice\n", (ull)(uintptr_t)p);
    kfree(p);
    kfree(p);                                       /* must panic */
    panic("Heap test: double free was not detected.");
}

void vmtest_heap_invalid(void)
{
    uint32_t local[4] __attribute__((aligned(16))) = { 0 };
    klogf("Heap test: freeing stack address 0x%016llx\n", (ull)(uintptr_t)local);
    kfree(local);                                   /* must panic */
    panic("Heap test: invalid free was not detected.");
}

void vmtest_heap_corrupt(void)
{
    uint32_t *p = kmalloc(64);
    if (!p)
        panic("Heap test: kmalloc(64) returned NULL (heap disabled?).");
    volatile uint32_t *header = p - HEAP_HEADER / sizeof(uint32_t);
    klogf("Heap test: overwriting the header at 0x%016llx, then freeing 0x%016llx\n",
          (ull)(uintptr_t)header, (ull)(uintptr_t)p);
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
    /* 1.0: the filesystem keeps its boot nodes (/, /home, /tmp) in the heap
     * from boot, so "everything freed" means back to this baseline. */
    uint32_t used_before = s->used_bytes, live_before = s->live_blocks;

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
    bool empty = s->used_bytes == used_before && s->live_blocks == live_before &&
                 s->free_bytes == s->arena_bytes - used_before;
    uint32_t grown = (arena_full - arena_before) / 4096;
    uint32_t tables = paging_stats()->page_tables - tables_before;
    uint32_t free_after = pmm_free_pages();
    bool no_leak = free_after == free_before - grown - tables;
    /* Without the cap, growth stopped because a minimum growth step (plus
     * possibly a new page table) no longer fits in the free pages. */
    bool out_of_frames = pmm_left < VM_HEAP_GROW_MIN / 4096 + 1;
    bool ok = count > 0 && (at_cap || out_of_frames) && still_null && empty && no_leak;
    klogf("Heap exhaustion test: %s (%u blocks freed; heap check %s; free pages %u -> %u = "
          "%u heap pages + %u page tables; baseline used %u B, live %u blocks)\n",
          ok ? "PASS" : "FAIL", count, empty ? "OK" : "MISMATCH", free_before, free_after,
          grown, tables, used_before, live_before);
    return ok;
}
