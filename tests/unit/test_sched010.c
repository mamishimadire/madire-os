/* Unit tests for the 0.10 change to sched_order_check() (kernel/proc/
 * sched_core.c): a preemption that lands after a thread has logged its turn
 * but before it yields shows as a wrong turn only later (after every other
 * thread of the cycle has logged once more), so a wrong turn is accepted
 * when the preemption count rose within the previous k turns (k = the
 * cycle's length; 2 for the ping-pong). The 0.9 cases in test_sched.c are
 * unchanged. Each sequence below is what the kernel's turn logic produces
 * (log_turn, then thread_yield; a preempted thread goes to the queue's
 * tail). */
#include <stdint.h>
#include <string.h>
#include "kernel/proc/sched_core.h"
#include "tests/unit/unit.h"

#define A 1
#define B 2
#define ROUNDS 50

static struct sched_turn log_[2 * ROUNDS + 8];
static uint32_t n;

static void add(int32_t tid, uint64_t pre)
{
    log_[n].tid = tid;
    log_[n].preemptions = pre;
    n++;
}

static const int32_t pp[2] = { A, B };

/* The boot self-test's failure (run 19 of the stage-2 repeats): A logs its
 * turn at round k, is preempted before thread_yield (count 0 -> 1), B logs
 * (expected), B yields, A finishes its yield without logging, B logs again
 * (the wrong turn), then A and B alternate. */
static void build_preempted_after_log(int k, int rise_at)
{
    n = 0;
    int a = 0, b = 0;
    for (int r = 0; r < k; r++) {
        add(A, 0);
        a++;
        add(B, 0);
        b++;
    }
    add(A, 0);                      /* A's turn k, then the preemption */
    a++;
    add(B, 1);                      /* B's turn k: expected, count already 1 */
    b++;
    add(B, 1);                      /* B again: the wrong turn */
    b++;
    while (a < ROUNDS || b < ROUNDS) {
        if (a < ROUNDS) {
            add(A, 1);
            a++;
        }
        if (b < ROUNDS) {
            add(B, 1);
            b++;
        }
    }
    /* rise_at >= 0: make the count rise earlier instead (at that index),
     * for the negative cases. */
    if (rise_at >= 0)
        for (uint32_t i = 0; i < n; i++)
            log_[i].preemptions = (int)i >= rise_at ? 1 : 0;
}

static void test_preempted_after_logging_is_accepted(void)
{
    uint32_t br = 99;
    build_preempted_after_log(10, -1);
    CHECK_EQ(n, 2 * ROUNDS);
    /* The 0.9 rule compared with the previous turn only (B's turn k, count
     * already 1) and called this BAD_ORDER: a correct kernel failed the
     * fatal boot self-test. */
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_RELAXED);
    CHECK_EQ(br, 1);
    build_preempted_after_log(0, -1);           /* at the very first turn */
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_RELAXED);
    build_preempted_after_log(48, -1);          /* near the end */
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_RELAXED);
}

static void test_nothing_looser_than_two_turns(void)
{
    uint32_t br = 99;
    /* The wrong turn is at index 2k + 2. A rise at index 2k + 1 or 2k + 2
     * (within the previous two turns) explains it; a rise at 2k (A's turn,
     * three turns earlier counting the wrong turn's own entry as the last)
     * does not: the count is then the same two turns back. */
    int k = 10, wrong = 2 * k + 2;
    build_preempted_after_log(k, wrong);        /* rose at the wrong turn itself */
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_RELAXED);
    build_preempted_after_log(k, wrong - 1);    /* rose one turn before */
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_RELAXED);
    build_preempted_after_log(k, wrong - 2);    /* rose two turns before: too early */
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_BAD_ORDER);
    build_preempted_after_log(k, wrong - 5);
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_BAD_ORDER);
    build_preempted_after_log(k, 1000);         /* never rose */
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_BAD_ORDER);
}

/* test=pingpongbad mode 1: A gives its turn at round 10 away without
 * logging it (no preemption): B, B. Must still fail. */
static void test_pingpongbad_skip_still_fails(void)
{
    uint32_t br;
    n = 0;
    int a = 0, b = 0;
    for (int r = 0; r < 10; r++) {
        add(A, 0);
        a++;
        add(B, 0);
        b++;
    }
    add(B, 0);                      /* A yielded unlogged: B's turn 10 */
    b++;
    while (a < ROUNDS || b < ROUNDS) {
        if (a < ROUNDS) {
            add(A, 0);
            a++;
        }
        if (b < ROUNDS) {
            add(B, 0);
            b++;
        }
    }
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_BAD_ORDER);
}

/* test=pingpongbad mode 2: A takes an extra turn: A has 51. */
static void test_pingpongbad_repeat_still_fails(void)
{
    uint32_t br;
    n = 0;
    for (int r = 0; r < ROUNDS; r++) {
        if (r == 10)
            add(A, 0);
        add(A, 0);
        add(B, 0);
    }
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_BAD_COUNT);
}

/* test=pingpongpreempt: A runs past its slice before logging round 10 and
 * is preempted then (after B's turn 9): B, B, with the rise in between. */
static void test_pingpongpreempt_still_passes(void)
{
    uint32_t br = 99;
    n = 0;
    for (int r = 0; r < 10; r++) {
        add(A, 0);
        add(B, 0);
    }
    add(B, 1);
    for (int r = 10; r < ROUNDS; r++) {
        add(A, 1);
        if (r + 1 < ROUNDS)
            add(B, 1);
    }
    CHECK_EQ(n, 2 * ROUNDS);
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_RELAXED);
    CHECK_EQ(br, 1);
}

static void test_strict_unchanged(void)
{
    uint32_t br = 99;
    n = 0;
    for (int r = 0; r < ROUNDS; r++) {
        add(A, (uint64_t)r);        /* preemptions that changed nothing */
        add(B, (uint64_t)r);
    }
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_STRICT);
    CHECK_EQ(br, 0);
    n = 0;
    for (int r = 0; r < ROUNDS; r++) {
        add(A, 0);
        add(B, 0);
    }
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_STRICT);
    /* First turn not cycle[0] without a preemption: still BAD_ORDER. */
    log_[0].tid = B;
    log_[1].tid = A;
    CHECK_EQ(sched_order_check(log_, n, pp, 2, ROUNDS, 0, &br), SCHED_ORDER_BAD_ORDER);
}

/* Three threads (test=threads has eight): A preempted after logging, before
 * yielding. B and C log, A finishes its yield, B logs again (the wrong
 * turn), and the rotation continues shifted: A B C B C A B C A. The rise
 * is three turns before the wrong turn: a two-turn window would reject it,
 * the cycle-length window (3) accepts it; a rise four turns before does not
 * count. */
static void test_three_thread_cycle(void)
{
    static const int32_t c3[3] = { 1, 2, 3 };
    static const int32_t seq[9] = { 1, 2, 3, 2, 3, 1, 2, 3, 1 };
    uint32_t br = 99;
    for (int i = 0; i < 9; i++) {
        log_[i].tid = seq[i];
        log_[i].preemptions = i >= 1 ? 1 : 0;   /* the rise: just after A's first turn */
    }
    CHECK_EQ(sched_order_check(log_, 9, c3, 3, 3, 0, &br), SCHED_ORDER_RELAXED);
    CHECK_EQ(br, 1);
    for (int i = 0; i < 9; i++)
        log_[i].preemptions = 1;                /* rose before the first turn (pre_start 0) */
    CHECK_EQ(sched_order_check(log_, 9, c3, 3, 3, 1, &br), SCHED_ORDER_BAD_ORDER);
    /* A shifted rotation with no preemption at all: BAD_ORDER. */
    for (int i = 0; i < 9; i++)
        log_[i].preemptions = 0;
    CHECK_EQ(sched_order_check(log_, 9, c3, 3, 3, 0, &br), SCHED_ORDER_BAD_ORDER);
}

int main(void)
{
    RUN(test_preempted_after_logging_is_accepted);
    RUN(test_nothing_looser_than_two_turns);
    RUN(test_pingpongbad_skip_still_fails);
    RUN(test_pingpongbad_repeat_still_fails);
    RUN(test_pingpongpreempt_still_passes);
    RUN(test_strict_unchanged);
    RUN(test_three_thread_cycle);
    return unit_report("sched010 (order check)");
}
