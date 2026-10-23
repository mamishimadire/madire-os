/* Polled i8042 controller access (see i8042.h). */
#include "drivers/ps2/i8042.h"

static volatile bool aux_owned;     /* read in IRQ1 context */

void i8042_wait_begin(const struct i8042_bus *bus, struct i8042_wait *w)
{
    uint64_t now;
    w->timed = bus->ticks(&now);
    w->deadline = w->timed ? now + I8042_WAIT_TICKS : 0;
    w->spins = 0;
}

/* The status first, then the bounds. A clock that stops being usable
 * during a timed wait leaves only the spin bound. */
static bool wait_status(const struct i8042_bus *bus, struct i8042_wait *w,
                        uint8_t mask, uint8_t want)
{
    for (;;) {
        if ((bus->read_status() & mask) == want)
            return true;
        if (++w->spins >= I8042_SPIN_LIMIT)
            return false;
        uint64_t now;
        if (w->timed && bus->ticks(&now) && now >= w->deadline)
            return false;
    }
}

bool i8042_wait_writable(const struct i8042_bus *bus, struct i8042_wait *w)
{
    return wait_status(bus, w, I8042_STATUS_INPUT_FULL, 0);
}

bool i8042_wait_readable(const struct i8042_bus *bus, struct i8042_wait *w)
{
    return wait_status(bus, w, I8042_STATUS_OUTPUT_FULL, I8042_STATUS_OUTPUT_FULL);
}

bool i8042_send_command(const struct i8042_bus *bus, uint8_t cmd)
{
    struct i8042_wait w;
    i8042_wait_begin(bus, &w);
    if (!i8042_wait_writable(bus, &w))
        return false;
    bus->write_command(cmd);
    return true;
}

bool i8042_send_data(const struct i8042_bus *bus, uint8_t byte)
{
    struct i8042_wait w;
    i8042_wait_begin(bus, &w);
    if (!i8042_wait_writable(bus, &w))
        return false;
    bus->write_data(byte);
    return true;
}

bool i8042_read_data_within(const struct i8042_bus *bus, struct i8042_wait *w, uint8_t *out)
{
    if (!i8042_wait_readable(bus, w))
        return false;
    *out = bus->read_data();
    return true;
}

bool i8042_read_data(const struct i8042_bus *bus, uint8_t *out)
{
    struct i8042_wait w;
    i8042_wait_begin(bus, &w);
    return i8042_read_data_within(bus, &w, out);
}

void i8042_flush(const struct i8042_bus *bus)
{
    for (uint32_t i = 0; i < I8042_FLUSH_LIMIT && (bus->read_status() & I8042_STATUS_OUTPUT_FULL); i++)
        (void)bus->read_data();
}

void i8042_set_aux_owned(bool owned)
{
    aux_owned = owned;
}

bool i8042_aux_owned(void)
{
    return aux_owned;
}

enum i8042_irq1_action i8042_irq1_action(uint8_t status)
{
    if (!(status & I8042_STATUS_OUTPUT_FULL))
        return I8042_IRQ1_NONE;
    if (!(status & I8042_STATUS_AUX_DATA))
        return I8042_IRQ1_KEYBOARD;
    return aux_owned ? I8042_IRQ1_LEAVE : I8042_IRQ1_DISCARD;
}
