/* Unit tests for rtc_clock_text (kernel/rtc_time.c), the desktop's clock
 * (1.3 stage 3, Design Guide section 10): "HH:MM" from the registers as
 * cmos_rtc_read() reads them, in BCD and binary, 24-hour and 12-hour
 * (12 AM and 12 PM included), leading zeros; "--:--" when the read failed
 * (cmos_rtc_read() gives up while the RTC's update-in-progress flag stays
 * set, or when two reads never agree: read_ok false) and for an invalid
 * field. */
#include <stdio.h>
#include <string.h>
#include "kernel/rtc_time.h"
#include "tests/unit/unit.h"

#define BCD24  RTC_STATUS_B_24H
#define BIN24  (RTC_STATUS_B_24H | RTC_STATUS_B_BINARY)
#define BCD12  0x00
#define BIN12  RTC_STATUS_B_BINARY

static int shows(uint8_t hour, uint8_t minute, uint8_t status_b, bool ok, const char *want)
{
    struct rtc_raw r = { 0x30, minute, hour, 0x15, 0x06, 0x26, status_b };
    char out[RTC_CLOCK_SIZE];
    memset(out, 'x', sizeof(out));
    rtc_clock_text(&r, ok, out);
    if (strcmp(out, want) != 0) {
        printf("    got %s, expected %s\n", out, want);
        return 0;
    }
    return 1;
}

static void test_bcd_24(void)
{
    CHECK(shows(0x14, 0x03, BCD24, true, "14:03"));
    CHECK(shows(0x00, 0x00, BCD24, true, "00:00"));
    CHECK(shows(0x23, 0x59, BCD24, true, "23:59"));
    CHECK(shows(0x09, 0x05, BCD24, true, "09:05"));
}

static void test_binary_24(void)
{
    CHECK(shows(14, 3, BIN24, true, "14:03"));
    CHECK(shows(0, 0, BIN24, true, "00:00"));
    CHECK(shows(23, 59, BIN24, true, "23:59"));
}

static void test_12_hour(void)
{
    CHECK(shows(0x12, 0x00, BCD12, true, "00:00"));                 /* 12 AM */
    CHECK(shows(0x12 | RTC_HOUR_PM, 0x30, BCD12, true, "12:30"));   /* 12 PM */
    CHECK(shows(0x01 | RTC_HOUR_PM, 0x07, BCD12, true, "13:07"));
    CHECK(shows(0x11 | RTC_HOUR_PM, 0x59, BCD12, true, "23:59"));
    CHECK(shows(0x09, 0x41, BCD12, true, "09:41"));
    CHECK(shows(12 | RTC_HOUR_PM, 15, BIN12, true, "12:15"));
    CHECK(shows(12, 15, BIN12, true, "00:15"));
    CHECK(shows(7 | RTC_HOUR_PM, 45, BIN12, true, "19:45"));
}

static void test_unreadable(void)
{
    /* The update-in-progress flag never cleared (or the reads never
     * agreed): cmos_rtc_read() returned false. */
    CHECK(shows(0x14, 0x03, BCD24, false, "--:--"));
    CHECK(shows(14, 3, BIN24, false, "--:--"));
    /* Invalid fields. */
    CHECK(shows(0x1A, 0x03, BCD24, true, "--:--"));         /* a BCD nibble above 9 */
    CHECK(shows(0x24, 0x00, BCD24, true, "--:--"));         /* hour 24 */
    CHECK(shows(0x12, 0x60, BCD24, true, "--:--"));         /* minute 60 */
    CHECK(shows(0x00, 0x10, BCD12, true, "--:--"));         /* hour 0 on a 12-hour clock */
    CHECK(shows(0x13, 0x10, BCD12, true, "--:--"));         /* hour 13 on a 12-hour clock */
}

int main(void)
{
    printf("test_rtc13\n");
    RUN(test_bcd_24);
    RUN(test_binary_24);
    RUN(test_12_hour);
    RUN(test_unreadable);
    return unit_report("test_rtc13");
}
