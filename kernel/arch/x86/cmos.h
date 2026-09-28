#ifndef MADIRE_ARCH_X86_CMOS_H
#define MADIRE_ARCH_X86_CMOS_H

/*
 * Minimal, read-only CMOS real-time clock access: the seconds value only.
 * Used as an independent reference clock by the timer diagnostic. No RTC
 * interrupts, no writes, no date/time support.
 */

#include <stdbool.h>
#include <stdint.h>

/* Current RTC seconds (0-59), waiting out an update in progress (bounded).
 * Returns false if the RTC stays busy or returns an invalid value. */
bool cmos_rtc_seconds(uint8_t *seconds);

#endif
