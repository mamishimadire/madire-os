#ifndef MADIRE_DRIVERS_PIT_H
#define MADIRE_DRIVERS_PIT_H

/* 8253/8254 Programmable Interval Timer, channel 0 -> IRQ0. */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/arch/x86/irq.h"

/* Registers on_tick for IRQ0, then programs channel 0 in mode 2 (rate
 * generator) at `hz`. Does NOT unmask IRQ0: the caller does that with
 * irq_enable_line() once everything is ready. */
enum irq_result pit_init(uint32_t hz, irq_handler_t on_tick, uint16_t *divisor_out);

/* Current channel-0 count (latched read). Changes continuously while the
 * PIT is running, independently of whether IRQ0 is delivered. */
uint16_t pit_read_count(void);

#endif
