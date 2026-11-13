/* The per-disk I/O lock (see disklock.h). Every static is disklock_-prefixed. */
#include "kernel/arch/x86/irq.h"
#include "kernel/fs/vfs.h"
#include "kernel/kprintf.h"
#include "kernel/panic.h"
#include "kernel/proc/disklock.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

/* Off (Live mode) and free from the first instruction: no owner (thread 0
 * is not the owner of a zeroed lock), no ticket out (next == serving);
 * disklock_start initializes it fully. */
static struct disklock disklock_lock = { .owner = DISKLOCK_FREE };

static const char *disklock_state_str(enum sched_state s)
{
    switch (s) {
    case SCHED_FREE:    return "gone (free slot)";
    case SCHED_NEW:     return "not started";
    case SCHED_READY:   return "ready";
    case SCHED_RUNNING: return "running";
    case SCHED_BLOCKED: return "blocked (sleeping or joining)";
    case SCHED_ZOMBIE:  return "exited";
    }
    return "?";
}

void disklock_start(void)
{
    preempt_disable();
    disklock_core_init(&disklock_lock);
    disklock_lock.on = true;
    preempt_enable();
}

void disklock_stop(void)
{
    preempt_disable();
    disklock_lock.on = false;
    preempt_enable();
}

bool disklock_on(void)
{
    return disklock_lock.on;
}

/* A broken rule: its text, then the function and the thread. */
static void __attribute__((noreturn)) disklock_broken(enum disklock_result r, const char *fn, int32_t me)
{
    panicf("%s\n %s: thread %d '%s'.", disklock_result_str(r), fn, (int)me, thread_name(me));
}

uint32_t disklock_acquire(void)
{
    int32_t me = thread_current();
    bool vfs_held = klock_held(vfs_lock());
    bool irq_on = irq_enabled();
    preempt_disable();
    enum disklock_result r = disklock_core_check(&disklock_lock, me, vfs_held, irq_on);
    uint32_t ticket = 0;
    uint64_t first = timer_ticks();
    if (r == DISKLOCK_OK)
        r = disklock_core_ticket(&disklock_lock, me, &ticket);
    if (r != DISKLOCK_QUEUED) {
        preempt_enable();
        disklock_broken(r, "disklock_acquire", me);
    }
    uint32_t yields = 0;
    for (;;) {
        enum sched_state bs = SCHED_READY;
        int32_t bad = DISKLOCK_FREE;
        r = disklock_core_try(&disklock_lock, me, ticket, timer_ticks(), first, yields);
        if (r == DISKLOCK_TAKEN) {
            preempt_enable();                   /* L2: the holder keeps preemption on */
            return yields;
        }
        if (r == DISKLOCK_BUSY) {
            bad = disklock_core_ahead_bad(&disklock_lock, ticket, thread_state, &bs);
            disklock_core_waited(&disklock_lock, me);
        }
        preempt_enable();
        if (r != DISKLOCK_BUSY)
            disklock_broken(r, "disklock_acquire", me);
        if (bad == DISKLOCK_NO_THREAD)          /* a ticket slot that names no thread */
            panicf("disklock_acquire: thread %d '%s' waits behind no thread (ticket slot empty),\n "
                   "a ticket ahead of its own: it can never take its turn.", (int)me, thread_name(me));
        if (bad != DISKLOCK_FREE)               /* an abandoned turn: the wait would never end */
            panicf("disklock_acquire: thread %d '%s' waits behind thread %d '%s',\n which holds "
                   "the disk lock or a ticket ahead and is %s: it can never take its turn.",
                   (int)me, thread_name(me), (int)bad, thread_name(bad), disklock_state_str(bs));
        yields++;
        thread_yield();                         /* a waiter yields: never spins with preemption off */
        preempt_disable();
    }
}

void disklock_release(void)
{
    int32_t me = thread_current();
    preempt_disable();
    enum disklock_result r = disklock_core_release(&disklock_lock, me, timer_ticks());
    preempt_enable();
    if (r != DISKLOCK_RELEASED)
        disklock_broken(r, "disklock_release", me);
}

bool disklock_held(void)
{
    return disklock_core_held_by(&disklock_lock, thread_current());
}

bool disklock_may_yield(void)
{
    return disklock_core_may_yield(&disklock_lock, thread_current(), preempt_count(), irq_enabled());
}

void disklock_check_io(const char *where)
{
    int32_t me = thread_current();
    if (!disklock_core_io_ok(&disklock_lock, me))
        panicf("AHCI used without the disk lock (L5)\n %s: thread %d '%s'.", where, (int)me,
               thread_name(me));
}

void disklock_check_none(const char *at, const char *fn)
{
    int32_t me = thread_current();
    preempt_disable();
    bool involved = disklock_core_involved(&disklock_lock, me);
    preempt_enable();
    if (involved)
        panicf("disk lock still held at %s\n %s: thread %d '%s'.", at, fn, (int)me, thread_name(me));
}

void disklock_figures(struct disklock_figs *f)
{
    preempt_disable();
    const struct disklock *l = &disklock_lock;
    f->takes = l->takes;
    f->contended = l->contended;
    f->yields = l->yields;
    f->yields0 = l->yields0;
    f->hold_longest = l->hold_longest;
    f->wait_longest = l->wait_longest;
    f->wait_longest0 = l->wait_longest0;
    preempt_enable();
}

/* ---- test=selfcheckdisklock and test=selfcheckdiskrelock ---- */

void disklock_selfcheck_order(void)
{
    klock_acquire(vfs_lock());
    disklock_acquire();
    klogf("Self-check test: the disk lock taken under the VFS lock returned (WRONG)\n");
    panic("selfcheckdisklock: the self-check did not fire.");
}

void disklock_selfcheck_relock(void)
{
    disklock_acquire();
    disklock_acquire();
    klogf("Self-check test: the disk lock taken twice returned (WRONG)\n");
    panic("selfcheckdiskrelock: the self-check did not fire.");
}
