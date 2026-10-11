/* Unit tests for the bounded diagnostic waits, kernel/proc/diagwait_core.c
 * (0.12 stage 3): a wait ends at its limit in ticks, or as a stall once the
 * tick count has not moved for 100 x max(the most polls it saw within one
 * tick, the boot calibration) polls; 50,000,000 without a calibration. */
#include <stdint.h>
#include "kernel/proc/diagwait_core.h"
#include "tests/unit/unit.h"

static struct diag_wait w;

/* Polls `n` times at tick `now`; returns the first state that is not
 * DIAG_WAITING (or DIAG_WAITING after n polls). */
static enum diag_state polls(uint64_t now, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) {
        enum diag_state st = diag_core_poll(&w, now);
        if (st != DIAG_WAITING)
            return st;
    }
    return DIAG_WAITING;
}

/* A duration: due exactly when `limit` ticks have passed. */
static void test_due_at_limit(void)
{
    diag_core_begin(&w, 1000, 3, 50);
    CHECK_EQ(polls(1000, 10), DIAG_WAITING);
    CHECK_EQ(polls(1001, 10), DIAG_WAITING);
    CHECK_EQ(polls(1002, 10), DIAG_WAITING);
    CHECK_EQ(diag_core_poll(&w, 1003), DIAG_DUE);
    CHECK_EQ(w.polls, 31);
    diag_core_begin(&w, 1000, 3, 50);         /* ticks may jump past the limit */
    CHECK_EQ(diag_core_poll(&w, 1007), DIAG_DUE);
}

/* Limit 0: due at once; limit 1: due as soon as the tick count moves. */
static void test_limit_zero_and_one(void)
{
    diag_core_begin(&w, 500, 0, 50);
    CHECK_EQ(diag_core_poll(&w, 500), DIAG_DUE);
    diag_core_begin(&w, 500, 1, 50);
    CHECK_EQ(polls(500, 1000), DIAG_WAITING);
    CHECK_EQ(diag_core_poll(&w, 501), DIAG_DUE);
}

/* Calibrated, no tick seen: the bound is 100 x the calibration, exactly. */
static void test_stall_at_calibrated_bound(void)
{
    diag_core_begin(&w, 7, 1000, 40);
    CHECK_EQ(diag_core_bound(&w), 4000);
    CHECK_EQ(polls(7, 3999), DIAG_WAITING);
    CHECK_EQ(diag_core_poll(&w, 7), DIAG_STALLED);
    CHECK_EQ(w.still, 4000);
    CHECK_EQ(w.polls, 4000);
}

/* The wait's own rate raises the bound above the calibration. */
static void test_own_rate_raises_bound(void)
{
    diag_core_begin(&w, 0, 1000, 10);
    CHECK_EQ(polls(0, 600), DIAG_WAITING);      /* 600 polls within tick 0 */
    CHECK_EQ(polls(1, 1), DIAG_WAITING);        /* the poll that sees the move */
    CHECK_EQ(w.own_max, 600);
    CHECK_EQ(diag_core_bound(&w), 60000);
    CHECK_EQ(polls(1, 59999), DIAG_WAITING);
    CHECK_EQ(diag_core_poll(&w, 1), DIAG_STALLED);
}

/* A slow wait (fewer polls per tick than the calibration) keeps the
 * calibrated bound: its rate rising later is no stall while ticks move. */
static void test_rising_rate_no_false_stall(void)
{
    diag_core_begin(&w, 0, 1000, 5000);
    for (uint64_t t = 0; t < 20; t++)
        CHECK_EQ(polls(t, 1), DIAG_WAITING);    /* 1 poll per tick (e.g. a yield) */
    for (uint64_t t = 20; t < 40; t++)
        CHECK_EQ(polls(t, 5000), DIAG_WAITING); /* then 5000 per tick, ticks moving */
    CHECK_EQ(w.polls, 20 + 20 * 5000);
}

/* Moving ticks reset the stall count, however many polls in all. */
static void test_moving_ticks_never_stall(void)
{
    diag_core_begin(&w, 0, 1000000, 100);
    for (uint64_t t = 0; t < 500; t++)
        CHECK_EQ(polls(t, 9000), DIAG_WAITING);
    CHECK(w.polls > 100ull * DIAG_STALL_FACTOR * 100);
}

/* Due is decided first: the poll that sees the limit reached is due, even
 * after many polls without a tick just before it. */
static void test_due_before_stall(void)
{
    diag_core_begin(&w, 0, 1, 1);
    CHECK_EQ(polls(0, 99), DIAG_WAITING);
    CHECK_EQ(diag_core_poll(&w, 1), DIAG_DUE);
}

/* Not calibrated: 50,000,000 polls, before and after a tick. */
static void test_uncalibrated_bound(void)
{
    diag_core_begin(&w, 0, 1000, 0);
    CHECK_EQ(diag_core_bound(&w), DIAG_STALL_FIRST);
    CHECK_EQ(polls(0, 10), DIAG_WAITING);
    CHECK_EQ(polls(1, 1), DIAG_WAITING);
    CHECK_EQ(diag_core_bound(&w), DIAG_STALL_FIRST);
    CHECK_EQ(polls(1, DIAG_STALL_FIRST - 1), DIAG_WAITING);
    CHECK_EQ(diag_core_poll(&w, 1), DIAG_STALLED);
    CHECK_EQ(w.still, DIAG_STALL_FIRST);
}

/* A wait begun at tick count T counts only its own polls. */
static void test_begin_resets(void)
{
    diag_core_begin(&w, 3, 10, 2);
    polls(3, 150);
    diag_core_begin(&w, 9, 10, 2);
    CHECK_EQ(w.polls, 0);
    CHECK_EQ(w.still, 0);
    CHECK_EQ(w.own_max, 0);
    CHECK_EQ(w.start, 9);
    CHECK_EQ(w.last, 9);
    CHECK_EQ(diag_core_bound(&w), 200);
}

int main(void)
{
    printf("test_diagwait\n");
    RUN(test_due_at_limit);
    RUN(test_limit_zero_and_one);
    RUN(test_stall_at_calibrated_bound);
    RUN(test_own_rate_raises_bound);
    RUN(test_rising_rate_no_false_stall);
    RUN(test_moving_ticks_never_stall);
    RUN(test_due_before_stall);
    RUN(test_uncalibrated_bound);
    RUN(test_begin_resets);
    return unit_report("test_diagwait");
}
