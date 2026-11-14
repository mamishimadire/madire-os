/* Unit tests for drivers/mouse/mouse_core.c (the PS/2 mouse's pure part,
 * 1.2 stage 7): the start sequence against a fake controller in both
 * starting states seen in stage 3 (QEMU 0x61: port 2's clock off;
 * VirtualBox 0x45: on) and every failure, the undo, the IRQ12 decision,
 * the byte queue, and the packet decoder (sync on bit 3, the sign and
 * overflow bits, the button edges, resynchronisation after a bad byte).
 * 1.5 stage 7 (C8): the IntelliMouse probe (ID 3, other IDs, a failed
 * step, stray motion bytes, reporting not back on), the 4-byte decoder
 * (the wheel's sign and range, the middle button) and its
 * resynchronisation after a lost byte at every offset of a packet; the
 * 3-byte path unchanged.
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
/* 1.5 stage 7: the mouse's side of the wheel probe (a model of the
 * IntelliMouse knock): every rate byte after 0xF3 is acknowledged and
 * followed; 200, 100, 80 in a row makes it a wheel mouse when `can_wheel`;
 * 0xF2 answers ACK and the ID (0, or 3 after the knock; `id_reply` -1:
 * no ID byte). `rate_arg`: the next byte to the mouse is a rate. */
static bool can_wheel, rate_arg;
static int id_reply;
static uint8_t rates[3];
static uint32_t n_rates;
static uint8_t dev_id;
static uint8_t rate_now;

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
    can_wheel = true;
    rate_arg = false;
    id_reply = 0;
    n_rates = 0;
    dev_id = 0;
    rate_now = 100;
    mouse_reply[MOUSE_DEV_SET_RATE] = MOUSE_DEV_ACK;
    mouse_reply[MOUSE_DEV_GET_ID] = MOUSE_DEV_ACK;
    mouse_reply[MOUSE_DEV_DISABLE_REPORTING] = MOUSE_DEV_ACK;
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

/* 1.5 stage 7: a rate byte (after 0xF3): acknowledged; the knock seen. */
static void fake_rate(uint8_t b)
{
    rate_arg = false;
    rate_now = b;
    rates[0] = rates[1];
    rates[1] = rates[2];
    rates[2] = b;
    if (++n_rates >= 3 && rates[0] == 200 && rates[1] == 100 && rates[2] == 80 && can_wheel)
        dev_id = MOUSE_ID_WHEEL;
    out(MOUSE_DEV_ACK, true);
}

static void fake_write(uint8_t b)
{
    log_bytes[log_len++ % 64] = b;
    if (next_data == 1 && !cfg_stuck) {
        cfg = b;
    } else if (next_data == 2 && rate_arg) {
        fake_rate(b);
    } else if (next_data == 2 && mouse_reply[b]) {
        out(mouse_reply[b], true);
        if (b == MOUSE_DEV_SET_RATE && mouse_reply[b] == MOUSE_DEV_ACK)
            rate_arg = true;
        if (b == MOUSE_DEV_GET_ID && mouse_reply[b] == MOUSE_DEV_ACK && id_reply >= 0)
            out(id_reply ? (uint8_t)id_reply : dev_id, true);
    }
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

/* ---- 1.5 stage 7 (C8): the IntelliMouse wheel ---- */

/* The bytes the probe wrote after mouse_start, in order. */
static bool logged_from(uint32_t from, const uint8_t *want, uint32_t n)
{
    return log_len - from == n && memcmp(log_bytes + from, want, n) == 0;
}

static const uint8_t wheel_seq[] = {
    0xD4, 0xF5, 0xD4, 0xF3, 0xD4, 200, 0xD4, 0xF3, 0xD4, 100, 0xD4, 0xF3, 0xD4, 80,
    0xD4, 0xF2, 0xD4, 0xF3, 0xD4, 100, 0xD4, 0xF4,
};

static void test_wheel_probe(void)
{
    struct mouse_start r;
    struct mouse_wheel w;

    /* QEMU's mouse: ID 3 after the knock; the rate back to 100, reporting
     * on, nothing left in the controller; the rest of the start as 1.2's. */
    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r));
    uint32_t from = log_len;
    CHECK(mouse_wheel_start(&bus, &r, &w));
    CHECK(w.wheel && w.have_id && w.id == MOUSE_ID_WHEEL && !w.failed && !w.rate_failed && w.stray == 0);
    CHECK(logged_from(from, wheel_seq, sizeof(wheel_seq)));
    CHECK_EQ(rate_now, 100);
    CHECK_EQ(q_len, 0);
    CHECK_EQ(r.step, MOUSE_START_OK);
    CHECK_EQ(r.skipped, 0);
    CHECK(mouse_enable_irq(&bus, &r) && mouse_finish(&bus, &r));
    CHECK_EQ(cfg, 0x43);

    /* A standard mouse: ID 0; the same bytes; no wheel. */
    fake_reset(0x45);
    can_wheel = false;
    CHECK(mouse_start(&bus, &r));
    from = log_len;
    CHECK(mouse_wheel_start(&bus, &r, &w));
    CHECK(!w.wheel && w.have_id && w.id == 0 && !w.failed && !w.rate_failed);
    CHECK(logged_from(from, wheel_seq, sizeof(wheel_seq)));
    CHECK_EQ(rate_now, 100);

    /* Another ID (4, the five-button mouse's): the 3-byte path. */
    fake_reset(0x61);
    id_reply = 4;
    CHECK(mouse_start(&bus, &r));
    CHECK(mouse_wheel_start(&bus, &r, &w));
    CHECK(!w.wheel && w.have_id && w.id == 4);

    /* Get Device ID acknowledged but no ID byte: no ID, the rate still
     * set back, reporting on. */
    fake_reset(0x61);
    id_reply = -1;
    CHECK(mouse_start(&bus, &r));
    from = log_len;
    CHECK(mouse_wheel_start(&bus, &r, &w));
    CHECK(!w.wheel && !w.have_id && w.failed && w.failed_byte == MOUSE_DEV_GET_ID && w.timed_out);
    CHECK(logged_from(from, wheel_seq, sizeof(wheel_seq)));
    CHECK(!w.rate_failed && rate_now == 100);
    CHECK_EQ(r.step, MOUSE_START_OK);

    /* Set Sample Rate refused (0xFE): the knock ends there; setting the
     * rate back fails too (noted); reporting is turned on again. */
    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r));
    mouse_reply[MOUSE_DEV_SET_RATE] = 0xFE;
    from = log_len;
    CHECK(mouse_wheel_start(&bus, &r, &w));
    CHECK(!w.wheel && !w.have_id && w.failed && w.failed_byte == MOUSE_DEV_SET_RATE && !w.timed_out &&
          w.reply == 0xFE && w.rate_failed);
    static const uint8_t refused[] = { 0xD4, 0xF5, 0xD4, 0xF3, 0xD4, 0xF3, 0xD4, 0xF4 };
    CHECK(logged_from(from, refused, sizeof(refused)));

    /* Reporting off gets no reply: no knock, no rate byte, reporting on. */
    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r));
    mouse_reply[MOUSE_DEV_DISABLE_REPORTING] = 0;
    from = log_len;
    CHECK(mouse_wheel_start(&bus, &r, &w));
    CHECK(!w.wheel && w.failed && w.failed_byte == MOUSE_DEV_DISABLE_REPORTING && w.timed_out && !w.rate_failed);
    static const uint8_t silent[] = { 0xD4, 0xF5, 0xD4, 0xF4 };
    CHECK(logged_from(from, silent, sizeof(silent)));

    /* Reporting off answered 0xFE (resend): that reply at once, not taken
     * for a motion byte; no knock; reporting on. */
    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r));
    mouse_reply[MOUSE_DEV_DISABLE_REPORTING] = MOUSE_DEV_RESEND;
    from = log_len;
    CHECK(mouse_wheel_start(&bus, &r, &w));
    CHECK(!w.wheel && w.failed && w.failed_byte == MOUSE_DEV_DISABLE_REPORTING && !w.timed_out &&
          w.reply == MOUSE_DEV_RESEND && w.stray == 0);
    CHECK(logged_from(from, silent, sizeof(silent)));

    /* A motion packet already in the controller before 0xF5's ACK:
     * dropped and counted; the probe goes on. */
    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r));
    out(0x09, true);
    out(0x02, true);
    out(0x03, true);
    CHECK(mouse_wheel_start(&bus, &r, &w));
    CHECK(w.wheel && w.id == MOUSE_ID_WHEEL && w.stray == 3 && !w.failed);
    CHECK_EQ(q_len, 0);

    /* More motion bytes than MOUSE_STRAY_LIMIT: no ACK found; reporting
     * cannot be turned on again either (a stray byte is its reply): the
     * start fails as enable reporting would. */
    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r));
    for (uint32_t i = 0; i < MOUSE_STRAY_LIMIT + 4; i++)
        out(0x08, true);
    CHECK(!mouse_wheel_start(&bus, &r, &w));
    CHECK(!w.wheel && w.failed && w.failed_byte == MOUSE_DEV_DISABLE_REPORTING && w.stray == MOUSE_STRAY_LIMIT);
    CHECK_EQ(r.step, MOUSE_START_REPORTING);
    CHECK(!r.timed_out && r.reply == 0x08);

    /* Reporting on again gets no reply: false, as enable reporting. */
    fake_reset(0x61);
    CHECK(mouse_start(&bus, &r));
    mouse_reply[MOUSE_DEV_ENABLE_REPORTING] = 0;
    CHECK(!mouse_wheel_start(&bus, &r, &w));
    CHECK(w.wheel);
    CHECK_EQ(r.step, MOUSE_START_REPORTING);
    CHECK(r.timed_out);
}

static bool pkt(const struct mouse_event *e, int32_t dx, int32_t dy, int32_t wheel, uint8_t buttons)
{
    return e->dx == dx && e->dy == dy && e->wheel == wheel && e->buttons == buttons;
}

static void test_decode_wheel(void)
{
    struct mouse_decoder d;
    struct mouse_event e[8];

    /* The 3-byte path is unchanged: no wheel field set, 3 bytes a packet. */
    mouse_decoder_init(&d);
    CHECK(!d.wheel);
    static const uint8_t three[] = { 0x08, 1, 2, 0x08, 3, 4 };
    CHECK_EQ(feed(&d, three, sizeof(three), e, 8), 2);
    CHECK(pkt(&e[0], 1, -2, 0, 0) && pkt(&e[1], 3, -4, 0, 0));

    /* 4-byte packets: nothing before the fourth byte; the wheel's sign
     * (0xFF up -1, 0x01 down +1) and range (-128 .. 127); the middle
     * button's edges; an overflow drops the motion, not the wheel. */
    mouse_decoder_wheel(&d);
    CHECK(d.wheel);
    static const uint8_t four[] = {
        0x08, 5, 3, 0xFF,
        0x08, 0, 0, 0x01,
        0x0C, 0, 0, 0x00,
        0x08, 0, 0, 0x80,
        0x08, 0, 0, 0x7F,
        0x49, 0x7F, 0x10, 0xFE,
    };
    CHECK_EQ(feed(&d, four, 3, e, 8), 0);
    CHECK_EQ(feed(&d, four + 3, sizeof(four) - 3, e, 8), 6);
    CHECK(pkt(&e[0], 5, -3, -1, 0));
    CHECK(pkt(&e[1], 0, 0, 1, 0));
    CHECK(pkt(&e[2], 0, 0, 0, 4) && e[2].pressed == 4);
    CHECK(pkt(&e[3], 0, 0, -128, 0) && e[3].released == 4);
    CHECK(pkt(&e[4], 0, 0, 127, 0));
    CHECK(e[5].overflow && pkt(&e[5], 0, 0, -2, 1) && e[5].pressed == 1);
    CHECK_EQ(d.packets, 8);
    CHECK_EQ(d.bad_bytes, 0);
}

/* Six 4-byte packets whose bytes 1-3 have bit 3 clear (dx 1..6, dy
 * 0x10..0x15 up, the wheel +1 or -16), the left button on odd ones. */
static uint32_t wheel_stream(uint8_t *s)
{
    for (uint32_t k = 0; k < 6; k++) {
        s[4 * k] = (uint8_t)(0x08 | (k & 1));
        s[4 * k + 1] = (uint8_t)(1 + k);
        s[4 * k + 2] = (uint8_t)(0x10 + k);
        s[4 * k + 3] = (k & 1) ? 0xF0 : 0x01;
    }
    return 24;
}

static bool is_packet(const struct mouse_event *e, uint32_t k)
{
    return pkt(e, (int32_t)(1 + k), -(int32_t)(0x10 + k), (k & 1) ? -16 : 1, (uint8_t)(k & 1));
}

static void test_resync_wheel(void)
{
    uint8_t s[24], lost[24];
    struct mouse_decoder d;
    struct mouse_event e[8];
    uint32_t n = wheel_stream(s);

    /* A byte of packet 1 lost, at each offset 0..3: at most one wrong
     * packet (offsets 1-3: the next packet's first byte completes it),
     * then the bytes without bit 3 are dropped (3 each time) and every
     * packet from packet 3 on is exact; packet 0 is exact before it. */
    for (uint32_t off = 0; off < 4; off++) {
        uint32_t m = 0;
        for (uint32_t i = 0; i < n; i++)
            if (i != 4 + off)
                lost[m++] = s[i];
        mouse_decoder_init(&d);
        mouse_decoder_wheel(&d);
        int got = feed(&d, lost, m, e, 8);
        CHECK_EQ(got, 5);
        CHECK_EQ(d.bad_bytes, 3);
        CHECK(is_packet(&e[0], 0));
        CHECK(off == 0 ? is_packet(&e[1], 2) : !is_packet(&e[1], 1) && !is_packet(&e[1], 2));
        CHECK(is_packet(&e[2], 3) && is_packet(&e[3], 4) && is_packet(&e[4], 5));
    }

    /* A stream that starts at offset 1, 2 or 3 of packet 0 (its first
     * bytes lost): the rest of packet 0 dropped, then every packet exact. */
    for (uint32_t start = 1; start < 4; start++) {
        mouse_decoder_init(&d);
        mouse_decoder_wheel(&d);
        CHECK_EQ(feed(&d, s + start, n - start, e, 8), 5);
        CHECK_EQ(d.bad_bytes, 4 - start);
        for (uint32_t k = 1; k < 6; k++)
            CHECK(is_packet(&e[k - 1], k));
    }

    /* A wheel byte with bit 3 set (wheel -1, 0xFF) can be taken for a
     * first byte: after a lost first byte the decoder stays out of step
     * while every wheel byte is 0xFF, and is back in step at the first
     * packet whose wheel byte has bit 3 clear (here 0: no wheel motion). */
    static const uint8_t ff[] = {
        0x08, 1, 2, 0xFF,  /* lost: */ 0x08, 1, 2, 0xFF,  0x08, 1, 2, 0xFF,
        0x08, 1, 2, 0x00,  0x08, 3, 4, 0x00,  0x08, 5, 6, 0x00,
    };
    uint32_t m = 0;
    for (uint32_t i = 0; i < sizeof(ff); i++)
        if (i != 4)
            lost[m++] = ff[i];
    mouse_decoder_init(&d);
    mouse_decoder_wheel(&d);
    int got = feed(&d, lost, m, e, 8);
    CHECK_EQ(got, 5);
    CHECK(pkt(&e[0], 1, -2, -1, 0));
    CHECK(pkt(&e[3], 3, -4, 0, 0) && pkt(&e[4], 5, -6, 0, 0));
    CHECK_EQ(d.bad_bytes, 3);
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
    RUN(test_wheel_probe);
    RUN(test_decode_wheel);
    RUN(test_resync_wheel);
    return unit_report("mouse");
}
