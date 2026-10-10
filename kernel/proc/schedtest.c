/* Scheduler self-test and thread diagnostics (see schedtest.h). */
#include <stddef.h>
#include <stdint.h>
#include "kernel/arch/x86/irq.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/schedtest.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

/* ---- result lines ---- */

static const char *prefix;
static unsigned items, failures;

static void begin(const char *p)
{
    prefix = p;
    items = failures = 0;
    thread_turn_reset();
}

static void line(const char *what, const char *detail, bool ok)
{
    items++;
    if (!ok)
        failures++;
    klogf("%s: %s: %s: %s\n", prefix, what, detail, ok ? "OK" : "MISMATCH");
}

static bool finish(void)
{
    /* Metrics (0.9): the deferred switch can stretch turns of threads that
     * live inside preempt-guarded sections; keep the effect visible. */
    klogf("%s: longest turn: %u ticks (slice %u); highest thread stack peak: %u of %u bytes\n",
          prefix, (unsigned)thread_turn_max(), SCHED_SLICE_TICKS, thread_peak_max(),
          KSTACK_SIZE);
    char who[80];                   /* 0.11: whose turn it was */
    thread_turn_owner(who, sizeof(who));
    klogf("%s: longest turn taken by %s\n", prefix, who);
    klogf("%s: %s (%u checks, %u mismatches)\n", prefix, failures ? "FAIL" : "PASS", items,
          failures);
    return failures == 0;
}

/* Everything a test borrowed must be back: frames, stack slots, threads. */
struct baseline {
    uint32_t free_pages, stacks, live;
    uint64_t switches;
};

static void take_baseline(struct baseline *b)
{
    b->free_pages = pmm_free_pages();
    b->stacks = thread_stacks_in_use();
    b->live = thread_live();
    b->switches = thread_switches();
}

static void check_restored(const struct baseline *b)
{
    char d[112];
    uint32_t free_now = pmm_free_pages(), stacks = thread_stacks_in_use(), live = thread_live();
    ksnprintf(d, sizeof(d), "free pages %u -> %u, stack slots in use %u, live threads %u",
              b->free_pages, free_now, stacks, live);
    line("everything returned", d, free_now == b->free_pages && stacks == b->stacks &&
                                   live == b->live);
}

/* ---- cooperative ping-pong (boot self-test, test=pingpongpreempt,
 * test=pingpongbad) ----
 *
 * Two threads take turns through yield. A timer preemption may legally
 * reorder the turns (VirtualBox can deliver several ticks at once after
 * the host delayed the VM, so a slice can run out within microseconds):
 * the order is judged by sched_order_check(), which accepts a deviation
 * only where a preemption came within the previous two turns (0.10: a
 * thread preempted after logging, before yielding, shows the deviation one
 * turn later), and requires every thread's full count. The log lines still
 * say "right after a preemption" (an existing test matches them). */

#define PP_ROUNDS        50
#define PP_CAP           (2 * PP_ROUNDS + 8)
#define PP_SPECIAL_ROUND 10

static struct sched_turn pp_log[PP_CAP];
static volatile uint32_t pp_len;
static volatile uint32_t pp_if_bad;
static volatile int32_t pp_force_tid = -1;  /* runs one turn past its slice */
static volatile int32_t pp_bad_tid = -1;    /* breaks the order on purpose */
static volatile int pp_bad_mode;            /* 1: skips a turn, 2: repeats one */

/* One logged turn: the thread and the preemption count now. Preemption is
 * off while the entry is written, so no turn is ever lost or doubled. */
static void log_turn(struct sched_turn *log, volatile uint32_t *len, uint32_t cap, int32_t who)
{
    preempt_disable();
    uint32_t i = *len;
    if (i < cap) {
        log[i].tid = who;
        log[i].preemptions = thread_preemptions();
        *len = i + 1;
    }
    preempt_enable();
}

static int32_t pingpong(void *arg)
{
    int32_t me = thread_current();
    if (!irq_enabled())
        pp_if_bad++;                            /* new threads start with IF=1 */
    for (int i = 0; i < PP_ROUNDS; i++) {
        if (i == PP_SPECIAL_ROUND && me == pp_force_tid) {
            /* Runs past its slice before taking its turn: the timer must
             * preempt it (bounded by 50 ticks). */
            uint64_t p0 = thread_preemptions(), t0 = timer_ticks();
            while (thread_preemptions() == p0 && timer_ticks() < t0 + 50)
                __asm__ volatile ("pause");
        }
        if (i == PP_SPECIAL_ROUND && me == pp_bad_tid) {
            if (pp_bad_mode == 1)
                thread_yield();                 /* gives a turn away, unlogged */
            else
                log_turn(pp_log, &pp_len, PP_CAP, me);   /* takes an extra turn */
        }
        log_turn(pp_log, &pp_len, PP_CAP, me);
        thread_yield();
        if (!irq_enabled())
            pp_if_bad++;                        /* and resume with it */
    }
    return (int32_t)(intptr_t)arg;
}

struct pp_result {
    int32_t a, b;                               /* the two threads */
    int32_t code_a, code_b;
    enum sched_order order;
    uint32_t breaks, turns;
    uint64_t preemptions, switches;
};

static bool order_ok(enum sched_order o);

/* 0.11, a diagnostic for a rejected order (the verdict is unchanged): the
 * logged turns as A or B with the preemption count each saw, 25 a line. */
static void log_turns(int32_t a, int32_t b, uint64_t pre0)
{
    klogf("%s: ping-pong turns rejected (A = thread %d, B = thread %d; preemptions at the "
          "start %u; thread:count):\n", prefix, (int)a, (int)b, (unsigned)pre0);
    char d[400];                    /* 25 entries of at most 14 characters */
    uint32_t n = 0;
    for (uint32_t i = 0; i < pp_len; i++) {
        char who = pp_log[i].tid == a ? 'A' : pp_log[i].tid == b ? 'B' : '?';
        n += (uint32_t)ksnprintf(d + n, sizeof(d) - n, "%s%c:%u", n ? " " : "", who,
                                 (unsigned)pp_log[i].preemptions);
        if ((i + 1) % 25 == 0 || i + 1 == pp_len) {
            klogf("%s:   turns %u-%u: %s\n", prefix, i / 25 * 25 + 1, i + 1, d);
            n = 0;
        }
    }
}

/* One ping-pong. force: thread A runs one turn past its slice (the timer
 * must preempt it); bad_mode 1/2: thread A skips / repeats a turn. */
static bool run_pingpong(struct pp_result *r, bool force, int bad_mode)
{
    pp_len = 0;
    pp_if_bad = 0;
    pp_force_tid = pp_bad_tid = -1;
    pp_bad_mode = bad_mode;
    uint64_t sw0 = thread_switches(), pre0 = thread_preemptions();
    /* 0.11 (stage 4a.1): both threads are created, and their roles set,
     * with preemption off, so neither runs before the whole cycle is
     * queued. A timer preemption of thread 0 between the two creations let
     * A take its first turn alone (A A B ... B B, which the order rule
     * rightly rejects). thread_create never yields, joins or sleeps. */
    preempt_disable();
    r->a = thread_create("pingA", pingpong, (void *)(intptr_t)11, false);
    r->b = thread_create("pingB", pingpong, (void *)(intptr_t)22, false);
    if (r->a > 0 && r->b > 0) {
        if (force)                              /* set before either thread runs */
            pp_force_tid = r->a;
        if (bad_mode)
            pp_bad_tid = r->a;
    }
    preempt_enable();
    if (r->a <= 0 || r->b <= 0) {
        if (r->a > 0)
            thread_join(r->a);
        if (r->b > 0)
            thread_join(r->b);
        return false;
    }
    r->code_a = thread_join(r->a);
    r->code_b = thread_join(r->b);
    const int32_t cycle[2] = { r->a, r->b };
    r->turns = pp_len;
    r->order = sched_order_check(pp_log, pp_len, cycle, 2, PP_ROUNDS, pre0, &r->breaks);
    if (!order_ok(r->order))
        log_turns(r->a, r->b, pre0);        /* 0.11: a diagnostic only */
    r->preemptions = thread_preemptions() - pre0;
    r->switches = thread_switches() - sw0;
    pp_force_tid = pp_bad_tid = -1;
    pp_bad_mode = 0;
    return true;
}

static bool order_ok(enum sched_order o)
{
    return o == SCHED_ORDER_STRICT || o == SCHED_ORDER_RELAXED;
}

/* The ping-pong lines shared by the self-test and test=pingpongpreempt.
 * A strict run prints exactly the line earlier stages printed. */
static void report_pingpong(const struct pp_result *r)
{
    char d[112];
    if (r->order == SCHED_ORDER_STRICT)
        ksnprintf(d, sizeof(d), "%u yields, strictly alternating", r->turns);
    else if (r->order == SCHED_ORDER_RELAXED)
        ksnprintf(d, sizeof(d), "%u yields, order differs at %u point(s), each right after a "
                  "preemption", r->turns, r->breaks);
    else
        ksnprintf(d, sizeof(d), "%u yields, %s", r->turns, sched_order_str(r->order));
    line("cooperative ping-pong", d, order_ok(r->order));
    ksnprintf(d, sizeof(d), "%u", (unsigned)r->preemptions);
    line("preemptions during the ping-pong", d, true);
    ksnprintf(d, sizeof(d), "exit codes %d and %d", (int)r->code_a, (int)r->code_b);
    line("join", d, r->code_a == 11 && r->code_b == 22);
    ksnprintf(d, sizeof(d), "%u checks with IF=0 in the threads; thread 0 IF=%u",
              pp_if_bad, irq_enabled() ? 1u : 0u);
    line("interrupt flag", d, pp_if_bad == 0 && irq_enabled());
    ksnprintf(d, sizeof(d), "%u switches", (unsigned)r->switches);
    line("context switches", d, r->switches >= 2 * PP_ROUNDS);
}

static volatile uint32_t spin_stop;

/* Never yields: only the timer can take the CPU away from it. */
static int32_t spinner(void *arg)
{
    (void)arg;
    while (!spin_stop)
        __asm__ volatile ("pause");
    return 0;
}

static int32_t sleeper10(void *arg)
{
    (void)arg;
    uint64_t t0 = timer_ticks();
    thread_sleep(10);
    return (int32_t)(timer_ticks() - t0);
}

bool schedtest_boot(void)
{
    begin("Scheduler self-test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    struct pp_result pr;
    bool made = run_pingpong(&pr, false, 0);
    ksnprintf(d, sizeof(d), "threads %d and %d", (int)pr.a, (int)pr.b);
    line("create", d, made);
    if (!made)
        return finish();
    report_pingpong(&pr);

    /* Preemption: a thread that never yields is switched out by the timer;
     * a sleeper wakes on time. Needs a working timer (if the timer self-
     * test failed, boot continues without preemption). */
    if (!timer_verified()) {
        line("preemption and sleep", "NOT APPLICABLE (timer not verified)", true);
    } else {
        spin_stop = 0;
        uint64_t pre0 = thread_preemptions();
        int32_t sp = thread_create("spin", spinner, 0, false);
        thread_sleep(30);                       /* thread 0 polls; the spinner runs */
        spin_stop = 1;
        if (sp > 0)
            thread_join(sp);
        uint64_t pre = thread_preemptions() - pre0;
        ksnprintf(d, sizeof(d), "a thread that never yields was preempted %u times in 30 ticks",
                  (unsigned)pre);
        line("preemption", d, sp > 0 && pre >= 3);
        int32_t sl = thread_create("sleeper", sleeper10, 0, false);
        int32_t slept = sl > 0 ? thread_join(sl) : -1;
        ksnprintf(d, sizeof(d), "sleep(10) woke after %d ticks (10 or 11 expected)", (int)slept);
        line("sleep", d, slept == 10 || slept == 11);
    }
    check_restored(&b);
    return finish();
}

/* ---- test=threads: 8 threads x 100 yields ---- */

#define TT_THREADS 8
#define TT_ROUNDS  100

static struct sched_turn tt_log[TT_THREADS * TT_ROUNDS];
static volatile uint32_t tt_len;

/* Returns (register mismatches << 16) | (stack mismatches << 8) | IF misses. */
static int32_t worker(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    volatile uint64_t locals[8];
    for (uint32_t k = 0; k < 8; k++)
        locals[k] = 0xC0DE000000000000ull | ((uint64_t)idx << 32) | k;
    uint32_t regs_bad = 0, locals_bad = 0, if_bad = irq_enabled() ? 0 : 1;
    for (uint32_t r = 0; r < TT_ROUNDS; r++) {
        log_turn(tt_log, &tt_len, TT_THREADS * TT_ROUNDS, (int32_t)idx);
        /* Values unique to this thread and round, held in every callee-
         * saved register across the switch away and back. */
        uint64_t seed = ((uint64_t)(idx + 1) << 56) | ((uint64_t)r << 16);
        if (switch_test_regs(seed))
            regs_bad++;
        for (uint32_t k = 0; k < 8; k++)
            if (locals[k] != (0xC0DE000000000000ull | ((uint64_t)idx << 32) | k))
                locals_bad++;
        if (!irq_enabled())
            if_bad++;
    }
    return (int32_t)((regs_bad << 16) | ((locals_bad & 0xFF) << 8) | (if_bad & 0xFF));
}

bool schedtest_threads(void)
{
    begin("Thread test");
    struct baseline b;
    take_baseline(&b);
    tt_len = 0;
    uint64_t pre0 = thread_preemptions();
    int32_t tids[TT_THREADS];
    bool created = true;
    static const char *const names[TT_THREADS] = { "w0", "w1", "w2", "w3",
                                                   "w4", "w5", "w6", "w7" };
    preempt_disable();                  /* 0.11: the whole cycle first (run_pingpong) */
    for (uint32_t i = 0; i < TT_THREADS; i++) {
        tids[i] = thread_create(names[i], worker, (void *)(uintptr_t)i, false);
        created = created && tids[i] > 0;
    }
    preempt_enable();
    char d[112];
    ksnprintf(d, sizeof(d), "%u threads", TT_THREADS);
    line("create", d, created);
    if (!created)
        return finish();

    uint32_t regs_bad = 0, locals_bad = 0, if_bad = 0;
    for (uint32_t i = 0; i < TT_THREADS; i++) {
        uint32_t code = (uint32_t)thread_join(tids[i]);
        regs_bad += code >> 16;
        locals_bad += (code >> 8) & 0xFF;
        if_bad += code & 0xFF;
    }
    /* Strict round robin unless a preemption reordered turns (allowed only
     * where one came within the previous 8 turns, one per thread of the
     * cycle (0.10); every thread must still have all 100 turns). */
    int32_t cycle[TT_THREADS];
    for (uint32_t i = 0; i < TT_THREADS; i++)
        cycle[i] = (int32_t)i;
    uint32_t breaks = 0;
    enum sched_order o = sched_order_check(tt_log, tt_len, cycle, TT_THREADS, TT_ROUNDS, pre0,
                                           &breaks);
    if (o == SCHED_ORDER_STRICT)
        ksnprintf(d, sizeof(d), "%u turns, strict round robin (w0..w7, 100 rounds)", tt_len);
    else if (o == SCHED_ORDER_RELAXED)
        ksnprintf(d, sizeof(d), "%u turns, round robin except at %u point(s), each right after "
                  "a preemption", tt_len, breaks);
    else
        ksnprintf(d, sizeof(d), "%u turns, %s", tt_len, sched_order_str(o));
    line("order", d, order_ok(o));
    ksnprintf(d, sizeof(d), "%u", (unsigned)(thread_preemptions() - pre0));
    line("preemptions during the cooperative phase", d, true);
    ksnprintf(d, sizeof(d), "%u switches checked, %u with a changed register",
              TT_THREADS * TT_ROUNDS, regs_bad);
    line("callee-saved registers preserved", d, regs_bad == 0);
    ksnprintf(d, sizeof(d), "%u mismatches", locals_bad);
    line("stack contents preserved", d, locals_bad == 0);
    ksnprintf(d, sizeof(d), "%u checks with IF=0", if_bad);
    line("interrupt flag 1 after every switch", d, if_bad == 0);
    uint64_t sw = thread_switches() - b.switches;
    ksnprintf(d, sizeof(d), "%u switches", (unsigned)sw);
    line("context switches", d, sw >= TT_THREADS * TT_ROUNDS);
    check_restored(&b);
    return finish();
}

/* ---- test=join ---- */

static int32_t returns_arg(void *arg)
{
    return (int32_t)(intptr_t)arg;
}

static volatile int32_t join_a = -1;

/* B: yields a few times, then reports (77) whether A is blocked joining it. */
static int32_t join_target(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++)
        thread_yield();
    return thread_state(join_a) == SCHED_BLOCKED ? 77 : 70;
}

/* A: joins B (a thread other than 0 blocks) and adds one to B's code. */
static int32_t join_joiner(void *arg)
{
    return thread_join((int32_t)(intptr_t)arg) + 1;
}

static int32_t detached_body(void *arg)
{
    (void)arg;
    thread_yield();
    return 5;
}

bool schedtest_join(void)
{
    begin("Join test");
    struct baseline b;
    take_baseline(&b);
    char d[112];

    static const int32_t codes[] = { 0, 1, 255, 0x7FFFFFFF, -1, (int32_t)0x80000000 };
    unsigned n = sizeof(codes) / sizeof(codes[0]), exact = 0;
    int32_t tids[sizeof(codes) / sizeof(codes[0])];
    for (unsigned i = 0; i < n; i++)
        tids[i] = thread_create("code", returns_arg, (void *)(intptr_t)codes[i], false);
    for (unsigned i = 0; i < n; i++)
        if (tids[i] > 0 && thread_join(tids[i]) == codes[i])
            exact++;
    ksnprintf(d, sizeof(d), "%u of %u exact (0, 1, 255, 0x7fffffff, -1, -2147483648)", exact, n);
    line("exit codes", d, exact == n);

    int32_t tb = thread_create("target", join_target, 0, false);
    join_a = thread_create("joiner", join_joiner, (void *)(intptr_t)tb, false);
    int32_t ra = (tb > 0 && join_a > 0) ? thread_join(join_a) : -1;
    ksnprintf(d, sizeof(d), "thread %d joined thread %d while it ran: code %d", (int)join_a,
              (int)tb, (int)ra);
    line("join by a thread other than 0 (blocked, then woken)", d, ra == 78);

    uint32_t stacks_before = thread_stacks_in_use();
    int32_t td = thread_create("detached", detached_body, 0, true);
    bool reaped = false;
    for (int i = 0; i < 1000 && td > 0 && !reaped; i++) {
        thread_yield();
        reaped = thread_reap();
    }
    ksnprintf(d, sizeof(d), "thread %d reaped by thread 0; stack slots %u -> %u", (int)td,
              stacks_before + 1, thread_stacks_in_use());
    line("detached thread", d, td > 0 && reaped && thread_stacks_in_use() == stacks_before &&
                               thread_state(td) == SCHED_FREE);
    check_restored(&b);
    return finish();
}

/* ---- test=ifstate (condition A) ---- */

static volatile uint32_t ifs_start_if = 0xFF, ifs_yield_if1_bad, ifs_off_resumed_if = 0xFF;
static volatile uint32_t ifs_other_saw_tick, ifs_other_if = 0xFF;

/* Yields 10 times with IF=1; each time it must come back with IF=1. */
static int32_t if_on_body(void *arg)
{
    (void)arg;
    ifs_start_if = irq_enabled() ? 1 : 0;
    for (int i = 0; i < 10; i++) {
        thread_yield();
        if (!irq_enabled())
            ifs_yield_if1_bad++;
    }
    return 0;
}

/* Yields with IF=0 (inside irq_save): it must come back with IF=0, while
 * the thread that ran in between had interrupts on. */
static int32_t if_off_body(void *arg)
{
    (void)arg;
    uint32_t f = irq_save();
    thread_yield();
    ifs_off_resumed_if = irq_enabled() ? 1 : 0;
    irq_restore(f);
    return 0;
}

/* Runs while if_off_body is away: with IF=1, so timer ticks arrive. */
static int32_t if_other_body(void *arg)
{
    (void)arg;
    ifs_other_if = irq_enabled() ? 1 : 0;
    uint64_t t0 = timer_ticks();
    for (uint32_t spin = 0; spin < 200000000u && timer_ticks() == t0; spin++)
        __asm__ volatile ("pause");
    ifs_other_saw_tick = timer_ticks() != t0;
    return 0;
}

/* Preempted, not yielded: the thread sets RFLAGS.AC (bit 18; harmless in
 * ring 0 while SMAP is off) and spins without yielding until the witness
 * thread has run, i.e. until the timer preempted it and it came back. It
 * must come back with AC=1 and IF=1: its own flags, restored by iretq at
 * the end of the interrupt (switch_to does not save RFLAGS), and the
 * witness must never see AC=1 (flags do not leak between threads). */
#define RFLAGS_AC (1ull << 18)

static uint64_t read_rflags(void)
{
    uint64_t f;
    __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(f));
    return f;
}

static volatile uint32_t ifp_witness_runs, ifp_witness_saw_ac, ifp_done;
static volatile uint64_t ifp_flags_after = 0, ifp_preemptions;

static int32_t if_preempted_body(void *arg)
{
    (void)arg;
    uint64_t pre0 = thread_preemptions();
    __asm__ volatile ("pushfq\n\torq %0, (%%rsp)\n\tpopfq" :: "r"(RFLAGS_AC) : "cc", "memory");
    uint64_t t0 = timer_ticks();
    while (ifp_witness_runs == 0 && timer_ticks() < t0 + 50)
        __asm__ volatile ("pause");
    ifp_flags_after = read_rflags();
    __asm__ volatile ("pushfq\n\tandq %0, (%%rsp)\n\tpopfq" :: "r"(~RFLAGS_AC) : "cc", "memory");
    ifp_preemptions = thread_preemptions() - pre0;
    ifp_done = 1;
    return 0;
}

static int32_t if_witness_body(void *arg)
{
    (void)arg;
    while (!ifp_done) {
        if (read_rflags() & RFLAGS_AC)
            ifp_witness_saw_ac++;
        ifp_witness_runs++;
    }
    return 0;
}

bool schedtest_ifstate(void)
{
    begin("IF-state test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    ifs_yield_if1_bad = 0;
    ifs_other_saw_tick = 0;

    int32_t t1 = thread_create("if-on", if_on_body, 0, false);
    int32_t t2 = thread_create("if-off", if_off_body, 0, false);
    int32_t t3 = thread_create("if-other", if_other_body, 0, false);
    bool created = t1 > 0 && t2 > 0 && t3 > 0;
    line("create", "3 threads", created);
    if (!created)
        return finish();
    thread_join(t1);
    thread_join(t2);
    thread_join(t3);

    ksnprintf(d, sizeof(d), "IF=%u at the first instruction of a new thread", ifs_start_if);
    line("new thread starts with IF=1 (trampoline)", d, ifs_start_if == 1);
    ksnprintf(d, sizeof(d), "10 yields, %u resumed with IF=0", ifs_yield_if1_bad);
    line("yield with IF=1 resumes with IF=1", d, ifs_yield_if1_bad == 0);
    ksnprintf(d, sizeof(d), "resumed with IF=%u; meanwhile another thread ran with IF=%u "
              "and %s a timer tick", ifs_off_resumed_if, ifs_other_if,
              ifs_other_saw_tick ? "received" : "did not receive");
    line("yield with IF=0 resumes with IF=0", d, ifs_off_resumed_if == 0 && ifs_other_if == 1 &&
                                                 ifs_other_saw_tick);
    ksnprintf(d, sizeof(d), "IF=%u after joining", irq_enabled() ? 1u : 0u);
    line("thread 0 keeps IF=1", d, irq_enabled());

    ifp_witness_runs = ifp_witness_saw_ac = ifp_done = 0;
    int32_t tp = thread_create("if-preempted", if_preempted_body, 0, false);
    int32_t tw = thread_create("if-witness", if_witness_body, 0, false);
    if (tp > 0)
        thread_join(tp);
    if (tw > 0)
        thread_join(tw);
    bool ac = (ifp_flags_after & RFLAGS_AC) != 0, iflag = (ifp_flags_after & EFLAGS_IF) != 0;
    bool ac0 = (read_rflags() & RFLAGS_AC) != 0;    /* thread 0, resumed through yields */
    ksnprintf(d, sizeof(d), "AC=%u IF=%u after %u preemptions; other thread AC=%u, thread 0 AC=%u",
              ac ? 1u : 0u, iflag ? 1u : 0u, (unsigned)ifp_preemptions,
              ifp_witness_saw_ac ? 1u : 0u, ac0 ? 1u : 0u);
    line("preempted thread resumes through iretq with its own flags", d,
         tp > 0 && tw > 0 && ac && iflag && ifp_preemptions >= 1 && ifp_witness_runs > 0 &&
         ifp_witness_saw_ac == 0 && !ac0);
    check_restored(&b);
    return finish();
}

/* ---- deliberate bugs: blocking with preemption disabled ---- */

void schedtest_preempt_yield(void)
{
    klogf("Preemption test: thread_yield with preemption disabled (must panic)\n");
    preempt_disable();
    thread_yield();
    klogf("Preemption test: thread_yield returned (WRONG)\n");
    preempt_enable();
}

void schedtest_preempt_join(void)
{
    int32_t t = thread_create("joined", returns_arg, 0, false);
    klogf("Preemption test: thread_join(%d) with preemption disabled (must panic)\n", (int)t);
    preempt_disable();
    thread_join(t);
    klogf("Preemption test: thread_join returned (WRONG)\n");
    preempt_enable();
}

/* ---- shared helpers for the timed tests ---- */

static uint32_t rng_next(uint32_t *s)
{
    *s = *s * 1103515245u + 12345u;
    return *s >> 8;
}

/* "33.4%" from parts per thousand. */
static void pct(char *buf, size_t size, uint64_t part, uint64_t whole)
{
    uint32_t pm = whole ? (uint32_t)(part * 1000 / whole) : 0;
    ksnprintf(buf, size, "%u.%u%%", pm / 10, pm % 10);
}

/* ---- test=preempt: three threads that never yield ---- */

#define PT_THREADS 3
#define PT_TICKS   300

static volatile uint64_t pt_end;
static volatile uint64_t pt_work[PT_THREADS];

/* Work between tick reads: timer_ticks() runs with interrupts off (cli ...
 * sti), and a loop doing nothing else leaves them on for only an
 * instruction or two per pass. QEMU's TCG emulator delivers interrupts only
 * between translated blocks, so such a loop can starve IRQ0 there (real
 * hardware takes the interrupt right after the one-instruction sti window). */
static int32_t pt_spin(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg;
    while (timer_ticks() < pt_end)
        for (int k = 0; k < 1000; k++)
            pt_work[idx]++;
    return 0;
}

bool schedtest_preempt(void)
{
    begin("Preempt test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    static const char *const names[PT_THREADS] = { "spin0", "spin1", "spin2" };
    int32_t tids[PT_THREADS];
    uint64_t pre0 = thread_preemptions(), t0 = timer_ticks();
    pt_end = t0 + PT_TICKS;
    bool created = true;
    for (uint32_t i = 0; i < PT_THREADS; i++) {
        pt_work[i] = 0;
        tids[i] = thread_create(names[i], pt_spin, (void *)(uintptr_t)i, false);
        created = created && tids[i] > 0;
    }
    klogf("Preempt test: %u threads spinning for %u ticks (keys may be typed now)\n", PT_THREADS,
          PT_TICKS);
    line("create", "3 threads that never yield", created);
    if (!created)
        return finish();

    thread_sleep(PT_TICKS + 10);            /* thread 0 polls; all three have ended */
    uint64_t ticks[PT_THREADS], sum = 0;
    for (uint32_t i = 0; i < PT_THREADS; i++) {
        ticks[i] = thread_ticks_run(tids[i]);  /* kept until the thread is reaped */
        sum += ticks[i];
    }
    for (uint32_t i = 0; i < PT_THREADS; i++)
        thread_join(tids[i]);
    uint64_t elapsed = timer_ticks() - t0, pre = thread_preemptions() - pre0;

    bool fair = sum > 0;
    char p[PT_THREADS][16];
    for (uint32_t i = 0; i < PT_THREADS; i++) {
        pct(p[i], sizeof(p[i]), ticks[i], sum);
        fair = fair && ticks[i] * 100 >= sum * 25 && ticks[i] * 100 <= sum * 42;
    }
    ksnprintf(d, sizeof(d), "spin0 %s (%u ticks), spin1 %s (%u), spin2 %s (%u) of %u (each 25-42%%)",
              p[0], (unsigned)ticks[0], p[1], (unsigned)ticks[1], p[2], (unsigned)ticks[2],
              (unsigned)sum);
    line("fair shares", d, fair);
    ksnprintf(d, sizeof(d), "%u preemptions in %u ticks (about %u expected: one per %u-tick slice)",
              (unsigned)pre, (unsigned)PT_TICKS, PT_TICKS / SCHED_SLICE_TICKS, SCHED_SLICE_TICKS);
    line("preemptions", d, pre >= 40 && pre <= 80);
    ksnprintf(d, sizeof(d), "work counters %u, %u, %u (all advanced)", (unsigned)pt_work[0],
              (unsigned)pt_work[1], (unsigned)pt_work[2]);
    line("every thread made progress", d, pt_work[0] && pt_work[1] && pt_work[2]);
    ksnprintf(d, sizeof(d), "%u ticks elapsed during the test", (unsigned)elapsed);
    line("timer kept ticking", d, elapsed >= PT_TICKS);
    check_restored(&b);
    return finish();
}

/* ---- test=preemptoff: the deferred switch (0.9 policy (a); 0.11 policy (b):
 * the preempt_enable() that releases the guard switches at once) ---- */

#define PO_HOLD_TICKS 20

static volatile uint32_t po_b_count, po_released, po_stop;
static volatile uint64_t po_release_tick, po_b_resume_tick;
static volatile uint32_t po_b_frozen, po_no_switch, po_pending;
static volatile uint64_t po_held;
/* 0.11: the holder is inside its releasing preempt_enable(), and what the
 * other thread saw when it first ran after the release. */
static volatile uint32_t po_enabling, po_b_saw_enabling;
/* 0.11: preemptions counted before the release, and when the other
 * thread first ran after it (a switch at the release is a preemption,
 * whether preempt_enable or the timer interrupt right after it made it). */
static volatile uint64_t po_pre_release, po_b_pre;

static int32_t po_holder(void *arg)
{
    (void)arg;
    preempt_disable();
    uint64_t sw0 = thread_switches(), t0 = timer_ticks();
    uint32_t b0 = po_b_count;
    while (timer_ticks() < t0 + PO_HOLD_TICKS)
        __asm__ volatile ("pause");
    po_b_frozen = po_b_count == b0;
    po_no_switch = thread_switches() == sw0;
    po_pending = thread_need_resched();
    po_held = timer_ticks() - t0;
    po_release_tick = timer_ticks();
    po_pre_release = thread_preemptions();
    po_released = 1;
    po_enabling = 1;
    preempt_enable();                       /* 0.11, policy (b): switches here */
    po_enabling = 0;
    uint64_t t1 = timer_ticks();
    while (!po_b_resume_tick && timer_ticks() < t1 + 50)
        __asm__ volatile ("pause");
    po_stop = 1;
    return 0;
}

static int32_t po_other(void *arg)
{
    (void)arg;
    while (!po_stop) {
        po_b_count++;
        if (po_released && !po_b_resume_tick) {
            po_b_saw_enabling = po_enabling;
            po_b_pre = thread_preemptions();
            po_b_resume_tick = timer_ticks();
        }
    }
    return 0;
}

bool schedtest_preemptoff(void)
{
    begin("Preemptoff test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    po_b_count = po_released = po_stop = 0;
    po_release_tick = po_b_resume_tick = 0;
    po_enabling = po_b_saw_enabling = 0;
    po_pre_release = po_b_pre = 0;
    int32_t ta = thread_create("holder", po_holder, 0, false);
    int32_t tb = thread_create("other", po_other, 0, false);
    line("create", "a thread that disables preemption, and another", ta > 0 && tb > 0);
    if (ta <= 0 || tb <= 0)
        return finish();
    thread_join(ta);
    thread_join(tb);
    /* 0.11: each thread's own longest turn (the holder's includes its
     * deliberate hold, which no turn bound applies to). */
    klogf("Preemptoff test: thread %d 'other': longest turn %u ticks; thread %d 'holder': "
          "longest turn %u ticks (includes its deliberate %u-tick hold)\n", (int)tb,
          (unsigned)thread_turn_longest(tb), (int)ta, (unsigned)thread_turn_longest(ta),
          PO_HOLD_TICKS);
    ksnprintf(d, sizeof(d), "preemption off for %u ticks: no switch, the other thread did not run",
              (unsigned)po_held);
    line("no switch while the guard is held", d,
         po_held >= PO_HOLD_TICKS && po_no_switch && po_b_frozen);
    line("switch request kept pending (need_resched) while held", po_pending ? "yes" : "no",
         po_pending);
    uint64_t delay = po_b_resume_tick - po_release_tick;
    ksnprintf(d, sizeof(d), "released at tick %u, other thread ran at tick %u: %u tick(s) later "
              "(at most 1)", (unsigned)po_release_tick, (unsigned)po_b_resume_tick,
              (unsigned)delay);
    line("switch within one tick after the release", d,
         po_b_resume_tick != 0 && po_b_resume_tick >= po_release_tick && delay <= 1);
    /* 0.11: where the switch at the release was made (a log line, not a
     * check item). */
    klogf("Preemptoff test: switch at the release: by %s; preemptions %u -> %u\n",
          po_b_saw_enabling ? "preempt_enable" : "the timer interrupt right after it",
          (unsigned)po_pre_release, (unsigned)po_b_pre);
    line("switch inside preempt_enable at the release (policy (b))",
         po_b_saw_enabling ? "yes" : "no", po_b_saw_enabling);
    check_restored(&b);
    return finish();
}

/* ---- test=sleep ---- */

static volatile uint32_t sl_done;
static volatile int32_t sl_elapsed[3];
static const uint32_t sl_lengths[3] = { 1, 10, 100 };

static int32_t sl_sleeper(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3; i++) {
        uint64_t t0 = timer_ticks();
        thread_sleep(sl_lengths[i]);
        sl_elapsed[i] = (int32_t)(timer_ticks() - t0);
    }
    sl_done = 1;
    return 0;
}

static int32_t sl_spinner(void *arg)
{
    (void)arg;
    while (!sl_done)
        __asm__ volatile ("pause");
    return 0;
}

bool schedtest_sleep(void)
{
    begin("Sleep test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    sl_done = 0;
    uint64_t t0 = timer_ticks();
    int32_t ts = thread_create("sleeper", sl_sleeper, 0, false);
    int32_t tp = thread_create("spinner", sl_spinner, 0, false);
    line("create", "a sleeper and a spinner", ts > 0 && tp > 0);
    if (ts <= 0 || tp <= 0)
        return finish();
    thread_join(ts);                        /* the spinner stops right after */
    uint64_t span = timer_ticks() - t0;
    uint64_t spin_ticks = thread_ticks_run(tp);
    thread_join(tp);
    for (int i = 0; i < 3; i++) {
        char what[48];
        ksnprintf(what, sizeof(what), "sleep(%u)", sl_lengths[i]);
        ksnprintf(d, sizeof(d), "woke after %d ticks (%u or %u expected)", (int)sl_elapsed[i],
                  sl_lengths[i], sl_lengths[i] + 1);
        line(what, d, sl_elapsed[i] == (int32_t)sl_lengths[i] ||
                      sl_elapsed[i] == (int32_t)sl_lengths[i] + 1);
    }
    char p[16];
    pct(p, sizeof(p), spin_ticks, span);
    ksnprintf(d, sizeof(d), "spinner ran %u of %u ticks (%s; at least 95%%)", (unsigned)spin_ticks,
              (unsigned)span, p);
    line("a sleeping thread uses no CPU", d, spin_ticks * 100 >= span * 95);
    uint64_t z0 = timer_ticks();
    thread_sleep(10);
    uint64_t z = timer_ticks() - z0;
    ksnprintf(d, sizeof(d), "thread 0 slept 10 ticks: %u elapsed (10 or 11 expected)", (unsigned)z);
    line("thread 0 sleeps by polling", d, z == 10 || z == 11);
    check_restored(&b);
    return finish();
}

/* ---- test=heapthreads: the heap under preemption ---- */

#define HT_THREADS 4
#define HT_TICKS   200
#define HT_SLOTS   32

static volatile uint64_t ht_end;
static volatile uint32_t ht_ops[HT_THREADS];

static bool block_ok(const uint8_t *p, uint32_t size, uint8_t tag)
{
    for (uint32_t j = 0; j < size; j++)
        if (p[j] != (uint8_t)(tag + j))
            return false;
    return true;
}

/* Random kmalloc / kfree with tagged payloads until ht_end; returns the
 * number of blocks whose pattern was damaged (must be 0). */
static int32_t ht_body(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg, rng = 0x4D41u + idx * 7919u, bad = 0, ops = 0;
    struct { uint8_t *p; uint32_t size; uint8_t tag; } slot[HT_SLOTS];
    for (int i = 0; i < HT_SLOTS; i++)
        slot[i].p = 0;
    while (timer_ticks() < ht_end) {
        uint32_t i = rng_next(&rng) % HT_SLOTS;
        if (slot[i].p) {
            if (!block_ok(slot[i].p, slot[i].size, slot[i].tag))
                bad++;
            kfree(slot[i].p);
            slot[i].p = 0;
        } else {
            uint32_t size = rng_next(&rng) % 700 + 1;
            uint8_t *p = kmalloc(size);
            if (p) {
                uint8_t tag = (uint8_t)(idx * 64 + ops);
                for (uint32_t j = 0; j < size; j++)
                    p[j] = (uint8_t)(tag + j);
                slot[i].p = p;
                slot[i].size = size;
                slot[i].tag = tag;
            }
        }
        ops++;
    }
    for (int i = 0; i < HT_SLOTS; i++) {
        if (slot[i].p) {
            if (!block_ok(slot[i].p, slot[i].size, slot[i].tag))
                bad++;
            kfree(slot[i].p);
        }
    }
    ht_ops[idx] = ops;
    return (int32_t)bad;
}

bool schedtest_heapthreads(void)
{
    begin("Heap threads test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    const struct heap_stats *hs = kheap_stats();
    uint32_t used0 = hs->used_bytes, blocks0 = hs->live_blocks, arena0 = hs->arena_bytes;
    uint32_t tables0 = paging_stats()->page_tables;
    uint64_t pre0 = thread_preemptions();
    ht_end = timer_ticks() + HT_TICKS;
    int32_t tids[HT_THREADS];
    bool created = true;
    static const char *const names[HT_THREADS] = { "heap0", "heap1", "heap2", "heap3" };
    for (uint32_t i = 0; i < HT_THREADS; i++) {
        tids[i] = thread_create(names[i], ht_body, (void *)(uintptr_t)i, false);
        created = created && tids[i] > 0;
    }
    line("create", "4 threads", created);
    if (!created)
        return finish();
    uint32_t bad = 0, ops = 0, fewest = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < HT_THREADS; i++)
        bad += (uint32_t)thread_join(tids[i]);
    for (uint32_t i = 0; i < HT_THREADS; i++) {
        ops += ht_ops[i];
        if (ht_ops[i] < fewest)
            fewest = ht_ops[i];
    }
    uint64_t pre = thread_preemptions() - pre0;

    /* None of the threads yields: without preemption the first would use
     * the whole time and the others would do (almost) nothing. */
    ksnprintf(d, sizeof(d), "%u kmalloc/kfree operations (fewest %u per thread), %u preemptions",
              ops, fewest, (unsigned)pre);
    line("concurrent use under preemption", d, fewest > 100 && pre >= 1);
    ksnprintf(d, sizeof(d), "%u damaged blocks", bad);
    line("every payload intact", d, bad == 0);
    hs = kheap_stats();                     /* runs a full heap_check (panics if bad) */
    ksnprintf(d, sizeof(d), "heap_check OK; in use %u -> %u bytes, %u -> %u blocks", used0,
              hs->used_bytes, blocks0, hs->live_blocks);
    line("heap consistent and empty again", d, hs->used_bytes == used0 && hs->live_blocks == blocks0);
    uint32_t grown = (hs->arena_bytes - arena0) / 4096;
    uint32_t tables = paging_stats()->page_tables - tables0;
    uint32_t free_now = pmm_free_pages();
    ksnprintf(d, sizeof(d), "free pages %u -> %u = heap grew %u pages + %u new page tables",
              b.free_pages, free_now, grown, tables);
    line("frames accounted for exactly (the heap never shrinks)", d,
         free_now == b.free_pages - grown - tables);
    uint32_t stacks = thread_stacks_in_use(), live = thread_live();
    ksnprintf(d, sizeof(d), "stack slots in use %u, live threads %u", stacks, live);
    line("threads returned", d, stacks == b.stacks && live == b.live);
    return finish();
}

/* ---- test=framethreads: the frame allocator and the window ---- */

#define FT_THREADS 4
#define FT_TICKS   200
#define FT_SLOTS   16

static volatile uint64_t ft_end;
static volatile uint32_t ft_ops[FT_THREADS];

/* Writes or checks a frame's tag through the window (preemption is off
 * from vmm_window_map to vmm_window_unmap). */
static bool frame_tag(uint32_t frame, uint32_t tag, bool write)
{
    volatile uint32_t *w = (volatile uint32_t *)vmm_window_map(frame);
    bool ok = true;
    if (write) {
        w[0] = tag;
        w[1023] = ~tag;
    } else {
        ok = w[0] == tag && w[1023] == ~tag;
    }
    vmm_window_unmap();
    return ok;
}

/* Random frame allocation and freeing with tags until ft_end; returns the
 * number of frames that lost their tag (one handed out twice would). */
static int32_t ft_body(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg, rng = 0x4652u + idx * 104729u, bad = 0, ops = 0;
    uint32_t seq = 0;
    struct { uint32_t frame, tag; } slot[FT_SLOTS];
    for (int i = 0; i < FT_SLOTS; i++)
        slot[i].frame = 0;
    while (timer_ticks() < ft_end) {
        uint32_t i = rng_next(&rng) % FT_SLOTS;
        if (slot[i].frame) {
            if (!frame_tag(slot[i].frame, slot[i].tag, false))
                bad++;
            memory_free_page(slot[i].frame);
            slot[i].frame = 0;
        } else {
            uint32_t f = memory_alloc_page();
            if (f) {
                slot[i].tag = (idx << 24) | (seq++ & 0xFFFFFFu);
                frame_tag(f, slot[i].tag, true);
                slot[i].frame = f;
            }
        }
        ops++;
    }
    for (int i = 0; i < FT_SLOTS; i++) {
        if (slot[i].frame) {
            if (!frame_tag(slot[i].frame, slot[i].tag, false))
                bad++;
            memory_free_page(slot[i].frame);
        }
    }
    ft_ops[idx] = ops;
    return (int32_t)bad;
}

bool schedtest_framethreads(void)
{
    begin("Frame threads test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    uint64_t pre0 = thread_preemptions();
    ft_end = timer_ticks() + FT_TICKS;
    int32_t tids[FT_THREADS];
    bool created = true;
    static const char *const names[FT_THREADS] = { "frame0", "frame1", "frame2", "frame3" };
    for (uint32_t i = 0; i < FT_THREADS; i++) {
        tids[i] = thread_create(names[i], ft_body, (void *)(uintptr_t)i, false);
        created = created && tids[i] > 0;
    }
    line("create", "4 threads", created);
    if (!created)
        return finish();
    uint32_t bad = 0, ops = 0, fewest = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < FT_THREADS; i++)
        bad += (uint32_t)thread_join(tids[i]);
    for (uint32_t i = 0; i < FT_THREADS; i++) {
        ops += ft_ops[i];
        if (ft_ops[i] < fewest)
            fewest = ft_ops[i];
    }
    uint64_t pre = thread_preemptions() - pre0;
    ksnprintf(d, sizeof(d), "%u allocate/free operations (fewest %u per thread), %u preemptions",
              ops, fewest, (unsigned)pre);
    line("concurrent use under preemption", d, fewest > 100 && pre >= 1);
    ksnprintf(d, sizeof(d), "%u frames lost their tag (none handed out twice)", bad);
    line("every frame tag intact", d, bad == 0);
    check_restored(&b);
    return finish();
}

/* ---- test=preemptlong: threads that live in guarded sections (0.11) ----
 *
 * Policy (b): each thread spends almost all its time with preemption
 * disabled; every section lasts until the next timer tick, so every tick
 * lands inside one. Under the 0.9 policy (a) a switch waited for an
 * interrupt with the count at 0, so such a thread could keep the CPU for a
 * long time; now the preempt_enable() that ends the section switches, and
 * every turn stays within a slice plus one section (at most 6 ticks). */

#define PL_THREADS  4
#define PL_TICKS    300
#define PL_MAX_TURN 6

static volatile uint64_t pl_end;
static volatile uint64_t pl_inside[PL_THREADS], pl_outside[PL_THREADS];
static volatile uint32_t pl_turns[PL_THREADS], pl_sections[PL_THREADS];

static inline uint64_t pl_tsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Time inside a section counts as inside; the gap between two sections
 * counts as outside only when no switch came in between (otherwise the gap
 * is mostly other threads' time, and the thread's own part of it is a few
 * instructions). A changed switch count means the thread left the CPU and
 * came back: one more turn. */
static int32_t pl_body(void *arg)
{
    uint32_t idx = (uint32_t)(uintptr_t)arg, turns = 1, sections = 0;
    uint64_t inside = 0, outside = 0, sw = thread_switches(), gap_start = 0;
    while (timer_ticks() < pl_end) {
        preempt_disable();
        uint64_t a = pl_tsc();
        if (gap_start && thread_switches() == sw)
            outside += a - gap_start;
        uint64_t t = timer_ticks();
        while (timer_ticks() == t)
            __asm__ volatile ("pause");
        uint64_t b = pl_tsc();
        inside += b - a;
        sections++;
        preempt_enable();               /* policy (b): a pending switch happens here */
        uint64_t s = thread_switches();
        if (s != sw) {
            turns++;
            sw = s;
        }
        gap_start = pl_tsc();
    }
    pl_inside[idx] = inside;
    pl_outside[idx] = outside;
    pl_turns[idx] = turns;
    pl_sections[idx] = sections;
    return 0;
}

bool schedtest_preemptlong(void)
{
    begin("Preemptlong test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    pl_end = timer_ticks() + PL_TICKS;
    int32_t tids[PL_THREADS];
    bool created = true;
    static const char *const names[PL_THREADS] = { "long0", "long1", "long2", "long3" };
    for (uint32_t i = 0; i < PL_THREADS; i++) {
        tids[i] = thread_create(names[i], pl_body, (void *)(uintptr_t)i, false);
        created = created && tids[i] > 0;
    }
    line("create", "4 threads that keep preemption disabled until each next tick", created);
    if (!created)
        return finish();
    for (uint32_t i = 0; i < PL_THREADS; i++)
        thread_join(tids[i]);
    /* Read before any new thread can reuse the slots. */
    for (uint32_t i = 0; i < PL_THREADS; i++) {
        uint64_t longest = thread_turn_longest(tids[i]);
        uint64_t all = pl_inside[i] + pl_outside[i];
        uint32_t permille = all ? (uint32_t)(pl_inside[i] * 1000 / all) : 0;
        ksnprintf(d, sizeof(d), "thread %d: %u.%u%% in guarded sections (%u), %u turns, "
                  "longest turn %u ticks (at most %u)", (int)tids[i], permille / 10, permille % 10,
                  pl_sections[i], pl_turns[i], (unsigned)longest, PL_MAX_TURN);
        line(names[i], d, permille > 950 && pl_turns[i] >= 5 && longest <= PL_MAX_TURN);
    }
    check_restored(&b);
    return finish();
}

/* ---- deliberate bug: sleeping with preemption disabled ---- */

void schedtest_preempt_sleep(void)
{
    klogf("Preemption test: thread_sleep with preemption disabled (must panic)\n");
    preempt_disable();
    thread_sleep(5);
    klogf("Preemption test: thread_sleep returned (WRONG)\n");
    preempt_enable();
}

/* ---- test=threadleak: 2000 threads, and per-thread stack peaks ---- */

#define TL_TOTAL     2000
#define TL_BATCH     32
#define TL_DETACHED  8                      /* per batch, reaped by thread 0 */
#define TL_DEEP      16                     /* 16 x 512-byte frames: over 8 KiB */

static int32_t tl_body(void *arg)
{
    return (int32_t)(intptr_t)arg;
}

static __attribute__((noinline)) uint32_t tl_recurse(uint32_t depth)
{
    volatile uint8_t buf[512];
    buf[0] = (uint8_t)depth;
    buf[511] = (uint8_t)~depth;
    uint32_t below = depth > 1 ? tl_recurse(depth - 1) : 0;
    return below + buf[0] + buf[511];
}

/* Uses more than 8 KiB of its stack, then exits: its exit line must report
 * a peak of at least 8192 bytes. */
static int32_t tl_deep(void *arg)
{
    (void)arg;
    return (int32_t)(tl_recurse(TL_DEEP) & 0x7FFFFFFF);
}

bool schedtest_threadleak(void)
{
    begin("Thread churn test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    const struct heap_stats *hs = kheap_stats();
    uint32_t used0 = hs->used_bytes, blocks0 = hs->live_blocks;

    /* Per-thread stack peak: a thread that goes over 8 KiB deep. */
    int32_t deep = thread_create("deep", tl_deep, 0, false);
    if (deep > 0)
        thread_join(deep);
    ksnprintf(d, sizeof(d), "a thread %u frames of 512 bytes deep: final peak %u of %u bytes",
              TL_DEEP, thread_peak_max(), KSTACK_SIZE);
    line("stack peak reported per thread", d, deep > 0 && thread_peak_max() >= 8192 &&
                                              thread_peak_max() < KSTACK_SIZE);

    /* Churn: create, run and free 2000 threads, 32 at a time (24 joined,
     * 8 detached and reaped by thread 0), without one exit line each. */
    thread_log_exits(false);
    uint32_t created = 0, joined = 0, reaped = 0, wrong = 0, failed = 0;
    uint32_t most_live = 0, most_stacks = 0;
    while (created < TL_TOTAL) {
        int32_t tids[TL_BATCH];
        uint32_t n = 0, detached = 0;
        for (uint32_t i = 0; i < TL_BATCH && created < TL_TOTAL; i++) {
            bool det = i >= TL_BATCH - TL_DETACHED;
            int32_t t = thread_create("churn", tl_body, (void *)(intptr_t)created, det);
            if (t <= 0) {
                failed++;
                break;
            }
            if (det)
                detached++;
            else
                tids[n++] = t;
            created++;
        }
        if (thread_live() > most_live)
            most_live = thread_live();
        if (thread_stacks_in_use() > most_stacks)
            most_stacks = thread_stacks_in_use();
        for (uint32_t i = 0; i < n; i++) {
            int32_t code = thread_join(tids[i]);
            joined++;
            if (code < 0 || (uint32_t)code >= created)
                wrong++;
        }
        for (uint32_t spins = 0; detached > 0 && spins < 100000; spins++) {
            if (thread_reap()) {
                detached--;
                reaped++;
            } else {
                thread_yield();
            }
        }
        if (failed || detached)
            break;
    }
    thread_log_exits(true);

    ksnprintf(d, sizeof(d), "%u created (%u joined, %u detached reaped), %u create failures",
              created, joined, reaped, failed);
    line("2000 threads created and freed", d, created == TL_TOTAL && failed == 0 &&
                                              joined + reaped == TL_TOTAL);
    ksnprintf(d, sizeof(d), "%u wrong exit codes; at most %u threads and %u stacks at once",
              wrong, most_live, most_stacks);
    line("exit codes and limits", d, wrong == 0 && most_live <= SCHED_MAX_THREADS &&
                                     most_stacks <= KSTACK_SLOTS);
    hs = kheap_stats();
    ksnprintf(d, sizeof(d), "heap in use %u -> %u bytes, %u -> %u blocks", used0, hs->used_bytes,
              blocks0, hs->live_blocks);
    line("heap unchanged", d, hs->used_bytes == used0 && hs->live_blocks == blocks0);
    check_restored(&b);
    return finish();
}

/* ---- test=threadoverflow: a kernel thread overruns its stack ---- */

static __attribute__((noinline)) uint32_t to_recurse(uint32_t depth)
{
    volatile uint8_t buf[512];
    buf[0] = (uint8_t)depth;
    buf[511] = (uint8_t)depth;
    uint32_t below = depth < 1000000u ? to_recurse(depth + 1) : 0;
    return below + buf[0] + buf[511];
}

static int32_t to_body(void *arg)
{
    (void)arg;
    return (int32_t)to_recurse(0);
}

void schedtest_threadoverflow(void)
{
    klogf("Thread stack test: a kernel thread recurses until its stack reaches the guard\n");
    int32_t t = thread_create("overflow", to_body, 0, false);
    if (t > 0)
        thread_join(t);
    klogf("Thread stack test: the thread returned (WRONG)\n");
}

/* ---- test=pingpongpreempt: the relaxed branch, on purpose ---- */

bool schedtest_pingpong_preempt(void)
{
    begin("Ping-pong preemption test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    struct pp_result pr;
    bool made = run_pingpong(&pr, true, 0);
    line("create", "two ping-pong threads; thread A runs one turn past its slice", made);
    if (!made)
        return finish();
    report_pingpong(&pr);
    ksnprintf(d, sizeof(d), "%u preemption(s), %u break(s), each right after a preemption",
              (unsigned)pr.preemptions, pr.breaks);
    line("forced preemption accepted by the relaxed branch", d,
         pr.order == SCHED_ORDER_RELAXED && pr.breaks >= 1 && pr.preemptions >= 1);
    check_restored(&b);
    return finish();
}

/* ---- test=pingpongbad: a wrong order must still fail ---- */

bool schedtest_pingpong_bad(void)
{
    begin("Ping-pong order test");
    struct baseline b;
    take_baseline(&b);
    char d[112];
    static const char *const what[3] = { "", "a skipped turn without preemption",
                                         "a repeated turn without preemption" };
    for (int mode = 1; mode <= 2; mode++) {
        struct pp_result pr;
        bool made = false;
        /* The case needs a run with no preemption at all (otherwise the
         * deviation could be a legal one); retry if a tick interfered. */
        for (int attempt = 0; attempt < 3; attempt++) {
            made = run_pingpong(&pr, false, mode);
            if (!made || pr.preemptions == 0)
                break;
        }
        ksnprintf(d, sizeof(d), "detected: %s (%u turns, %u preemptions)",
                  made ? sched_order_str(pr.order) : "no threads", pr.turns,
                  made ? (unsigned)pr.preemptions : 0u);
        line(what[mode], d, made && pr.preemptions == 0 && !order_ok(pr.order) &&
                            pr.code_a == 11 && pr.code_b == 22);
    }
    check_restored(&b);
    return finish();
}
