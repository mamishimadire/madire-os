/* System timer (see timer.h). */
#include "drivers/pit/pit.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/timer.h"

#define SELFTEST_TICKS        3u
#define SELFTEST_SPIN_LIMIT   50000000u   /* bounds the wait if IRQ0 never comes */

/* 0.11: 8-byte aligned, so one load reads it whole (timer_ticks). */
static volatile uint64_t ticks __attribute__((aligned(8)));
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

/* 0.11: one aligned 64-bit load, which x86-64 performs atomically, so no
 * cli/sti: timer_irq is the counter's only writer, and a reader sees the
 * value before or after an increment, never half of one. (The 32-bit kernel
 * needed cli for its two 32-bit loads.) With more CPUs later this stays
 * correct: one writer, and aligned 64-bit loads are atomic on every CPU.
 * The cli/sti pair also made QEMU's TCG starve IRQ0 in tight wait loops:
 * it checks for interrupts when leaving a translated block, and the block
 * ended by sti left it inside the STI shadow (0.11 stage 4a diagnosis).
 * The barriers and the atomic load keep the compiler from caching or
 * hoisting the read out of a wait loop. */
uint64_t timer_ticks(void)
{
    __asm__ volatile ("" ::: "memory");
    uint64_t t = __atomic_load_n(&ticks, __ATOMIC_RELAXED);
    __asm__ volatile ("" ::: "memory");
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
