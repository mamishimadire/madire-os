#ifndef MADIRE_DEVTEST_H
#define MADIRE_DEVTEST_H

/*
 * Device diagnostics for the timer, keyboard and IRQ layer, selected with
 * test=<name> on the boot command line (GRUB Diagnostics menu or QEMU
 * -append). Runs in normal context, never in interrupt context. Results go
 * to the boot screen's message line and to the serial log.
 */

#include <stddef.h>
#include "drivers/keyboard/scancode.h"

/* test=timer: counts PIT ticks over 3 RTC seconds (independent clock).
 * PASS if within +-10% of 300; the exact count is logged. */
void devtest_timer(void);

/* test=keyboard: shows every key event (never returns). */
void devtest_keyboard(void) __attribute__((noreturn));

/* test=kbdflood: stops reading keys for 3 s so the queue overflows, then
 * drains it and reports kept / dropped events. */
void devtest_kbd_flood(void);

/* test=irqunexpected: raises IRQ2's vector in software (no handler, still
 * masked at the PIC) and checks it is counted, acknowledged and survived. */
void devtest_irq_unexpected(void);

/* One-line description of a key event, e.g.
 * "key: press sc=0x1e key=CHAR ascii='a' flags=0x01". */
void devtest_describe_key(char *buf, size_t size, const struct key_event *e);

/* Writes devtest_describe_key() output to the serial log. */
void devtest_log_key(const struct key_event *e);

#endif
