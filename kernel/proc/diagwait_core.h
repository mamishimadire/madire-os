#ifndef MADIRE_PROC_DIAGWAIT_CORE_H
#define MADIRE_PROC_DIAGWAIT_CORE_H

/*
 * Bounded diagnostic waits, the pure part (0.12 stage 3; host unit tests in
 * tests/unit/test_diagwait.c; the kernel side is diagwait.c).
 *
 * A wait polls the tick count. It ends when the caller's condition holds,
 * when `limit` ticks have passed since it began (DIAG_DUE), or when the tick
 * count has not moved for too many polls (DIAG_STALLED): the timer stopped,
 * and a limit in ticks alone would never end the wait.
 *
 * The stall bound counts the wait's own polls against guest ticks (the
 * guest's progress, not host wall-clock time):
 *   calibrated (calib > 0):  100 x max(the most polls this wait saw within
 *                            one tick, calib), where calib is the most polls
 *                            per tick on this machine, measured at boot by
 *                            the tightest loop of the same poll, alone, while
 *                            the timer worked (diag_wait_calibrate);
 *   not calibrated:          50,000,000 polls (the timer self-test's limit),
 *                            only until calibration completes, or when it
 *                            could not run (no working timer).
 * No fixed floor: a fast machine polls millions of times per tick, and its
 * calibration says so.
 */

#include <stdbool.h>
#include <stdint.h>

#define DIAG_STALL_FACTOR 100u
#define DIAG_STALL_FIRST  50000000ull

enum diag_state {
    DIAG_WAITING,       /* neither due nor stalled */
    DIAG_DUE,           /* `limit` ticks have passed */
    DIAG_STALLED,       /* the tick count stopped moving */
};

struct diag_wait {
    uint64_t start;     /* the tick count when the wait began */
    uint64_t limit;     /* ticks */
    uint64_t last;      /* the tick count at the last poll */
    uint64_t polls;     /* polls in all */
    uint64_t still;     /* polls since the tick count last moved */
    uint64_t own_max;   /* the most polls seen within one tick */
    uint64_t calib;     /* the boot calibration (0: none) */
};

void diag_core_begin(struct diag_wait *w, uint64_t now, uint64_t limit, uint64_t calib);

/* One poll at tick count `now`: DIAG_DUE comes first once `limit` ticks
 * have passed (limit 0: at once; limit 1: as soon as the count moves). */
enum diag_state diag_core_poll(struct diag_wait *w, uint64_t now);

/* The number of polls without a tick that counts as a stall, now. */
uint64_t diag_core_bound(const struct diag_wait *w);

#endif
