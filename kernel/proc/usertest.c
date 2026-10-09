/* User-mode diagnostics (see usertest.h). */
#include <stdint.h>
#include "kernel/arch/x86/cpuprot.h"
#include "kernel/arch/x86/gdt_desc.h"
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/pagefault.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/uspace.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/panic.h"
#include "kernel/proc/process.h"
#include "kernel/proc/syscall.h"
#include "kernel/proc/thread.h"
#include "kernel/proc/usertest.h"
#include "kernel/timer.h"

/* ---- test=rsp0 ---- */

#define RSP0_WORKERS 3
#define RSP0_TICKS   40             /* long enough for several preemptions each */

static volatile uint32_t rsp0_checks, rsp0_mismatches;
static volatile bool rsp0_stop;
static uint64_t rsp0_seen[RSP0_WORKERS + 1];

/* Checks RSP0 against the running thread's own stack top; records the
 * value per thread (index 0 = thread 0). */
static void rsp0_check(int index)
{
    uint64_t want = thread_kstack_top(thread_current());
    uint64_t got = gdt_rsp0();
    rsp0_checks++;
    if (got != want || want == 0)
        rsp0_mismatches++;
    rsp0_seen[index] = got;
}

static int32_t rsp0_worker(void *arg)
{
    int index = (int)(intptr_t)arg;
    uint32_t round = 0;
    while (!rsp0_stop) {
        rsp0_check(index);
        volatile uint32_t work = 0;         /* real work: TCG delivers interrupts between blocks */
        for (uint32_t i = 0; i < 4000; i++)
            work += i;
        /* Worker 1 never yields: it runs out its slice and is preempted
         * (switches from an interrupt). Workers 2 and 3 also yield (the
         * cooperative path). */
        if (index != 1 && ++round % 8 == 0)
            thread_yield();
    }
    return 0;
}

bool usertest_rsp0(void)
{
    rsp0_checks = rsp0_mismatches = 0;
    rsp0_stop = false;
    uint64_t switches0 = thread_switches(), preempt0 = thread_preemptions();
    int32_t tid[RSP0_WORKERS];
    for (int i = 0; i < RSP0_WORKERS; i++) {
        tid[i] = thread_create("rsp0", rsp0_worker, (void *)(intptr_t)(i + 1), false);
        if (tid[i] < 0) {
            klogf("RSP0 test: FAIL (could not create worker %d)\n", i + 1);
            return false;
        }
    }
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < RSP0_TICKS) {
        rsp0_check(0);
        thread_yield();
    }
    rsp0_stop = true;
    for (int i = 0; i < RSP0_WORKERS; i++)
        thread_join(tid[i]);
    rsp0_check(0);

    unsigned distinct = 0;
    for (int i = 0; i <= RSP0_WORKERS; i++) {
        bool seen_before = false;
        for (int j = 0; j < i; j++)
            if (rsp0_seen[j] == rsp0_seen[i])
                seen_before = true;
        if (!seen_before && rsp0_seen[i])
            distinct++;
    }
    uint64_t switches = thread_switches() - switches0, preempted = thread_preemptions() - preempt0;
    bool ok = rsp0_mismatches == 0 && rsp0_checks > 0 && preempted >= 3 && distinct == RSP0_WORKERS + 1;
    klogf("RSP0 test: %u checks in %u threads after %llu switches (%llu preemptions), "
          "%u distinct RSP0 values, %u mismatches\n", (unsigned)rsp0_checks, RSP0_WORKERS + 1,
          (unsigned long long)switches, (unsigned long long)preempted, distinct,
          (unsigned)rsp0_mismatches);
    klogf("RSP0 test: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=clac ---- */

#define RFLAGS_AC 0x40000ull
#define CLAC_TICKS 10

static inline void set_ac(bool on)
{
    uint64_t f;
    __asm__ volatile ("pushfq; pop %0" : "=r"(f));
    f = on ? (f | RFLAGS_AC) : (f & ~RFLAGS_AC);
    __asm__ volatile ("push %0; popfq" : : "r"(f) : "memory", "cc");
}

bool usertest_clac(void)
{
    const struct cpuprot_status *st = cpuprot_status();
    uint64_t clac0 = cpuprot_clac_count;
    interrupts_ac_probe_start();
    set_ac(true);
    uint64_t start = timer_ticks();
    while (timer_ticks() - start < CLAC_TICKS) {
        volatile uint32_t work = 0;
        for (uint32_t i = 0; i < 4000; i++)
            work += i;
    }
    set_ac(false);
    uint32_t with_ac, without_ac;
    interrupts_ac_probe_stop(&with_ac, &without_ac);
    uint64_t clacs = cpuprot_clac_count - clac0;
    uint32_t total = with_ac + without_ac;
    bool ok = st->smap_on ? (total >= CLAC_TICKS && with_ac == 0 && clacs >= total)
                          : (total >= CLAC_TICKS && clacs == 0 && cpuprot_clac_count == 0);
    klogf("clac test: SMAP %s; %u interrupts taken with AC=1 in the interrupted code; "
          "AC=1 seen by the handler %u times; clac executed %llu times\n",
          st->smap_on ? "on" : "off", total, with_ac, (unsigned long long)clacs);
    klogf("clac test: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=uaspace (stage 3: address spaces, no ring 3) ---- */

#define UA_WORKERS     2
#define UA_ROUNDS      500                      /* in total, shared by the workers */
#define UA_TEXT_PAGES  3
#define UA_DATA_PAGES  2
#define UA_STACK_PAGES ((int)(VM_USER_STACK_SIZE / VM_PAGE_SIZE))
#define UA_PAGES       (UA_TEXT_PAGES + UA_DATA_PAGES + UA_STACK_PAGES)
#define UA_TABLES      6                        /* PDPT+PD+PT for 0x400000/0x500000, and for the stack */
#define UA_WORK        20000                    /* busy work per page, so slices run out mid-build */
#define UA_HEAP_BLOCK  (2560u * 1024)           /* > 2 MiB: the heap needs a new page table */

struct ua_worker {
    volatile uint32_t rounds, create_bad, map_bad, content_bad, pml4_bad, user_upper;
    volatile uint32_t cr3_checks, cr3_bad, teardown_bad;
    volatile uint32_t preempt_build, preempt_teardown, yields;
    volatile uint32_t pages_freed, tables_freed;
};

static struct ua_worker ua[UA_WORKERS];
static uint64_t ua_kernel_upper[VM_ENTRIES / 2];    /* the kernel PML4's entries 256-511 */

static void ua_work(uint32_t n)
{
    volatile uint32_t x = 0;                /* real work: TCG delivers interrupts between blocks */
    for (uint32_t i = 0; i < n; i++)
        x += i;
}

/* Every UA_LONG_EVERY-th round, a build and a teardown each run past a
 * whole slice in the middle (UA_LONG_TICKS > SCHED_SLICE_TICKS): the timer
 * must preempt them there, with the address space half built / half torn
 * down. The other rounds are short and switch only where they yield. */
#define UA_LONG_EVERY  50
#define UA_LONG_TICKS  7

static void ua_spin_ticks(uint64_t ticks)
{
    uint64_t t0 = timer_ticks();
    while (timer_ticks() - t0 < ticks)
        ua_work(2000);
}

static uint64_t ua_va(int p)
{
    if (p < UA_TEXT_PAGES)
        return VM_USER_TEXT + (uint64_t)p * VM_PAGE_SIZE;
    if (p < UA_TEXT_PAGES + UA_DATA_PAGES)
        return VM_USER_DATA + (uint64_t)(p - UA_TEXT_PAGES) * VM_PAGE_SIZE;
    return VM_USER_STACK_TOP - VM_USER_STACK_SIZE +
           (uint64_t)(p - UA_TEXT_PAGES - UA_DATA_PAGES) * VM_PAGE_SIZE;
}

/* Code pages read + execute; data and stack read + write (+ no-execute). */
static uint64_t ua_flags(int p)
{
    return p < UA_TEXT_PAGES ? 0 : (PTE_WRITE | paging_nx_flag());
}

static uint64_t ua_pattern(int w, uint32_t r, int p, uint32_t i)
{
    return ((uint64_t)(w + 1) << 56) ^ ((uint64_t)r << 32) ^ ((uint64_t)p << 20) ^ i;
}

static uint64_t ua_read_cr3(void)
{
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v & PTE_ADDR_MASK;
}

static void ua_cr3_check(struct ua_worker *s, const struct uspace *as)
{
    s->cr3_checks++;
    if (!uspace_is_current(as))
        s->cr3_bad++;
}

/* The loaded PML4, through the recursive mapping: its kernel half equal to
 * the kernel's (entry 510: its own frame), and lower-half entries only
 * where allowed (none, or slots 0 and 255 once pages are mapped). */
static bool ua_pml4_ok(const struct uspace *as, bool built)
{
    const volatile uint64_t *p = (const volatile uint64_t *)(uintptr_t)VM_PML4;
    for (unsigned i = 0; i < VM_ENTRIES / 2; i++) {
        bool allowed = built && (i == 0 || i == 255);
        if (pte_present(p[i]) != allowed)
            return false;
    }
    for (unsigned i = VM_ENTRIES / 2; i < VM_ENTRIES; i++) {
        if (i == VM_RECURSIVE_INDEX) {
            if (!pte_present(p[i]) || pte_frame(p[i]) != as->pml4)
                return false;
        } else if (p[i] != ua_kernel_upper[i - VM_ENTRIES / 2]) {
            return false;
        }
    }
    return true;
}

static int32_t ua_worker_fn(void *arg)
{
    int w = (int)(intptr_t)arg;
    struct ua_worker *s = &ua[w];
    int32_t me = thread_current();
    uint32_t rounds = UA_ROUNDS / UA_WORKERS;
    for (uint32_t r = 0; r < rounds; r++) {
        struct uspace as;
        if (!uspace_create(&as)) {
            s->create_bad++;
            break;
        }
        uspace_enter(&as);
        ua_cr3_check(s, &as);
        if (!ua_pml4_ok(&as, false))
            s->pml4_bad++;

        /* Build, with work and one yield between pages: other threads (and
         * their address spaces) run in the middle of it. */
        uint64_t p0 = thread_preempted(me);
        uint64_t frames[UA_PAGES];
        for (int p = 0; p < UA_PAGES; p++) {
            frames[p] = 0;
            if (uspace_map_page(&as, ua_va(p), 0, 0, ua_flags(p), &frames[p]) != UVM_OK) {
                s->map_bad++;
                continue;
            }
            volatile uint64_t *pw = vmm_window_map(frames[p]);
            for (uint32_t i = 0; i < VM_PAGE_SIZE / 8; i++)
                pw[i] = ua_pattern(w, r, p, i);
            vmm_window_unmap();
            ua_work(UA_WORK);
            if (p == 3 && r % UA_LONG_EVERY == 0)
                ua_spin_ticks(UA_LONG_TICKS);
            if (p == UA_PAGES / 2) {
                thread_yield();
                s->yields++;
            }
            ua_cr3_check(s, &as);
        }
        if (thread_preempted(me) != p0)
            s->preempt_build++;

        /* Check through the recursive mapping: frame, user bit and
         * permissions per page; the contents through the window. */
        for (int p = 0; p < UA_PAGES; p++) {
            uint64_t phys = 0, fl = 0;
            bool want_w = ua_flags(p) & PTE_WRITE, want_nx = ua_flags(p) & PTE_NX;
            if (!uvm_translate(&as.uvm, ua_va(p), &phys, &fl) || phys != frames[p] ||
                !(fl & PTE_USER) || !!(fl & PTE_WRITE) != want_w || !!(fl & PTE_NX) != want_nx) {
                s->map_bad++;
                continue;
            }
            const volatile uint64_t *pw = vmm_window_map(frames[p]);
            for (uint32_t i = 0; i < VM_PAGE_SIZE / 8; i++)
                if (pw[i] != ua_pattern(w, r, p, i)) {
                    s->content_bad++;
                    break;
                }
            vmm_window_unmap();
        }
        if (!ua_pml4_ok(&as, true))
            s->pml4_bad++;
        if (uvm_upper_user_entries(paging_recursive_ops(), as.pml4) != 0)
            s->user_upper++;

        /* Tear down. Every other round page by page first, with work and a
         * yield between pages (switches while it is half torn down), then
         * the bottom-up teardown of what is left. */
        uint64_t p1 = thread_preempted(me);
        bool by_page = (r % 2) == 0;
        if (by_page) {
            for (int p = 0; p < UA_PAGES; p++) {
                uint64_t phys = 0;
                if (uvm_unmap(&as.uvm, ua_va(p), &phys) != UVM_OK || phys != frames[p]) {
                    s->teardown_bad++;
                    continue;
                }
                memory_free_page((uint32_t)phys);
                s->pages_freed++;
                ua_work(UA_WORK);
                if (p == 3 && r % UA_LONG_EVERY == 0)     /* r even: a by-page round */
                    ua_spin_ticks(UA_LONG_TICKS);
                if (p == UA_PAGES / 2) {
                    thread_yield();
                    s->yields++;
                }
                ua_cr3_check(s, &as);
            }
        }
        struct uvm_teardown_stats st = { 0, 0, 0 };
        uspace_destroy(&as, &st);
        if (st.pages != (by_page ? 0u : (uint32_t)UA_PAGES) || st.tables != UA_TABLES ||
            st.bad_entries != 0)
            s->teardown_bad++;
        s->pages_freed += st.pages;
        s->tables_freed += st.tables;
        if (thread_preempted(me) != p1)
            s->preempt_teardown++;
        if (ua_read_cr3() != paging_kernel_cr3())
            s->cr3_bad++;
        s->rounds++;
    }
    return 0;
}

/* Heap growth under a process CR3: the new heap page table goes into the
 * kernel half's shared tables, so the block is there under the kernel's
 * CR3 too, and no PML4 entry changes (the freeze). */
static bool ua_heap_step(char *detail, size_t size)
{
    struct uspace as;
    if (!uspace_create(&as)) {
        ksnprintf(detail, size, "no frame for a PML4");
        return false;
    }
    uint32_t arena0 = kheap_stats()->arena_bytes, tables0 = paging_stats()->page_tables;
    uspace_enter(&as);
    uint8_t *blk = kmalloc(UA_HEAP_BLOCK);
    if (blk)
        for (uint32_t i = 0; i < UA_HEAP_BLOCK; i++)
            blk[i] = (uint8_t)(i * 7u ^ 0x5Au);
    bool in_process = uspace_is_current(&as);
    struct uvm_teardown_stats st;
    uspace_destroy(&as, &st);                   /* back under the kernel's CR3 */
    uint32_t bad = 0;
    if (blk)
        for (uint32_t i = 0; i < UA_HEAP_BLOCK; i++)
            if (blk[i] != (uint8_t)(i * 7u ^ 0x5Au))
                bad++;
    uint32_t arena1 = kheap_stats()->arena_bytes, tables1 = paging_stats()->page_tables;
    const volatile uint64_t *k = (const volatile uint64_t *)(uintptr_t)VM_PML4;
    unsigned upper_changed = 0;
    for (unsigned i = VM_ENTRIES / 2; i < VM_ENTRIES; i++)
        if (i != VM_RECURSIVE_INDEX && k[i] != ua_kernel_upper[i - VM_ENTRIES / 2])
            upper_changed++;
    kfree(blk);
    ksnprintf(detail, size, "heap grew by %u KiB and %u page table(s) under a process CR3; "
              "the block read back under the kernel CR3 with %u wrong bytes; "
              "kernel PML4 entries changed: %u", (arena1 - arena0) / 1024, tables1 - tables0, bad,
              upper_changed);
    return blk && in_process && bad == 0 && arena1 > arena0 && tables1 > tables0 &&
           upper_changed == 0 && st.pages == 0 && st.tables == 0;
}

bool usertest_uaspace(void)
{
    char d[200];
    bool ok = true;
    const volatile uint64_t *k = (const volatile uint64_t *)(uintptr_t)VM_PML4;
    for (unsigned i = 0; i < VM_ENTRIES / 2; i++)
        ua_kernel_upper[i] = k[VM_ENTRIES / 2 + i];

    bool heap_ok = ua_heap_step(d, sizeof(d));
    klogf("Address-space test: %s: %s\n", d, heap_ok ? "OK" : "MISMATCH");
    ok = ok && heap_ok;

    /* The baseline after the heap step (the heap never shrinks). */
    uint32_t free0 = pmm_free_pages(), tables0 = paging_stats()->page_tables;
    uint32_t stacks0 = thread_stacks_in_use(), live0 = thread_live();
    for (int i = 0; i < UA_WORKERS; i++) {
        struct ua_worker zero = { 0 };
        ua[i] = zero;
    }
    uint64_t pre0 = thread_preemptions();
    int32_t tid[UA_WORKERS];
    for (int i = 0; i < UA_WORKERS; i++)
        tid[i] = thread_create("uaspace", ua_worker_fn, (void *)(intptr_t)i, false);
    for (int i = 0; i < UA_WORKERS; i++)
        if (tid[i] > 0)
            thread_join(tid[i]);
        else
            ok = false;

    struct ua_worker t = { 0 };
    for (int i = 0; i < UA_WORKERS; i++) {
        t.rounds += ua[i].rounds;
        t.create_bad += ua[i].create_bad;
        t.map_bad += ua[i].map_bad;
        t.content_bad += ua[i].content_bad;
        t.pml4_bad += ua[i].pml4_bad;
        t.user_upper += ua[i].user_upper;
        t.cr3_checks += ua[i].cr3_checks;
        t.cr3_bad += ua[i].cr3_bad;
        t.teardown_bad += ua[i].teardown_bad;
        t.preempt_build += ua[i].preempt_build;
        t.preempt_teardown += ua[i].preempt_teardown;
        t.yields += ua[i].yields;
        t.pages_freed += ua[i].pages_freed;
        t.tables_freed += ua[i].tables_freed;
    }
    bool line_ok;

    line_ok = t.rounds == UA_ROUNDS && t.create_bad == 0 && t.map_bad == 0 && t.content_bad == 0;
    klogf("Address-space test: %u address spaces built, entered and checked by %d threads "
          "(%d user pages, %d lower-half tables and a PML4 each); %u mapping and %u content "
          "mismatches: %s\n", (unsigned)t.rounds, UA_WORKERS, UA_PAGES, UA_TABLES,
          (unsigned)t.map_bad, (unsigned)t.content_bad, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    line_ok = t.pml4_bad == 0 && t.user_upper == 0;
    klogf("Address-space test: kernel half identical in every PML4, slot 510 its own: "
          "%u wrong; upper-half entries with the user bit: %u (%u walks): %s\n",
          (unsigned)t.pml4_bad, (unsigned)t.user_upper, (unsigned)t.rounds, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    line_ok = t.cr3_bad == 0 && t.preempt_build > 0 && t.preempt_teardown > 0 && t.yields > 0;
    klogf("Address-space test: switches in the middle: %u builds and %u teardowns preempted, "
          "%u yields; CR3 right after every switch: %u checks, %u wrong (%llu preemptions): %s\n",
          (unsigned)t.preempt_build, (unsigned)t.preempt_teardown, (unsigned)t.yields,
          (unsigned)t.cr3_checks, (unsigned)t.cr3_bad,
          (unsigned long long)(thread_preemptions() - pre0), line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    uint32_t free1 = pmm_free_pages(), tables1 = paging_stats()->page_tables;
    uint32_t stacks1 = thread_stacks_in_use(), live1 = thread_live();
    line_ok = t.teardown_bad == 0 && t.pages_freed == UA_ROUNDS * UA_PAGES &&
              t.tables_freed == UA_ROUNDS * UA_TABLES && free1 == free0 && tables1 == tables0 &&
              stacks1 == stacks0 && live1 == live0;
    klogf("Address-space test: torn down bottom-up: %u user frames and %u lower-half tables "
          "freed, %u wrong; free pages %u -> %u, kernel page tables %u -> %u, stack slots %u, "
          "live threads %u: %s\n", (unsigned)t.pages_freed, (unsigned)t.tables_freed,
          (unsigned)t.teardown_bad, free0, free1, tables0, tables1, stacks1, live1,
          line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    klogf("Address-space test: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=userspin (stage 4a: the first program in ring 3) ---- */

#define US_TICKS       100          /* observed: about 20 slices of spin */
#define US_MIN_IRQS    10
#define US_MIN_PREEMPT 5
#define US_MAX_TURN    6            /* the plan's bound for a user process's longest turn */

static struct uspace us_as;         /* static: the spin thread never returns */
static struct user_iret us_frame;
static struct process_load_stats us_load;
static volatile int us_state;       /* 0 not yet, 1 entering ring 3, -1 failed */

static int32_t us_spin_thread(void *arg)
{
    const struct userimg *img = arg;
    if (!uspace_create(&us_as)) {
        us_state = -1;
        return 1;
    }
    uspace_enter(&us_as);
    if (!process_load(&us_as, img, &us_load)) {
        struct uvm_teardown_stats st;
        uspace_destroy(&us_as, &st);
        us_state = -1;
        return 1;
    }
    user_iret_build(&us_frame, img);
    us_state = 1;
    enter_user(&us_frame, 0);           /* arg 0: spin's pure loop, no system calls */
}

bool usertest_userspin(void)
{
    bool ok = true, line_ok;
    uint64_t bad = 0;
    enum userimg_result tr = userimg_check_table(user_images, user_images_count, &bad);
    const struct userimg *img = userimg_find(user_images, user_images_count, "spin");
    line_ok = tr == USERIMG_OK && img;
    klogf("User-mode test: %llu built-in images checked: %s; 'spin': %llu bytes of code at "
          "0x%016llx, entry 0x%016llx, %llu bytes of data and bss: %s\n",
          (unsigned long long)user_images_count, userimg_result_str(tr),
          img ? (unsigned long long)img->text_size : 0ull, (unsigned long long)VM_USER_TEXT,
          img ? (unsigned long long)img->entry : 0ull, img ? (unsigned long long)img->data_mem : 0ull,
          line_ok ? "OK" : "MISMATCH");
    if (!line_ok) {
        klogf("User-mode test: FAIL\n");
        return false;
    }

    thread_turn_reset();
    uint64_t switches0 = thread_switches();
    interrupts_user_probe_start();
    int32_t tid = thread_create("spin", us_spin_thread, (void *)img, true);
    if (tid < 0) {
        interrupts_user_probe_stop(&(struct user_irq_probe){ 0 });
        klogf("User-mode test: FAIL (could not create the spin thread)\n");
        return false;
    }
    thread_sleep(US_TICKS);             /* thread 0 polls: spin runs, and is preempted */
    struct user_irq_probe p;
    interrupts_user_probe_stop(&p);
    uint64_t switches = thread_switches() - switches0, turn = thread_turn_max();

    line_ok = us_state == 1 && us_load.text == vm_ceil(img->text_size) / VM_PAGE_SIZE &&
              us_load.data == vm_ceil(img->data_mem) / VM_PAGE_SIZE &&
              us_load.stack == VM_USER_STACK_SIZE / VM_PAGE_SIZE;
    klogf("User-mode test: thread %d 'spin' loaded the image into its own address space: "
          "%u code, %u data and %u stack pages: %s\n", (int)tid, us_load.text, us_load.data,
          us_load.stack, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    line_ok = us_state == 1 && user_iret_ok(&us_frame, img);
    klogf("User-mode test: IRETQ frame RIP=0x%016llx CS=0x%04llx RFLAGS=0x%016llx "
          "RSP=0x%016llx SS=0x%04llx (IF=%u, IOPL=%u, RIP inside the code): %s\n",
          (unsigned long long)us_frame.rip, (unsigned long long)us_frame.cs,
          (unsigned long long)us_frame.rflags, (unsigned long long)us_frame.rsp,
          (unsigned long long)us_frame.ss, (unsigned)((us_frame.rflags >> 9) & 1),
          (unsigned)((us_frame.rflags >> 12) & 3), line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    const struct interrupt_frame *f = &p.first;
    line_ok = p.irqs > 0 && p.tid == tid && f->cs == GDT_USER_CODE_SEL && f->ss == GDT_USER_DATA_SEL &&
              user_rip_in_code(f->rip, img) && user_rsp_in_stack(f->rsp) &&
              p.first_at + sizeof(*f) == p.first_rsp0 && p.first_rsp0 == p.first_top;
    klogf("User-mode test: first timer interrupt from ring 3, in thread %d: CS=0x%04llx "
          "RIP=0x%016llx RSP=0x%016llx SS=0x%04llx RFLAGS=0x%016llx; frame at 0x%016llx = "
          "RSP0 0x%016llx - %u, RSP0 = the thread's stack top 0x%016llx: %s\n", (int)p.tid,
          (unsigned long long)f->cs, (unsigned long long)f->rip, (unsigned long long)f->rsp,
          (unsigned long long)f->ss, (unsigned long long)f->rflags,
          (unsigned long long)p.first_at, (unsigned long long)p.first_rsp0,
          (unsigned)sizeof(*f), (unsigned long long)p.first_top, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    line_ok = p.irqs >= US_MIN_IRQS && p.cs_bad == 0 && p.ss_bad == 0 && p.if_clear == 0 &&
              p.iopl_set == 0 && p.stack_bad == 0 && p.other_thread == 0 &&
              user_rip_in_code(p.rip_min, img) && user_rip_in_code(p.rip_max, img) &&
              user_rsp_in_stack(p.rsp_min) && user_rsp_in_stack(p.rsp_max);
    klogf("User-mode test: %u timer interrupts from ring 3: %u with CS not 0x3B, %u with SS "
          "not 0x33, %u with IF clear, %u with IOPL set, %u not on the thread's RSP0 stack, %u "
          "in another thread; RIP 0x%llx-0x%llx (inside the code), RSP 0x%llx-0x%llx (inside "
          "the stack): %s\n", p.irqs, p.cs_bad, p.ss_bad, p.if_clear, p.iopl_set, p.stack_bad,
          p.other_thread, (unsigned long long)p.rip_min, (unsigned long long)p.rip_max,
          (unsigned long long)p.rsp_min, (unsigned long long)p.rsp_max, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    line_ok = p.preempted >= US_MIN_PREEMPT && p.after_preempt > 0 && thread_preempted(tid) >= p.preempted &&
              turn <= US_MAX_TURN;
    klogf("User-mode test: spin preempted %u times from ring 3 and back in ring 3 after a "
          "preemption %u times; %llu switches, %llu ticks run by spin; longest turn %llu "
          "ticks (slice %u, at most %u): %s\n", p.preempted, p.after_preempt,
          (unsigned long long)switches, (unsigned long long)thread_ticks_run(tid),
          (unsigned long long)turn, (unsigned)SCHED_SLICE_TICKS, (unsigned)US_MAX_TURN,
          line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    klogf("User-mode test: spin keeps running in ring 3 (no exit or kill before stage 4b)\n");
    klogf("User-mode test: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=userkill (stage 4b: the first process teardown) ---- */

#define UK_ROUNDS   20              /* two spin processes each; the first killed, then the second */
#define UK_EARLY    20              /* killed right after process_start */
#define UK_WAIT     200             /* ticks allowed for any one step */

/* Thread 0 waits (polling, so the processes run) until `pid` has returned
 * to ring 3 more than `after` times. false: not within UK_WAIT ticks. */
static bool uk_wait_returns(int32_t pid, uint64_t after)
{
    uint64_t t0 = timer_ticks();
    while (process_user_returns(pid) <= after) {
        if (timer_ticks() - t0 > UK_WAIT)
            return false;
        thread_sleep(1);
    }
    return true;
}

static bool uk_killed(enum proc_status st, const struct proc_result *res)
{
    return st == PROC_OK && res->end == PROC_END_KILLED && res->code == 0;
}

bool usertest_userkill(void)
{
    bool ok = true, line_ok;
    const struct userimg *img = userimg_find(user_images, user_images_count, "spin");
    if (!img) {
        klogf("User kill test: FAIL (no image 'spin')\n");
        return false;
    }
    uint32_t free0 = pmm_free_pages(), tables0 = paging_stats()->page_tables;
    uint32_t stacks0 = thread_stacks_in_use(), live0 = thread_live();
    struct process_teardown_totals t0, t1;
    process_teardown_totals(&t0);
    thread_turn_reset();

    uint32_t started = 0, start_bad = 0, pid_bad = 0, ran = 0, killed = 0, wrong = 0;
    uint32_t other_ran = 0, early_killed = 0, early_wrong = 0, early_before = 0;
    int32_t last = 0, first = 0;
    for (int r = 0; r < UK_ROUNDS; r++) {
        int32_t pid[2];
        for (int i = 0; i < 2; i++) {
            pid[i] = process_start("spin", 0);
            if (pid[i] <= 0) {
                start_bad++;
                continue;
            }
            started++;
            if (!first)
                first = pid[i];
            if (last && pid[i] != last + 1)   /* consecutive (boot self-test: 1-3) */
                pid_bad++;
            last = pid[i];
        }
        if (pid[0] <= 0 || pid[1] <= 0)
            break;
        /* Both have run in ring 3 (an interrupt came back to it). */
        for (int i = 0; i < 2; i++)
            if (uk_wait_returns(pid[i], 0))
                ran++;
        /* The first ends at its next return to ring 3 ... */
        struct proc_result res;
        process_kill(pid[0]);
        enum proc_status st = process_wait(pid[0], &res);
        if (uk_killed(st, &res))
            killed++;
        else
            wrong++;
        /* ... the second keeps running there ... */
        if (uk_wait_returns(pid[1], process_user_returns(pid[1])))
            other_ran++;
        /* ... until it is killed too. */
        process_kill(pid[1]);
        st = process_wait(pid[1], &res);
        if (uk_killed(st, &res))
            killed++;
        else
            wrong++;
    }

    /* Kills requested at once: the thread has not run yet (or only just),
     * so most end before ever entering ring 3. */
    for (int r = 0; r < UK_EARLY; r++) {
        int32_t pid = process_start("spin", 0);
        if (pid <= 0) {
            start_bad++;
            continue;
        }
        started++;
        if (pid != last + 1)
            pid_bad++;
        last = pid;
        process_kill(pid);
        if (process_user_returns(pid) == 0)
            early_before++;
        struct proc_result res;
        if (uk_killed(process_wait(pid, &res), &res))
            early_killed++;
        else
            early_wrong++;
    }
    process_teardown_totals(&t1);

    line_ok = start_bad == 0 && pid_bad == 0 && first > 0 && ran == 2 * UK_ROUNDS &&
              killed == 2 * UK_ROUNDS && wrong == 0;
    klogf("User kill test: %d rounds of two spin processes: %u started (PIDs %d-%d, one "
          "more each time, none reused), %u had returned to ring 3 before the kill; ended at "
          "a return to ring 3: %u results 'killed' (code 0), %u wrong: %s\n", UK_ROUNDS, started,
          (int)first, (int)last, ran, killed, wrong, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    line_ok = other_ran == UK_ROUNDS;
    klogf("User kill test: the second process kept running in ring 3 after the first one "
          "ended: %u of %d rounds: %s\n", other_ran, UK_ROUNDS, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    line_ok = early_killed == UK_EARLY && early_wrong == 0;
    klogf("User kill test: %d processes killed right after they were started (%u before "
          "entering ring 3): %u results 'killed', %u wrong: %s\n", UK_EARLY, early_before,
          early_killed, early_wrong, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    uint32_t procs = t1.processes - t0.processes, pages = t1.pages - t0.pages;
    uint32_t tables = t1.tables - t0.tables, bad = t1.bad_entries - t0.bad_entries;
    uint32_t free1 = pmm_free_pages(), tables1 = paging_stats()->page_tables;
    uint32_t stacks1 = thread_stacks_in_use(), live1 = thread_live(), left = process_live();
    uint32_t n = 2 * UK_ROUNDS + UK_EARLY;
    line_ok = procs == n && pages == n * userimg_pages(img) && tables == n * UA_TABLES && bad == 0 &&
              free1 == free0 && tables1 == tables0 && stacks1 == stacks0 && live1 == live0 && left == 0;
    klogf("User kill test: %u address spaces torn down by their own threads: %u user frames "
          "and %u lower-half tables freed, %u wrong; free pages %u -> %u, kernel page tables "
          "%u -> %u, stack slots %u -> %u, live threads %u -> %u, processes left %u: %s\n",
          procs, pages, tables, bad, free0, free1, tables0, tables1, stacks0, stacks1, live0,
          live1, left, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    uint64_t turn = thread_turn_max();
    line_ok = turn <= US_MAX_TURN;
    klogf("User kill test: longest turn %llu ticks (slice %u, at most %u): %s\n",
          (unsigned long long)turn, (unsigned)SCHED_SLICE_TICKS, (unsigned)US_MAX_TURN,
          line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    klogf("User kill test: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=userfault (stage 4c: fault containment, part 1) ---- */

enum uf_where { UF_CODE, UF_DATA };

/* Where a page fault's address (CR2) must be. */
enum uf_addr {
    UF_ADDR_NONE,                   /* not a page fault */
    UF_ADDR_ZERO,                   /* address 0 */
    UF_ADDR_BELOW_STACK,            /* the page just below the 32 KiB stack */
    UF_ADDR_CODE,                   /* inside the program's code */
    UF_ADDR_RIP_DATA,               /* = RIP, in the first data page */
    UF_ADDR_EXACT,                  /* = the case's `at` */
};

struct uf_case {
    const char *image;
    uint64_t arg;
    const char *what;
    uint32_t vector;                /* 14 page fault, 6 invalid opcode, 13 #GP, 7 #NM */
    uint32_t err;                   /* page faults: the error code's low 5 bits; else all of it */
    enum uf_where rip_in;
    enum uf_addr addr;
    bool needs_nx;
    uint64_t at;                    /* UF_ADDR_EXACT */
    struct faultop op;              /* the instruction it must fault on (its first bytes) */
};

/* Expected opcodes: fixed by the instruction (cli fa, ud2 0f 0b, ...), or
 * for the memory accesses the compiled code's instruction, checked against
 * the programs' disassembly (the build is reproducible). */
#define OP1(a)          { 1, { a, 0, 0, 0 } }
#define OP2(a, b)       { 2, { a, b, 0, 0 } }
#define OP3(a, b, c)    { 3, { a, b, c, 0 } }
#define OP4(a, b, c, d) { 4, { a, b, c, d } }

static const struct uf_case uf_cases[] = {
    { "nullptr",   0, "read of address 0",          14, PF_USER,
      UF_CODE, UF_ADDR_ZERO, false, 0, OP4(0x48, 0x8b, 0x04, 0x25) },
    { "ud2",       0, "invalid instruction",         6, 0,
      UF_CODE, UF_ADDR_NONE, false, 0, OP2(0x0f, 0x0b) },
    { "stackover", 0, "user stack past its limit",  14, PF_USER | PF_WRITE,
      UF_CODE, UF_ADDR_BELOW_STACK, false, 0, OP4(0x48, 0x89, 0x3c, 0x24) },
    { "wx",        0, "write to its own code",      14, PF_USER | PF_WRITE | PF_PRESENT,
      UF_CODE, UF_ADDR_CODE, false, 0, OP2(0xc6, 0x05) },
    { "wx",        1, "execute from its own data",  14, PF_USER | PF_FETCH | PF_PRESENT,
      UF_DATA, UF_ADDR_RIP_DATA, true, 0, OP1(0xc3) },
};
#define UF_CASES (int)(sizeof(uf_cases) / sizeof(uf_cases[0]))

static bool uf_cr2_ok(const struct uf_case *c, const struct process_fault *f, const struct userimg *img)
{
    uint64_t stack_bottom = VM_USER_STACK_TOP - VM_USER_STACK_SIZE;
    switch (c->addr) {
    case UF_ADDR_NONE:        return true;
    case UF_ADDR_ZERO:        return f->cr2 == 0;
    case UF_ADDR_BELOW_STACK: return f->cr2 < stack_bottom && f->cr2 >= stack_bottom - VM_PAGE_SIZE;
    case UF_ADDR_CODE:        return user_rip_in_code(f->cr2, img);
    case UF_ADDR_RIP_DATA:    return f->cr2 == f->rip && f->cr2 >= VM_USER_DATA &&
                                     f->cr2 < VM_USER_DATA + VM_PAGE_SIZE;
    case UF_ADDR_EXACT:       return f->cr2 == c->at;
    }
    return false;
}

/* Runs each case as a process while a spin process keeps running, then
 * checks that everything was given back. t: the log lines' prefix.
 * extra (optional): one more check, logged before the verdict. */
static bool uf_run(const char *t, const struct uf_case *cases, int count, bool (*extra)(const char *t))
{
    bool ok = true, line_ok;
    uint32_t free0 = pmm_free_pages(), tables0 = paging_stats()->page_tables;
    uint32_t stacks0 = thread_stacks_in_use(), live0 = thread_live();
    struct process_teardown_totals t0, t1;
    process_teardown_totals(&t0);

    /* A process that keeps running in ring 3 through all the faults. */
    int32_t spin = process_start("spin", 0);
    if (spin <= 0 || !uk_wait_returns(spin, 0)) {
        klogf("%s: FAIL (spin did not start)\n", t);
        return false;
    }

    uint32_t ended = 0;
    for (int i = 0; i < count; i++) {
        const struct uf_case *c = &cases[i];
        const struct userimg *img = userimg_find(user_images, user_images_count, c->image);
        if (c->needs_nx && !paging_nx_flag()) {
            klogf("%s: %s %llu (%s): NOT APPLICABLE (no NX on this CPU)\n",
                  t, c->image, (unsigned long long)c->arg, c->what);
            continue;
        }
        int32_t pid = process_start(c->image, c->arg);
        struct proc_result res = { PROC_END_EXIT, -1 };
        enum proc_status st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
        struct process_fault f;
        process_last_fault(&f);
        bool rip_ok = c->rip_in == UF_CODE ? (img && user_rip_in_code(f.rip, img))
                                           : (f.rip >= VM_USER_DATA && f.rip < VM_USER_DATA + VM_PAGE_SIZE);
        line_ok = img && pid > 0 && st == PROC_OK && res.end == PROC_END_FAULT &&
                  res.code == (int32_t)c->vector && f.pid == pid && f.vector == c->vector && rip_ok &&
                  (c->vector == 14 ? (f.error_code & 0x1F) == c->err && uf_cr2_ok(c, &f, img)
                                   : f.error_code == c->err) &&
                  faultop_match(&f.op, &c->op);     /* the fault came from that instruction */
        if (line_ok)
            ended++;
        char got[16], want[16];
        struct faultop shown = f.op;        /* as many bytes as expected, for the log */
        if (shown.n > c->op.n)
            shown.n = c->op.n;
        klogf("%s: %s %llu (%s): process %d %s: %s at RIP 0x%016llx, instruction %s (expected "
              "%s), address 0x%016llx, error code 0x%llx (result code %d): %s\n", t, c->image,
              (unsigned long long)c->arg, c->what, (int)pid, proc_end_str(res.end),
              exception_mnemonic(f.vector), (unsigned long long)f.rip, faultop_str(&shown, got),
              faultop_str(&c->op, want), (unsigned long long)f.cr2,
              (unsigned long long)f.error_code, (int)res.code, line_ok ? "OK" : "MISMATCH");
        ok = ok && line_ok;
    }

    /* spin still runs in ring 3 after all of them; then it is killed. */
    bool spin_ran = uk_wait_returns(spin, process_user_returns(spin));
    process_kill(spin);
    struct proc_result sres;
    enum proc_status sst = process_wait(spin, &sres);
    line_ok = spin_ran && uk_killed(sst, &sres);
    klogf("%s: spin kept running in ring 3 through the faults, then was killed: %s\n", t,
          line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    process_teardown_totals(&t1);
    uint32_t procs = t1.processes - t0.processes, bad = t1.bad_entries - t0.bad_entries;
    uint32_t free1 = pmm_free_pages(), tables1 = paging_stats()->page_tables;
    uint32_t stacks1 = thread_stacks_in_use(), live1 = thread_live(), left = process_live();
    line_ok = procs == ended + 1 && bad == 0 && free1 == free0 && tables1 == tables0 &&
              stacks1 == stacks0 && live1 == live0 && left == 0;
    klogf("%s: %u address spaces torn down by their own threads, %u wrong; free "
          "pages %u -> %u, kernel page tables %u -> %u, stack slots %u -> %u, live threads %u "
          "-> %u, processes left %u: %s\n", t, procs, bad, free0, free1, tables0, tables1, stacks0,
          stacks1, live0, live1, left, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;
    if (extra && !extra(t))
        ok = false;

    klogf("%s: %s\n", t, ok ? "PASS" : "FAIL");
    return ok;
}

bool usertest_userfault(void)
{
    return uf_run("User fault test", uf_cases, UF_CASES, 0);
}

/* ---- test=userpriv (stage 4d: fault containment, part 2) ---- */

/* An INT n gate that is not open to ring 3 gives #GP with the gate in the
 * error code: index * 8 + 2 (the IDT bit). */
#define UP_GATE(n) ((uint32_t)(n) * 8u + 2u)

static const struct uf_case up_cases[] = {
    { "privileged", 0, "cli",                      13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP1(0xfa) },
    { "privileged", 1, "hlt",                      13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP1(0xf4) },
    { "privileged", 2, "out to port 0x80",         13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP2(0xe6, 0x80) },
    { "privileged", 3, "in from port 0x60",        13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP2(0xe4, 0x60) },
    { "privileged", 4, "mov from cr3",             13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP3(0x0f, 0x20, 0xd8) },
    { "privileged", 5, "lgdt",                     13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP3(0x0f, 0x01, 0x15) },
    { "privileged", 6, "wrmsr",                    13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP2(0x0f, 0x30) },
    { "privileged", 7, "rdmsr",                    13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP2(0x0f, 0x32) },
    { "privileged", 8, "invlpg",                   13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP3(0x0f, 0x01, 0x38) },
    { "int80",      1, "int3 (gate 3, ring 0)",    13, UP_GATE(3), UF_CODE, UF_ADDR_NONE, false, 0, OP1(0xcc) },
    { "int80",      0, "int 0x80 (gate, ring 0)",  13, UP_GATE(0x80), UF_CODE, UF_ADDR_NONE, false, 0, OP2(0xcd, 0x80) },
    { "int80",      2, "int 0x20 (timer gate)",    13, UP_GATE(0x20), UF_CODE, UF_ADDR_NONE, false, 0, OP2(0xcd, 0x20) },
    { "fpu",        0, "x87 fld1, CR0.EM set",      7, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP2(0xd9, 0xe8) },
    { "fpu",        1, "MMX pxor, CR0.EM set",      6, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP3(0x0f, 0xef, 0xc0) },
    { "fpu",        2, "SSE xorps, CR0.EM set",     6, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP3(0x0f, 0x57, 0xc0) },
};
#define UP_CASES (int)(sizeof(up_cases) / sizeof(up_cases[0]))

bool usertest_userpriv(void)
{
    return uf_run("User privilege test", up_cases, UP_CASES, 0);
}

/* ---- test=userkmem (stage 4e: fault containment, part 3) ---- */

/* The kernel addresses kread and kwrite use (user/ulib.h): the kernel
 * image's first page (code), the kernel heap's first block header (data)
 * and the PML4 through the recursive slot. Each is mapped, but for the
 * kernel only: a protection violation from user mode. */
#define UKM_TEXT 0xFFFFFFFF80100000ull
#define UKM_HEAP VM_HEAP_BASE
#define UKM_PML4 VM_PML4
#define UKM_READ  (PF_PRESENT | PF_USER)
#define UKM_WRITE (PF_PRESENT | PF_USER | PF_WRITE)

static const struct uf_case ukm_cases[] = {
    { "privileged", 9, "sti",                      13, 0, UF_CODE, UF_ADDR_NONE, false, 0, OP1(0xfb) },
    { "int80",      3, "int 0x0e (gate, ring 0)",  13, UP_GATE(0x0E), UF_CODE, UF_ADDR_NONE, false, 0, OP2(0xcd, 0x0e) },
    { "kread",      0, "read kernel text",         14, UKM_READ,  UF_CODE, UF_ADDR_EXACT, false, UKM_TEXT, OP3(0x48, 0x8b, 0x00) },
    { "kread",      2, "read kernel data (heap)",  14, UKM_READ,  UF_CODE, UF_ADDR_EXACT, false, UKM_HEAP, OP3(0x48, 0x8b, 0x00) },
    { "kread",      1, "read the PML4 (recursive)", 14, UKM_READ, UF_CODE, UF_ADDR_EXACT, false, UKM_PML4, OP3(0x48, 0x8b, 0x00) },
    { "kwrite",     0, "write kernel text",        14, UKM_WRITE, UF_CODE, UF_ADDR_EXACT, false, UKM_TEXT, OP3(0x48, 0x89, 0x08) },
    { "kwrite",     2, "write kernel data (heap)", 14, UKM_WRITE, UF_CODE, UF_ADDR_EXACT, false, UKM_HEAP, OP3(0x48, 0x89, 0x08) },
    { "kwrite",     1, "write the PML4 (recursive)", 14, UKM_WRITE, UF_CODE, UF_ADDR_EXACT, false, UKM_PML4, OP3(0x48, 0x89, 0x08) },
};
#define UKM_CASES (int)(sizeof(ukm_cases) / sizeof(ukm_cases[0]))

extern const char __text_start[], __rodata_end[];

/* FNV-1a, 64 bits. */
static uint64_t ukm_hash(uint64_t h, const volatile uint8_t *p, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

/* Kernel text and read-only data, the kernel PML4 (thread 0 runs on the
 * kernel's CR3) and the heap's first block header: what kwrite aims at,
 * or could reach. None of it changes while the test runs. */
static uint64_t ukm_sum(void)
{
    uint64_t h = 0xCBF29CE484222325ull;
    h = ukm_hash(h, (const volatile uint8_t *)__text_start, (uint64_t)(__rodata_end - __text_start));
    h = ukm_hash(h, (const volatile uint8_t *)(uintptr_t)UKM_PML4, VM_PAGE_SIZE);
    h = ukm_hash(h, (const volatile uint8_t *)(uintptr_t)UKM_HEAP, 16);
    return h;
}

static uint64_t ukm_before;

/* kwrite's value ("MADIRE!") must be at none of its targets. */
static bool ukm_after(const char *t)
{
    uint64_t after = ukm_sum();
    const uint64_t mark = 0x4D414449524521ull;
    unsigned found = 0;
    static const uint64_t targets[] = { UKM_TEXT, UKM_HEAP, UKM_PML4 };
    for (unsigned i = 0; i < 3; i++)
        if (*(const volatile uint64_t *)(uintptr_t)targets[i] == mark)
            found++;
    bool ok = after == ukm_before && found == 0;
    klogf("%s: kernel text and read-only data (%llu KiB), the kernel PML4 and the heap's first "
          "block header: checksum 0x%016llx before, 0x%016llx after; kwrite's value found at %u "
          "of its 3 targets: %s\n", t, (unsigned long long)((__rodata_end - __text_start) / 1024),
          (unsigned long long)ukm_before, (unsigned long long)after, found, ok ? "OK" : "MISMATCH");
    return ok;
}

bool usertest_userkmem(void)
{
    ukm_before = ukm_sum();
    return uf_run("User kernel-memory test", ukm_cases, UKM_CASES, ukm_after);
}

/* ---- test=usersys (stage 5a: the first system calls) ---- */

struct us_case {
    const char *image;
    uint64_t arg;
    const char *what;
    int32_t status;                 /* expected exit status; -1: the process ID & 0xFF */
};

static const struct us_case us_cases[] = {
    { "exitcode", 0, "exit(42)",                                    42 },
    { "exitcode", 1, "exit_group(43)",                              43 },
    { "exitcode", 2, "exit(256 + 7): status & 0xFF",                 7 },
    { "exitcode", 3, "return 44 from main (crt0's exit)",           44 },
    { "getpid",   0, "getpid twice, exits with it",                 -1 },
    { "yield",    0, "100 sched_yield calls, each 0",                0 },
    { "enosys",   0, "1028 numbers without a call: -ENOSYS each",    0 },
    { "sysregs",  0, "registers and RFLAGS across a call",           0 },
};
#define US_CASES (int)(sizeof(us_cases) / sizeof(us_cases[0]))

bool usertest_usersys(void)
{
    const char *t = "User syscall test";
    bool ok = true, line_ok;
    uint32_t free0 = pmm_free_pages(), tables0 = paging_stats()->page_tables;
    uint32_t stacks0 = thread_stacks_in_use(), live0 = thread_live();
    struct process_teardown_totals t0, t1;
    struct syscall_counts c0, c1;
    process_teardown_totals(&t0);
    syscall_counts(&c0);

    int32_t spin = process_start("spin", 0);
    if (spin <= 0 || !uk_wait_returns(spin, 0)) {
        klogf("%s: FAIL (spin did not start)\n", t);
        return false;
    }
    uint32_t done = 0;
    for (int i = 0; i < US_CASES; i++) {
        const struct us_case *c = &us_cases[i];
        int32_t pid = process_start(c->image, c->arg);
        struct proc_result res = { PROC_END_FAULT, -1 };
        enum proc_status st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
        int32_t want = c->status >= 0 ? c->status : (pid & 0xFF);
        line_ok = pid > 0 && st == PROC_OK && res.end == PROC_END_EXIT && res.code == want;
        if (line_ok)
            done++;
        klogf("%s: %s %llu (%s): process %d %s, status %d (expected %d): %s\n", t, c->image,
              (unsigned long long)c->arg, c->what, (int)pid, proc_end_str(res.end), (int)res.code,
              (int)want, line_ok ? "OK" : "MISMATCH");
        ok = ok && line_ok;
    }

    /* Exactly these calls were made: getpid 2 (getpid) + 1 (sysregs);
     * sched_yield 100; exit 7 (exitcode 0, 2, 3 and crt0 in getpid, yield,
     * enosys, sysregs); exit_group 1; write 4 (getpid 1, yield 1,
     * enosys 2; since stage 5b, before that these were -ENOSYS too);
     * -ENOSYS 1028 (enosys). */
    syscall_counts(&c1);
    uint64_t gp = c1.by_id[SC_GETPID] - c0.by_id[SC_GETPID];
    uint64_t sy = c1.by_id[SC_SCHED_YIELD] - c0.by_id[SC_SCHED_YIELD];
    uint64_t ex = c1.by_id[SC_EXIT] - c0.by_id[SC_EXIT];
    uint64_t eg = c1.by_id[SC_EXIT_GROUP] - c0.by_id[SC_EXIT_GROUP];
    uint64_t wr = c1.by_id[SC_WRITE] - c0.by_id[SC_WRITE];
    uint64_t en = c1.enosys - c0.enosys;
    line_ok = gp == 3 && sy == 100 && ex == 7 && eg == 1 && wr == 4 && en == 1028;
    klogf("%s: system calls made: getpid %llu, sched_yield %llu, exit %llu, exit_group %llu, "
          "write %llu, -ENOSYS %llu: %s\n", t, (unsigned long long)gp, (unsigned long long)sy,
          (unsigned long long)ex, (unsigned long long)eg, (unsigned long long)wr,
          (unsigned long long)en, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    bool spin_ran = uk_wait_returns(spin, process_user_returns(spin));
    process_kill(spin);
    struct proc_result sres;
    enum proc_status sst = process_wait(spin, &sres);
    line_ok = spin_ran && uk_killed(sst, &sres);
    klogf("%s: spin kept running in ring 3 meanwhile, then was killed: %s\n", t,
          line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    process_teardown_totals(&t1);
    uint32_t procs = t1.processes - t0.processes, bad = t1.bad_entries - t0.bad_entries;
    uint32_t free1 = pmm_free_pages(), tables1 = paging_stats()->page_tables;
    uint32_t stacks1 = thread_stacks_in_use(), live1 = thread_live(), left = process_live();
    line_ok = procs == done + 1 && bad == 0 && free1 == free0 && tables1 == tables0 &&
              stacks1 == stacks0 && live1 == live0 && left == 0;
    klogf("%s: %u address spaces torn down by their own threads, %u wrong; free pages %u -> %u, "
          "kernel page tables %u -> %u, stack slots %u -> %u, live threads %u -> %u, processes "
          "left %u: %s\n", t, procs, bad, free0, free1, tables0, tables1, stacks0, stacks1, live0,
          live1, left, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    klogf("%s: %s\n", t, ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=userio (stage 5b: system calls with pointer arguments) ---- */

static const struct us_case ui_cases[] = {
    { "hello",  0, "one line through write",                         0 },
    { "hello",  1, "control characters and a fake kernel line",      0 },
    { "hello",  2, "5000 bytes in one write: 4096 taken",            0 },
    { "sleep",  0, "nanosleep 100 ms",                               0 },
    { "sleep",  1, "nanosleep argument errors",                      0 },
    { "badptr", 0, "bad pointers and descriptors",                   0 },
};
#define UI_CASES (int)(sizeof(ui_cases) / sizeof(ui_cases[0]))

static bool ui_exit(const char *t, const char *what, int32_t pid, enum proc_status st,
                    const struct proc_result *res, int32_t want)
{
    bool ok = pid > 0 && st == PROC_OK && res->end == PROC_END_EXIT && res->code == want;
    klogf("%s: %s: process %d %s, status %d (expected %d): %s\n", t, what, (int)pid,
          proc_end_str(res->end), (int)res->code, (int)want, ok ? "OK" : "MISMATCH");
    return ok;
}

bool usertest_userio(void)
{
    const char *t = "User I/O test";
    bool ok = true, line_ok;
    uint32_t free0 = pmm_free_pages(), tables0 = paging_stats()->page_tables;
    uint32_t stacks0 = thread_stacks_in_use(), live0 = thread_live();
    struct process_teardown_totals t0, t1;
    process_teardown_totals(&t0);

    int32_t spin = process_start("spin", 0);
    if (spin <= 0 || !uk_wait_returns(spin, 0)) {
        klogf("%s: FAIL (spin did not start)\n", t);
        return false;
    }
    uint32_t done = 0;
    char what[96];
    for (int i = 0; i < UI_CASES; i++) {
        const struct us_case *c = &ui_cases[i];
        int32_t pid = process_start(c->image, c->arg);
        struct proc_result res = { PROC_END_FAULT, -1 };
        enum proc_status st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
        ksnprintf(what, sizeof(what), "%s %llu (%s)", c->image, (unsigned long long)c->arg, c->what);
        line_ok = ui_exit(t, what, pid, st, &res, c->status);
        if (line_ok)
            done++;
        ok = ok && line_ok;
        if (!(c->image[0] == 's' && c->arg == 0))
            continue;
        /* sleep 0: 100 ms = 10 ticks at 100 Hz, rounded up; it wakes at
         * the 10th tick and runs again then or one tick later. */
        struct syscall_sleep_stats ss;
        syscall_sleep_stats(&ss);
        line_ok = ss.last_ticks == 10 && ss.last_slept >= 10 && ss.last_slept <= 11;
        klogf("%s: nanosleep 100 ms: %u ticks asked for, %llu slept: %s\n", t, ss.last_ticks,
              (unsigned long long)ss.last_slept, line_ok ? "OK" : "MISMATCH");
        ok = ok && line_ok;
    }

    /* isolate: two processes at the same virtual addresses, at the same
     * time, sleeping and yielding; each must see only its own data. */
    int32_t a = process_start("isolate", 1), b = process_start("isolate", 2);
    struct proc_result ra = { PROC_END_FAULT, -1 }, rb = { PROC_END_FAULT, -1 };
    enum proc_status sa = a > 0 ? process_wait(a, &ra) : PROC_ERR_NO_SUCH;
    enum proc_status sb = b > 0 ? process_wait(b, &rb) : PROC_ERR_NO_SUCH;
    line_ok = ui_exit(t, "isolate 1 (same addresses as isolate 2, at the same time)", a, sa, &ra, 0);
    line_ok = ui_exit(t, "isolate 2 (same addresses as isolate 1, at the same time)", b, sb, &rb, 0) &&
              line_ok;
    if (line_ok)
        done += 2;
    ok = ok && line_ok;

    bool spin_ran = uk_wait_returns(spin, process_user_returns(spin));
    process_kill(spin);
    struct proc_result sres;
    enum proc_status sst = process_wait(spin, &sres);
    line_ok = spin_ran && uk_killed(sst, &sres);
    klogf("%s: spin kept running in ring 3 meanwhile, then was killed: %s\n", t,
          line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    process_teardown_totals(&t1);
    uint32_t procs = t1.processes - t0.processes, bad = t1.bad_entries - t0.bad_entries;
    uint32_t free1 = pmm_free_pages(), tables1 = paging_stats()->page_tables;
    uint32_t stacks1 = thread_stacks_in_use(), live1 = thread_live(), left = process_live();
    line_ok = procs == done + 1 && bad == 0 && free1 == free0 && tables1 == tables0 &&
              stacks1 == stacks0 && live1 == live0 && left == 0;
    klogf("%s: %u address spaces torn down by their own threads, %u wrong; free pages %u -> %u, "
          "kernel page tables %u -> %u, stack slots %u -> %u, live threads %u -> %u, processes "
          "left %u: %s\n", t, procs, bad, free0, free1, tables0, tables1, stacks0, stacks1, live0,
          live1, left, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    klogf("%s: %s\n", t, ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- the user-mode boot self-test (stage 6a) ---- */

/* Deterministic by construction: each process is waited for (joined)
 * without any time limit, and only results are checked (exit status,
 * exception, address, instruction bytes, exact page counts), never how
 * long anything took or how often a thread was switched. Preemption may
 * happen anywhere in it without changing any result. */
bool usertest_boot(void)
{
    const char *t = "User-mode self-test";
    bool ok = true, line_ok;
    uint32_t free0 = pmm_free_pages(), tables0 = paging_stats()->page_tables;
    uint32_t stacks0 = thread_stacks_in_use(), live0 = thread_live();
    struct process_teardown_totals t0, t1;
    process_teardown_totals(&t0);

    struct proc_result res = { PROC_END_FAULT, -1 };
    int32_t pid = process_start("hello", 0);
    enum proc_status st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
    line_ok = ui_exit(t, "hello (one line through write)", pid, st, &res, 0);
    ok = ok && line_ok;

    res.end = PROC_END_FAULT;
    res.code = -1;
    pid = process_start("exitcode", 0);
    st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
    line_ok = ui_exit(t, "exitcode (exit(42))", pid, st, &res, 42);
    ok = ok && line_ok;

    static const struct faultop want = OP4(0x48, 0x8b, 0x04, 0x25);
    res.end = PROC_END_EXIT;
    res.code = -1;
    pid = process_start("nullptr", 0);
    st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
    struct process_fault f;
    process_last_fault(&f);
    char got[16];
    line_ok = pid > 0 && st == PROC_OK && res.end == PROC_END_FAULT && res.code == 14 &&
              f.pid == pid && f.vector == 14 && f.cr2 == 0 && (f.error_code & 0x1F) == PF_USER &&
              faultop_match(&f.op, &want);
    struct faultop shown = f.op;
    if (shown.n > want.n)
        shown.n = want.n;
    klogf("%s: nullptr (read of address 0): process %d %s: %s, address 0x%016llx, instruction "
          "%s: %s\n", t, (int)pid, proc_end_str(res.end), exception_mnemonic(f.vector),
          (unsigned long long)f.cr2, faultop_str(&shown, got), line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    process_teardown_totals(&t1);
    uint32_t procs = t1.processes - t0.processes, bad = t1.bad_entries - t0.bad_entries;
    uint32_t free1 = pmm_free_pages(), tables1 = paging_stats()->page_tables;
    uint32_t stacks1 = thread_stacks_in_use(), live1 = thread_live(), left = process_live();
    line_ok = procs == 3 && bad == 0 && free1 == free0 && tables1 == tables0 &&
              stacks1 == stacks0 && live1 == live0 && left == 0;
    klogf("%s: %u address spaces torn down, %u wrong; free pages %u -> %u, kernel page tables "
          "%u -> %u, stack slots %u -> %u, live threads %u -> %u, processes left %u: %s\n", t,
          procs, bad, free0, free1, tables0, tables1, stacks0, stacks1, live0, live1, left,
          line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;
    klogf("%s: %s\n", t, ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=userpreempt (stage 6a: policy (a) with user processes) ---- */

#define UPR_TICKS    300
#define UPR_MAX_TURN 6              /* the plan's bound for a user process */
#define UPR_FRAMES   8

static volatile bool upr_stop;
static volatile uint32_t upr_ops;

/* A kernel thread that allocates, tags, checks and frees frames until told
 * to stop: preemption is off inside the allocator and the window, so its
 * turns can run longer (the deferred switch, policy (a)). */
static int32_t upr_frames(void *arg)
{
    (void)arg;
    uint32_t bad = 0, ops = 0, frames[UPR_FRAMES];
    while (!upr_stop) {
        for (unsigned i = 0; i < UPR_FRAMES; i++) {
            frames[i] = memory_alloc_page();
            if (frames[i]) {
                volatile uint32_t *w = vmm_window_map(frames[i]);
                w[0] = 0xF4A3E000u + i;
                vmm_window_unmap();
            }
        }
        for (unsigned i = 0; i < UPR_FRAMES; i++) {
            if (!frames[i])
                continue;
            const volatile uint32_t *w = vmm_window_map(frames[i]);
            if (w[0] != 0xF4A3E000u + i)
                bad++;
            vmm_window_unmap();
            memory_free_page(frames[i]);
        }
        ops++;
    }
    upr_ops = ops;
    return (int32_t)bad;
}

struct upr_case {
    uint64_t arg;
    const char *what;
};

static const struct upr_case upr_cases[] = {
    { 0, "a pure loop" },
    { 0, "a second pure loop" },
    { 1, "a getpid loop" },
    { 2, "a sched_yield loop" },
    { 3, "a 10 ms nanosleep loop" },
};
#define UPR_CASES (int)(sizeof(upr_cases) / sizeof(upr_cases[0]))

bool usertest_userpreempt(void)
{
    const char *t = "User preemption test";
    bool ok = true, line_ok;
    uint32_t free0 = pmm_free_pages(), tables0 = paging_stats()->page_tables;
    uint32_t stacks0 = thread_stacks_in_use(), live0 = thread_live();
    int32_t pid[UPR_CASES];
    for (int i = 0; i < UPR_CASES; i++)
        pid[i] = process_start("spin", upr_cases[i].arg);
    upr_stop = false;
    int32_t kt = thread_create("frames", upr_frames, 0, false);
    thread_turn_reset();
    thread_sleep(UPR_TICKS);            /* thread 0 polls: everything else runs */

    for (int i = 0; i < UPR_CASES; i++) {
        int32_t tid = process_tid(pid[i]);
        uint64_t longest = thread_turn_longest(tid), returns = process_user_returns(pid[i]);
        line_ok = pid[i] > 0 && tid > 0 && longest <= UPR_MAX_TURN && returns > 0;
        klogf("%s: spin %llu (%s): process %d, thread %d: longest turn %llu ticks (at most %u), "
              "%llu ticks run, preempted %llu times, back in ring 3 %llu times: %s\n", t,
              (unsigned long long)upr_cases[i].arg, upr_cases[i].what, (int)pid[i], (int)tid,
              (unsigned long long)longest, (unsigned)UPR_MAX_TURN,
              (unsigned long long)thread_ticks_run(tid), (unsigned long long)thread_preempted(tid),
              (unsigned long long)returns, line_ok ? "OK" : "MISMATCH");
        ok = ok && line_ok;
    }
    uint64_t klongest = thread_turn_longest(kt), kticks = thread_ticks_run(kt);

    for (int i = 0; i < UPR_CASES; i++) {
        struct proc_result res;
        process_kill(pid[i]);
        if (!uk_killed(process_wait(pid[i], &res), &res))
            ok = false;
    }
    upr_stop = true;
    int32_t kbad = kt > 0 ? thread_join(kt) : -1;
    line_ok = kt > 0 && kbad == 0 && upr_ops > 0;
    klogf("%s: kernel frame thread: longest turn %llu ticks (the deferred switch, policy (a); "
          "not bound by the user-process rule), %llu ticks run, %u rounds of %u frames, %d tag "
          "mismatches: %s\n", t, (unsigned long long)klongest, (unsigned long long)kticks,
          (unsigned)upr_ops, (unsigned)UPR_FRAMES, (int)kbad, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    uint32_t free1 = pmm_free_pages(), tables1 = paging_stats()->page_tables;
    uint32_t stacks1 = thread_stacks_in_use(), live1 = thread_live(), left = process_live();
    line_ok = free1 == free0 && tables1 == tables0 && stacks1 == stacks0 && live1 == live0 && left == 0;
    klogf("%s: all killed; free pages %u -> %u, kernel page tables %u -> %u, stack slots %u -> "
          "%u, live threads %u -> %u, processes left %u: %s\n", t, free0, free1, tables0, tables1,
          stacks0, stacks1, live0, live1, left, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;
    klogf("%s: %s\n", t, ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=userchurn (stage 6a: many processes) ---- */

#define UC_HELLO   500
#define UC_NULLPTR 200
#define UC_BATCHES 10
#define UC_BATCH   30               /* processes alive at once in a batch */

/* Starts `n` processes of `image` one after another and waits for each;
 * returns how many ended as `end` with `code`. */
static uint32_t uc_sequence(const char *image, uint32_t n, enum proc_end end, int32_t code)
{
    uint32_t right = 0;
    for (uint32_t i = 0; i < n; i++) {
        struct proc_result res = { PROC_END_KILLED, -1 };
        int32_t pid = process_start(image, 0);
        if (pid > 0 && process_wait(pid, &res) == PROC_OK && res.end == end && res.code == code)
            right++;
    }
    return right;
}

bool usertest_userchurn(void)
{
    const char *t = "User churn test";
    bool ok = true, line_ok;
    const struct userimg *hello = userimg_find(user_images, user_images_count, "hello");
    const struct userimg *nptr = userimg_find(user_images, user_images_count, "nullptr");
    uint32_t free0 = pmm_free_pages(), tables0 = paging_stats()->page_tables;
    uint32_t stacks0 = thread_stacks_in_use(), live0 = thread_live();
    struct process_teardown_totals t0, t1;
    process_teardown_totals(&t0);
    thread_log_exits(false);

    uint32_t h = uc_sequence("hello", UC_HELLO, PROC_END_EXIT, 0);
    line_ok = h == UC_HELLO;
    klogf("%s: %u hello processes one after another: %u exited with status 0: %s\n", t,
          (unsigned)UC_HELLO, h, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    uint32_t n = uc_sequence("nullptr", UC_NULLPTR, PROC_END_FAULT, 14);
    line_ok = n == UC_NULLPTR;
    klogf("%s: %u nullptr processes one after another: %u ended by #PF: %s\n", t,
          (unsigned)UC_NULLPTR, n, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;

    uint32_t right = 0, most = 0;
    for (int b = 0; b < UC_BATCHES; b++) {
        int32_t pid[UC_BATCH];
        for (int i = 0; i < UC_BATCH; i++)
            pid[i] = process_start(i % 2 ? "nullptr" : "hello", 0);
        if (process_live() > most)
            most = process_live();
        for (int i = 0; i < UC_BATCH; i++) {
            struct proc_result res = { PROC_END_KILLED, -1 };
            bool want_fault = i % 2;
            if (pid[i] > 0 && process_wait(pid[i], &res) == PROC_OK &&
                res.end == (want_fault ? PROC_END_FAULT : PROC_END_EXIT) &&
                res.code == (want_fault ? 14 : 0))
                right++;
        }
    }
    line_ok = right == UC_BATCHES * UC_BATCH && most == UC_BATCH;
    klogf("%s: %d batches of %d at once (hello and nullptr alternating; at most %u alive): "
          "%u of %d results right: %s\n", t, UC_BATCHES, UC_BATCH, most, right,
          UC_BATCHES * UC_BATCH, line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;
    thread_log_exits(true);

    process_teardown_totals(&t1);
    uint32_t nh = UC_HELLO + UC_BATCHES * UC_BATCH / 2, nn = UC_NULLPTR + UC_BATCHES * UC_BATCH / 2;
    uint32_t procs = t1.processes - t0.processes, pages = t1.pages - t0.pages;
    uint32_t tables = t1.tables - t0.tables, bad = t1.bad_entries - t0.bad_entries;
    uint32_t want_pages = (hello && nptr) ? nh * userimg_pages(hello) + nn * userimg_pages(nptr) : 0;
    uint32_t free1 = pmm_free_pages(), tables1 = paging_stats()->page_tables;
    uint32_t stacks1 = thread_stacks_in_use(), live1 = thread_live(), left = process_live();
    line_ok = procs == nh + nn && pages == want_pages && tables == (nh + nn) * UA_TABLES && bad == 0 &&
              free1 == free0 && tables1 == tables0 && stacks1 == stacks0 && live1 == live0 && left == 0;
    klogf("%s: %u address spaces torn down by their own threads: %u user frames and %u "
          "lower-half tables freed, %u wrong; free pages %u -> %u, kernel page tables %u -> %u, "
          "stack slots %u -> %u, live threads %u -> %u, processes left %u: %s\n", t, procs, pages,
          tables, bad, free0, free1, tables0, tables1, stacks0, stacks1, live0, live1, left,
          line_ok ? "OK" : "MISMATCH");
    ok = ok && line_ok;
    klogf("%s: %s\n", t, ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=smep, test=smap (stage 6b) ---- */

/* An address space with one user page at va (contents src, len bytes),
 * entered by the calling thread; the page's entry is read back and must
 * be present, user and on the frame filled. Returns the entry's flags. */
static uint64_t smx_user_page(struct uspace *as, const char *name, uint64_t va, const void *src,
                              size_t len, uint64_t flags)
{
    if (!uspace_create(as))
        panicf("%s test: no frame for the address space.", name);
    uspace_enter(as);
    uint64_t frame = 0, phys = 0, pte = 0;
    enum uvm_result r = uspace_map_page(as, va, src, len, flags, &frame);
    if (r != UVM_OK)
        panicf("%s test: mapping the user page failed (%s).", name, uvm_result_str(r));
    if (!uvm_translate(&as->uvm, va, &phys, &pte) || phys != frame ||
        !(pte & PTE_PRESENT) || !(pte & PTE_USER))
        panicf("%s test: the user page's entry is wrong (0x%016llx).", name,
               (unsigned long long)pte);
    return pte;
}

/* SMEP: the kernel calls a present, executable user page holding `ret`.
 * The CPU must refuse the fetch with #PF (present, instruction fetch,
 * kernel mode), which ends on the crash screen. Returns only when SMEP is
 * not on (not supported by this CPU). */
void usertest_smep(void)
{
    if (!cpuprot_status()->smep_on)
        return;
    static const uint8_t ret_insn[] = { 0xC3 };
    struct uspace as;
    uint64_t pte = smx_user_page(&as, "SMEP", VM_USER_TEXT, ret_insn, sizeof(ret_insn), 0);
    if (pte & PTE_NX)
        panic("SMEP test: the user page is no-execute; the fault would not prove SMEP.");
    klogf("SMEP test: user page 0x%016llx (present, user, executable: ret); CR4.SMEP=1: "
          "the kernel calls it, this must fault\n", (unsigned long long)VM_USER_TEXT);
    /* Through a volatile pointer: the compiler can neither see nor drop it. */
    void (*volatile user_code)(void) = (void (*)(void))(uintptr_t)VM_USER_TEXT;
    user_code();
    panic("SMEP test: the kernel executed a user page without a fault.");
}

/* SMAP: with RFLAGS.AC clear, the kernel reads a present user data page.
 * The CPU must refuse the read with #PF (present, read, kernel mode),
 * which ends on the crash screen. Returns only when SMAP is not on. */
void usertest_smap(void)
{
    if (!cpuprot_status()->smap_on)
        return;
    static const uint8_t pattern[8] = { 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A };
    struct uspace as;
    smx_user_page(&as, "SMAP", VM_USER_DATA, pattern, sizeof(pattern),
                  PTE_WRITE | paging_nx_flag());
    __asm__ volatile ("clac" ::: "memory", "cc");   /* SMAP is on: clac exists */
    klogf("SMAP test: user page 0x%016llx (present, user, writable, holds 0x5a5a5a5a5a5a5a5a); "
          "RFLAGS.AC=0, CR4.SMAP=1: the kernel reads it, this must fault\n",
          (unsigned long long)VM_USER_DATA);
    volatile uint64_t *user_data = (volatile uint64_t *)(uintptr_t)VM_USER_DATA;
    uint64_t value = *user_data;
    panicf("SMAP test: the kernel read a user page without a fault (0x%016llx).",
           (unsigned long long)value);
}
