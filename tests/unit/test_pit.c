/* Unit tests for drivers/pit/pit_divisor.h (PIT rate -> divisor). */
#include <stdint.h>
#include "drivers/pit/pit_divisor.h"
#include "tests/unit/unit.h"

static void test_standard_rates(void)
{
    uint16_t d = 0;
    CHECK(pit_divisor_for(100, &d));
    CHECK_EQ(d, 11932);                         /* 0.4 system timer rate */
    CHECK(pit_divisor_for(1000, &d));
    CHECK_EQ(d, 1193);
    CHECK(pit_divisor_for(50, &d));
    CHECK_EQ(d, 23864);
}

static void test_extremes(void)
{
    uint16_t d = 0;
    CHECK(pit_divisor_for(PIT_BASE_HZ, &d));
    CHECK_EQ(d, 1);                             /* fastest possible */
    CHECK(pit_divisor_for(19, &d));
    CHECK_EQ(d, 62799);                         /* slowest that fits 16 bits */
}

static void test_out_of_range_rejected(void)
{
    uint16_t d = 1234;
    CHECK(!pit_divisor_for(18, &d));            /* would need 66288 > 65535 */
    CHECK(!pit_divisor_for(0, &d));
    CHECK(!pit_divisor_for(PIT_BASE_HZ + 1, &d));
    CHECK(!pit_divisor_for(0xFFFFFFFFu, &d));
    CHECK_EQ(d, 1234);                          /* untouched on failure */
}

static void test_rounding_to_nearest(void)
{
    uint16_t d = 0;
    /* 1193182 / 60 = 19886.37 -> 19886 (down); / 70 = 17045.46 -> 17045
     * (down); / 90 = 13257.58 -> 13258 (up). */
    CHECK(pit_divisor_for(60, &d));
    CHECK_EQ(d, 19886);
    CHECK(pit_divisor_for(70, &d));
    CHECK_EQ(d, 17045);
    CHECK(pit_divisor_for(90, &d));
    CHECK_EQ(d, 13258);
}

static void test_actual_rate_close_to_requested(void)
{
    /* For every rate from 19 to 10000 Hz the chosen divisor yields a rate
     * within 0.5% of the request (integer check: |base - hz*d| <= hz*d/200). */
    for (uint32_t hz = 19; hz <= 10000; hz++) {
        uint16_t d = 0;
        CHECK(pit_divisor_for(hz, &d));
        uint64_t achieved_x = (uint64_t)hz * d;   /* equals base if exact */
        uint64_t diff = achieved_x > PIT_BASE_HZ ? achieved_x - PIT_BASE_HZ : PIT_BASE_HZ - achieved_x;
        CHECK(diff * 200 <= achieved_x);
    }
}

int main(void)
{
    printf("pit unit tests\n");
    RUN(test_standard_rates);
    RUN(test_extremes);
    RUN(test_out_of_range_rejected);
    RUN(test_rounding_to_nearest);
    RUN(test_actual_rate_close_to_requested);
    return unit_report("pit");
}
