#ifndef MADIRE_DRIVERS_PS2_I8042_H
#define MADIRE_DRIVERS_PS2_I8042_H

/*
 * Polled access to the i8042 PS/2 controller (1.2), shared by the PS/2
 * drivers. Pure module: the ports and the clock come in through a bus, so
 * it is unit-tested on the host (tests/unit/test_i8042.c); the keyboard
 * driver passes the real ports and the timer.
 *
 * Every wait is bounded twice:
 *   by time, when the bus's clock can be used at the wait's start (the
 *     timer verified and interrupts on): it ends I8042_WAIT_TICKS ticks
 *     later, after between 90 and 100 ms at 100 Hz;
 *   by I8042_SPIN_LIMIT status reads, always: the only bound without a
 *     clock (the timer failed, or interrupts are off), a backstop with one
 *     (a timer that stops during the wait cannot make it endless).
 * The status is read before either bound is checked, so a byte that is
 * ready when the wait ends is still taken.
 */

#include <stdbool.h>
#include <stdint.h>

#define I8042_DATA               0x60
#define I8042_STATUS             0x64    /* read */
#define I8042_COMMAND            0x64    /* write */

#define I8042_STATUS_OUTPUT_FULL 0x01    /* a byte is waiting in port 0x60 */
#define I8042_STATUS_INPUT_FULL  0x02    /* the controller has not taken our last byte */
#define I8042_STATUS_AUX_DATA    0x20    /* the waiting byte is from port 2 (mouse) */

#define I8042_WAIT_TICKS         10u        /* per wait with the clock: 90-100 ms at 100 Hz */
#define I8042_SPIN_LIMIT         1000000u   /* status reads per wait */
#define I8042_FLUSH_LIMIT        64u        /* bytes discarded by i8042_flush */

struct i8042_bus {
    uint8_t (*read_status)(void);           /* port 0x64 */
    uint8_t (*read_data)(void);             /* port 0x60 */
    void (*write_command)(uint8_t cmd);     /* port 0x64 */
    void (*write_data)(uint8_t byte);       /* port 0x60 */
    bool (*ticks)(uint64_t *now);           /* false: no usable clock now */
};

/* One bounded wait. It may span several bytes: a reply read after bytes
 * that are not replies shares the bound begun before the first. */
struct i8042_wait {
    bool timed;             /* the clock was usable when the wait began */
    uint64_t deadline;      /* if timed: the tick count that ends it */
    uint32_t spins;         /* status reads so far */
};

void i8042_wait_begin(const struct i8042_bus *bus, struct i8042_wait *w);

/* Until the controller can take a byte (input buffer empty), or a bound. */
bool i8042_wait_writable(const struct i8042_bus *bus, struct i8042_wait *w);

/* Until a byte is waiting (output buffer full), or a bound. */
bool i8042_wait_readable(const struct i8042_bus *bus, struct i8042_wait *w);

/* Each begins its own wait. A byte is written, or read, only after its
 * wait succeeded; on false nothing was written or read. */
bool i8042_send_command(const struct i8042_bus *bus, uint8_t cmd);
bool i8042_send_data(const struct i8042_bus *bus, uint8_t byte);
bool i8042_read_data(const struct i8042_bus *bus, uint8_t *out);

/* Reads the next byte within the wait <w> (begun by the caller). */
bool i8042_read_data_within(const struct i8042_bus *bus, struct i8042_wait *w, uint8_t *out);

/* Discards waiting bytes, at most I8042_FLUSH_LIMIT, without waiting. */
void i8042_flush(const struct i8042_bus *bus);

/*
 * Port 2 (aux) ownership. One output buffer serves both ports, so an
 * unread port 2 byte with no IRQ12 handler would block the keyboard. Until
 * a mouse driver owns port 2 (only the PS/2 mouse driver sets this, once
 * its IRQ12 handler is registered), IRQ1 reads and discards a port 2 byte,
 * as before 1.2; once it is owned, IRQ1 leaves the byte unread for IRQ12.
 * Not owned at start.
 */
void i8042_set_aux_owned(bool owned);
bool i8042_aux_owned(void);

enum i8042_irq1_action {
    I8042_IRQ1_NONE,        /* nothing waiting (an edge latched while masked) */
    I8042_IRQ1_KEYBOARD,    /* a port 1 byte: read it */
    I8042_IRQ1_DISCARD,     /* a port 2 byte, port 2 not owned: read it, not ours */
    I8042_IRQ1_LEAVE,       /* a port 2 byte, port 2 owned: leave it for IRQ12 */
};

/* What the IRQ1 handler does with the status it read from port 0x64. */
enum i8042_irq1_action i8042_irq1_action(uint8_t status);

#endif
