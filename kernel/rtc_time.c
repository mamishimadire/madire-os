/* Decoding the CMOS real-time clock's date and time (see rtc_time.h). */
#include "kernel/rtc_time.h"

/* A register value in the RTC's mode; false for a BCD nibble above 9. */
static bool value(uint8_t reg, bool binary, uint8_t *out)
{
    if (binary) {
        *out = reg;
        return true;
    }
    uint8_t hi = reg >> 4, lo = reg & 0x0F;
    if (hi > 9 || lo > 9)
        return false;
    *out = (uint8_t)(hi * 10 + lo);
    return true;
}

uint8_t rtc_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
        return 29;
    return days[month - 1];
}

bool rtc_decode(const struct rtc_raw *raw, struct rtc_time *t)
{
    bool binary = raw->status_b & RTC_STATUS_B_BINARY;
    bool h24 = raw->status_b & RTC_STATUS_B_24H;
    uint8_t sec, min, hour, day, mon, year;

    /* In 12-hour mode the PM flag sits on top of the hour value. */
    bool pm = !h24 && (raw->hour & RTC_HOUR_PM);
    uint8_t hour_reg = h24 ? raw->hour : (uint8_t)(raw->hour & ~RTC_HOUR_PM);

    if (!value(raw->second, binary, &sec) || !value(raw->minute, binary, &min) ||
        !value(hour_reg, binary, &hour) || !value(raw->day, binary, &day) ||
        !value(raw->month, binary, &mon) || !value(raw->year, binary, &year))
        return false;

    if (!h24) {
        if (hour < 1 || hour > 12)
            return false;
        if (hour == 12)
            hour = 0;               /* 12 AM = 0, 12 PM = 12 */
        if (pm)
            hour = (uint8_t)(hour + 12);
    }
    if (sec > 59 || min > 59 || hour > 23 || year > 99)
        return false;
    uint16_t full_year = (uint16_t)(2000 + year);
    if (day < 1 || day > rtc_days_in_month(full_year, mon))
        return false;               /* also catches a month outside 1-12 */

    t->year = full_year;
    t->month = mon;
    t->day = day;
    t->hour = hour;
    t->minute = min;
    t->second = sec;
    return true;
}

static char *two(char *p, uint8_t v)
{
    p[0] = (char)('0' + v / 10);
    p[1] = (char)('0' + v % 10);
    return p + 2;
}

void rtc_format(const struct rtc_time *t, char out[RTC_TEXT_SIZE])
{
    char *p = out;
    p = two(p, (uint8_t)(t->year / 100));
    p = two(p, (uint8_t)(t->year % 100));
    *p++ = '-';
    p = two(p, t->month);
    *p++ = '-';
    p = two(p, t->day);
    *p++ = ' ';
    p = two(p, t->hour);
    *p++ = ':';
    p = two(p, t->minute);
    *p++ = ':';
    p = two(p, t->second);
    *p = '\0';
}

void rtc_clock_text(const struct rtc_raw *raw, bool read_ok, char out[RTC_CLOCK_SIZE])
{
    struct rtc_time t;
    if (!read_ok || !rtc_decode(raw, &t)) {
        out[0] = out[1] = out[3] = out[4] = '-';
        out[2] = ':';
        out[5] = '\0';
        return;
    }
    char *p = two(out, t.hour);
    *p++ = ':';
    p = two(p, t.minute);
    *p = '\0';
}
