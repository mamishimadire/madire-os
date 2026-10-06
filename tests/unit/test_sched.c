/* Unit tests for kernel/proc/sched_core.c: thread states, round robin,
 * slices, preemption decisions, sleep and join (0.9). */
#include <stdint.h>
#include <string.h>
#include "kernel/proc/sched_core.h"
#include "tests/unit/unit.h"

static struct sched s;

/* Allocates and starts a thread; returns its tid. */
static int32_t spawn(bool detached)
{
    int32_t tid = SCHED_NONE;
    CHECK_EQ(sched_alloc(&s, detached, &tid), SCHED_OK);
    CHECK_EQ(sched_start(&s, tid), SCHED_OK);
    return tid;
}

/* Yields until `tid` is current (bounded). */
static void run_until(int32_t tid)
{
    int32_t next;
    for (int i = 0; i < 2 * SCHED_MAX_THREADS && s.current != tid; i++)
        CHECK_EQ(sched_yield(&s, 0, &next), SCHED_OK);
    CHECK_EQ(s.current, tid);
}

static void test_init(void)
{
    sched_init(&s);
    CHECK_EQ(s.current, 0);
    CHECK_EQ(s.t[0].state, SCHED_RUNNING);
    CHECK_EQ(s.t[0].slice_left, SCHED_SLICE_TICKS);
    CHECK_EQ(s.live, 1);
    CHECK(sched_queue_empty(&s));
    CHECK_EQ(s.switches, 0);
    CHECK(!s.need_resched);
    for (int i = 1; i < SCHED_MAX_THREADS; i++)
        CHECK_EQ(s.t[i].state, SCHED_FREE);
    CHECK_EQ(SCHED_SLICE_TICKS, 5);         /* 50 ms at 100 Hz */
}

static void test_alloc_start_limits(void)
{
    sched_init(&s);
    int32_t tid = SCHED_NONE;
    for (int32_t i = 1; i < SCHED_MAX_THREADS; i++) {
        CHECK_EQ(sched_alloc(&s, false, &tid), SCHED_OK);
        CHECK_EQ(tid, i);                   /* lowest free slot first */
        CHECK_EQ(s.t[i].state, SCHED_NEW);
    }
    CHECK_EQ(s.live, SCHED_MAX_THREADS);
    CHECK_EQ(sched_alloc(&s, false, &tid), SCHED_ERR_FULL);

    CHECK_EQ(sched_abandon(&s, 7), SCHED_OK);
    CHECK_EQ(s.t[7].state, SCHED_FREE);
    CHECK_EQ(sched_alloc(&s, false, &tid), SCHED_OK);
    CHECK_EQ(tid, 7);                       /* the freed slot is reused */

    CHECK_EQ(sched_start(&s, 3), SCHED_OK);
    CHECK_EQ(s.t[3].state, SCHED_READY);
    CHECK_EQ(sched_start(&s, 3), SCHED_ERR_STATE);
    CHECK_EQ(sched_abandon(&s, 3), SCHED_ERR_STATE);    /* only NEW */
    CHECK_EQ(sched_start(&s, -1), SCHED_ERR_NO_THREAD);
    CHECK_EQ(sched_start(&s, SCHED_MAX_THREADS), SCHED_ERR_NO_THREAD);
    CHECK_EQ(sched_start(&s, 0), SCHED_ERR_STATE);      /* thread 0 is running */
    CHECK_EQ(sched_abandon(&s, 3000), SCHED_ERR_NO_THREAD);

    sched_init(&s);
    CHECK_EQ(sched_start(&s, 5), SCHED_ERR_NO_THREAD);  /* FREE slot */
}

static void test_round_robin(void)
{
    sched_init(&s);
    spawn(false);
    spawn(false);
    spawn(false);
    CHECK_EQ(sched_ready_count(&s), 3);
    static const int32_t want[] = { 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0 };
    int32_t next;
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        CHECK_EQ(sched_yield(&s, 0, &next), SCHED_OK);
        CHECK_EQ(next, want[i]);
        CHECK_EQ(s.current, want[i]);
        CHECK_EQ(s.t[want[i]].state, SCHED_RUNNING);
        CHECK_EQ(sched_ready_count(&s), 3);
    }
    CHECK_EQ(s.switches, 12);
    /* Exactly one thread is RUNNING at any time. */
    unsigned running = 0;
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
        running += s.t[i].state == SCHED_RUNNING;
    CHECK_EQ(running, 1);
}

static void test_yield_alone(void)
{
    sched_init(&s);
    int32_t next = SCHED_NONE;
    CHECK_EQ(sched_yield(&s, 0, &next), SCHED_OK);
    CHECK_EQ(next, 0);
    CHECK_EQ(s.current, 0);
    CHECK_EQ(s.switches, 0);                /* no switch to itself */
    CHECK(sched_queue_empty(&s));
}

static void test_slice_and_need_resched(void)
{
    sched_init(&s);
    /* Alone: the slice runs out but there is nobody to switch to. */
    for (uint64_t t = 1; t <= 10; t++)
        sched_tick(&s, t);
    CHECK(!s.need_resched);
    CHECK_EQ(s.t[0].ticks_run, 10);
    CHECK_EQ(s.t[0].slice_left, 0);
    int32_t next;
    CHECK(!sched_preempt(&s, 0, &next));
    CHECK_EQ(next, 0);

    /* Someone becomes ready with the slice already used up: next tick. */
    int32_t a = spawn(false);
    CHECK(!s.need_resched);
    sched_tick(&s, 11);
    CHECK(s.need_resched);
    CHECK(sched_preempt(&s, 0, &next));
    CHECK_EQ(next, a);
    CHECK(!s.need_resched);
    CHECK_EQ(s.t[a].slice_left, SCHED_SLICE_TICKS);
    CHECK_EQ(s.t[0].state, SCHED_READY);

    /* A fresh slice: need_resched exactly on its 5th tick, not before. */
    for (uint64_t t = 12; t < 12 + SCHED_SLICE_TICKS - 1; t++) {
        sched_tick(&s, t);
        CHECK(!s.need_resched);
    }
    sched_tick(&s, 12 + SCHED_SLICE_TICKS - 1);
    CHECK(s.need_resched);
    CHECK_EQ(s.t[a].ticks_run, SCHED_SLICE_TICKS);
    CHECK(sched_preempt(&s, 0, &next));
    CHECK_EQ(next, 0);
}

static void test_preempt_disabled(void)
{
    sched_init(&s);
    int32_t a = spawn(false);
    for (uint64_t t = 1; t <= SCHED_SLICE_TICKS; t++)
        sched_tick(&s, t);
    CHECK(s.need_resched);
    int32_t next;
    CHECK(!sched_preempt(&s, 1, &next));    /* preemption disabled */
    CHECK_EQ(next, 0);
    CHECK_EQ(s.current, 0);
    CHECK(s.need_resched);                  /* kept for the next safe point */
    CHECK(!sched_preempt(&s, 2, &next));    /* nested */
    CHECK(s.need_resched);
    CHECK(sched_preempt(&s, 0, &next));     /* enabled again */
    CHECK_EQ(next, a);
    CHECK(!s.need_resched);
    /* No request, no switch. */
    CHECK(!sched_preempt(&s, 0, &next));
    CHECK_EQ(next, a);
}

static void test_blocking_refused_with_preemption_off(void)
{
    sched_init(&s);
    int32_t a = spawn(false);
    int32_t b = spawn(false);
    run_until(a);
    uint64_t switches = s.switches;
    int32_t next = 99, code = 99;
    CHECK_EQ(sched_yield(&s, 1, &next), SCHED_ERR_PREEMPT_OFF);
    CHECK_EQ(sched_sleep(&s, 1, 100, &next), SCHED_ERR_PREEMPT_OFF);
    CHECK_EQ(sched_join(&s, 3, b, &code, &next), SCHED_ERR_PREEMPT_OFF);
    /* Nothing changed. */
    CHECK_EQ(s.current, a);
    CHECK_EQ(s.t[a].state, SCHED_RUNNING);
    CHECK_EQ(s.t[a].block, SCHED_BLOCK_NONE);
    CHECK_EQ(s.t[b].joiner, SCHED_NONE);
    CHECK_EQ(s.switches, switches);
    CHECK_EQ(sched_ready_count(&s), 2);
    CHECK_EQ(code, 99);
    /* Also when the target is already a zombie: refused before anything. */
    run_until(b);
    CHECK_EQ(sched_exit(&s, 5, &next), SCHED_OK);
    run_until(a);
    CHECK_EQ(sched_join(&s, 1, b, &code, &next), SCHED_ERR_PREEMPT_OFF);
    CHECK_EQ(s.t[b].state, SCHED_ZOMBIE);
}

static void test_sleep_and_wake_order(void)
{
    sched_init(&s);
    int32_t a = spawn(false), b = spawn(false), c = spawn(false);
    int32_t next;
    run_until(a);
    CHECK_EQ(sched_sleep(&s, 0, 10, &next), SCHED_BLOCKED_NOW);
    CHECK_EQ(s.t[a].state, SCHED_BLOCKED);
    CHECK_EQ(s.t[a].block, SCHED_BLOCK_SLEEP);
    run_until(b);
    CHECK_EQ(sched_sleep(&s, 0, 5, &next), SCHED_BLOCKED_NOW);
    run_until(c);
    CHECK_EQ(sched_sleep(&s, 0, 10, &next), SCHED_BLOCKED_NOW);
    CHECK_EQ(s.current, 0);                 /* only thread 0 is left */
    CHECK(sched_queue_empty(&s));

    sched_tick(&s, 4);
    CHECK(sched_queue_empty(&s));
    CHECK(!s.need_resched);
    sched_tick(&s, 5);                      /* b wakes on exactly its tick */
    CHECK_EQ(s.t[b].state, SCHED_READY);
    CHECK_EQ(s.head, b);
    CHECK(s.need_resched);                  /* a woken thread runs soon */
    CHECK(sched_preempt(&s, 0, &next));
    CHECK_EQ(next, b);

    for (uint64_t t = 6; t < 10; t++)
        sched_tick(&s, t);
    CHECK_EQ(s.t[a].state, SCHED_BLOCKED);
    sched_tick(&s, 10);                     /* a and c: same tick, lower tid first */
    CHECK_EQ(s.t[a].state, SCHED_READY);
    CHECK_EQ(s.t[c].state, SCHED_READY);
    CHECK_EQ(s.head, 0);                    /* thread 0 was requeued before them */
    CHECK_EQ(s.t[0].next, a);
    CHECK_EQ(s.t[a].next, c);

    /* Overdue sleepers (ticks missed) wake earliest wake tick first. */
    sched_init(&s);
    int32_t x = spawn(false), y = spawn(false);
    run_until(x);
    CHECK_EQ(sched_sleep(&s, 0, 7, &next), SCHED_BLOCKED_NOW);
    run_until(y);
    CHECK_EQ(sched_sleep(&s, 0, 6, &next), SCHED_BLOCKED_NOW);
    sched_tick(&s, 20);
    CHECK_EQ(s.head, y);
    CHECK_EQ(s.t[y].next, x);
}

static void test_tick_wrap(void)
{
    sched_init(&s);
    int32_t a = spawn(false);
    int32_t next;
    run_until(a);
    uint64_t start = UINT64_MAX - 1;
    uint64_t wake = start + 3;              /* wraps to 1 */
    CHECK_EQ(wake, 1);
    CHECK_EQ(sched_sleep(&s, 0, wake, &next), SCHED_BLOCKED_NOW);
    sched_tick(&s, UINT64_MAX - 1);
    sched_tick(&s, UINT64_MAX);
    sched_tick(&s, 0);
    CHECK_EQ(s.t[a].state, SCHED_BLOCKED);
    sched_tick(&s, 1);
    CHECK_EQ(s.t[a].state, SCHED_READY);
}

static void test_thread0_never_blocks_or_exits(void)
{
    sched_init(&s);
    int32_t next = 99;
    CHECK_EQ(sched_sleep(&s, 0, 10, &next), SCHED_WOULD_BLOCK);
    CHECK_EQ(s.t[0].state, SCHED_RUNNING);
    CHECK_EQ(s.t[0].block, SCHED_BLOCK_NONE);
    CHECK_EQ(sched_exit(&s, 0, &next), SCHED_ERR_THREAD0);
    CHECK_EQ(s.t[0].state, SCHED_RUNNING);
    int32_t a = spawn(false), code;
    run_until(a);
    CHECK_EQ(sched_join(&s, 0, 0, &code, &next), SCHED_ERR_THREAD0);
    CHECK_EQ(s.current, a);
}

static void test_join(void)
{
    sched_init(&s);
    int32_t a = spawn(false), b = spawn(false), c = spawn(false);
    int32_t next, code = 0;
    run_until(a);
    CHECK_EQ(sched_join(&s, 0, b, &code, &next), SCHED_BLOCKED_NOW);
    CHECK_EQ(s.t[a].state, SCHED_BLOCKED);
    CHECK_EQ(s.t[a].block, SCHED_BLOCK_JOIN);
    CHECK_EQ(s.t[a].join_tid, b);
    CHECK_EQ(s.t[b].joiner, a);
    CHECK(next != a);

    run_until(c);                           /* a second joiner is refused */
    CHECK_EQ(sched_join(&s, 0, b, &code, &next), SCHED_ERR_JOINED);
    CHECK_EQ(sched_join(&s, 0, c, &code, &next), SCHED_ERR_SELF);
    CHECK_EQ(sched_join(&s, 0, -3, &code, &next), SCHED_ERR_NO_THREAD);
    CHECK_EQ(sched_join(&s, 0, 40, &code, &next), SCHED_ERR_NO_THREAD);   /* FREE */

    run_until(b);
    uint32_t live = s.live;
    CHECK_EQ(sched_exit(&s, -7, &next), SCHED_OK);
    CHECK_EQ(s.t[b].state, SCHED_ZOMBIE);
    CHECK_EQ(s.t[a].state, SCHED_READY);    /* the joiner is woken */
    CHECK_EQ(s.t[a].block, SCHED_BLOCK_NONE);
    run_until(a);
    CHECK_EQ(sched_join(&s, 0, b, &code, &next), SCHED_OK);
    CHECK_EQ(code, -7);
    CHECK_EQ(s.t[b].state, SCHED_FREE);
    CHECK_EQ(s.live, live - 1);
    CHECK_EQ(s.t[a].join_tid, SCHED_NONE);

    /* Joining a thread that is already a zombie never blocks. */
    run_until(c);
    CHECK_EQ(sched_exit(&s, 3, &next), SCHED_OK);
    run_until(a);
    CHECK_EQ(sched_join(&s, 0, c, &code, &next), SCHED_OK);
    CHECK_EQ(code, 3);
    CHECK_EQ(next, a);
}

static void test_thread0_join_polls(void)
{
    sched_init(&s);
    int32_t a = spawn(false), next, code = 0;
    CHECK_EQ(sched_join(&s, 0, a, &code, &next), SCHED_WOULD_BLOCK);
    CHECK_EQ(s.t[0].state, SCHED_RUNNING);  /* thread 0 never blocks */
    CHECK_EQ(s.t[a].joiner, 0);
    run_until(a);
    CHECK_EQ(sched_exit(&s, 9, &next), SCHED_OK);
    CHECK_EQ(next, 0);                      /* nobody to wake; thread 0 runs */
    CHECK_EQ(sched_join(&s, 0, a, &code, &next), SCHED_OK);
    CHECK_EQ(code, 9);
    CHECK_EQ(s.live, 1);
}

static void test_exit_codes(void)
{
    static const int32_t codes[] = { 0, 1, 255, 0x7FFFFFFF, -1, (int32_t)0x80000000 };
    for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        sched_init(&s);
        int32_t a = spawn(false), next, code = 12345;
        run_until(a);
        CHECK_EQ(sched_exit(&s, codes[i], &next), SCHED_OK);
        CHECK_EQ(sched_join(&s, 0, a, &code, &next), SCHED_OK);
        CHECK_EQ((uint32_t)code, (uint32_t)codes[i]);
    }
}

static void test_detached(void)
{
    sched_init(&s);
    int32_t a = spawn(true), next, code, tid = SCHED_NONE;
    CHECK_EQ(sched_reap_detached(&s, &tid), SCHED_ERR_NO_THREAD);
    run_until(a);
    CHECK_EQ(sched_exit(&s, 1, &next), SCHED_OK);
    CHECK_EQ(sched_join(&s, 0, a, &code, &next), SCHED_ERR_DETACHED);
    CHECK_EQ(s.t[a].state, SCHED_ZOMBIE);
    CHECK_EQ(sched_reap_detached(&s, &tid), SCHED_OK);
    CHECK_EQ(tid, a);
    CHECK_EQ(s.t[a].state, SCHED_FREE);
    CHECK_EQ(s.live, 1);
    CHECK_EQ(sched_reap_detached(&s, &tid), SCHED_ERR_NO_THREAD);
}

static void test_full_table_churn(void)
{
    sched_init(&s);
    for (int round = 0; round < 100; round++) {
        int32_t tids[SCHED_MAX_THREADS];
        int n = 0;
        int32_t tid;
        while (sched_alloc(&s, false, &tid) == SCHED_OK) {
            CHECK_EQ(sched_start(&s, tid), SCHED_OK);
            tids[n++] = tid;
        }
        CHECK_EQ(n, SCHED_MAX_THREADS - 1);
        CHECK_EQ(s.live, SCHED_MAX_THREADS);
        int32_t next;
        for (int i = 0; i < n; i++) {      /* each runs and exits with its tid */
            run_until(tids[i]);
            CHECK_EQ(sched_exit(&s, tids[i] * 3, &next), SCHED_OK);
        }
        CHECK_EQ(s.current, 0);
        for (int i = 0; i < n; i++) {
            int32_t code = -1;
            CHECK_EQ(sched_join(&s, 0, tids[i], &code, &next), SCHED_OK);
            CHECK_EQ(code, tids[i] * 3);
        }
        CHECK_EQ(s.live, 1);
        CHECK(sched_queue_empty(&s));
    }
    for (int i = 1; i < SCHED_MAX_THREADS; i++)
        CHECK_EQ(s.t[i].state, SCHED_FREE);
}

/* Builds a turn log from tids and per-turn preemption counts. */
static struct sched_turn turns[16];

static uint32_t mk(const int32_t *tid, const uint64_t *pre, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        turns[i].tid = tid[i];
        turns[i].preemptions = pre[i];
    }
    return n;
}

static void test_order_check(void)
{
    static const int32_t c2[] = { 1, 2 }, c3[] = { 1, 2, 3 };
    uint32_t br = 99, n;

    /* Strict: alternating, no preemption. */
    static const int32_t t1[] = { 1, 2, 1, 2, 1, 2 };
    static const uint64_t p0[] = { 0, 0, 0, 0, 0, 0 };
    n = mk(t1, p0, 6);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 0, &br), SCHED_ORDER_STRICT);
    CHECK_EQ(br, 0);
    /* Preemptions that did not change the order: still strict. */
    static const uint64_t p1[] = { 0, 1, 1, 2, 2, 3 };
    n = mk(t1, p1, 6);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 0, &br), SCHED_ORDER_STRICT);

    /* Relaxed: each break right after a preemption. */
    static const int32_t t2[] = { 1, 2, 2, 1, 1, 2 };
    static const uint64_t p2[] = { 0, 0, 1, 1, 2, 2 };
    n = mk(t2, p2, 6);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 0, &br), SCHED_ORDER_RELAXED);
    CHECK_EQ(br, 2);
    /* The same order with one break not explained by a preemption. */
    static const uint64_t p3[] = { 0, 0, 1, 1, 1, 1 };
    n = mk(t2, p3, 6);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 0, &br), SCHED_ORDER_BAD_ORDER);

    /* A repeated turn without any preemption fails. */
    static const int32_t t3[] = { 1, 1, 2, 1, 2, 2 };
    n = mk(t3, p0, 6);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 0, &br), SCHED_ORDER_BAD_ORDER);
    /* A skipped turn: one thread one turn short, the other one over. */
    static const int32_t t4[] = { 1, 2, 2, 1, 2, 2 };
    n = mk(t4, p0, 6);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 0, &br), SCHED_ORDER_BAD_COUNT);
    /* Too few turns; an unknown thread. */
    n = mk(t1, p0, 5);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 0, &br), SCHED_ORDER_BAD_COUNT);
    static const int32_t t5[] = { 1, 2, 1, 7, 1, 2 };
    n = mk(t5, p0, 6);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 0, &br), SCHED_ORDER_BAD_COUNT);

    /* The first turn must be cycle[0], unless a preemption came first. */
    static const int32_t t6[] = { 2, 1, 2, 1, 2, 1 };
    n = mk(t6, p0, 6);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 0, &br), SCHED_ORDER_BAD_ORDER);
    static const uint64_t p6[] = { 5, 5, 5, 5, 5, 5 };
    n = mk(t6, p6, 6);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 4, &br), SCHED_ORDER_RELAXED);
    CHECK_EQ(br, 1);
    CHECK_EQ(sched_order_check(turns, n, c2, 2, 3, 5, &br), SCHED_ORDER_BAD_ORDER);

    /* Three threads: a preemption shifts the rotation once (1 3 1 2 3 2
     * would need a second preemption before the last turn). */
    static const int32_t t7[] = { 1, 3, 1, 2, 3, 2 };
    static const uint64_t p7[] = { 0, 1, 1, 1, 1, 1 };
    n = mk(t7, p7, 6);
    CHECK_EQ(sched_order_check(turns, n, c3, 3, 2, 0, &br), SCHED_ORDER_BAD_ORDER);
    static const uint64_t p8[] = { 0, 1, 1, 1, 1, 2 };
    n = mk(t7, p8, 6);
    CHECK_EQ(sched_order_check(turns, n, c3, 3, 2, 0, &br), SCHED_ORDER_RELAXED);
    CHECK_EQ(br, 2);
    static const int32_t t8[] = { 1, 3, 1, 2, 3, 1 };   /* 1 three times, 2 once */
    n = mk(t8, p8, 6);
    CHECK_EQ(sched_order_check(turns, n, c3, 3, 2, 0, &br), SCHED_ORDER_BAD_COUNT);
    CHECK_EQ(sched_order_check(turns, 0, c3, 0, 0, 0, &br), SCHED_ORDER_BAD_COUNT);
    for (int o = SCHED_ORDER_STRICT; o <= SCHED_ORDER_BAD_ORDER; o++)
        CHECK(strcmp(sched_order_str((enum sched_order)o), "?") != 0);
}

static void test_strings(void)
{
    for (int st = SCHED_FREE; st <= SCHED_ZOMBIE; st++)
        CHECK(strcmp(sched_state_str((enum sched_state)st), "?") != 0);
    for (int r = SCHED_OK; r <= SCHED_ERR_PREEMPT_OFF; r++)
        CHECK(strcmp(sched_result_str((enum sched_result)r), "?") != 0);
    CHECK(strcmp(sched_result_str(SCHED_ERR_PREEMPT_OFF), "called with preemption disabled") == 0);
}

int main(void)
{
    printf("sched unit tests\n");
    RUN(test_init);
    RUN(test_alloc_start_limits);
    RUN(test_round_robin);
    RUN(test_yield_alone);
    RUN(test_slice_and_need_resched);
    RUN(test_preempt_disabled);
    RUN(test_blocking_refused_with_preemption_off);
    RUN(test_sleep_and_wake_order);
    RUN(test_tick_wrap);
    RUN(test_thread0_never_blocks_or_exits);
    RUN(test_join);
    RUN(test_thread0_join_polls);
    RUN(test_exit_codes);
    RUN(test_detached);
    RUN(test_full_table_churn);
    RUN(test_order_check);
    RUN(test_strings);
    return unit_report("sched");
}
