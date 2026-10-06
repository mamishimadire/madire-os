/* Unit tests for kernel/rtc_time.c (RTC date and time decoding, 0.9.1). */
#include <stdio.h>
#include <string.h>
#include "kernel/rtc_time.h"
#include "tests/unit/unit.h"

#define BCD24  RTC_STATUS_B_24H                         /* BCD, 24-hour */
#define BIN24  (RTC_STATUS_B_24H | RTC_STATUS_B_BINARY)
#define BCD12  0x00                                     /* BCD, 12-hour */
#define BIN12  RTC_STATUS_B_BINARY

static struct rtc_raw raw(uint8_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s,
                          uint8_t b)
{
    struct rtc_raw r = { s, mi, h, d, mo, y, b };
    return r;
}

static int decodes_to(struct rtc_raw r, const char *text)
{
    struct rtc_time t;
    char out[RTC_TEXT_SIZE];
    if (!rtc_decode(&r, &t))
        return 0;
    rtc_format(&t, out);
    if (strcmp(out, text) != 0) {
        printf("    got %s, expected %s\n", out, text);
        return 0;
    }
    return 1;
}

static int rejected(struct rtc_raw r)
{
    struct rtc_time t = { 1234, 5, 6, 7, 8, 9 };
    bool ok = rtc_decode(&r, &t);
    /* *t stays unchanged on failure */
    return !ok && t.year == 1234 && t.month == 5 && t.second == 9;
}

static void test_bcd_24h(void)
{
    CHECK(decodes_to(raw(0x26, 0x10, 0x05, 0x14, 0x03, 0x27, BCD24), "2026-10-05 14:03:27"));
    CHECK(decodes_to(raw(0x00, 0x01, 0x01, 0x00, 0x00, 0x00, BCD24), "2000-01-01 00:00:00"));
    CHECK(decodes_to(raw(0x99, 0x12, 0x31, 0x23, 0x59, 0x59, BCD24), "2099-12-31 23:59:59"));
}

static void test_binary_24h(void)
{
    CHECK(decodes_to(raw(26, 10, 5, 14, 3, 27, BIN24), "2026-10-05 14:03:27"));
    CHECK(decodes_to(raw(99, 12, 31, 23, 59, 59, BIN24), "2099-12-31 23:59:59"));
}

static void test_12_hour(void)
{
    /* BCD: 12 AM is midnight, 12 PM is noon, PM adds 12 to 1-11. */
    CHECK(decodes_to(raw(0x26, 0x10, 0x05, 0x12, 0x00, 0x00, BCD12), "2026-10-05 00:00:00"));
    CHECK(decodes_to(raw(0x26, 0x10, 0x05, 0x01, 0x30, 0x00, BCD12), "2026-10-05 01:30:00"));
    CHECK(decodes_to(raw(0x26, 0x10, 0x05, 0x11, 0x59, 0x59, BCD12), "2026-10-05 11:59:59"));
    CHECK(decodes_to(raw(0x26, 0x10, 0x05, 0x92, 0x00, 0x00, BCD12), "2026-10-05 12:00:00"));
    CHECK(decodes_to(raw(0x26, 0x10, 0x05, 0x82, 0x15, 0x00, BCD12), "2026-10-05 14:15:00"));
    CHECK(decodes_to(raw(0x26, 0x10, 0x05, 0x91, 0x59, 0x59, BCD12), "2026-10-05 23:59:59"));
    /* Binary 12-hour */
    CHECK(decodes_to(raw(26, 10, 5, 0x80 | 12, 0, 0, BIN12), "2026-10-05 12:00:00"));
    CHECK(decodes_to(raw(26, 10, 5, 0x80 | 2, 15, 0, BIN12), "2026-10-05 14:15:00"));
    CHECK(decodes_to(raw(26, 10, 5, 12, 0, 0, BIN12), "2026-10-05 00:00:00"));
    /* Hour 0 and 13 don't exist on a 12-hour clock. */
    CHECK(rejected(raw(0x26, 0x10, 0x05, 0x00, 0, 0, BCD12)));
    CHECK(rejected(raw(0x26, 0x10, 0x05, 0x13, 0, 0, BCD12)));
    CHECK(rejected(raw(0x26, 0x10, 0x05, 0x80, 0, 0, BCD12)));
    /* In 24-hour mode bit 7 is not a PM flag: 0x80 is an invalid hour. */
    CHECK(rejected(raw(0x26, 0x10, 0x05, 0x82, 0, 0, BCD24)));
}

static void test_ranges(void)
{
    CHECK(rejected(raw(0x26, 0x10, 0x05, 0x24, 0, 0, BCD24)));     /* hour 24 */
    CHECK(rejected(raw(0x26, 0x10, 0x05, 0x00, 0x60, 0, BCD24)));  /* minute 60 */
    CHECK(rejected(raw(0x26, 0x10, 0x05, 0x00, 0, 0x60, BCD24)));  /* second 60 */
    CHECK(rejected(raw(0x26, 0x00, 0x05, 0, 0, 0, BCD24)));        /* month 0 */
    CHECK(rejected(raw(0x26, 0x13, 0x05, 0, 0, 0, BCD24)));        /* month 13 */
    CHECK(rejected(raw(0x26, 0x10, 0x00, 0, 0, 0, BCD24)));        /* day 0 */
    CHECK(rejected(raw(0x26, 0x10, 0x32, 0, 0, 0, BCD24)));        /* day 32 */
    CHECK(rejected(raw(0x26, 0x04, 0x31, 0, 0, 0, BCD24)));        /* 31 April */
    CHECK(rejected(raw(100, 10, 5, 0, 0, 0, BIN24)));              /* year 100 */
    CHECK(rejected(raw(26, 10, 5, 24, 0, 0, BIN24)));
    CHECK(rejected(raw(26, 10, 5, 0, 60, 0, BIN24)));
    CHECK(rejected(raw(26, 10, 5, 0, 0, 255, BIN24)));
}

static void test_bad_bcd(void)
{
    CHECK(rejected(raw(0x2A, 0x10, 0x05, 0, 0, 0, BCD24)));        /* nibble A */
    CHECK(rejected(raw(0x26, 0x1F, 0x05, 0, 0, 0, BCD24)));
    CHECK(rejected(raw(0x26, 0x10, 0x05, 0, 0, 0xA0, BCD24)));
    CHECK(rejected(raw(0x26, 0x10, 0x05, 0xFF, 0, 0, BCD24)));
    /* Every byte with a nibble above 9 is refused as seconds. */
    int bad = 0;
    for (int v = 0; v < 256; v++) {
        bool nib_ok = (v >> 4) <= 9 && (v & 0x0F) <= 9;
        bool ok_val = nib_ok && ((v >> 4) * 10 + (v & 0x0F)) <= 59;
        struct rtc_raw r = raw(0x26, 0x10, 0x05, 0x10, 0x10, (uint8_t)v, BCD24);
        struct rtc_time t;
        if (rtc_decode(&r, &t) != ok_val)
            bad++;
    }
    CHECK_EQ(bad, 0);
}

static void test_leap_years(void)
{
    CHECK_EQ(rtc_days_in_month(2024, 2), 29);
    CHECK_EQ(rtc_days_in_month(2026, 2), 28);
    CHECK_EQ(rtc_days_in_month(2000, 2), 29);       /* divisible by 400 */
    CHECK_EQ(rtc_days_in_month(2100, 2), 28);       /* by 100, not 400 */
    CHECK_EQ(rtc_days_in_month(2026, 1), 31);
    CHECK_EQ(rtc_days_in_month(2026, 4), 30);
    CHECK_EQ(rtc_days_in_month(2026, 12), 31);
    CHECK_EQ(rtc_days_in_month(2026, 0), 0);
    CHECK_EQ(rtc_days_in_month(2026, 13), 0);
    CHECK(decodes_to(raw(0x24, 0x02, 0x29, 0, 0, 0, BCD24), "2024-02-29 00:00:00"));
    CHECK(decodes_to(raw(0x00, 0x02, 0x29, 0, 0, 0, BCD24), "2000-02-29 00:00:00"));
    CHECK(rejected(raw(0x26, 0x02, 0x29, 0, 0, 0, BCD24)));
    CHECK(rejected(raw(0x24, 0x02, 0x30, 0, 0, 0, BCD24)));
    /* Every day of every month of a leap and a common year. */
    int bad = 0;
    for (int y = 24; y <= 25; y++)
        for (int m = 1; m <= 12; m++)
            for (int d = 1; d <= 32; d++) {
                struct rtc_raw r = raw((uint8_t)y, (uint8_t)m, (uint8_t)d, 12, 0, 0, BIN24);
                struct rtc_time t;
                bool want = d <= rtc_days_in_month((uint16_t)(2000 + y), (uint8_t)m);
                if (rtc_decode(&r, &t) != want)
                    bad++;
            }
    CHECK_EQ(bad, 0);
}

static void test_format(void)
{
    struct rtc_time t = { 2007, 3, 9, 4, 5, 6 };
    char out[RTC_TEXT_SIZE];
    memset(out, 'x', sizeof(out));
    rtc_format(&t, out);
    CHECK(strcmp(out, "2007-03-09 04:05:06") == 0);
    CHECK_EQ(strlen(out), RTC_TEXT_SIZE - 1);
}

int main(void)
{
    printf("rtc unit tests\n");
    RUN(test_bcd_24h);
    RUN(test_binary_24h);
    RUN(test_12_hour);
    RUN(test_ranges);
    RUN(test_bad_bcd);
    RUN(test_leap_years);
    RUN(test_format);
    return unit_report("rtc");
}
