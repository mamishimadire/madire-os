/* Unit tests for drivers/keyboard/ps2leds.c (the "set lights" exchange). */
#include <stdint.h>
#include "drivers/keyboard/ps2leds.h"
#include "tests/unit/unit.h"

static struct leds_exchange x;

static void test_normal_exchange(void)
{
    uint8_t send = 0;
    CHECK_EQ(leds_begin(&x, 0x02), 0xED);
    CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_SEND);
    CHECK_EQ(send, 0x02);
    CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_DONE);
    CHECK_EQ(x.state, 0x02);
    CHECK_EQ(x.outcome, LEDS_DONE);
    CHECK(!x.timed_out);
}

static void test_every_state(void)
{
    for (unsigned s = 0; s < 8; s++) {
        uint8_t send = 0;
        CHECK_EQ(leds_begin(&x, (uint8_t)s), 0xED);
        CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_SEND);
        CHECK_EQ(send, s);
        CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_DONE);
    }
}

static void test_state_masked(void)
{
    uint8_t send = 0;
    leds_begin(&x, 0xFF);
    CHECK_EQ(x.state, 0x07);
    CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_SEND);
    CHECK_EQ(send, 0x07);
    leds_begin(&x, 0xED);               /* 0xED & 7 = 5: never sent as the command */
    CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_SEND);
    CHECK_EQ(send, 0x05);
    CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_DONE);
}

/* 0xFE on the command byte 1-3 times: the same byte again, then it goes on. */
static void test_resend_command(void)
{
    for (unsigned n = 1; n <= 3; n++) {
        uint8_t send = 0;
        leds_begin(&x, 0x04);
        for (unsigned i = 0; i < n; i++) {
            send = 0;
            CHECK_EQ(leds_reply(&x, 0xFE, &send), LEDS_SEND);
            CHECK_EQ(send, 0xED);
        }
        CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_SEND);
        CHECK_EQ(send, 0x04);
        CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_DONE);
    }
}

/* 0xFE on the state byte 1-3 times, after 3 on the command (the count is
 * per byte). */
static void test_resend_state(void)
{
    for (unsigned n = 1; n <= 3; n++) {
        uint8_t send = 0;
        leds_begin(&x, 0x01);
        for (unsigned i = 0; i < 3; i++)
            CHECK_EQ(leds_reply(&x, 0xFE, &send), LEDS_SEND);
        CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_SEND);
        CHECK_EQ(send, 0x01);
        for (unsigned i = 0; i < n; i++) {
            send = 0;
            CHECK_EQ(leds_reply(&x, 0xFE, &send), LEDS_SEND);
            CHECK_EQ(send, 0x01);
        }
        CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_DONE);
    }
}

static void test_resend_four_times_fails(void)
{
    uint8_t send = 0;
    leds_begin(&x, 0x02);
    for (unsigned i = 0; i < 3; i++)
        CHECK_EQ(leds_reply(&x, 0xFE, &send), LEDS_SEND);
    CHECK_EQ(leds_reply(&x, 0xFE, &send), LEDS_FAILED);
    CHECK_EQ(x.failed_byte, 0xED);
    CHECK_EQ(x.reply, 0xFE);
    CHECK(!x.timed_out);

    leds_begin(&x, 0x06);
    CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_SEND);
    for (unsigned i = 0; i < 3; i++)
        CHECK_EQ(leds_reply(&x, 0xFE, &send), LEDS_SEND);
    CHECK_EQ(leds_reply(&x, 0xFE, &send), LEDS_FAILED);
    CHECK_EQ(x.failed_byte, 0x06);
    CHECK_EQ(x.reply, 0xFE);
}

static void test_timeout_on_either_byte(void)
{
    uint8_t send = 0;
    leds_begin(&x, 0x02);
    CHECK_EQ(leds_reply(&x, LEDS_TIMEOUT, &send), LEDS_FAILED);
    CHECK_EQ(x.failed_byte, 0xED);
    CHECK(x.timed_out);
    CHECK_EQ(x.reply, 0);

    leds_begin(&x, 0x03);
    CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_SEND);
    CHECK_EQ(leds_reply(&x, LEDS_TIMEOUT, &send), LEDS_FAILED);
    CHECK_EQ(x.failed_byte, 0x03);
    CHECK(x.timed_out);
}

static void test_other_replies_fail(void)
{
    static const int bad[] = { 0x00, 0xFF, 0xEE, 0x48, 0xFC };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        uint8_t send = 0;
        leds_begin(&x, 0x02);
        CHECK_EQ(leds_reply(&x, bad[i], &send), LEDS_FAILED);
        CHECK_EQ(x.failed_byte, 0xED);
        CHECK_EQ(x.reply, bad[i]);
        CHECK(!x.timed_out);

        leds_begin(&x, 0x02);
        CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_SEND);
        CHECK_EQ(leds_reply(&x, bad[i], &send), LEDS_FAILED);
        CHECK_EQ(x.failed_byte, 0x02);
        CHECK_EQ(x.reply, bad[i]);
    }
}

/* A finished exchange keeps its step and its failure details. */
static void test_finished_stays_finished(void)
{
    uint8_t send = 0x55;
    leds_begin(&x, 0x02);
    CHECK_EQ(leds_reply(&x, 0x00, &send), LEDS_FAILED);
    CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_FAILED);
    CHECK_EQ(leds_reply(&x, LEDS_TIMEOUT, &send), LEDS_FAILED);
    CHECK_EQ(x.failed_byte, 0xED);
    CHECK_EQ(x.reply, 0x00);
    CHECK(!x.timed_out);
    CHECK_EQ(send, 0x55);

    leds_begin(&x, 0x02);
    leds_reply(&x, 0xFA, &send);
    CHECK_EQ(leds_reply(&x, 0xFA, &send), LEDS_DONE);
    CHECK_EQ(leds_reply(&x, 0xFE, &send), LEDS_DONE);
    CHECK_EQ(leds_reply(&x, LEDS_TIMEOUT, &send), LEDS_DONE);
    CHECK(!x.timed_out);
}

/* begin clears the previous exchange's failure. */
static void test_begin_resets(void)
{
    uint8_t send = 0;
    leds_begin(&x, 0x02);
    leds_reply(&x, LEDS_TIMEOUT, &send);
    CHECK_EQ(leds_begin(&x, 0x01), 0xED);
    CHECK_EQ(x.outcome, LEDS_SEND);
    CHECK(!x.timed_out);
    CHECK_EQ(x.failed_byte, 0);
    CHECK_EQ(x.resends, 0);
}

int main(void)
{
    RUN(test_normal_exchange);
    RUN(test_every_state);
    RUN(test_state_masked);
    RUN(test_resend_command);
    RUN(test_resend_state);
    RUN(test_resend_four_times_fails);
    RUN(test_timeout_on_either_byte);
    RUN(test_other_replies_fail);
    RUN(test_finished_stays_finished);
    RUN(test_begin_resets);
    return unit_report("ps2leds");
}
