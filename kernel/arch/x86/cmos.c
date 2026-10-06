/* Read-only CMOS RTC seconds (see cmos.h). */
#include "kernel/arch/x86/cmos.h"
#include "kernel/arch/x86/io.h"

#define CMOS_ADDR           0x70    /* bit 7 = NMI disable: left clear */
#define CMOS_DATA           0x71
#define RTC_SECONDS         0x00
#define RTC_STATUS_A        0x0A
#define RTC_STATUS_B        0x0B
#define STATUS_A_UPDATING   0x80
#define STATUS_B_BINARY     0x04
#define SPIN_LIMIT          100000u

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static bool wait_not_updating(void)
{
    for (uint32_t i = 0; i < SPIN_LIMIT; i++)
        if (!(cmos_read(RTC_STATUS_A) & STATUS_A_UPDATING))
            return true;
    return false;
}

bool cmos_rtc_seconds(uint8_t *seconds)
{
    /* Read until two consecutive reads agree, so an update between them
     * cannot give a torn value. */
    uint8_t value = 0, again = 0xFF;
    for (int attempt = 0; attempt < 8 && value != again; attempt++) {
        if (!wait_not_updating())
            return false;
        value = cmos_read(RTC_SECONDS);
        if (!wait_not_updating())
            return false;
        again = cmos_read(RTC_SECONDS);
    }
    if (value != again)
        return false;

    if (!(cmos_read(RTC_STATUS_B) & STATUS_B_BINARY))
        value = (uint8_t)((value & 0x0F) + (value >> 4) * 10);     /* BCD */
    if (value > 59)
        return false;
    *seconds = value;
    return true;
}

/* 0.9.1: the date and time registers (see cmos.h). */
#define RTC_MINUTES         0x02
#define RTC_HOURS           0x04
#define RTC_DAY             0x07
#define RTC_MONTH           0x08
#define RTC_YEAR            0x09

static bool read_all(struct rtc_raw *r)
{
    if (!wait_not_updating())
        return false;
    r->second = cmos_read(RTC_SECONDS);
    r->minute = cmos_read(RTC_MINUTES);
    r->hour = cmos_read(RTC_HOURS);
    r->day = cmos_read(RTC_DAY);
    r->month = cmos_read(RTC_MONTH);
    r->year = cmos_read(RTC_YEAR);
    return true;
}

static bool same(const struct rtc_raw *a, const struct rtc_raw *b)
{
    return a->second == b->second && a->minute == b->minute && a->hour == b->hour
        && a->day == b->day && a->month == b->month && a->year == b->year;
}

bool cmos_rtc_read(struct rtc_raw *raw)
{
    /* Read until two complete reads agree, so an update between the
     * registers cannot give a torn date (e.g. 23:59:59 on the next day). */
    struct rtc_raw a, b;
    for (int attempt = 0; attempt < 8; attempt++) {
        if (!read_all(&a) || !read_all(&b))
            return false;
        if (same(&a, &b)) {
            a.status_b = cmos_read(RTC_STATUS_B);
            *raw = a;
            return true;
        }
    }
    return false;
}
