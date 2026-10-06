/* Kernel threads (see thread.h). */
#include "kernel/arch/x86/gdt.h"
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/kernel.h"
#include "kernel/kprintf.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pte.h"
#include "kernel/panic.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/sched_core.h"
#include "kernel/proc/thread.h"
#include "kernel/stackpeak.h"
#include "kernel/timer.h"

/* ---- preemption-disable count (preempt.h) ---- */

volatile uint32_t preempt_count_value;

void preempt_underflow(void)
{
    panic("preempt_enable() without a matching preempt_disable().");
}

/* ---- threads ---- */

struct kthread {
    char name[THREAD_NAME_MAX];
    uint64_t rsp;               /* saved by switch_to while off the CPU */
    int slot;                   /* kernel stack slot; -1 for thread 0 */
    uint64_t turn_start;        /* tick at which it last got the CPU */
};

/* All scheduler state is changed with interrupts disabled (irq_save). */
static struct sched sched;
static struct kstack_pool stacks;
static struct kthread threads[SCHED_MAX_THREADS];
/* Set by thread_init(). Until then the interrupt hooks do nothing: the
 * zero-initialised `sched` is not a valid state (its empty queue is -1). */
static volatile bool ready;
static volatile uint64_t preemptions;
static volatile uint64_t turn_max;      /* longest turn of a thread other than 0 */
static volatile bool log_exits = true;
static volatile uint32_t peak_max;      /* highest final stack peak since reset */

#define STACK_PAGES (KSTACK_SIZE / VM_PAGE_SIZE)

extern void switch_to(uint64_t *save_rsp, uint64_t new_rsp);
extern char stack_top[];                    /* boot/boot.asm: thread 0's stack */
extern char thread_trampoline[];

static void copy_name(char *dst, const char *src)
{
    int i = 0;
    for (; src && src[i] && i < THREAD_NAME_MAX - 1; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void thread_init(void)
{
    uint32_t f = irq_save();
    sched_init(&sched);
    kstack_init(&stacks);
    for (int i = 0; i < SCHED_MAX_THREADS; i++) {
        threads[i].name[0] = '\0';
        threads[i].rsp = 0;
        threads[i].slot = -1;
    }
    copy_name(threads[0].name, "kernel");
    ready = true;
    irq_restore(f);
}

int32_t thread_current(void)
{
    return sched.current;
}

const char *thread_name(int32_t tid)
{
    return (tid >= 0 && tid < SCHED_MAX_THREADS) ? threads[tid].name : "?";
}

enum sched_state thread_state(int32_t tid)
{
    return (tid >= 0 && tid < SCHED_MAX_THREADS) ? sched.t[tid].state : SCHED_FREE;
}

uint32_t thread_live(void)
{
    return sched.live;
}

uint32_t thread_stacks_in_use(void)
{
    return kstack_in_use(&stacks);
}

uint64_t thread_switches(void)
{
    return sched.switches;
}

uint64_t thread_preemptions(void)
{
    return preemptions;
}

uint64_t thread_ticks_run(int32_t tid)
{
    return (tid >= 0 && tid < SCHED_MAX_THREADS) ? sched.t[tid].ticks_run : 0;
}

bool thread_need_resched(void)
{
    return sched.need_resched;
}

uint64_t thread_kstack_top(int32_t tid)
{
    if (tid == 0)
        return (uint64_t)(uintptr_t)stack_top;
    if (tid < 0 || tid >= SCHED_MAX_THREADS || threads[tid].slot < 0)
        return 0;
    return kstack_bottom(threads[tid].slot) + KSTACK_SIZE;
}

/* Interrupts are off. Returns once `prev` runs again (never, after exit).
 * Also measures turns: how many ticks `prev` ran since it got the CPU.
 * 0.10: every switch, from every path (yield, sleep, join, exit,
 * preemption), comes through here, and sets the TSS's RSP0 to the next
 * thread's kernel stack top: an interrupt or exception from ring 3 then
 * always lands on the stack of the thread that was running. */
static void switch_threads(int32_t prev, int32_t next)
{
    if (next == prev)
        return;
    uint64_t now = timer_ticks();
    if (prev != 0 && now - threads[prev].turn_start > turn_max)
        turn_max = now - threads[prev].turn_start;
    threads[next].turn_start = now;
    gdt_set_rsp0(thread_kstack_top(next));
    switch_to(&threads[prev].rsp, threads[next].rsp);
}

int32_t thread_slot_owner(int slot)
{
    for (int32_t i = 1; i < SCHED_MAX_THREADS; i++)
        if (sched.t[i].state != SCHED_FREE && threads[i].slot == slot)
            return i;
    return -1;
}

uint32_t thread_stack_size(int32_t tid)
{
    return tid == 0 ? kernel_stack_size() : KSTACK_SIZE;
}

uint32_t thread_stack_peak(int32_t tid)
{
    if (tid == 0)
        return kernel_stack_peak();
    if (tid < 0 || tid >= SCHED_MAX_THREADS || threads[tid].slot < 0)
        return 0;
    return (uint32_t)stack_peak((const uint8_t *)(uintptr_t)kstack_bottom(threads[tid].slot),
                                KSTACK_SIZE);
}

void thread_log_exits(bool on)
{
    log_exits = on;
}

uint32_t thread_peak_max(void)
{
    return peak_max;
}

void thread_turn_reset(void)
{
    uint32_t f = irq_save();
    turn_max = 0;
    peak_max = 0;
    threads[sched.current].turn_start = timer_ticks();
    irq_restore(f);
}

uint64_t thread_turn_max(void)
{
    return turn_max;
}

static void require_preemption(const char *fn)
{
    uint32_t pc = preempt_count();
    if (pc)
        panicf("%s: called with preemption disabled (count %u, thread %d '%s').", fn, pc,
               (int)sched.current, threads[sched.current].name);
}

/* Unmaps a stack slot's pages, frees their frames, and the slot. */
static void free_stack(int slot)
{
    for (uint32_t i = 0; i < STACK_PAGES; i++) {
        uint64_t phys = 0;
        enum vmm_result r = paging_unmap(kstack_bottom(slot) + i * VM_PAGE_SIZE, &phys);
        if (r != VMM_OK)
            panicf("Thread stack slot %d: unmap failed (%s).", slot, vmm_result_str(r));
        memory_free_page((uint32_t)phys);
    }
    uint32_t f = irq_save();
    bool ok = kstack_free(&stacks, slot);
    irq_restore(f);
    if (!ok)
        panicf("Thread stack slot %d freed twice.", slot);
}

/* Maps fresh frames for a stack slot. All or nothing. */
static bool map_stack(int slot)
{
    uint32_t done = 0;
    for (; done < STACK_PAGES; done++) {
        uint32_t frame = memory_alloc_page();
        if (!frame)
            break;
        if (paging_map(kstack_bottom(slot) + done * VM_PAGE_SIZE, frame,
                       PTE_WRITE | paging_nx_flag()) != VMM_OK) {
            memory_free_page(frame);
            break;
        }
    }
    if (done == STACK_PAGES)
        return true;
    for (uint32_t i = 0; i < done; i++) {
        uint64_t phys = 0;
        if (paging_unmap(kstack_bottom(slot) + i * VM_PAGE_SIZE, &phys) == VMM_OK)
            memory_free_page((uint32_t)phys);
    }
    return false;
}

int32_t thread_create(const char *name, thread_fn fn, void *arg, bool detached)
{
    if (!paging_enabled() || !fn)
        return -1;
    int32_t tid = SCHED_NONE;
    uint32_t f = irq_save();
    enum sched_result r = sched_alloc(&sched, detached, &tid);
    int slot = r == SCHED_OK ? kstack_alloc(&stacks) : -1;
    if (r == SCHED_OK && slot < 0)
        sched_abandon(&sched, tid);
    irq_restore(f);
    if (r != SCHED_OK || slot < 0)
        return -1;

    if (!map_stack(slot)) {
        f = irq_save();
        kstack_free(&stacks, slot);
        sched_abandon(&sched, tid);
        irq_restore(f);
        return -1;
    }

    /* Paint the whole stack (high-water mark), then build the frame that
     * switch_to pops on the first switch: it "returns" into
     * thread_trampoline with r12 = fn, r13 = arg and RFLAGS = 0x2 (every
     * flag clear; thread_start then sets IF). */
    stack_paint((uint8_t *)(uintptr_t)kstack_bottom(slot), KSTACK_SIZE);
    uint64_t *top = (uint64_t *)(uintptr_t)kstack_top(slot);
    top[-1] = 0;                                    /* padding */
    top[-2] = 0;
    top[-3] = (uint64_t)(uintptr_t)thread_trampoline;
    top[-4] = 0x2;                                  /* RFLAGS: reserved bit 1 only */
    top[-5] = 0;                                    /* rbp */
    top[-6] = 0;                                    /* rbx */
    top[-7] = (uint64_t)(uintptr_t)fn;              /* r12 */
    top[-8] = (uint64_t)(uintptr_t)arg;             /* r13 */
    top[-9] = 0;                                    /* r14 */
    top[-10] = 0;                                   /* r15 */

    copy_name(threads[tid].name, name);
    threads[tid].slot = slot;
    threads[tid].rsp = (uint64_t)(uintptr_t)&top[-10];

    f = irq_save();
    r = sched_start(&sched, tid);
    irq_restore(f);
    if (r != SCHED_OK)
        panicf("thread_create: start of thread %d failed (%s).", (int)tid, sched_result_str(r));
    return tid;
}

/* Called by thread_trampoline (switch.asm) on a new thread's first run.
 * It arrives with interrupts off (every switch happens with them off), so
 * it enables them: a new thread always starts with IF=1. */
void thread_start(thread_fn fn, void *arg) __attribute__((noreturn));
void thread_start(thread_fn fn, void *arg)
{
    interrupts_enable();
    thread_exit(fn(arg));
}

/* Thread 0 never blocks; while it waits it lets others run, or halts until
 * the next interrupt when nobody else can (a wake-up then preempts it at
 * that interrupt's end). With interrupts off, hlt would never return, so it
 * yields instead. */
static void poll_step(void)
{
    if (!sched_queue_empty(&sched) || !irq_enabled())
        thread_yield();
    else
        __asm__ volatile ("hlt");
}

void thread_yield(void)
{
    require_preemption("thread_yield");
    uint32_t f = irq_save();
    int32_t prev = sched.current, next = prev;
    sched_yield(&sched, 0, &next);
    switch_threads(prev, next);
    irq_restore(f);                 /* this thread's own IF state */
}

int32_t thread_join(int32_t tid)
{
    require_preemption("thread_join");
    for (;;) {
        uint32_t f = irq_save();
        int32_t prev = sched.current, next = prev, code = 0;
        enum sched_result r = sched_join(&sched, 0, tid, &code, &next);
        int slot = -1;
        if (r == SCHED_OK) {
            slot = threads[tid].slot;   /* taken before the tid can be reused */
            threads[tid].slot = -1;
        } else if (r == SCHED_BLOCKED_NOW) {
            switch_threads(prev, next);
        }
        irq_restore(f);

        switch (r) {
        case SCHED_OK:
            free_stack(slot);
            return code;
        case SCHED_BLOCKED_NOW:
            continue;               /* woken: the thread has exited */
        case SCHED_WOULD_BLOCK:     /* thread 0: poll */
            poll_step();
            continue;
        default:
            panicf("thread_join(%d) from thread %d: %s.", (int)tid, (int)prev,
                   sched_result_str(r));
        }
    }
}

void thread_exit(int32_t code)
{
    require_preemption("thread_exit");
    int32_t me = sched.current;
    /* The final high-water mark, measured on the stack it is still using. */
    uint32_t peak = thread_stack_peak(me);
    if (peak > peak_max)
        peak_max = peak;
    if (log_exits)
        klogf("Thread %d '%s' exited: code %d, stack peak %u of %u\n", (int)me,
              threads[me].name, (int)code, peak, (unsigned)KSTACK_SIZE);
    irq_save();                     /* never restored: this thread is done */
    int32_t prev = sched.current, next = prev;
    enum sched_result r = sched_exit(&sched, code, &next);
    if (r != SCHED_OK)
        panicf("thread_exit from thread %d: %s.", (int)prev, sched_result_str(r));
    switch_threads(prev, next);
    panic("thread_exit: an exited thread was resumed.");
}

bool thread_reap(void)
{
    int32_t tid = SCHED_NONE;
    int slot = -1;
    uint32_t f = irq_save();
    bool ok = sched_reap_detached(&sched, &tid) == SCHED_OK;
    if (ok) {
        slot = threads[tid].slot;
        threads[tid].slot = -1;
    }
    irq_restore(f);
    if (ok)
        free_stack(slot);
    return ok;
}

void thread_sleep(uint32_t ticks)
{
    require_preemption("thread_sleep");
    if (ticks == 0) {
        thread_yield();
        return;
    }
    uint64_t wake = timer_ticks() + ticks;
    if (sched.current == 0) {               /* thread 0 never blocks: poll */
        while (timer_ticks() < wake)
            poll_step();
        return;
    }
    uint32_t f = irq_save();
    int32_t prev = sched.current, next = prev;
    enum sched_result r = sched_sleep(&sched, 0, wake, &next);
    if (r != SCHED_BLOCKED_NOW)
        panicf("thread_sleep from thread %d: %s.", (int)prev, sched_result_str(r));
    switch_threads(prev, next);
    irq_restore(f);
}

void thread_idle(void)
{
    while (thread_reap())
        ;
    poll_step();
}

void thread_tick(void)
{
    if (ready)
        sched_tick(&sched, timer_ticks());
}

/* Interrupts are off (interrupt gate). The switch happens only after the
 * EOI, and only with preemption enabled: a request made while it was
 * disabled stays pending (sched_preempt keeps need_resched) and is taken
 * at the first interrupt after preempt_enable() brings the count to 0. */
void thread_preempt_point(void)
{
    if (!ready)
        return;
    int32_t prev = sched.current, next = prev;
    if (sched_preempt(&sched, preempt_count(), &next)) {
        preemptions++;
        switch_threads(prev, next);
    }
}

bool thread_screen_allowed(void)
{
    return panic_active || sched.current == 0;
}

void thread_screen_violation(void)
{
    int32_t cur = sched.current;
    panicf("Screen output from thread %d '%s': only thread 0 writes to the screen "
           "(use klogf).", (int)cur, threads[cur].name);
}
