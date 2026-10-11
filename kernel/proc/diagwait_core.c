/* Bounded diagnostic waits, the pure part (see diagwait_core.h). */
#include "kernel/proc/diagwait_core.h"

void diag_core_begin(struct diag_wait *w, uint64_t now, uint64_t limit, uint64_t calib)
{
    w->start = now;
    w->limit = limit;
    w->last = now;
    w->polls = 0;
    w->still = 0;
    w->own_max = 0;
    w->calib = calib;
}

uint64_t diag_core_bound(const struct diag_wait *w)
{
    if (!w->calib)
        return DIAG_STALL_FIRST;
    uint64_t m = w->own_max > w->calib ? w->own_max : w->calib;
    return m * DIAG_STALL_FACTOR;
}

enum diag_state diag_core_poll(struct diag_wait *w, uint64_t now)
{
    w->polls++;
    if (now != w->last) {
        /* The polls since the last move all fell within the tick that just
         * ended (the first stretch may be part of a tick: never more). */
        if (w->still > w->own_max)
            w->own_max = w->still;
        w->still = 0;
        w->last = now;
    } else {
        w->still++;
    }
    if (now - w->start >= w->limit)
        return DIAG_DUE;
    if (w->still >= diag_core_bound(w))
        return DIAG_STALLED;
    return DIAG_WAITING;
}
