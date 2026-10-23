/* Unit tests for drivers/mouse/mouse_core.c (the PS/2 mouse's pure part,
 * 1.2 stage 7): the start sequence against a fake controller in both
 * starting states seen in stage 3 (QEMU 0x61: port 2's clock off;
 * VirtualBox 0x45: on) and every failure, the undo, the IRQ12 decision,
 * the byte queue, and the packet decoder (sync on bit 3, the sign and
 * overflow bits, the button edges, resynchronisation after a bad byte).
 * The module is included directly; drivers/ps2/i8042.c is linked. */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "drivers/mouse/mouse_core.c"

/*
 * A fake i8042 with a mouse. The controller keeps its configuration byte
 * and an output queue of (byte, from port 2); it answers 0x20 with the
 * configuration, takes the next data byte after 0x60 as the configuration
 * (unless `cfg_stuck`), sets and clears the clock bits for 0xA7/0xA8 and
 * 0xAD/0xAE (0xA8 leaves port 2's clock off if `single_port`), answers
 * 0xA9 with `test_reply`, and sends the data byte after 0xD4 to the mouse,
 * which answers with `mouse_reply[command]` (0: no answer). No clock: only
 * the spin bound ends a wait.
 */
static uint8_t cfg;
static bool single_port, cfg_stuck;
static uint8_t test_reply;
static uint8_t mouse_reply[256];
static uint8_t q_byte[64];
static bool q_aux[64];
static uint32_t q_head, q_len;
static int next_data;               /* 0: none; 1: config; 2: mouse */
static uint8_t log_bytes[64];       /* every command and data byte written, in order */
static uint32_t log_len;

static void fake_reset(uint8_t start_cfg)
{
    cfg = start_cfg;
    single_port = false;
    cfg_stuck = false;
    test_reply = 0x00;
    memset(mouse_reply, 0, sizeof(mouse_reply));
    mouse_reply[MOUSE_DEV_SET_DEFAULTS] = MOUSE_DEV_ACK;
    mouse_reply[MOUSE_DEV_ENABLE_REPORTING] = MOUSE_DEV_ACK;
    q_head = 0;
    q_len = 0;
    next_data = 0;
    log_len = 0;
}

static void out(uint8_t b, bool aux)
{
    q_byte[(q_head + q_len) % 64] = b;
    q_aux[(q_head + q_len) % 64] = aux;
    q_len++;
}

static uint8_t fake_status(void)
{
    if (!q_len)
        return 0;
    return I8042_STATUS_OUTPUT_FULL | (q_aux[q_head] ? I8042_STATUS_AUX_DATA : 0);
}

static uint8_t fake_data(void)
{
    if (!q_len)
        return 0;
    uint8_t b = q_byte[q_head];
    q_head = (q_head + 1) % 64;
    q_len--;
    return b;
}

static void fake_command(uint8_t c)
{
    log_bytes[log_len++ % 64] = c;
    switch (c) {
    case MOUSE_CTL_READ_CONFIG:   out(cfg, false); break;
    case MOUSE_CTL_WRITE_CONFIG:  next_data = 1; break;
    case MOUSE_CTL_DISABLE_AUX:   cfg |= MOUSE_CFG_PORT2_CLOCK_OFF; break;
    case MOUSE_CTL_ENABLE_AUX:    if (!single_port) cfg &= (uint8_t)~MOUSE_CFG_PORT2_CLOCK_OFF; break;
    case MOUSE_CTL_TEST_AUX:      out(test_reply, false); break;
    case MOUSE_CTL_DISABLE_PORT1: cfg |= MOUSE_CFG_PORT1_CLOCK_OFF; break;
    case MOUSE_CTL_ENABLE_PORT1:  cfg &= (uint8_t)~MOUSE_CFG_PORT1_CLOCK_OFF; break;
    case MOUSE_CTL_WRITE_AUX:     next_data = 2; break;
    }
}

static void fake_write(uint8_t b)
{
    log_bytes[log_len++ % 64] = b;
    if (next_data == 1 && !cfg_stuck)
        cfg = b;
    else if (next_data == 2 && mouse_reply[b])
        out(mouse_reply[b], true);
    next_data = 0;
}

static bool no_clock(uint64_t *now)
{
    (void)now;
    return false;
}

static const struct i8042_bus bus = { fake_status, fake_data, fake_command, fake_write, no_clock };

static bool logged(const uint8_t *want, uint32_t n)
{
    return log_len == n && memcmp(log_bytes, want, n) == 0;
}

static void test_start_qemu(void)
{
    struct mouse_start r;
    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r));
    CHECK_EQ(r.step, MOUSE_START_OK);
    CHECK_EQ(r.config_before, 0x61);
    CHECK_EQ(r.config_enabled, 0x51);           /* port 1 off, port 2's clock on */
    static const uint8_t seq[] = { 0xAD, 0x20, 0xA8, 0x20, 0xA9, 0xD4, 0xF6, 0xD4, 0xF4 };
    CHECK(logged(seq, sizeof(seq)));
    CHECK(mouse_enable_irq(&bus, &r));
    CHECK_EQ(r.config_after, 0x43);
    CHECK_EQ(r.config_read, 0x53);
    CHECK(mouse_port1_on(&bus));
    CHECK_EQ(cfg, 0x43);
    static const uint8_t all[] = { 0xAD, 0x20, 0xA8, 0x20, 0xA9, 0xD4, 0xF6, 0xD4, 0xF4, 0x60, 0x53, 0x20, 0xAE };
    CHECK(logged(all, sizeof(all)));
    CHECK_EQ(q_len, 0);
    CHECK_EQ(r.dropped + r.skipped, 0);
}

static void test_start_virtualbox(void)
{
    struct mouse_start r;
    fake_reset(0x45);
    CHECK(mouse_start(&bus, &r));
    CHECK_EQ(r.config_before, 0x45);
    CHECK_EQ(r.config_enabled, 0x55);
    CHECK(mouse_enable_irq(&bus, &r));
    CHECK_EQ(r.config_after, 0x47);
    CHECK(mouse_port1_on(&bus));
    CHECK_EQ(cfg, 0x47);
}

static void test_bytes_on_the_way(void)
{
    struct mouse_start r;
    fake_reset(0x61);
    out(0x1E, false);                           /* a key byte already waiting */
    out(0x9E, false);
    CHECK(mouse_start(&bus, &r));
    CHECK_EQ(r.dropped, 2);
    CHECK_EQ(r.skipped, 0);

    /* A port 1 byte in front of the mouse's reply: skipped, the reply taken. */
    fake_reset(0x61);
    uint8_t reply = 0;
    CHECK(i8042_send_command(&bus, MOUSE_CTL_DISABLE_PORT1));
    out(0x2A, false);
    r = (struct mouse_start){ 0 };
    CHECK(aux_command(&bus, &r, MOUSE_DEV_SET_DEFAULTS, &reply));
    CHECK_EQ(reply, MOUSE_DEV_ACK);
    CHECK_EQ(r.skipped, 1);
}

static void test_failures(void)
{
    struct mouse_start r;

    fake_reset(0x61);
    single_port = true;
    CHECK(!mouse_start(&bus, &r));
    CHECK_EQ(r.step, MOUSE_START_NO_PORT2);
    CHECK_EQ(r.reply, 0x71);
    mouse_undo(&bus, &r);
    CHECK_EQ(cfg, 0x61);                        /* as before, port 1 on */

    fake_reset(0x61);
    test_reply = 0x03;
    CHECK(!mouse_start(&bus, &r));
    CHECK_EQ(r.step, MOUSE_START_TEST);
    CHECK(!r.timed_out);
    CHECK_EQ(r.reply, 0x03);

    fake_reset(0x45);
    mouse_reply[MOUSE_DEV_SET_DEFAULTS] = 0;    /* no mouse answers */
    CHECK(!mouse_start(&bus, &r));
    CHECK_EQ(r.step, MOUSE_START_DEFAULTS);
    CHECK(r.timed_out);
    mouse_undo(&bus, &r);
    CHECK_EQ(cfg, 0x45);                        /* port 2's clock back on, as it was */
    CHECK_EQ(log_bytes[(log_len - 4) % 64], MOUSE_CTL_DISABLE_AUX);

    fake_reset(0x61);
    mouse_reply[MOUSE_DEV_SET_DEFAULTS] = 0xFE; /* resend */
    CHECK(!mouse_start(&bus, &r));
    CHECK_EQ(r.step, MOUSE_START_DEFAULTS);
    CHECK(!r.timed_out);
    CHECK_EQ(r.reply, 0xFE);

    fake_reset(0x61);
    mouse_reply[MOUSE_DEV_ENABLE_REPORTING] = 0xFC;
    CHECK(!mouse_start(&bus, &r));
    CHECK_EQ(r.step, MOUSE_START_REPORTING);
    CHECK_EQ(r.reply, 0xFC);

    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r));
    cfg_stuck = true;
    CHECK(!mouse_enable_irq(&bus, &r));
    CHECK_EQ(r.step, MOUSE_START_IRQ_CONFIG);
    CHECK(!r.timed_out);
    CHECK_EQ(r.reply, 0x51);                    /* as mouse_start left it */
}

/* test=mousefail=N: step N fails without its command being sent; every
 * step before it ran; the undo leaves the configuration as it was. */
static void test_injection(void)
{
    static const uint8_t full[] = { 0xAD, 0x20, 0xA8, 0x20, 0xA9, 0xD4, 0xF6, 0xD4, 0xF4, 0x60, 0x53, 0x20, 0xAE };
    static const uint32_t sent[MOUSE_FAIL_STEPS + 1] = { 0, 1, 2, 4, 5, 7, 9, 12 };   /* bytes before step N */
    static const enum mouse_start_step as[MOUSE_FAIL_STEPS + 1] = {
        MOUSE_START_OK, MOUSE_START_CONFIG, MOUSE_START_CONFIG, MOUSE_START_TEST, MOUSE_START_DEFAULTS,
        MOUSE_START_REPORTING, MOUSE_START_IRQ_CONFIG, MOUSE_START_PORT1,
    };
    for (uint32_t n = 1; n <= MOUSE_FAIL_STEPS; n++) {
        struct mouse_start r;
        fake_reset(0x61);
        mouse_fail_at(n);
        CHECK_EQ(mouse_fail_step(), n);
        bool ok = mouse_start(&bus, &r) && mouse_enable_irq(&bus, &r) && mouse_finish(&bus, &r);
        CHECK(!ok);
        CHECK_EQ(r.step, as[n]);
        CHECK(r.injected && r.timed_out);
        CHECK(logged(full, sent[n]));
        CHECK_EQ(r.have_before, n >= 2);
        mouse_undo(&bus, &r);
        CHECK_EQ(cfg, 0x61);
        uint8_t now = 0;
        CHECK(mouse_read_config(&bus, &now) && now == 0x61);
    }
    mouse_fail_at(0);
    struct mouse_start r;
    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r) && mouse_enable_irq(&bus, &r) && mouse_finish(&bus, &r));
    CHECK(!r.injected);
    CHECK_EQ(cfg, 0x43);
}

static void test_irq_action(void)
{
    CHECK_EQ(mouse_irq_action(0x00), MOUSE_IRQ_NONE);
    CHECK_EQ(mouse_irq_action(I8042_STATUS_AUX_DATA), MOUSE_IRQ_NONE);
    CHECK_EQ(mouse_irq_action(I8042_STATUS_OUTPUT_FULL), MOUSE_IRQ_KEYBOARD);
    CHECK_EQ(mouse_irq_action(I8042_STATUS_OUTPUT_FULL | I8042_STATUS_AUX_DATA), MOUSE_IRQ_AUX);
    CHECK_EQ(mouse_irq_action(0xFF), MOUSE_IRQ_AUX);
}

static void test_queue(void)
{
    static struct mouse_queue q;
    uint8_t b;
    mouse_queue_init(&q);
    CHECK(!mouse_queue_pop(&q, &b));
    for (uint32_t round = 0; round < 3; round++) {          /* wraps around */
        for (uint32_t i = 0; i < MOUSE_QUEUE_SIZE; i++)
            CHECK(mouse_queue_push(&q, (uint8_t)(i + round)));
        CHECK(!mouse_queue_push(&q, 0x55));
        CHECK_EQ(q.dropped, round + 1);
        for (uint32_t i = 0; i < MOUSE_QUEUE_SIZE / 2; i++)
            CHECK(mouse_queue_pop(&q, &b) && b == (uint8_t)(i + round));
        for (uint32_t i = 0; i < MOUSE_QUEUE_SIZE / 2; i++)
            CHECK(mouse_queue_push(&q, (uint8_t)(0x80 + i)));
        for (uint32_t i = MOUSE_QUEUE_SIZE / 2; i < MOUSE_QUEUE_SIZE; i++)
            CHECK(mouse_queue_pop(&q, &b) && b == (uint8_t)(i + round));
        for (uint32_t i = 0; i < MOUSE_QUEUE_SIZE / 2; i++)
            CHECK(mouse_queue_pop(&q, &b) && b == (uint8_t)(0x80 + i));
        CHECK(!mouse_queue_pop(&q, &b));
    }
}

static int feed(struct mouse_decoder *d, const uint8_t *bytes, uint32_t n, struct mouse_event *e, uint32_t max)
{
    int got = 0;
    for (uint32_t i = 0; i < n; i++)
        if (mouse_decode(d, bytes[i], &e[got < (int)max ? got : (int)max - 1]))
            got++;
    return got;
}

static void test_decode(void)
{
    struct mouse_decoder d;
    struct mouse_event e[8];
    mouse_decoder_init(&d);

    /* dx +10, dy +5 (up): screen dy -5. */
    static const uint8_t p1[] = { 0x08, 10, 5 };
    CHECK_EQ(feed(&d, p1, 2, e, 8), 0);         /* not before its third byte */
    CHECK_EQ(feed(&d, p1 + 2, 1, e, 8), 1);
    CHECK(e[0].dx == 10 && e[0].dy == -5 && !e[0].buttons && !e[0].pressed && !e[0].released && !e[0].overflow);

    /* The 9-bit values: -1 / -1, -256 / +255, +255 / -256. */
    static const uint8_t p2[] = { 0x38, 0xFF, 0xFF, 0x18, 0x00, 0xFF, 0x28, 0xFF, 0x00 };
    CHECK_EQ(feed(&d, p2, sizeof(p2), e, 8), 3);
    CHECK(e[0].dx == -1 && e[0].dy == 1);
    CHECK(e[1].dx == -256 && e[1].dy == -255);
    CHECK(e[2].dx == 255 && e[2].dy == 256);

    /* Buttons: left down, left + right, right only, none; middle on its own. */
    static const uint8_t p3[] = { 0x09, 0, 0, 0x0B, 1, 0, 0x0A, 0, 0, 0x08, 0, 0, 0x0C, 0, 0, 0x08, 0, 0 };
    CHECK_EQ(feed(&d, p3, sizeof(p3), e, 8), 6);
    CHECK(e[0].buttons == 1 && e[0].pressed == 1 && e[0].released == 0);
    CHECK(e[1].buttons == 3 && e[1].pressed == 2 && e[1].released == 0 && e[1].dx == 1);
    CHECK(e[2].buttons == 2 && e[2].pressed == 0 && e[2].released == 1);
    CHECK(e[3].buttons == 0 && e[3].pressed == 0 && e[3].released == 2);
    CHECK(e[4].buttons == 4 && e[4].pressed == 4);
    CHECK(e[5].buttons == 0 && e[5].released == 4);

    /* Overflow (either bit): the motion dropped, the buttons kept. */
    static const uint8_t p4[] = { 0x49, 0x7F, 0x10, 0x88, 0x01, 0x02, 0x08, 3, 4 };
    CHECK_EQ(feed(&d, p4, sizeof(p4), e, 8), 3);
    CHECK(e[0].overflow && e[0].dx == 0 && e[0].dy == 0 && e[0].buttons == 1 && e[0].pressed == 1);
    CHECK(e[1].overflow && e[1].dx == 0 && e[1].dy == 0 && e[1].released == 1);
    CHECK(!e[2].overflow && e[2].dx == 3 && e[2].dy == -4);
    CHECK_EQ(d.overflows, 2);

    CHECK_EQ(d.packets, 13);
    CHECK_EQ(d.bad_bytes, 0);
}

static void test_resync(void)
{
    struct mouse_decoder d;
    struct mouse_event e[8];
    mouse_decoder_init(&d);

    /* Two bytes without bit 3 before a packet: dropped, counted; the
     * packet after them decoded. A byte without bit 3 in the middle of a
     * packet is data (bytes 1 and 2 may hold any value). */
    static const uint8_t p[] = { 0x00, 0x07, 0x08, 0x02, 0x00, 0x09, 0x05, 0x06 };
    CHECK_EQ(feed(&d, p, sizeof(p), e, 8), 2);
    CHECK_EQ(d.bad_bytes, 2);
    CHECK(e[0].dx == 2 && e[0].dy == 0 && e[0].buttons == 0);
    CHECK(e[1].dx == 5 && e[1].dy == -6 && e[1].buttons == 1 && e[1].pressed == 1);

    /* Lost bytes: a stream started in the middle of a packet (its last
     * byte 0x30, no bit 3) resynchronises at the next packet. */
    mouse_decoder_init(&d);
    static const uint8_t q[] = { 0x30, 0x0A, 0x01, 0x01, 0x08, 0x04, 0x04 };
    int got = feed(&d, q, sizeof(q), e, 8);
    CHECK_EQ(d.bad_bytes, 1);
    CHECK_EQ(got, 2);           /* 0x0A 0x01 0x01 is taken as a packet: no way to tell */
    CHECK(e[1].dx == 4 && e[1].dy == -4);
}

int main(void)
{
    RUN(test_start_qemu);
    RUN(test_start_virtualbox);
    RUN(test_bytes_on_the_way);
    RUN(test_failures);
    RUN(test_injection);
    RUN(test_irq_action);
    RUN(test_queue);
    RUN(test_decode);
    RUN(test_resync);
    return unit_report("mouse");
}
