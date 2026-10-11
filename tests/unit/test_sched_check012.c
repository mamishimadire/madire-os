/* Unit tests for the 0.12 stage 2b self-check record in kernel/proc/
 * sched_core.c: a thread taken off the run queue must be READY; any other
 * is counted in bad_picks (with its tid and state), and thread.c panics
 * before switching to it. Normal scheduling never records one. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "kernel/proc/sched_core.h"
#include "tests/unit/unit.h"

static struct sched s;

static int32_t start_thread(void)
{
    int32_t tid = -1;
    CHECK_EQ(sched_alloc(&s, false, &tid), SCHED_OK);
    CHECK_EQ(sched_start(&s, tid), SCHED_OK);
    return tid;
}

static void test_init(void)
{
    memset(&s, 0x5A, sizeof(s));
    sched_init(&s);
    CHECK_EQ(s.bad_picks, 0);
    CHECK_EQ(s.bad_pick_tid, SCHED_NONE);
    CHECK_EQ(s.bad_pick_state, SCHED_FREE);
}

/* Yields, sleeps, wake-ups, preemptions, exits and joins: no bad pick. */
static void test_normal_use_records_nothing(void)
{
    int32_t next;
    sched_init(&s);
    int32_t a = start_thread(), b = start_thread(), c = start_thread();
    for (int i = 0; i < 300; i++) {
        CHECK_EQ(sched_yield(&s, 0, &next), SCHED_OK);
        if (s.current != 0 && i % 7 == 0)
            sched_sleep(&s, 0, (uint64_t)i + 2, &next);
        sched_tick(&s, (uint64_t)i + 1);
        sched_preempt(&s, 0, &next);
    }
    for (int i = 0; i < 1000 && s.current != a; i++) {
        sched_tick(&s, 1000 + (uint64_t)i);
        sched_yield(&s, 0, &next);
    }
    CHECK_EQ(s.current, a);
    CHECK_EQ(sched_exit(&s, 7, &next), SCHED_OK);
    for (int i = 0; i < 20; i++) {
        sched_tick(&s, 2000 + (uint64_t)i);
        sched_yield(&s, 0, &next);
    }
    (void)b;
    (void)c;
    CHECK_EQ(s.bad_picks, 0);
    CHECK_EQ(s.bad_pick_tid, SCHED_NONE);
}

/* A thread on the run queue whose state is not READY (a corrupted queue,
 * as test=selfcheckpick makes it in the kernel): taking it off records it. */
static void test_bad_pick_recorded(void)
{
    int32_t next = -1;
    sched_init(&s);
    int32_t t = start_thread();
    s.t[t].state = SCHED_BLOCKED;
    CHECK_EQ(sched_yield(&s, 0, &next), SCHED_OK);
    CHECK_EQ(next, t);
    CHECK_EQ(s.bad_picks, 1);
    CHECK_EQ(s.bad_pick_tid, t);
    CHECK_EQ(s.bad_pick_state, SCHED_BLOCKED);

    sched_init(&s);
    t = start_thread();
    s.t[t].state = SCHED_ZOMBIE;
    sched_yield(&s, 0, &next);
    CHECK_EQ(s.bad_picks, 1);
    CHECK_EQ(s.bad_pick_state, SCHED_ZOMBIE);

    sched_init(&s);
    t = start_thread();
    s.t[t].state = SCHED_FREE;
    sched_yield(&s, 0, &next);
    CHECK_EQ(s.bad_picks, 1);
    CHECK_EQ(s.bad_pick_state, SCHED_FREE);
}

/* The record is appended: the fields tests/qemu_capture.py reads by
 * offset (current at 3584, need_resched at 3596) did not move. */
static void test_capture_offsets_unchanged(void)
{
    CHECK_EQ(offsetof(struct sched, current), 3584);
    CHECK_EQ(offsetof(struct sched, need_resched), 3596);
    CHECK(offsetof(struct sched, bad_picks) > offsetof(struct sched, live));
}

int main(void)
{
    printf("scheduler self-check record (0.12 stage 2b) unit tests\n");
    RUN(test_init);
    RUN(test_normal_use_records_nothing);
    RUN(test_bad_pick_recorded);
    RUN(test_capture_offsets_unchanged);
    return unit_report("sched_check012");
}
