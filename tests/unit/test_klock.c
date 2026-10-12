/* Unit tests for the kernel lock's pure part, kernel/proc/klock_core.c
 * (1.0, stage 3, D10; a FIFO ticket lock since the stage 3 fix): taking and
 * releasing, FIFO order, ticket wrap-around, the errors the kernel side
 * turns into panics, the owner and abandoned-ticket rule, the wait method
 * (every thread waits by yielding, thread 0 above all) and the figures
 * test=klock checks. */
#include <stdint.h>
#include "kernel/proc/klock_core.h"
#include "tests/unit/unit.h"

static struct klock l;

/* Takes a ticket for `tid` (must succeed) and returns it. */
static uint32_t ticket(int32_t tid)
{
    uint32_t t = 0xDEADu;
    CHECK_EQ(klock_core_ticket(&l, tid, &t), KLOCK_QUEUED);
    return t;
}

/* A ticket, then a try at once: the usual uncontended take. */
static enum klock_result take(int32_t tid, uint64_t now)
{
    return klock_core_try(&l, tid, ticket(tid), now, now, 0);
}

static void test_init_free(void)
{
    klock_core_init(&l);
    CHECK(l.owner == KLOCK_FREE);
    CHECK_EQ(l.takes, 0);
    CHECK_EQ(l.hold_longest, 0);
    CHECK_EQ(l.next, 0);
    CHECK_EQ(l.serving, 0);
    CHECK(!klock_core_held_by(&l, 0));
    CHECK(!klock_core_held_by(&l, 3));
}

/* A free lock is taken; while held, another thread's ticket is busy and
 * the lock is unchanged; after the release that thread takes it. */
static void test_take_busy_release(void)
{
    klock_core_init(&l);
    CHECK_EQ(take(2, 100), KLOCK_TAKEN);
    CHECK_EQ(l.owner, 2);
    CHECK(klock_core_held_by(&l, 2));
    CHECK(!klock_core_held_by(&l, 5));
    uint32_t t5 = ticket(5), t0 = ticket(0);
    CHECK_EQ(klock_core_try(&l, 5, t5, 101, 101, 0), KLOCK_BUSY);
    CHECK_EQ(l.owner, 2);
    CHECK_EQ(l.takes, 1);
    CHECK_EQ(klock_core_try(&l, 0, t0, 101, 101, 0), KLOCK_BUSY);   /* thread 0 too */
    CHECK_EQ(klock_core_release(&l, 2, 102, 0), KLOCK_RELEASED);
    CHECK(l.owner == KLOCK_FREE);
    CHECK_EQ(klock_core_try(&l, 5, t5, 103, 101, 1), KLOCK_TAKEN);
    CHECK_EQ(l.owner, 5);
    CHECK_EQ(l.takes, 2);
}

/* FIFO: threads hold the lock in the order they took their tickets. A
 * later ticket is busy even while the lock is free between a release and
 * the next take; the thread that released cannot take it again ahead of
 * the waiters (the starvation of the first version). */
static void test_fifo_order(void)
{
    klock_core_init(&l);
    CHECK_EQ(take(0, 10), KLOCK_TAKEN);
    uint32_t t1 = ticket(1), t2 = ticket(2), t3 = ticket(3);
    CHECK_EQ(klock_core_ahead(&l, t1), 1);
    CHECK_EQ(klock_core_ahead(&l, t3), 3);
    CHECK_EQ(klock_core_release(&l, 0, 11, 0), KLOCK_RELEASED);
    uint32_t t0 = ticket(0);                    /* thread 0 asks again at once */
    CHECK_EQ(klock_core_ahead(&l, t0), 3);
    CHECK_EQ(klock_core_try(&l, 0, t0, 11, 11, 0), KLOCK_BUSY);
    CHECK_EQ(klock_core_try(&l, 3, t3, 11, 11, 0), KLOCK_BUSY);
    CHECK_EQ(klock_core_try(&l, 2, t2, 11, 11, 0), KLOCK_BUSY);
    CHECK_EQ(klock_core_try(&l, 1, t1, 11, 11, 0), KLOCK_TAKEN);
    CHECK_EQ(klock_core_release(&l, 1, 12, 0), KLOCK_RELEASED);
    CHECK_EQ(klock_core_try(&l, 0, t0, 12, 11, 1), KLOCK_BUSY);
    CHECK_EQ(klock_core_try(&l, 3, t3, 12, 11, 1), KLOCK_BUSY);
    CHECK_EQ(klock_core_try(&l, 2, t2, 12, 11, 1), KLOCK_TAKEN);
    CHECK_EQ(klock_core_release(&l, 2, 13, 0), KLOCK_RELEASED);
    CHECK_EQ(klock_core_try(&l, 0, t0, 13, 11, 2), KLOCK_BUSY);
    CHECK_EQ(klock_core_try(&l, 3, t3, 13, 11, 2), KLOCK_TAKEN);
    CHECK_EQ(klock_core_release(&l, 3, 14, 0), KLOCK_RELEASED);
    CHECK_EQ(klock_core_try(&l, 0, t0, 14, 11, 3), KLOCK_TAKEN);
    CHECK_EQ(l.ahead_max0, 3);
    CHECK_EQ(l.wait_longest0, 3);
}

/* Tickets wrap at 2^32: order, distances and the ticket table stay right
 * across the wrap. */
static void test_ticket_wrap(void)
{
    klock_core_init(&l);
    l.next = l.serving = 0xFFFFFFFEu;
    CHECK_EQ(take(4, 1), KLOCK_TAKEN);          /* ticket 0xFFFFFFFE */
    uint32_t a = ticket(5), b = ticket(6), c = ticket(7);
    CHECK_EQ(a, 0xFFFFFFFFu);
    CHECK_EQ(b, 0u);
    CHECK_EQ(c, 1u);
    CHECK_EQ(klock_core_ahead(&l, b), 2);
    CHECK_EQ(klock_core_ahead(&l, c), 3);
    CHECK_EQ(klock_core_release(&l, 4, 2, 0), KLOCK_RELEASED);
    CHECK_EQ(klock_core_try(&l, 6, b, 2, 1, 1), KLOCK_BUSY);
    CHECK_EQ(klock_core_try(&l, 5, a, 2, 1, 1), KLOCK_TAKEN);
    CHECK_EQ(klock_core_release(&l, 5, 3, 0), KLOCK_RELEASED);
    CHECK_EQ(l.serving, 0u);
    CHECK_EQ(klock_core_try(&l, 7, c, 3, 1, 2), KLOCK_BUSY);
    CHECK_EQ(klock_core_try(&l, 6, b, 3, 1, 2), KLOCK_TAKEN);
    CHECK_EQ(klock_core_release(&l, 6, 4, 0), KLOCK_RELEASED);
    CHECK_EQ(klock_core_try(&l, 7, c, 4, 1, 3), KLOCK_TAKEN);
    CHECK_EQ(klock_core_release(&l, 7, 5, 0), KLOCK_RELEASED);
    CHECK_EQ(l.serving, 2u);
    CHECK_EQ(l.next, 2u);
    CHECK_EQ(l.takes, 4);
}

/* Taking it twice is an error and leaves the lock held as it was. */
static void test_recursive(void)
{
    klock_core_init(&l);
    CHECK_EQ(take(4, 10), KLOCK_TAKEN);
    uint32_t t = 0;
    CHECK_EQ(klock_core_ticket(&l, 4, &t), KLOCK_ERR_RECURSIVE);
    CHECK_EQ(klock_core_try(&l, 4, 0, 11, 11, 0), KLOCK_ERR_RECURSIVE);
    CHECK_EQ(l.owner, 4);
    CHECK_EQ(l.takes, 1);
    CHECK_EQ(l.since, 10);
    CHECK_EQ(l.next, 1);
    klock_core_init(&l);
    CHECK_EQ(take(0, 10), KLOCK_TAKEN);
    CHECK_EQ(klock_core_ticket(&l, 0, &t), KLOCK_ERR_RECURSIVE);
}

/* Releasing a free lock, or one another thread holds, is an error and
 * changes nothing. */
static void test_release_not_owner(void)
{
    klock_core_init(&l);
    CHECK_EQ(klock_core_release(&l, 1, 5, 0), KLOCK_ERR_NOT_OWNER);
    CHECK(l.owner == KLOCK_FREE);
    CHECK_EQ(take(1, 5), KLOCK_TAKEN);
    CHECK_EQ(klock_core_release(&l, 7, 6, 0), KLOCK_ERR_NOT_OWNER);
    CHECK_EQ(klock_core_release(&l, 0, 6, 0), KLOCK_ERR_NOT_OWNER);
    CHECK_EQ(l.owner, 1);
    CHECK_EQ(l.serving, 0);
    CHECK_EQ(l.hold_longest, 0);
    CHECK_EQ(klock_core_release(&l, 1, 6, 0), KLOCK_RELEASED);
    CHECK_EQ(klock_core_release(&l, 1, 6, 0), KLOCK_ERR_NOT_OWNER);   /* twice */
    CHECK_EQ(l.serving, 1);
}

/* Not a thread id; a try with a ticket that is not the caller's; more
 * tickets out than threads. */
static void test_bad_requests(void)
{
    klock_core_init(&l);
    uint32_t t = 0;
    CHECK_EQ(klock_core_ticket(&l, -1, &t), KLOCK_ERR_BAD_TID);
    CHECK_EQ(klock_core_ticket(&l, SCHED_MAX_THREADS, &t), KLOCK_ERR_BAD_TID);
    CHECK_EQ(klock_core_try(&l, -1, 0, 0, 0, 0), KLOCK_ERR_BAD_TID);
    CHECK_EQ(klock_core_release(&l, -1, 0, 0), KLOCK_ERR_BAD_TID);
    CHECK_EQ(l.next, 0);
    CHECK_EQ(klock_core_try(&l, 3, 0, 0, 0, 0), KLOCK_ERR_NOT_TICKET);   /* no ticket */
    uint32_t t3 = ticket(3);
    CHECK_EQ(klock_core_try(&l, 9, t3, 0, 0, 0), KLOCK_ERR_NOT_TICKET);  /* 3's ticket */
    CHECK_EQ(klock_core_try(&l, 3, t3 + 5, 0, 0, 0), KLOCK_ERR_NOT_TICKET);
    CHECK(l.owner == KLOCK_FREE);
    CHECK_EQ(klock_core_try(&l, 3, t3, 0, 0, 0), KLOCK_TAKEN);
    klock_core_init(&l);
    for (int32_t i = 0; i < KLOCK_TICKETS; i++)
        ticket(i);
    CHECK_EQ(klock_core_ticket(&l, 5, &t), KLOCK_ERR_FULL);
    CHECK_EQ(l.next, KLOCK_TICKETS);
}

/* Every thread waits by yielding: no sleep queue, and never a blocking
 * wait for thread 0. */
static void test_wait_method(void)
{
    CHECK_EQ(klock_core_wait_method(0), KLOCK_WAIT_YIELD);
    for (int32_t t = 1; t < SCHED_MAX_THREADS; t++)
        CHECK_EQ(klock_core_wait_method(t), KLOCK_WAIT_YIELD);
}

/* Only a ready thread may hold the lock or a ticket ahead of a waiter. */
static void test_owner_rule(void)
{
    CHECK(klock_core_owner_ok(SCHED_READY));
    CHECK(!klock_core_owner_ok(SCHED_RUNNING));
    CHECK(!klock_core_owner_ok(SCHED_BLOCKED));
    CHECK(!klock_core_owner_ok(SCHED_ZOMBIE));
    CHECK(!klock_core_owner_ok(SCHED_FREE));
    CHECK(!klock_core_owner_ok(SCHED_NEW));
}

static enum sched_state states[SCHED_MAX_THREADS];

static enum sched_state fake_state(int32_t tid)
{
    return states[tid];
}

/* The abandoned-ticket self-check: the holder or any ticket ahead whose
 * thread can never take its turn is named, with its state; tickets behind
 * the waiter's own do not count. */
static void test_abandoned_ticket(void)
{
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
        states[i] = SCHED_READY;
    klock_core_init(&l);
    enum sched_state bs = SCHED_READY;
    CHECK_EQ(take(1, 0), KLOCK_TAKEN);
    uint32_t t2 = ticket(2), t3 = ticket(3), t0 = ticket(0), t4 = ticket(4);
    (void)t2;
    (void)t3;
    CHECK(klock_core_ahead_bad(&l, t0, fake_state, &bs) == KLOCK_FREE);
    static const enum sched_state bad[] = { SCHED_BLOCKED, SCHED_ZOMBIE, SCHED_FREE, SCHED_NEW };
    for (unsigned k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        states[3] = bad[k];                     /* a ticket ahead of thread 0 */
        CHECK_EQ(klock_core_ahead_bad(&l, t0, fake_state, &bs), 3);
        CHECK_EQ(bs, bad[k]);
        states[3] = SCHED_READY;
        states[1] = bad[k];                     /* the holder */
        CHECK_EQ(klock_core_ahead_bad(&l, t0, fake_state, &bs), 1);
        CHECK_EQ(bs, bad[k]);
        states[1] = SCHED_READY;
    }
    states[4] = SCHED_BLOCKED;                  /* behind thread 0: not its concern */
    CHECK(klock_core_ahead_bad(&l, t0, fake_state, &bs) == KLOCK_FREE);
    CHECK_EQ(klock_core_ahead_bad(&l, t4 + 1, fake_state, &bs), 4);
    states[4] = SCHED_READY;
    CHECK(klock_core_ahead_bad(&l, l.serving, fake_state, &bs) == KLOCK_FREE);   /* none ahead */
}

/* Holds: the longest in ticks, and the highest preemption count at a
 * release. */
static void test_hold_figures(void)
{
    klock_core_init(&l);
    take(1, 100);
    klock_core_release(&l, 1, 101, 0);
    CHECK_EQ(l.hold_longest, 1);
    take(1, 200);
    klock_core_release(&l, 1, 203, 2);
    CHECK_EQ(l.hold_longest, 3);
    CHECK_EQ(l.release_pc_max, 2);
    take(1, 300);
    klock_core_release(&l, 1, 300, 0);
    CHECK_EQ(l.hold_longest, 3);
    CHECK_EQ(l.release_pc_max, 2);
    CHECK_EQ(l.takes, 3);
    CHECK_EQ(l.contended, 0);
}

/* Waits: contended takes, the yields of everyone and of thread 0, the
 * longest wait (any thread, thread 0) and the tickets ahead of thread 0. */
static void test_wait_figures(void)
{
    klock_core_init(&l);
    take(3, 10);
    uint32_t t0 = ticket(0), t5 = ticket(5);
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(klock_core_try(&l, 0, t0, 11, 10, (uint32_t)i), KLOCK_BUSY);
        klock_core_waited(&l, 0);
    }
    CHECK_EQ(klock_core_try(&l, 5, t5, 11, 11, 0), KLOCK_BUSY);
    klock_core_waited(&l, 5);
    klock_core_release(&l, 3, 12, 0);
    CHECK_EQ(klock_core_try(&l, 0, t0, 12, 10, 4), KLOCK_TAKEN);
    CHECK_EQ(l.wait_longest0, 2);
    CHECK_EQ(l.wait_longest, 2);
    CHECK_EQ(l.ahead_max0, 1);
    klock_core_release(&l, 0, 12, 0);
    CHECK_EQ(klock_core_try(&l, 5, t5, 17, 11, 1), KLOCK_TAKEN);
    CHECK_EQ(l.wait_longest, 6);
    CHECK_EQ(l.wait_longest0, 2);
    CHECK_EQ(l.yields, 5);
    CHECK_EQ(l.yields0, 4);
    CHECK_EQ(l.takes, 3);
    CHECK_EQ(l.contended, 2);
}

/* A model of 7 threads, each asking again as soon as it releases (thread
 * 0's pattern in the failed run): at most one holder at a time, a counter
 * changed only by the holder ends at the number of takes, and every
 * thread gets its turn in ticket order (fairness: takes per thread differ
 * by at most one). */
static void test_mutual_exclusion_model(void)
{
    klock_core_init(&l);
    uint32_t tickets[7], takes[7] = { 0 }, seed = 12345, counter = 0, bad = 0, total = 0;
    int32_t held_by = KLOCK_FREE;
    for (int32_t t = 0; t < 7; t++)
        tickets[t] = ticket(t);
    for (uint64_t now = 0; now < 20000; now++) {
        seed = seed * 1103515245u + 12345u;
        int32_t t = (int32_t)((seed >> 8) % 7);
        if (held_by == t) {
            counter++;
            CHECK_EQ(klock_core_release(&l, t, now, 0), KLOCK_RELEASED);
            held_by = KLOCK_FREE;
            tickets[t] = ticket(t);             /* asks again at once */
        } else {
            enum klock_result r = klock_core_try(&l, t, tickets[t], now, now, 0);
            if (r == KLOCK_TAKEN) {
                if (held_by != KLOCK_FREE)
                    bad++;
                held_by = t;
                takes[t]++;
                total++;
            } else if (r != KLOCK_BUSY) {
                bad++;
            }
        }
    }
    CHECK_EQ(bad, 0);
    CHECK(total > 1000);
    CHECK_EQ(counter + (held_by != KLOCK_FREE ? 1 : 0), total);
    CHECK_EQ(l.takes, total);
    uint32_t lo = takes[0], hi = takes[0];
    for (int i = 1; i < 7; i++) {
        lo = takes[i] < lo ? takes[i] : lo;
        hi = takes[i] > hi ? takes[i] : hi;
    }
    CHECK(hi - lo <= 1);
}

static void test_result_strings(void)
{
    CHECK(klock_result_str(KLOCK_ERR_RECURSIVE)[0] == 't');
    CHECK(klock_result_str(KLOCK_ERR_NOT_OWNER)[0] == 'r');
    CHECK(klock_result_str(KLOCK_ERR_FULL)[0] == 'm');
    CHECK(klock_result_str((enum klock_result)99)[0] == '?');
}

int main(void)
{
    RUN(test_init_free);
    RUN(test_take_busy_release);
    RUN(test_fifo_order);
    RUN(test_ticket_wrap);
    RUN(test_recursive);
    RUN(test_release_not_owner);
    RUN(test_bad_requests);
    RUN(test_wait_method);
    RUN(test_owner_rule);
    RUN(test_abandoned_ticket);
    RUN(test_hold_figures);
    RUN(test_wait_figures);
    RUN(test_mutual_exclusion_model);
    RUN(test_result_strings);
    return unit_report("test_klock");
}
