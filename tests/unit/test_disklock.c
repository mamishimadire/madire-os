/* Unit tests for the per-disk I/O lock's pure part,
 * kernel/proc/disklock_core.c (1.5 stage 4; docs/plans/1.5.md, C1): the
 * tickets (FIFO order, wrap-around, the errors the kernel side turns into
 * panics), the owner, the order check (L4, L1, L3, L6 in that order), L5
 * (who may use the AHCI port and the read cache), the AHCI wait loop's
 * yield rule (never with preemption off) and the figures. */
#include <stdint.h>
#include "kernel/proc/disklock_core.c"
#include "tests/unit/unit.h"

static struct disklock l;

static void reset_on(void)
{
    disklock_core_init(&l);
    l.on = true;
}

/* Takes a ticket for `tid` (must succeed) and returns it. */
static uint32_t ticket(int32_t tid)
{
    uint32_t t = 0xDEADu;
    CHECK_EQ(disklock_core_ticket(&l, tid, &t), DISKLOCK_QUEUED);
    return t;
}

static enum disklock_result take(int32_t tid, uint64_t now)
{
    return disklock_core_try(&l, tid, ticket(tid), now, now, 0);
}

static enum sched_state all_ready(int32_t tid)
{
    (void)tid;
    return SCHED_READY;
}

static int32_t blocked_tid = -100;

static enum sched_state one_blocked(int32_t tid)
{
    return tid == blocked_tid ? SCHED_BLOCKED : SCHED_READY;
}

static void test_init_off_free(void)
{
    disklock_core_init(&l);
    CHECK(!l.on);
    CHECK(l.owner == DISKLOCK_FREE);
    CHECK_EQ(l.next, 0);
    CHECK_EQ(l.serving, 0);
    CHECK_EQ(l.takes, 0);
    CHECK(!disklock_core_held_by(&l, 0));
}

/* A free lock is taken; another thread's ticket is busy while it is held
 * and is served after the release. */
static void test_take_busy_release(void)
{
    reset_on();
    CHECK_EQ(take(3, 10), DISKLOCK_TAKEN);
    CHECK_EQ(l.owner, 3);
    CHECK(disklock_core_held_by(&l, 3));
    CHECK(!disklock_core_held_by(&l, 4));
    uint32_t t4 = ticket(4), t0 = ticket(0);
    CHECK_EQ(disklock_core_ahead(&l, t4), 1);
    CHECK_EQ(disklock_core_ahead(&l, t0), 2);
    CHECK_EQ(disklock_core_try(&l, 4, t4, 11, 11, 0), DISKLOCK_BUSY);
    CHECK_EQ(disklock_core_try(&l, 0, t0, 11, 11, 0), DISKLOCK_BUSY);
    CHECK_EQ(disklock_core_release(&l, 3, 12), DISKLOCK_RELEASED);
    CHECK(l.owner == DISKLOCK_FREE);
    CHECK_EQ(disklock_core_try(&l, 0, t0, 13, 11, 1), DISKLOCK_BUSY);  /* FIFO: 4 first */
    CHECK_EQ(disklock_core_try(&l, 4, t4, 13, 11, 1), DISKLOCK_TAKEN);
    CHECK_EQ(disklock_core_release(&l, 4, 14), DISKLOCK_RELEASED);
    CHECK_EQ(disklock_core_try(&l, 0, t0, 20, 11, 3), DISKLOCK_TAKEN);
    CHECK_EQ(l.takes, 3);
    CHECK_EQ(l.contended, 2);
    CHECK_EQ(l.wait_longest, 9);
    CHECK_EQ(l.wait_longest0, 9);
    CHECK_EQ(l.hold_longest, 2);
}

/* Tickets wrap at 2^32: compared by equality, distances unsigned. */
static void test_wrap(void)
{
    reset_on();
    l.next = l.serving = 0xFFFFFFFEu;
    CHECK_EQ(take(1, 0), DISKLOCK_TAKEN);
    uint32_t t2 = ticket(2), t5 = ticket(5);
    CHECK_EQ(t2, 0xFFFFFFFFu);
    CHECK_EQ(t5, 0u);
    CHECK_EQ(disklock_core_ahead(&l, t5), 2);
    CHECK_EQ(disklock_core_release(&l, 1, 1), DISKLOCK_RELEASED);
    CHECK_EQ(disklock_core_try(&l, 2, t2, 2, 0, 1), DISKLOCK_TAKEN);
    CHECK_EQ(disklock_core_release(&l, 2, 3), DISKLOCK_RELEASED);
    CHECK_EQ(disklock_core_try(&l, 5, t5, 4, 0, 1), DISKLOCK_TAKEN);
    CHECK_EQ(l.serving, 0u);
}

/* The errors: a second ticket or take by the owner (L3), a release by a
 * thread not holding it, a ticket that is not the caller's, bad ids, more
 * tickets than threads. Nothing changes on an error. */
static void test_errors(void)
{
    reset_on();
    CHECK_EQ(take(2, 0), DISKLOCK_TAKEN);
    uint32_t t = 0;
    CHECK_EQ(disklock_core_ticket(&l, 2, &t), DISKLOCK_ERR_RECURSIVE);
    CHECK_EQ(disklock_core_release(&l, 6, 1), DISKLOCK_ERR_NOT_OWNER);
    CHECK_EQ(l.owner, 2);
    uint32_t t6 = ticket(6);
    CHECK_EQ(disklock_core_ticket(&l, 6, &t), DISKLOCK_ERR_RECURSIVE);   /* its ticket is out */
    CHECK_EQ(disklock_core_try(&l, 7, t6, 1, 1, 0), DISKLOCK_ERR_NOT_TICKET);
    CHECK_EQ(disklock_core_try(&l, 6, t6 + 5, 1, 1, 0), DISKLOCK_ERR_NOT_TICKET);
    CHECK_EQ(disklock_core_ticket(&l, -1, &t), DISKLOCK_ERR_BAD_TID);
    CHECK_EQ(disklock_core_ticket(&l, SCHED_MAX_THREADS, &t), DISKLOCK_ERR_BAD_TID);
    CHECK_EQ(disklock_core_release(&l, SCHED_MAX_THREADS, 1), DISKLOCK_ERR_BAD_TID);
    CHECK_EQ(l.next - l.serving, 2);

    reset_on();
    for (int32_t i = 0; i < SCHED_MAX_THREADS; i++)
        (void)ticket(i);                            /* every thread's ticket out */
    CHECK_EQ(l.next - l.serving, DISKLOCK_TICKETS);
    CHECK_EQ(disklock_core_ticket(&l, 0, &t), DISKLOCK_ERR_RECURSIVE);
    for (uint32_t i = 0; i < DISKLOCK_TICKETS; i++)
        l.ticket_tid[i] = DISKLOCK_FREE;            /* threads unknown: only the count is full */
    CHECK_EQ(disklock_core_ticket(&l, 9, &t), DISKLOCK_ERR_FULL);
}

/* The order check: L4, then L1, then L3, then L6; OK otherwise; it changes
 * nothing. */
static void test_order_check(void)
{
    reset_on();
    CHECK_EQ(disklock_core_check(&l, 0, false, true), DISKLOCK_OK);
    CHECK_EQ(disklock_core_check(&l, 0, false, false), DISKLOCK_ERR_IRQ);       /* L4 */
    CHECK_EQ(disklock_core_check(&l, 0, true, false), DISKLOCK_ERR_IRQ);        /* L4 before L1 */
    CHECK_EQ(disklock_core_check(&l, 0, true, true), DISKLOCK_ERR_ORDER);       /* L1 */
    CHECK_EQ(take(0, 0), DISKLOCK_TAKEN);
    CHECK_EQ(disklock_core_check(&l, 0, false, true), DISKLOCK_ERR_RECURSIVE);  /* L3 */
    CHECK_EQ(disklock_core_check(&l, 0, true, true), DISKLOCK_ERR_ORDER);       /* L1 before L3 */
    CHECK_EQ(disklock_core_check(&l, 5, false, true), DISKLOCK_OK);             /* another thread */
    uint32_t t5 = ticket(5);
    (void)t5;
    CHECK_EQ(disklock_core_check(&l, 5, false, true), DISKLOCK_ERR_RECURSIVE);  /* its ticket is out */
    CHECK_EQ(l.owner, 0);
    CHECK_EQ(l.next - l.serving, 2);
    disklock_core_init(&l);                                                     /* off: Live mode */
    CHECK_EQ(disklock_core_check(&l, 0, false, true), DISKLOCK_ERR_LIVE);       /* L6 */
    CHECK_EQ(disklock_core_check(&l, 0, true, true), DISKLOCK_ERR_ORDER);       /* L1 before L6 */
    CHECK_EQ(disklock_core_check(&l, -1, false, true), DISKLOCK_ERR_BAD_TID);
}

/* L5: in Live mode anyone (the boot's probe); in Disk mode only the owner. */
static void test_io_rule(void)
{
    disklock_core_init(&l);
    CHECK(disklock_core_io_ok(&l, 0));
    CHECK(disklock_core_io_ok(&l, 7));
    l.on = true;
    CHECK(!disklock_core_io_ok(&l, 0));
    CHECK_EQ(take(7, 0), DISKLOCK_TAKEN);
    CHECK(disklock_core_io_ok(&l, 7));
    CHECK(!disklock_core_io_ok(&l, 0));
}

/* The AHCI wait loop yields only for the owner, at preemption count 0,
 * interrupts on, in Disk mode: never inside a VFS hold (count >= 1). */
static void test_may_yield(void)
{
    reset_on();
    CHECK(!disklock_core_may_yield(&l, 2, 0, true));        /* not the owner */
    CHECK_EQ(take(2, 0), DISKLOCK_TAKEN);
    CHECK(disklock_core_may_yield(&l, 2, 0, true));
    CHECK(!disklock_core_may_yield(&l, 2, 1, true));        /* a VFS hold: preemption off */
    CHECK(!disklock_core_may_yield(&l, 2, 5, true));
    CHECK(!disklock_core_may_yield(&l, 2, 0, false));       /* interrupts off */
    CHECK(!disklock_core_may_yield(&l, 3, 0, true));
    l.on = false;
    CHECK(!disklock_core_may_yield(&l, 2, 0, true));        /* Live mode */
}

/* A waiter behind a holder or ticket whose thread is not ready. */
static void test_ahead_bad(void)
{
    reset_on();
    CHECK_EQ(take(4, 0), DISKLOCK_TAKEN);
    uint32_t t8 = ticket(8), t1 = ticket(1);
    enum sched_state bs = SCHED_READY;
    CHECK_EQ(disklock_core_ahead_bad(&l, t1, all_ready, &bs), DISKLOCK_FREE);
    blocked_tid = 8;
    CHECK_EQ(disklock_core_ahead_bad(&l, t1, one_blocked, &bs), 8);
    CHECK_EQ(bs, SCHED_BLOCKED);
    CHECK_EQ(disklock_core_ahead_bad(&l, t8, one_blocked, &bs), DISKLOCK_FREE);   /* only the holder ahead */
    blocked_tid = 4;
    CHECK_EQ(disklock_core_ahead_bad(&l, t8, one_blocked, &bs), 4);                /* the holder itself */
}

/* A ticket ahead whose slot names no thread: DISKLOCK_NO_THREAD (not a
 * thread id, so the kernel never names it), with SCHED_FREE. */
static void test_ahead_bad_empty_slot(void)
{
    reset_on();
    CHECK_EQ(take(4, 0), DISKLOCK_TAKEN);
    uint32_t t8 = ticket(8), t1 = ticket(1);
    l.ticket_tid[t8 % DISKLOCK_TICKETS] = DISKLOCK_FREE;
    enum sched_state bs = SCHED_READY;
    int32_t bad = disklock_core_ahead_bad(&l, t1, all_ready, &bs);
    CHECK_EQ(bad, DISKLOCK_NO_THREAD);
    CHECK(bad < 0 && bad != DISKLOCK_FREE);
    CHECK_EQ(bs, SCHED_FREE);
}

/* Held or waiting: what syscall return and thread exit refuse. */
static void test_involved(void)
{
    reset_on();
    CHECK(!disklock_core_involved(&l, 2));
    CHECK_EQ(take(2, 0), DISKLOCK_TAKEN);
    CHECK(disklock_core_involved(&l, 2));       /* the owner */
    uint32_t t6 = ticket(6);
    (void)t6;
    CHECK(disklock_core_involved(&l, 6));       /* a ticket out */
    CHECK(!disklock_core_involved(&l, 0));
    CHECK(!disklock_core_involved(&l, -1));
    CHECK_EQ(disklock_core_release(&l, 2, 1), DISKLOCK_RELEASED);
    CHECK(!disklock_core_involved(&l, 2));
    struct disklock z = { .owner = DISKLOCK_FREE };     /* the kernel's static before the start */
    CHECK(!disklock_core_involved(&z, 0));
    CHECK(!disklock_core_held_by(&z, 0));
}

/* The waits counted, thread 0's apart. */
static void test_waited(void)
{
    reset_on();
    disklock_core_waited(&l, 3);
    disklock_core_waited(&l, 0);
    disklock_core_waited(&l, 0);
    CHECK_EQ(l.yields, 3);
    CHECK_EQ(l.yields0, 2);
}

/* The rule texts: the panics' first lines (C6b). */
static void test_texts(void)
{
    const char *want[4] = { "disk lock taken while the VFS lock is held (L1)",
                            "disk lock taken again by its owner (L3)",
                            "disk lock taken in an interrupt handler (L4)",
                            "disk lock taken in Live mode (L6)" };
    enum disklock_result r[4] = { DISKLOCK_ERR_ORDER, DISKLOCK_ERR_RECURSIVE, DISKLOCK_ERR_IRQ,
                                  DISKLOCK_ERR_LIVE };
    for (int i = 0; i < 4; i++) {
        const char *a = disklock_result_str(r[i]), *b = want[i];
        while (*a && *a == *b) {
            a++;
            b++;
        }
        CHECK(*a == 0 && *b == 0);
    }
}

int main(void)
{
    printf("disklock_core unit tests\n");
    RUN(test_init_off_free);
    RUN(test_take_busy_release);
    RUN(test_wrap);
    RUN(test_errors);
    RUN(test_order_check);
    RUN(test_io_rule);
    RUN(test_may_yield);
    RUN(test_ahead_bad);
    RUN(test_ahead_bad_empty_slot);
    RUN(test_involved);
    RUN(test_waited);
    RUN(test_texts);
    return unit_report("disklock_core");
}
