/* The kernel lock that threads wait on (see klock.h). */
#include "kernel/arch/x86/irq.h"
#include "kernel/panic.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

static volatile uint64_t thread0_checks;
static uint32_t holds[SCHED_MAX_THREADS];   /* stage 7b: klocks each thread holds */
static volatile bool diag_running;          /* stage 7b: a test= diagnostic runs */

void klock_init(struct klock *l)
{
    preempt_disable();
    klock_core_init(l);
    preempt_enable();
}

static const char *state_str(enum sched_state s)
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

/* A klock is taken outermost: the preemption count 0 and interrupts on. */
static void outermost(const char *fn, int32_t me)
{
    uint32_t pc = preempt_count();
    if (pc || !irq_enabled())
        panicf("%s: thread %d '%s' with preemption disabled (count %u)\n or "
               "interrupts off: a klock is taken outermost.", fn, (int)me, thread_name(me), pc);
}

/* Stage 7b: a switch point before a ticket: a switch already due is taken
 * now, so a hold never starts on a used-up turn. */
static void switch_point(void)
{
    preempt_disable();
    preempt_enable();
}

/* Waits by yielding until `ticket` (taken at tick `first`) is served;
 * returns the wait steps. Entered with preemption disabled (the ticket's
 * own section: the first try comes with no switch point in between).
 * Returns holding the lock with preemption still disabled (count 1, the
 * hold's own), or, with `reserve`, with the turn reserved and preemption
 * on. */
static uint32_t wait_turn(struct klock *l, int32_t me, uint32_t ticket, uint64_t first,
                          bool reserve)
{
    const char *fn = reserve ? "klock_test_reserve" : "klock_acquire";
    enum klock_result r;
    uint32_t yields = 0;
    for (;;) {
        enum sched_state bs = SCHED_READY;
        int32_t bad = KLOCK_FREE;
        uint64_t now = timer_ticks();
        r = reserve ? klock_core_reserve_try(l, me, ticket, now, first, yields, diag_running)
                    : klock_core_try(l, me, ticket, now, first, yields);
        if (r == KLOCK_TAKEN) {
            holds[me]++;
            return yields;              /* preemption stays disabled until the release */
        }
        if (r == KLOCK_RESERVED) {
            preempt_enable();
            return yields;
        }
        if (r == KLOCK_BUSY) {
            bad = klock_core_ahead_bad(l, ticket, thread_state, &bs);
            klock_core_waited(l, me);
        }
        preempt_enable();
        if (r != KLOCK_BUSY)
            panicf("%s: thread %d '%s': %s.", fn, (int)me, thread_name(me), klock_result_str(r));
        if (bad != KLOCK_FREE)          /* an abandoned turn: the wait would never end */
            panicf("%s: thread %d '%s' waits behind thread %d '%s',\n which holds "
                   "the lock or a ticket ahead and is %s: it can never take its turn.", fn,
                   (int)me, thread_name(me), (int)bad, thread_name(bad), state_str(bs));
        if (klock_core_wait_method(me) != KLOCK_WAIT_YIELD)
            panic(me == 0 ? "klock_acquire: a blocking wait was requested by thread 0 (the "
                            "console): thread 0 never blocks."
                          : "klock_acquire: a blocking wait was requested (no sleep queue in "
                            "1.0).");
        if (me == 0) {                  /* the thread-0 self-check, every wait step */
            if (thread_state(0) != SCHED_RUNNING)
                panicf("klock_acquire: thread 0 waits while %s, not running.",
                       state_str(thread_state(0)));
            thread0_checks++;
        }
        yields++;
        thread_yield();                 /* stays on the run queue: never blocks */
        preempt_disable();
    }
}

/* Takes a ticket and waits for it (see wait_turn). */
static uint32_t queue(struct klock *l, int32_t me, const char *fn, bool reserve)
{
    outermost(fn, me);
    switch_point();
    uint32_t ticket = 0;
    preempt_disable();
    uint64_t first = timer_ticks();
    enum klock_result r = klock_core_ticket(l, me, &ticket);
    if (r != KLOCK_QUEUED) {
        preempt_enable();
        panicf("%s: thread %d '%s': %s.", fn, (int)me, thread_name(me), klock_result_str(r));
    }
    return wait_turn(l, me, ticket, first, reserve);
}

uint32_t klock_acquire(struct klock *l)
{
    return queue(l, thread_current(), "klock_acquire", false);
}

uint32_t klock_requeue(struct klock *l)
{
    int32_t me = thread_current();
    uint32_t pc = preempt_count();
    if (!irq_enabled())
        panicf("klock_requeue: thread %d '%s' with interrupts off.", (int)me, thread_name(me));
    uint32_t ticket = 0;
    preempt_disable();
    uint64_t now = timer_ticks();
    enum klock_result r = klock_core_requeue(l, me, now, pc, &ticket);
    if (r == KLOCK_QUEUED)
        holds[me]--;
    preempt_enable();
    if (r != KLOCK_QUEUED)
        panicf("klock_requeue: thread %d '%s' (preemption count %u): %s.", (int)me,
               thread_name(me), pc, klock_result_str(r));
    preempt_enable();                   /* the hold's own: a switch due is taken here */
    preempt_disable();
    return wait_turn(l, me, ticket, now, false);
}

void klock_release(struct klock *l)
{
    int32_t me = thread_current();
    uint32_t pc = preempt_count();
    preempt_disable();
    enum klock_result r = klock_core_release(l, me, timer_ticks(), pc);
    if (r == KLOCK_RELEASED)
        holds[me]--;
    preempt_enable();
    if (r != KLOCK_RELEASED)
        panicf("klock_release: thread %d '%s': %s.", (int)me, thread_name(me),
               klock_result_str(r));
    if (pc == 0)
        panicf("klock_release: thread %d '%s' with preemption on: a hold keeps it\n "
               "disabled from its take to its release.", (int)me, thread_name(me));
    preempt_enable();                   /* the hold's own: a switch due is taken here */
}

uint32_t klock_test_reserve(struct klock *l)
{
    int32_t me = thread_current();
    if (!diag_running)
        panicf("klock_test_reserve: thread %d '%s' outside a test= diagnostic:\n a reserved "
               "turn is test code only.", (int)me, thread_name(me));
    return queue(l, me, "klock_test_reserve", true);
}

void klock_test_take(struct klock *l)
{
    int32_t me = thread_current();
    if (!diag_running)
        panicf("klock_test_take: thread %d '%s' outside a test= diagnostic:\n a reserved "
               "turn is test code only.", (int)me, thread_name(me));
    outermost("klock_test_take", me);
    preempt_disable();
    enum klock_result r = klock_core_take_reserved(l, me, diag_running);
    if (r == KLOCK_TAKEN) {
        holds[me]++;
        return;                         /* preemption stays disabled until the release */
    }
    preempt_enable();
    panicf("klock_test_take: thread %d '%s': %s.", (int)me, thread_name(me),
           klock_result_str(r));
}

void klock_test_allow(void)
{
    diag_running = true;
}

void klock_check_none(const char *fn)
{
    int32_t me = thread_current();
    if (me >= 0 && me < SCHED_MAX_THREADS && holds[me])
        panicf("%s: thread %d '%s' holds the VFS lock:\n a holder never yields, sleeps, joins "
               "or exits.", fn, (int)me, thread_name(me));
}

bool klock_held(const struct klock *l)
{
    return klock_core_held_by(l, thread_current());
}

uint64_t klock_thread0_checks(void)
{
    return thread0_checks;
}
