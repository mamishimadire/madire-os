/* Bounded diagnostic waits (see diagwait.h). */
#include <stdbool.h>
#include <stdint.h>
#include "kernel/arch/x86/irq.h"
#include "kernel/kprintf.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

#define CAL_TICKS 5                 /* whole ticks measured at boot */

static uint64_t calib;              /* 0: not calibrated */
static volatile uint32_t failures;

uint64_t diag_wait_calibration(void)
{
    return calib;
}

uint32_t diag_wait_failures(void)
{
    return failures;
}

static void count_failure(void)
{
    __atomic_fetch_add(&failures, 1, __ATOMIC_SEQ_CST);    /* waits run in many threads */
}

void diag_wait_calibrate(void)
{
    if (!timer_verified() || !irq_enabled()) {
        klogf("Diagnostic waits: not calibrated (timer not verified or interrupts off); stall "
              "bound %llu polls\n", (unsigned long long)DIAG_STALL_FIRST);
        return;
    }
    /* The same poll as every wait, in the tightest loop there is (a wait's
     * loop only adds work): a partial tick, then CAL_TICKS whole ones; the
     * most polls within one. Preemption is held off (nothing else may run;
     * thread 0 is alone anyway), timer interrupts still arrive and count.
     * Measured in guest ticks, not host time: a tick in which the guest was
     * descheduled holds fewer polls, one delivered late holds more; the
     * most of CAL_TICKS whole ticks is kept. */
    preempt_disable();
    struct diag_wait w;
    uint64_t t0 = timer_ticks();
    diag_core_begin(&w, t0, CAL_TICKS + 1, 0);
    enum diag_state st;
    do
        st = diag_core_poll(&w, timer_ticks());
    while (st == DIAG_WAITING);
    uint64_t spent = timer_ticks() - t0;
    preempt_enable();
    if (st != DIAG_DUE || w.own_max == 0) {
        klogf("Diagnostic waits: not calibrated (the tick count stayed at %llu for %llu polls); "
              "stall bound %llu polls\n", (unsigned long long)w.last, (unsigned long long)w.still,
              (unsigned long long)DIAG_STALL_FIRST);
        return;
    }
    calib = w.own_max;
    klogf("Diagnostic waits: calibrated: at most %llu polls per tick (%u whole ticks measured, "
          "%llu ticks spent); stall bound at least %llu polls\n", (unsigned long long)calib,
          (unsigned)CAL_TICKS, (unsigned long long)spent,
          (unsigned long long)(calib * DIAG_STALL_FACTOR));
}

uint64_t diag_wait_ticks_to(uint64_t end)
{
    uint64_t now = timer_ticks();
    return end > now ? end - now : 0;
}

void diag_wait_begin_at(struct diag_site *s, uint64_t limit, const char *file, int line)
{
    s->file = file;
    s->line = line;
    diag_core_begin(&s->w, timer_ticks(), limit, calib);
}

static bool poll(struct diag_site *s, bool strict)
{
    enum diag_state st = diag_core_poll(&s->w, timer_ticks());
    if (st == DIAG_WAITING)
        return true;
    if (st == DIAG_STALLED) {
        klogf("Diagnostic wait: %s:%d: stalled: the tick count stayed at %llu for %llu polls "
              "(bound %llu; %llu polls in all): MISMATCH\n", s->file, s->line,
              (unsigned long long)s->w.last, (unsigned long long)s->w.still,
              (unsigned long long)diag_core_bound(&s->w), (unsigned long long)s->w.polls);
        count_failure();
    } else if (strict) {
        klogf("Diagnostic wait: %s:%d: not done within %llu ticks (tick count %llu, %llu polls): "
              "MISMATCH\n", s->file, s->line, (unsigned long long)s->w.limit,
              (unsigned long long)s->w.last, (unsigned long long)s->w.polls);
        count_failure();
    }
    return false;
}

bool diag_wait_poll(struct diag_site *s)
{
    return poll(s, false);
}

bool diag_wait_poll_strict(struct diag_site *s)
{
    return poll(s, true);
}

bool diag_wait_stalled(const struct diag_site *s)
{
    return s->w.still >= diag_core_bound(&s->w) && s->w.last - s->w.start < s->w.limit;
}

bool diag_sleep_at(uint32_t ticks, const char *file, int line)
{
    if (ticks == 0) {               /* as thread_sleep(0) */
        thread_yield();
        return true;
    }
    struct diag_site s;
    diag_wait_begin_at(&s, ticks, file, line);
    while (diag_wait_poll(&s))
        thread_yield();             /* the others run; alone, it returns at once */
    return !diag_wait_stalled(&s);
}
