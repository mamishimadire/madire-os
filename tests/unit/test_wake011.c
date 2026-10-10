/* Unit tests for the 0.11 early wake (sched_wake, kernel/proc/sched_core.h):
 * a kill or Ctrl+C ends a process's nanosleep at once (Q7). */
#include <stdint.h>
#include "kernel/proc/sched_core.h"
#include "tests/unit/unit.h"

static struct sched s;

/* Thread 0 hands the CPU to `tid`, which then sleeps until `wake`. */
static int32_t sleeper(uint64_t wake)
{
    int32_t tid = -1, next = -1;
    sched_init(&s);
    CHECK_EQ(sched_alloc(&s, false, &tid), SCHED_OK);
    CHECK_EQ(sched_start(&s, tid), SCHED_OK);
    sched_yield(&s, 0, &next);                  /* thread 0 -> tid */
    CHECK_EQ(s.current, tid);
    CHECK_EQ(sched_sleep(&s, 0, wake, &next), SCHED_BLOCKED_NOW);
    CHECK_EQ(next, 0);
    CHECK_EQ(s.t[tid].state, SCHED_BLOCKED);
    return tid;
}

static void test_wakes_a_sleeper_at_once(void)
{
    int32_t tid = sleeper(1000);
    s.need_resched = false;
    CHECK_EQ(sched_wake(&s, tid), SCHED_OK);
    CHECK_EQ(s.t[tid].state, SCHED_READY);
    CHECK_EQ(s.t[tid].block, SCHED_BLOCK_NONE);
    CHECK(s.need_resched);                      /* it should run soon */
    CHECK_EQ(s.head, tid);                      /* on the run queue */
    CHECK_EQ(sched_ready_count(&s), 1);
    /* Its old wake tick no longer matters: the tick does not queue it twice. */
    sched_tick(&s, 1000);
    CHECK_EQ(sched_ready_count(&s), 1);
    int32_t next = -1;
    sched_yield(&s, 0, &next);                  /* thread 0 -> the woken thread */
    CHECK_EQ(next, tid);
    CHECK_EQ(s.current, tid);
}

static void test_only_sleepers(void)
{
    int32_t tid = sleeper(50);
    CHECK_EQ(sched_wake(&s, tid), SCHED_OK);
    CHECK_EQ(sched_wake(&s, tid), SCHED_ERR_STATE);      /* already ready */
    CHECK_EQ(sched_wake(&s, 0), SCHED_ERR_STATE);        /* thread 0 never sleeps */
    CHECK_EQ(sched_wake(&s, 5), SCHED_ERR_NO_THREAD);    /* free slot */
    CHECK_EQ(sched_wake(&s, -1), SCHED_ERR_NO_THREAD);
    CHECK_EQ(sched_wake(&s, SCHED_MAX_THREADS), SCHED_ERR_NO_THREAD);
}

static void test_a_joiner_is_not_woken(void)
{
    /* A thread blocked in join is not sleeping: only its target's exit
     * wakes it. */
    int32_t a = -1, b = -1, next = -1, code = 0;
    sched_init(&s);
    CHECK_EQ(sched_alloc(&s, false, &a), SCHED_OK);
    CHECK_EQ(sched_alloc(&s, false, &b), SCHED_OK);
    CHECK_EQ(sched_start(&s, a), SCHED_OK);
    CHECK_EQ(sched_start(&s, b), SCHED_OK);
    sched_yield(&s, 0, &next);                  /* -> a */
    CHECK_EQ(s.current, a);
    CHECK_EQ(sched_join(&s, 0, b, &code, &next), SCHED_BLOCKED_NOW);
    CHECK_EQ(sched_wake(&s, a), SCHED_ERR_STATE);
    CHECK_EQ(s.t[a].state, SCHED_BLOCKED);
    CHECK_EQ(s.t[a].block, SCHED_BLOCK_JOIN);
}

static void test_other_sleepers_keep_sleeping(void)
{
    int32_t a = -1, b = -1, next = -1;
    sched_init(&s);
    CHECK_EQ(sched_alloc(&s, false, &a), SCHED_OK);
    CHECK_EQ(sched_alloc(&s, false, &b), SCHED_OK);
    CHECK_EQ(sched_start(&s, a), SCHED_OK);
    CHECK_EQ(sched_start(&s, b), SCHED_OK);
    sched_yield(&s, 0, &next);                  /* -> a */
    CHECK_EQ(sched_sleep(&s, 0, 100, &next), SCHED_BLOCKED_NOW);   /* a sleeps */
    CHECK_EQ(s.current, b);
    CHECK_EQ(sched_sleep(&s, 0, 200, &next), SCHED_BLOCKED_NOW);   /* b sleeps */
    CHECK_EQ(sched_wake(&s, b), SCHED_OK);
    CHECK_EQ(s.t[a].state, SCHED_BLOCKED);      /* a still asleep until 100 */
    CHECK_EQ(s.t[a].wake_tick, 100);
    sched_tick(&s, 100);
    CHECK_EQ(s.t[a].state, SCHED_READY);
}

int main(void)
{
    RUN(test_wakes_a_sleeper_at_once);
    RUN(test_only_sleepers);
    RUN(test_a_joiner_is_not_woken);
    RUN(test_other_sleepers_keep_sleeping);
    return unit_report("wake011");
}
