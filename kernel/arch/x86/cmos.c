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
