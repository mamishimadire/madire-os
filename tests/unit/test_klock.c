/* Unit tests for the kernel lock's pure part, kernel/proc/klock_core.c
 * (1.0, stage 3, D10): taking and releasing, the errors the kernel side
 * turns into panics, the owner rule, the wait method (every thread waits by
 * yielding, thread 0 above all) and the figures test=klock checks. */
#include <stdint.h>
#include "kernel/proc/klock_core.h"
#include "tests/unit/unit.h"

static struct klock l;

static void test_init_free(void)
{
    klock_core_init(&l);
    CHECK(l.owner == KLOCK_FREE);
    CHECK_EQ(l.takes, 0);
    CHECK_EQ(l.hold_longest, 0);
    CHECK(!klock_core_held_by(&l, 0));
    CHECK(!klock_core_held_by(&l, 3));
}

/* A free lock is taken; while held, another thread finds it busy and the
 * lock is unchanged; after the release the other thread takes it. */
static void test_take_busy_release(void)
{
    klock_core_init(&l);
    CHECK_EQ(klock_core_try(&l, 2, 100, 100, 0), KLOCK_TAKEN);
    CHECK_EQ(l.owner, 2);
    CHECK(klock_core_held_by(&l, 2));
    CHECK(!klock_core_held_by(&l, 5));
    CHECK_EQ(klock_core_try(&l, 5, 101, 101, 0), KLOCK_BUSY);
    CHECK_EQ(l.owner, 2);
    CHECK_EQ(l.takes, 1);
    CHECK_EQ(klock_core_try(&l, 0, 101, 101, 0), KLOCK_BUSY);   /* thread 0 too */
    CHECK_EQ(klock_core_release(&l, 2, 102, 0), KLOCK_RELEASED);
    CHECK(l.owner == KLOCK_FREE);
    CHECK_EQ(klock_core_try(&l, 5, 103, 101, 1), KLOCK_TAKEN);
    CHECK_EQ(l.owner, 5);
    CHECK_EQ(l.takes, 2);
}

/* Taking it twice is an error and leaves the lock held as it was. */
static void test_recursive(void)
{
    klock_core_init(&l);
    CHECK_EQ(klock_core_try(&l, 4, 10, 10, 0), KLOCK_TAKEN);
    CHECK_EQ(klock_core_try(&l, 4, 11, 11, 0), KLOCK_ERR_RECURSIVE);
    CHECK_EQ(l.owner, 4);
    CHECK_EQ(l.takes, 1);
    CHECK_EQ(l.since, 10);
    klock_core_init(&l);
    CHECK_EQ(klock_core_try(&l, 0, 10, 10, 0), KLOCK_TAKEN);
    CHECK_EQ(klock_core_try(&l, 0, 10, 10, 0), KLOCK_ERR_RECURSIVE);
}

/* Releasing a free lock, or one another thread holds, is an error and
 * changes nothing. */
static void test_release_not_owner(void)
{
    klock_core_init(&l);
    CHECK_EQ(klock_core_release(&l, 1, 5, 0), KLOCK_ERR_NOT_OWNER);
    CHECK(l.owner == KLOCK_FREE);
    CHECK_EQ(klock_core_try(&l, 1, 5, 5, 0), KLOCK_TAKEN);
    CHECK_EQ(klock_core_release(&l, 7, 6, 0), KLOCK_ERR_NOT_OWNER);
    CHECK_EQ(klock_core_release(&l, 0, 6, 0), KLOCK_ERR_NOT_OWNER);
    CHECK_EQ(l.owner, 1);
    CHECK_EQ(l.hold_longest, 0);
    CHECK_EQ(klock_core_release(&l, 1, 6, 0), KLOCK_RELEASED);
    CHECK_EQ(klock_core_release(&l, 1, 6, 0), KLOCK_ERR_NOT_OWNER);   /* twice */
}

static void test_bad_tid(void)
{
    klock_core_init(&l);
    CHECK_EQ(klock_core_try(&l, -1, 0, 0, 0), KLOCK_ERR_BAD_TID);
    CHECK_EQ(klock_core_try(&l, SCHED_MAX_THREADS, 0, 0, 0), KLOCK_ERR_BAD_TID);
    CHECK_EQ(klock_core_release(&l, -1, 0, 0), KLOCK_ERR_BAD_TID);
    CHECK(l.owner == KLOCK_FREE);
    CHECK_EQ(klock_core_try(&l, SCHED_MAX_THREADS - 1, 0, 0, 0), KLOCK_TAKEN);
}

/* Every thread waits by yielding: no sleep queue, and never a blocking
 * wait for thread 0. */
static void test_wait_method(void)
{
    CHECK_EQ(klock_core_wait_method(0), KLOCK_WAIT_YIELD);
    for (int32_t t = 1; t < SCHED_MAX_THREADS; t++)
        CHECK_EQ(klock_core_wait_method(t), KLOCK_WAIT_YIELD);
}

/* Only a ready owner (on the run queue) can be waited for. */
static void test_owner_rule(void)
{
    CHECK(klock_core_owner_ok(SCHED_READY));
    CHECK(!klock_core_owner_ok(SCHED_RUNNING));
    CHECK(!klock_core_owner_ok(SCHED_BLOCKED));
    CHECK(!klock_core_owner_ok(SCHED_ZOMBIE));
    CHECK(!klock_core_owner_ok(SCHED_FREE));
    CHECK(!klock_core_owner_ok(SCHED_NEW));
}

/* Holds: the longest in ticks, and the highest preemption count at a
 * release. */
static void test_hold_figures(void)
{
    klock_core_init(&l);
    klock_core_try(&l, 1, 100, 100, 0);
    klock_core_release(&l, 1, 101, 0);
    CHECK_EQ(l.hold_longest, 1);
    klock_core_try(&l, 1, 200, 200, 0);
    klock_core_release(&l, 1, 203, 2);
    CHECK_EQ(l.hold_longest, 3);
    CHECK_EQ(l.release_pc_max, 2);
    klock_core_try(&l, 1, 300, 300, 0);
    klock_core_release(&l, 1, 300, 0);
    CHECK_EQ(l.hold_longest, 3);
    CHECK_EQ(l.release_pc_max, 2);
    CHECK_EQ(l.takes, 3);
    CHECK_EQ(l.contended, 0);
}

/* Waits: contended takes, the yields of everyone and of thread 0, and the
 * longest wait (any thread, thread 0). */
static void test_wait_figures(void)
{
    klock_core_init(&l);
    klock_core_try(&l, 3, 10, 10, 0);
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(klock_core_try(&l, 0, 11, 10, (uint32_t)i), KLOCK_BUSY);
        klock_core_waited(&l, 0);
    }
    CHECK_EQ(klock_core_try(&l, 5, 11, 11, 0), KLOCK_BUSY);
    klock_core_waited(&l, 5);
    klock_core_release(&l, 3, 12, 0);
    CHECK_EQ(klock_core_try(&l, 0, 12, 10, 4), KLOCK_TAKEN);
    CHECK_EQ(l.wait_longest0, 2);
    CHECK_EQ(l.wait_longest, 2);
    klock_core_release(&l, 0, 12, 0);
    CHECK_EQ(klock_core_try(&l, 5, 17, 11, 1), KLOCK_TAKEN);
    CHECK_EQ(l.wait_longest, 6);
    CHECK_EQ(l.wait_longest0, 2);
    CHECK_EQ(l.yields, 5);
    CHECK_EQ(l.yields0, 4);
    CHECK_EQ(l.takes, 3);
    CHECK_EQ(l.contended, 2);
}

/* A model of many threads taking turns: whatever the order of tries, at
 * most one holder at a time, and a counter changed only by the holder
 * ends at the number of takes (the property test=klock checks in QEMU). */
static void test_mutual_exclusion_model(void)
{
    klock_core_init(&l);
    uint32_t seed = 12345, holders = 0, counter = 0, takes = 0, bad = 0;
    int32_t held_by = KLOCK_FREE;
    for (uint64_t now = 0; now < 20000; now++) {
        seed = seed * 1103515245u + 12345u;
        int32_t t = (int32_t)((seed >> 8) % 7);
        if (held_by == t) {
            counter++;
            CHECK_EQ(klock_core_release(&l, t, now, 0), KLOCK_RELEASED);
            held_by = KLOCK_FREE;
            holders--;
        } else {
            enum klock_result r = klock_core_try(&l, t, now, now, 0);
            if (r == KLOCK_TAKEN) {
                if (held_by != KLOCK_FREE)
                    bad++;
                held_by = t;
                holders++;
                takes++;
            } else if (r != KLOCK_BUSY || held_by == KLOCK_FREE) {
                bad++;
            }
        }
        if (holders > 1)
            bad++;
    }
    CHECK_EQ(bad, 0);
    CHECK(takes > 1000);
    CHECK_EQ(counter + (held_by != KLOCK_FREE ? 1 : 0), takes);
    CHECK_EQ(l.takes, takes);
}

static void test_result_strings(void)
{
    CHECK(klock_result_str(KLOCK_ERR_RECURSIVE)[0] == 't');
    CHECK(klock_result_str(KLOCK_ERR_NOT_OWNER)[0] == 'r');
    CHECK(klock_result_str((enum klock_result)99)[0] == '?');
}

int main(void)
{
    RUN(test_init_free);
    RUN(test_take_busy_release);
    RUN(test_recursive);
    RUN(test_release_not_owner);
    RUN(test_bad_tid);
    RUN(test_wait_method);
    RUN(test_owner_rule);
    RUN(test_hold_figures);
    RUN(test_wait_figures);
    RUN(test_mutual_exclusion_model);
    RUN(test_result_strings);
    return unit_report("test_klock");
}
