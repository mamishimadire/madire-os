/* System timer (see timer.h). */
#include "drivers/pit/pit.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/timer.h"

#define SELFTEST_TICKS        3u
#define SELFTEST_SPIN_LIMIT   50000000u   /* bounds the wait if IRQ0 never comes */

static volatile uint64_t ticks;
static volatile uint32_t seconds;
static volatile uint32_t subticks;
static bool initialised;
static bool verified;
static uint16_t divisor;

/* IRQ0 context: counters only. */
static void timer_irq(void)
{
    ticks++;
    if (++subticks == TIMER_HZ) {
        subticks = 0;
        seconds++;
    }
}

bool timer_init(void)
{
    if (pit_init(TIMER_HZ, timer_irq, &divisor) != IRQ_OK)
        return false;
    initialised = irq_enable_line(IRQ_TIMER) == IRQ_OK;
    return initialised;
}

uint64_t timer_ticks(void)
{
    uint32_t flags = irq_save();
    uint64_t t = ticks;
    irq_restore(flags);
    return t;
}

uint32_t timer_uptime_seconds(void)
{
    return seconds;
}

bool timer_verified(void)
{
    return verified;
}

bool timer_selftest(struct timer_selftest *out)
{
    *out = (struct timer_selftest){ .divisor = divisor };
    if (!initialised)
        return false;

    /* 1. PIT counting: the count changes between latched reads. */
    uint16_t first = pit_read_count();
    for (uint32_t i = 0; i < 100000 && !out->pit_counting; i++)
        out->pit_counting = pit_read_count() != first;

    /* 2. IRQ0 delivered: ticks advance. Busy-wait, bounded. */
    uint64_t start = timer_ticks();
    uint32_t seen = 0;
    if (irq_enabled()) {
        for (uint32_t spin = 0; spin < SELFTEST_SPIN_LIMIT && seen < SELFTEST_TICKS; spin++) {
            __asm__ volatile ("pause");
            seen = (uint32_t)(timer_ticks() - start);
        }
    }
    out->ticks_seen = seen;
    out->irq_delivered = seen >= SELFTEST_TICKS;

    verified = out->pit_counting && out->irq_delivered;
    return verified;
}

bool timer_wait_ticks(uint32_t n)
{
    if (!verified || !irq_enabled())
        return false;
    uint64_t target = timer_ticks() + n;
    while (timer_ticks() < target)
        __asm__ volatile ("hlt");
    return true;
}
