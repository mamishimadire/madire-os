#ifndef MADIRE_PROC_DIAGWAIT_H
#define MADIRE_PROC_DIAGWAIT_H

/*
 * Bounded diagnostic waits (0.12 stage 3). Every wait for ticks in the
 * thread and process diagnostics (schedtest.c, usertest.c) polls through
 * these, so it ends even if the timer stops: at its condition, at its limit
 * in ticks, or at a stall (see diagwait_core.h for the bound). A stall, and
 * a limit reached where the caller says it must not be, logs one line
 * naming the wait (file:line), the tick count and the polls, ending in
 * MISMATCH, and counts in diag_wait_failures(), which the tests check.
 *
 * Blocking waits are not bounded here (documented exceptions): thread_join,
 * process_wait, thread_sleep in threads other than 0, and a ring-3 program
 * that is never preempted all need the timer or another thread to end.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/diagwait_core.h"

struct diag_site {
    struct diag_wait w;
    const char *file;
    int line;
};

/* Measures, at boot with the timer working and thread 0 alone, the most
 * polls per tick of the tightest poll loop, and logs it. Without a working
 * timer the waits stay uncalibrated (diagwait_core.h). */
void diag_wait_calibrate(void);
uint64_t diag_wait_calibration(void);

/* The number of MISMATCH lines logged by waits since boot. */
uint32_t diag_wait_failures(void);

void diag_wait_begin_at(struct diag_site *s, uint64_t limit, const char *file, int line);
#define diag_wait_begin(s, limit) diag_wait_begin_at((s), (limit), __FILE__, __LINE__)

/* A wait until tick count `end` (limit end - now; 0 if already past). */
#define diag_wait_until(s, end) diag_wait_begin_at((s), diag_wait_ticks_to(end), __FILE__, __LINE__)
uint64_t diag_wait_ticks_to(uint64_t end);

/* One poll. true: go on waiting. false: the limit was reached (normal for
 * a wait for a duration, the caller decides for a condition) or the wait
 * stalled (logged as a MISMATCH). */
bool diag_wait_poll(struct diag_site *s);

/* Like diag_wait_poll, but reaching the limit is also a MISMATCH (logged):
 * for a wait whose condition must come within its limit. */
bool diag_wait_poll_strict(struct diag_site *s);

bool diag_wait_stalled(const struct diag_site *s);

/* Thread 0's sleep in the diagnostics (instead of thread_sleep): `ticks`
 * ticks, yielding at every poll (so the other threads run), bounded like
 * any wait; false if it stalled (logged). Thread 0 only. */
bool diag_sleep_at(uint32_t ticks, const char *file, int line);
#define diag_sleep(ticks) diag_sleep_at((ticks), __FILE__, __LINE__)

#endif
