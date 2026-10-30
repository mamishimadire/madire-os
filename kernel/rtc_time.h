#ifndef MADIRE_RTC_TIME_H
#define MADIRE_RTC_TIME_H

/*
 * Decoding the CMOS real-time clock's date and time (0.9.1).
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_rtc.c). kernel/arch/x86/cmos.c reads the raw
 * registers (cmos_rtc_read); this module turns them into a checked date.
 *
 *   - Status register B bit 2 set: binary values; clear: BCD (each nibble
 *     must be 0-9).
 *   - Bit 1 set: 24-hour clock; clear: 12-hour clock, with bit 7 of the
 *     hours register meaning PM (12 AM is hour 0, 12 PM is hour 12).
 *   - The year register holds two digits. The century is assumed to be 20:
 *     the CMOS century register is only found through ACPI, which Madire OS
 *     doesn't read yet, so the years shown are 2000-2099.
 *   - Every field is range-checked, including the day against the month's
 *     length (leap years: every 4th year, 2000 included).
 *   - No time zone: the RTC holds whatever the firmware or hypervisor set
 *     (QEMU defaults to UTC, VirtualBox to the host's local time).
 */

#include <stdbool.h>
#include <stdint.h>

#define RTC_STATUS_B_24H      0x02
#define RTC_STATUS_B_BINARY   0x04
#define RTC_HOUR_PM           0x80

struct rtc_raw {            /* registers as read, unconverted */
    uint8_t second, minute, hour, day, month, year;
    uint8_t status_b;
};

struct rtc_time {
    uint16_t year;          /* 2000-2099 */
    uint8_t month;          /* 1-12 */
    uint8_t day;            /* 1-31 */
    uint8_t hour;           /* 0-23 */
    uint8_t minute;         /* 0-59 */
    uint8_t second;         /* 0-59 */
};

/* Decodes and checks the raw registers. false (and *t unchanged) on any
 * invalid field. */
bool rtc_decode(const struct rtc_raw *raw, struct rtc_time *t);

/* Days in a month of a year (0 for a month outside 1-12). */
uint8_t rtc_days_in_month(uint16_t year, uint8_t month);

#define RTC_TEXT_SIZE 20    /* "YYYY-MM-DD HH:MM:SS" + NUL */

/* Formats as "2026-10-05 14:03:27" into out[RTC_TEXT_SIZE]. */
void rtc_format(const struct rtc_time *t, char out[RTC_TEXT_SIZE]);

#define RTC_CLOCK_SIZE 6    /* "HH:MM" + NUL */

/* 1.3 stage 3, the desktop's clock (Design Guide, section 10): "HH:MM"
 * (24 hours, leading zeros) of the registers `raw` as cmos_rtc_read() read
 * them, or "--:--" if the read failed (`read_ok` false: the RTC stayed busy
 * updating, or never gave two equal reads) or any field is invalid
 * (rtc_decode). */
void rtc_clock_text(const struct rtc_raw *raw, bool read_ok, char out[RTC_CLOCK_SIZE]);

#endif
