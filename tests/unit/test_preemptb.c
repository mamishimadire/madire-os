/* Unit tests for the 0.11 policy (b): the switch decision at preempt_enable
 * (sched_switch_on_enable, kernel/proc/sched_core.h) and the inline
 * preempt_disable/preempt_enable (kernel/proc/preempt.h), with the
 * kernel's preempt_resched and preempt_underflow replaced by counting
 * stubs. */
#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/preempt.h"
#include "kernel/proc/sched_core.h"
#include "tests/unit/unit.h"

volatile uint32_t preempt_count_value;
volatile uint8_t preempt_resched_pending;
static uint32_t resched_calls, underflows;
static uint32_t count_at_call;

void preempt_resched(void)
{
    resched_calls++;
    count_at_call = preempt_count_value;
}

void preempt_underflow(void) { underflows++; }

static void reset(void)
{
    preempt_count_value = 0;
    preempt_resched_pending = 0;
    resched_calls = 0;
    underflows = 0;
    count_at_call = 0xFFFFFFFFu;
}

/* Every combination of the five inputs: true only for count 0, pending,
 * interrupts on, ready and no panic. */
static void test_decision_table(void)
{
    uint32_t yes = 0;
    for (uint32_t m = 0; m < 32; m++) {
        uint32_t count = (m & 1) ? 1 : 0;
        bool pending = (m & 2) != 0, irq = (m & 4) != 0, rdy = (m & 8) != 0, pan = (m & 16) != 0;
        bool want = count == 0 && pending && irq && rdy && !pan;
        bool got = sched_switch_on_enable(count, pending, irq, rdy, pan);
        CHECK(got == want);
        if (got)
            yes++;
    }
    CHECK_EQ(yes, 1);
}

static void test_decision_counts(void)
{
    CHECK(sched_switch_on_enable(0, true, true, true, false));
    CHECK(!sched_switch_on_enable(2, true, true, true, false));     /* nested */
    CHECK(!sched_switch_on_enable(UINT32_MAX, true, true, true, false));
    CHECK(!sched_switch_on_enable(0, true, false, true, false));    /* in an interrupt handler */
}

static void test_enable_switches_at_zero(void)
{
    reset();
    preempt_disable();
    preempt_resched_pending = 1;                    /* a tick asked for a switch */
    preempt_enable();
    CHECK_EQ(resched_calls, 1);
    CHECK_EQ(count_at_call, 0);                     /* called with the count already 0 */
    CHECK_EQ(preempt_count(), 0);
    CHECK_EQ(underflows, 0);
}

static void test_enable_nothing_pending(void)
{
    reset();
    preempt_disable();
    preempt_enable();
    CHECK_EQ(resched_calls, 0);
    CHECK_EQ(preempt_count(), 0);
}

static void test_nested_only_outermost(void)
{
    reset();
    preempt_disable();
    preempt_disable();
    preempt_disable();
    preempt_resched_pending = 1;
    preempt_enable();
    CHECK_EQ(resched_calls, 0);                     /* count 2: no switch */
    preempt_enable();
    CHECK_EQ(resched_calls, 0);                     /* count 1: no switch */
    preempt_enable();
    CHECK_EQ(resched_calls, 1);                     /* count 0: the switch */
    CHECK_EQ(count_at_call, 0);
}

static void test_pending_cleared_before_enable(void)
{
    reset();
    preempt_disable();
    preempt_resched_pending = 1;
    preempt_resched_pending = 0;                    /* the switch already happened */
    preempt_enable();
    CHECK_EQ(resched_calls, 0);
}

static void test_underflow(void)
{
    reset();
    preempt_enable();                               /* at 0: a kernel bug */
    CHECK_EQ(underflows, 1);
    CHECK_EQ(resched_calls, 0);
    preempt_count_value = 0;
}

static void test_disable_never_switches(void)
{
    reset();
    preempt_resched_pending = 1;
    preempt_disable();
    preempt_disable();
    CHECK_EQ(resched_calls, 0);
    CHECK_EQ(preempt_count(), 2);
    preempt_enable();
    preempt_enable();
    CHECK_EQ(resched_calls, 1);
}

int main(void)
{
    RUN(test_decision_table);
    RUN(test_decision_counts);
    RUN(test_enable_switches_at_zero);
    RUN(test_enable_nothing_pending);
    RUN(test_nested_only_outermost);
    RUN(test_pending_cleared_before_enable);
    RUN(test_underflow);
    RUN(test_disable_never_switches);
    return unit_report("preemptb");
}
