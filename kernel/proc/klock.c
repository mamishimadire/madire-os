/* The kernel lock that threads wait on (see klock.h). */
#include "kernel/arch/x86/irq.h"
#include "kernel/panic.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

static volatile uint64_t thread0_checks;

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

uint32_t klock_acquire(struct klock *l)
{
    int32_t me = thread_current();
    uint32_t pc = preempt_count();
    if (pc || !irq_enabled())
        panicf("klock_acquire: thread %d '%s' with preemption disabled (count %u)\n or "
               "interrupts off: a klock is taken outermost.", (int)me, thread_name(me), pc);
    uint64_t first = timer_ticks();
    uint32_t yields = 0;
    for (;;) {
        preempt_disable();
        enum klock_result r = klock_core_try(l, me, timer_ticks(), first, yields);
        int32_t owner = l->owner;
        enum sched_state os = thread_state(owner);
        if (r == KLOCK_BUSY)
            klock_core_waited(l, me);
        preempt_enable();
        if (r == KLOCK_TAKEN)
            return yields;
        if (r != KLOCK_BUSY)
            panicf("klock_acquire: thread %d '%s': %s.", (int)me, thread_name(me),
                   klock_result_str(r));
        if (!klock_core_owner_ok(os))
            panicf("klock_acquire: thread %d '%s' waits for thread %d '%s',\n which holds the "
                   "lock and is %s: it can never release it.", (int)me, thread_name(me),
                   (int)owner, thread_name(owner), state_str(os));
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
    }
}

void klock_release(struct klock *l)
{
    int32_t me = thread_current();
    uint32_t pc = preempt_count();
    preempt_disable();
    enum klock_result r = klock_core_release(l, me, timer_ticks(), pc);
    preempt_enable();
    if (r != KLOCK_RELEASED)
        panicf("klock_release: thread %d '%s': %s.", (int)me, thread_name(me),
               klock_result_str(r));
}

bool klock_held(const struct klock *l)
{
    return klock_core_held_by(l, thread_current());
}

uint64_t klock_thread0_checks(void)
{
    return thread0_checks;
}
