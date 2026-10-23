/* The PS/2 mouse, the pure part (see mouse_core.h). */
#include "drivers/mouse/mouse_core.h"

/* ---- The start sequence ---- */

static uint32_t fail_step;              /* test=mousefail=N */

void mouse_fail_at(uint32_t step)
{
    fail_step = step;
}

uint32_t mouse_fail_step(void)
{
    return fail_step;
}

static bool read_config(const struct i8042_bus *bus, uint8_t *out)
{
    return i8042_send_command(bus, MOUSE_CTL_READ_CONFIG) && i8042_read_data(bus, out);
}

static bool write_config(const struct i8042_bus *bus, uint8_t cfg)
{
    return i8042_send_command(bus, MOUSE_CTL_WRITE_CONFIG) && i8042_send_data(bus, cfg);
}

/* A mouse command and its reply: the next port 2 byte, within one wait.
 * Port 1 bytes on the way (port 1 is off, so only one already in the
 * controller) are dropped and counted. */
static bool aux_command(const struct i8042_bus *bus, struct mouse_start *r, uint8_t cmd, uint8_t *reply)
{
    if (!i8042_send_command(bus, MOUSE_CTL_WRITE_AUX) || !i8042_send_data(bus, cmd))
        return false;
    struct i8042_wait w;
    i8042_wait_begin(bus, &w);
    for (uint32_t i = 0; i < MOUSE_REPLY_LIMIT; i++) {
        if (!i8042_wait_readable(bus, &w))
            return false;
        bool aux = bus->read_status() & I8042_STATUS_AUX_DATA;
        uint8_t byte = bus->read_data();
        if (aux) {
            *reply = byte;
            return true;
        }
        r->skipped++;
    }
    return false;
}

static bool fail(struct mouse_start *r, enum mouse_start_step step, bool timed_out, uint8_t reply)
{
    r->step = step;
    r->timed_out = timed_out;
    r->reply = reply;
    return false;
}

/* test=mousefail: step `n` fails here as `as` would without a reply. */
static bool inject(struct mouse_start *r, uint32_t n, enum mouse_start_step as)
{
    if (fail_step != n)
        return false;
    fail(r, as, true, 0);
    r->injected = true;
    return true;
}

static bool acknowledged(const struct i8042_bus *bus, struct mouse_start *r, uint8_t cmd,
                         enum mouse_start_step step)
{
    uint8_t reply = 0;
    if (!aux_command(bus, r, cmd, &reply))
        return fail(r, step, true, 0);
    if (reply != MOUSE_DEV_ACK)
        return fail(r, step, false, reply);
    return true;
}

bool mouse_start(const struct i8042_bus *bus, struct mouse_start *r)
{
    *r = (struct mouse_start){ 0 };
    if (!i8042_send_command(bus, MOUSE_CTL_DISABLE_PORT1))
        return fail(r, MOUSE_START_CONFIG, true, 0);
    for (uint32_t i = 0; i < MOUSE_FLUSH_LIMIT && (bus->read_status() & I8042_STATUS_OUTPUT_FULL); i++) {
        (void)bus->read_data();
        r->dropped++;
    }

    /* Read with port 1 off, so no key byte can be taken for the reply:
     * its clock bit is the one just set; the keyboard driver left it on. */
    uint8_t cfg;
    if (inject(r, 1, MOUSE_START_CONFIG))
        return false;
    if (!read_config(bus, &cfg))
        return fail(r, MOUSE_START_CONFIG, true, 0);
    r->config_before = (uint8_t)(cfg & ~MOUSE_CFG_PORT1_CLOCK_OFF);
    r->have_before = true;

    /* Enabling port 2 turns its clock on; if the bit stays set, the
     * controller has no second port. Either starting state (the clock on
     * or off) ends here with it on. */
    if (inject(r, 2, MOUSE_START_CONFIG))
        return false;
    if (!i8042_send_command(bus, MOUSE_CTL_ENABLE_AUX) || !read_config(bus, &cfg))
        return fail(r, MOUSE_START_CONFIG, true, 0);
    r->config_enabled = cfg;
    if (cfg & MOUSE_CFG_PORT2_CLOCK_OFF)
        return fail(r, MOUSE_START_NO_PORT2, false, cfg);

    uint8_t reply = 0;
    if (inject(r, 3, MOUSE_START_TEST))
        return false;
    if (!i8042_send_command(bus, MOUSE_CTL_TEST_AUX) || !i8042_read_data(bus, &reply))
        return fail(r, MOUSE_START_TEST, true, 0);
    if (reply != 0x00)
        return fail(r, MOUSE_START_TEST, false, reply);

    return !inject(r, 4, MOUSE_START_DEFAULTS) &&
           acknowledged(bus, r, MOUSE_DEV_SET_DEFAULTS, MOUSE_START_DEFAULTS) &&
           !inject(r, 5, MOUSE_START_REPORTING) &&
           acknowledged(bus, r, MOUSE_DEV_ENABLE_REPORTING, MOUSE_START_REPORTING);
}

bool mouse_enable_irq(const struct i8042_bus *bus, struct mouse_start *r)
{
    r->config_after = (uint8_t)((r->config_before | MOUSE_CFG_PORT2_IRQ) & ~MOUSE_CFG_PORT2_CLOCK_OFF);
    uint8_t want = r->config_after | MOUSE_CFG_PORT1_CLOCK_OFF;
    uint8_t got = 0;
    if (inject(r, 6, MOUSE_START_IRQ_CONFIG))
        return false;
    if (!write_config(bus, want) || !read_config(bus, &got))
        return fail(r, MOUSE_START_IRQ_CONFIG, true, 0);
    r->config_read = got;
    if (got != want)
        return fail(r, MOUSE_START_IRQ_CONFIG, false, got);
    return true;
}

bool mouse_port1_on(const struct i8042_bus *bus)
{
    return i8042_send_command(bus, MOUSE_CTL_ENABLE_PORT1);
}

bool mouse_finish(const struct i8042_bus *bus, struct mouse_start *r)
{
    if (inject(r, 7, MOUSE_START_PORT1))
        return false;
    if (!mouse_port1_on(bus))
        return fail(r, MOUSE_START_PORT1, true, 0);
    return true;
}

bool mouse_read_config(const struct i8042_bus *bus, uint8_t *out)
{
    return read_config(bus, out);
}

void mouse_undo(const struct i8042_bus *bus, const struct mouse_start *r)
{
    (void)i8042_send_command(bus, MOUSE_CTL_DISABLE_AUX);
    if (r->have_before)
        (void)write_config(bus, (uint8_t)(r->config_before | MOUSE_CFG_PORT1_CLOCK_OFF));
    (void)mouse_port1_on(bus);
}

/* ---- IRQ12 ---- */

enum mouse_irq_action mouse_irq_action(uint8_t status)
{
    if (!(status & I8042_STATUS_OUTPUT_FULL))
        return MOUSE_IRQ_NONE;
    return (status & I8042_STATUS_AUX_DATA) ? MOUSE_IRQ_AUX : MOUSE_IRQ_KEYBOARD;
}

/* ---- The byte queue ---- */

void mouse_queue_init(struct mouse_queue *q)
{
    q->head = 0;
    q->count = 0;
    q->dropped = 0;
}

bool mouse_queue_push(struct mouse_queue *q, uint8_t byte)
{
    if (q->count == MOUSE_QUEUE_SIZE) {
        q->dropped++;
        return false;
    }
    q->bytes[(q->head + q->count) % MOUSE_QUEUE_SIZE] = byte;
    q->count++;
    return true;
}

bool mouse_queue_pop(struct mouse_queue *q, uint8_t *out)
{
    if (q->count == 0)
        return false;
    *out = q->bytes[q->head];
    q->head = (q->head + 1) % MOUSE_QUEUE_SIZE;
    q->count--;
    return true;
}

/* ---- The decoder ---- */

void mouse_decoder_init(struct mouse_decoder *d)
{
    *d = (struct mouse_decoder){ 0 };
}

bool mouse_decode(struct mouse_decoder *d, uint8_t byte, struct mouse_event *out)
{
    if (d->have == 0 && !(byte & MOUSE_B0_ALWAYS)) {
        d->bad_bytes++;
        return false;
    }
    d->bytes[d->have++] = byte;
    if (d->have < 3)
        return false;
    d->have = 0;

    uint8_t b0 = d->bytes[0];
    int32_t dx = (int32_t)d->bytes[1] - ((b0 & MOUSE_B0_X_SIGN) ? 256 : 0);
    int32_t dy = (int32_t)d->bytes[2] - ((b0 & MOUSE_B0_Y_SIGN) ? 256 : 0);
    out->overflow = (b0 & (MOUSE_B0_X_OVER | MOUSE_B0_Y_OVER)) != 0;
    if (out->overflow) {
        dx = 0;
        dy = 0;
        d->overflows++;
    }
    out->dx = dx;
    out->dy = -dy;                      /* the mouse counts up, the screen down */
    out->buttons = b0 & MOUSE_BUTTONS;
    out->pressed = out->buttons & (uint8_t)~d->buttons;
    out->released = d->buttons & (uint8_t)~out->buttons;
    d->buttons = out->buttons;
    d->packets++;
    return true;
}
