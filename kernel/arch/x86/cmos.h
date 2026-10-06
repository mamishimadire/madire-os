#ifndef MADIRE_ARCH_X86_CMOS_H
#define MADIRE_ARCH_X86_CMOS_H

/*
 * Minimal, read-only CMOS real-time clock access: the seconds value only.
 * Used as an independent reference clock by the timer diagnostic. No RTC
 * interrupts, no writes, no date/time support.
 *
 * 0.9.1 (additive): cmos_rtc_read() reads the raw date and time registers
 * for the shell's "date" command; kernel/rtc_time.c decodes them. Still
 * read-only, no interrupts.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/rtc_time.h"

/* Current RTC seconds (0-59), waiting out an update in progress (bounded).
 * Returns false if the RTC stays busy or returns an invalid value. */
bool cmos_rtc_seconds(uint8_t *seconds);

/* 0.9.1: the raw seconds, minutes, hours, day, month, year and status B
 * registers, waiting out an update in progress (bounded) and reading until
 * two complete reads agree. Returns false if the RTC stays busy or never
 * gives two equal reads. The values are not decoded or checked. */
bool cmos_rtc_read(struct rtc_raw *raw);

#endif
