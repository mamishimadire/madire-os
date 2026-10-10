/* Unit test for 0.11 stage 4a.1: the ping-pong order the boot self-test
 * logged under load (300 loaded boots, all three kernels; this exact
 * sequence each time), and the verdict sched_order_check gives it. Thread
 * 0 was preempted between creating A and B, so A took its first two turns
 * alone (A A B A ... B), and the 50-turns-each rule forces a compensating
 * B B at the end, 98 turns after the only preemption: rejected. The
 * kernel's fix creates the whole cycle with preemption off; the checker is
 * unchanged. */
#include <stdint.h>
#include "kernel/proc/sched_core.h"
#include "tests/unit/unit.h"

#define ROUNDS 50
#define TURNS  (2 * ROUNDS)

static struct sched_turn seq[TURNS];
static const int32_t cycle[2] = { 1, 2 };       /* A = thread 1, B = thread 2 */

/* The logged sequence: A:1 A:1 B:1 A:1 B:1 ... A:1 B:1 B:1, preemptions 0
 * at the start. */
static void logged(void)
{
    seq[0].tid = 1;
    for (uint32_t i = 1; i < TURNS - 1; i++)
        seq[i].tid = (i % 2) ? 1 : 2;       /* turns 2..99: A B A B ... B */
    seq[TURNS - 1].tid = 2;
    for (uint32_t i = 0; i < TURNS; i++)
        seq[i].preemptions = 1;
}

static void test_logged_sequence_is_rejected(void)
{
    logged();
    uint32_t a = 0, b = 0;
    for (uint32_t i = 0; i < TURNS; i++) {
        a += seq[i].tid == 1;
        b += seq[i].tid == 2;
    }
    CHECK_EQ(a, ROUNDS);                    /* 50 turns each: the count is right */
    CHECK_EQ(b, ROUNDS);
    CHECK(seq[0].tid == 1 && seq[1].tid == 1 && seq[2].tid == 2);
    CHECK(seq[TURNS - 2].tid == 2 && seq[TURNS - 1].tid == 2);
    uint32_t breaks = 99;
    CHECK_EQ(sched_order_check(seq, TURNS, cycle, 2, ROUNDS, 0, &breaks), SCHED_ORDER_BAD_ORDER);
}

static void test_the_early_break_alone_is_explained(void)
{
    /* Turn 2's break (A again) comes right after the preemption: accepted.
     * Only the end, B B with no new preemption, is rejected. */
    logged();
    seq[TURNS - 1].preemptions = 2;         /* had a preemption come there... */
    uint32_t breaks = 0;
    CHECK_EQ(sched_order_check(seq, TURNS, cycle, 2, ROUNDS, 0, &breaks), SCHED_ORDER_RELAXED);
    CHECK_EQ(breaks, 2);
}

static void test_a_cycle_queued_first_is_strict(void)
{
    /* With A and B both queued before either runs, the same preemption
     * leaves the order intact. */
    for (uint32_t i = 0; i < TURNS; i++) {
        seq[i].tid = (i % 2) ? 2 : 1;
        seq[i].preemptions = 1;
    }
    uint32_t breaks = 99;
    CHECK_EQ(sched_order_check(seq, TURNS, cycle, 2, ROUNDS, 0, &breaks), SCHED_ORDER_STRICT);
    CHECK_EQ(breaks, 0);
}

static void test_pingpongbad_stays_strict(void)
{
    /* test=pingpongbad: a skipped or repeated turn with no preemption is
     * still rejected. */
    for (uint32_t i = 0; i < TURNS; i++) {
        seq[i].tid = (i % 2) ? 2 : 1;
        seq[i].preemptions = 7;
    }
    seq[20].tid = 2;                        /* B twice, then A twice: a swap */
    seq[21].tid = 1;
    uint32_t breaks = 0;
    CHECK_EQ(sched_order_check(seq, TURNS, cycle, 2, ROUNDS, 7, &breaks), SCHED_ORDER_BAD_ORDER);
}

int main(void)
{
    RUN(test_logged_sequence_is_rejected);
    RUN(test_the_early_break_alone_is_explained);
    RUN(test_a_cycle_queued_first_is_strict);
    RUN(test_pingpongbad_stays_strict);
    return unit_report("order011");
}
