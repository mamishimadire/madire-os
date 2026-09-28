#ifndef MADIRE_TIMER_H
#define MADIRE_TIMER_H

/*
 * System timer: PIT channel 0 at TIMER_HZ on IRQ0.
 *
 * Ticks are a 64-bit counter (never wraps in practice: ~5.8 billion years
 * at 100 Hz); seconds are a separate 32-bit counter kept by the IRQ handler
 * so nothing needs 64-bit division.
 */

#include <stdbool.h>
#include <stdint.h>

#define TIMER_HZ 100u

struct timer_selftest {
    bool pit_counting;      /* channel-0 count changes between reads */
    bool irq_delivered;     /* IRQ0 advanced the tick counter */
    uint32_t ticks_seen;    /* ticks observed during the self-test */
    uint16_t divisor;       /* programmed divisor */
};

/* Registers the IRQ0 handler, programs the PIT and unmasks IRQ0 (in that
 * order). Returns false if any step failed; the timer then stays unusable. */
bool timer_init(void);

/* Proves the timer works: PIT counting and IRQ0 delivering ticks, using a
 * bounded busy-wait (never hlt, so a dead timer cannot hang the boot).
 * Marks the timer verified on success. */
bool timer_selftest(struct timer_selftest *out);

bool timer_verified(void);
uint64_t timer_ticks(void);             /* interrupt-safe 64-bit read */
uint32_t timer_uptime_seconds(void);

/* Waits n ticks using hlt. Returns false at once, without waiting, if the
 * timer is not verified or interrupts are disabled, so it can never hang. */
bool timer_wait_ticks(uint32_t n);

#endif
