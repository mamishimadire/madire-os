#ifndef MADIRE_DRIVERS_MOUSE_MOUSE_CORE_H
#define MADIRE_DRIVERS_MOUSE_MOUSE_CORE_H

/*
 * The PS/2 mouse, the pure part (1.2 stage 7; host unit tests in
 * tests/unit/test_mouse.c). The kernel side is ps2mouse.c.
 *
 *   - the start sequence on port 2 (aux), polled through the i8042
 *     module's bounded waits (drivers/ps2/i8042.h), and its undo;
 *   - what the IRQ12 handler does with the status it read;
 *   - the bounded byte queue from IRQ12 to thread 0;
 *   - the packet decoder (3-byte packets of the standard PS/2 mouse;
 *     1.5 stage 7: 4-byte packets of the IntelliMouse wheel mode).
 *
 * A packet: byte 0 holds the buttons (bit 0 left, 1 right, 2 middle), a
 * bit that is always 1 (bit 3), the sign of X and of Y (bits 4, 5) and
 * their overflow (bits 6, 7); bytes 1 and 2 the low 8 bits of X and Y, so
 * each motion is a 9-bit two's complement value, -256 to 255. Y counts
 * up; the decoder gives screen motion (Y counting down). In wheel mode
 * (device ID 3) byte 3 is the wheel's motion, a signed 8-bit value:
 * negative away from the user ("up"), positive towards the user.
 */

#include <stdbool.h>
#include <stdint.h>
#include "drivers/ps2/i8042.h"

/* Controller commands (port 0x64) and configuration bits. */
#define MOUSE_CTL_READ_CONFIG     0x20
#define MOUSE_CTL_WRITE_CONFIG    0x60
#define MOUSE_CTL_DISABLE_AUX     0xA7
#define MOUSE_CTL_ENABLE_AUX      0xA8
#define MOUSE_CTL_TEST_AUX        0xA9    /* reply 0x00: the port's lines are good */
#define MOUSE_CTL_DISABLE_PORT1   0xAD
#define MOUSE_CTL_ENABLE_PORT1    0xAE
#define MOUSE_CTL_WRITE_AUX       0xD4    /* the next data byte goes to port 2 */

#define MOUSE_CFG_PORT2_IRQ       0x02
#define MOUSE_CFG_PORT1_CLOCK_OFF 0x10
#define MOUSE_CFG_PORT2_CLOCK_OFF 0x20

/* Mouse commands (through MOUSE_CTL_WRITE_AUX) and its acknowledgement. */
#define MOUSE_DEV_GET_ID            0xF2  /* 1.5 stage 7: ACK, then the device ID */
#define MOUSE_DEV_SET_RATE          0xF3  /* 1.5 stage 7: ACK; the rate byte, ACK */
#define MOUSE_DEV_ENABLE_REPORTING  0xF4
#define MOUSE_DEV_DISABLE_REPORTING 0xF5  /* 1.5 stage 7 */
#define MOUSE_DEV_SET_DEFAULTS      0xF6
#define MOUSE_DEV_ACK               0xFA
#define MOUSE_DEV_ERROR             0xFC  /* 1.5 stage 7: a command refused */
#define MOUSE_DEV_RESEND            0xFE  /* 1.5 stage 7: a command not understood */

/* 1.5 stage 7 (C8): device ID 3 after the rates 200, 100, 80: the
 * IntelliMouse wheel mode; 100 samples a second is the defaults' rate. */
#define MOUSE_ID_WHEEL     3u
#define MOUSE_RATE_DEFAULT 100u

/* Packet byte 0. */
#define MOUSE_B0_LEFT     0x01
#define MOUSE_B0_RIGHT    0x02
#define MOUSE_B0_MIDDLE   0x04
#define MOUSE_B0_ALWAYS   0x08
#define MOUSE_B0_X_SIGN   0x10
#define MOUSE_B0_Y_SIGN   0x20
#define MOUSE_B0_X_OVER   0x40
#define MOUSE_B0_Y_OVER   0x80
#define MOUSE_BUTTONS     0x07

#define MOUSE_FLUSH_LIMIT 64u   /* bytes read and dropped when port 1 is turned off */
#define MOUSE_REPLY_LIMIT 16u   /* bytes read for one aux reply */

/* ---- The start sequence ---- */

enum mouse_start_step {
    MOUSE_START_OK,
    MOUSE_START_CONFIG,         /* the configuration byte could not be read */
    MOUSE_START_NO_PORT2,       /* port 2's clock stayed off after enabling it: no second port */
    MOUSE_START_TEST,           /* the interface test: no reply, or not 0x00 */
    MOUSE_START_DEFAULTS,       /* set defaults: no reply, or not acknowledged */
    MOUSE_START_REPORTING,      /* enable reporting: no reply, or not acknowledged */
    MOUSE_START_IRQ_CONFIG,     /* the configuration with IRQ12 on did not stick */
    MOUSE_START_PORT1,          /* port 1 could not be turned back on */
};

struct mouse_start {
    enum mouse_start_step step;
    bool timed_out;             /* the failed step got no reply (else: reply) */
    bool injected;              /* the failure was test=mousefail's */
    bool have_before;           /* config_before was read */
    uint8_t reply;              /* the failed step's reply */
    uint8_t config_before;      /* as the boot loader and the keyboard driver left it */
    uint8_t config_enabled;     /* after enabling port 2 (port 1 turned off) */
    uint8_t config_after;       /* what mouse_enable_irq wrote: IRQ12 on, port 2 clock on */
    uint8_t config_read;        /* what it read back (port 1 still off) */
    uint32_t dropped;           /* bytes waiting when port 1 was turned off, dropped */
    uint32_t skipped;           /* port 1 bytes read while waiting for an aux reply, dropped */
};

/* Port 1 off (its bytes wait in the keyboard), what was waiting dropped,
 * the configuration read, port 2 enabled and checked, its interface
 * tested, the mouse's defaults set and its reporting turned on: each reply
 * through a bounded wait. The caller has masked IRQ1 (the bytes are read
 * here) and IRQ12. Leaves port 1 off and IRQ12 off in the configuration:
 * next mouse_enable_irq, or mouse_undo. False with r->step on a failure. */
bool mouse_start(const struct i8042_bus *bus, struct mouse_start *r);

/*
 * 1.5 stage 7 (C8): the IntelliMouse wheel, right after mouse_start (IRQ1
 * and IRQ12 still masked, port 1 off): reporting off (0xF5; motion bytes
 * already on their way before its ACK are dropped and counted), the rates
 * 200, 100 and 80 (each 0xF3 and its rate byte acknowledged), Get Device
 * ID (ACK, then the ID), the rate back to 100 (as the defaults left it),
 * reporting on again (0xF4). ID 3: 4-byte
 * packets from now on (mouse_decoder_wheel); any other ID, or no ID: the
 * 3-byte path exactly as before. A step that gets no reply or a wrong one
 * ends the knock there (the rate is still set back if the knock began,
 * and reporting is always turned on again). False only if reporting could
 * not be turned on again: then r->step is MOUSE_START_REPORTING (the
 * mouse gives up as for enable reporting in mouse_start).
 */
struct mouse_wheel {
    bool wheel;                 /* ID 3: 4-byte packets */
    bool have_id;               /* the ID was read */
    uint8_t id;
    bool failed;                /* a step before the ID failed: failed_byte, timed_out, reply */
    bool rate_failed;           /* the rate could not be set back to 100 */
    uint8_t failed_byte;        /* the command or rate byte that failed */
    bool timed_out;             /* it got no reply (else: reply) */
    uint8_t reply;
    uint32_t stray;             /* motion bytes dropped before 0xF5's ACK */
};

#define MOUSE_STRAY_LIMIT 16u   /* aux bytes read while waiting for 0xF5's ACK */

bool mouse_wheel_start(const struct i8042_bus *bus, struct mouse_start *r, struct mouse_wheel *w);

/* After mouse_start: writes the configuration with IRQ12 on and port 2's
 * clock on (port 1 still off), reads it back; false if it did not stick. */
bool mouse_enable_irq(const struct i8042_bus *bus, struct mouse_start *r);

/* Port 1 back on (the keyboard's waiting bytes come in again). */
bool mouse_port1_on(const struct i8042_bus *bus);

/* After mouse_enable_irq: port 1 back on, the start's last step; false
 * with r->step on a failure. */
bool mouse_finish(const struct i8042_bus *bus, struct mouse_start *r);

/* After a failure: the mouse's port disabled, the configuration it had
 * before mouse_start written back, port 1 on. */
void mouse_undo(const struct i8042_bus *bus, const struct mouse_start *r);

/* The configuration byte (for the line after an undo); IRQ1 must be
 * masked, or it would take the reply. */
bool mouse_read_config(const struct i8042_bus *bus, uint8_t *out);

/*
 * test=mousefail=N (a test option): start-up step N fails as if no reply
 * came, without its command being sent, so the give-up path runs. The
 * steps, in order (ps2mouse.h: the ownership flag is set between 5 and 6):
 *   1 reading the configuration, 2 enabling port 2, 3 the interface test,
 *   4 set defaults, 5 enable reporting, 6 the configuration with IRQ12 on,
 *   7 port 1 back on.
 * 0: no failure.
 */
#define MOUSE_FAIL_STEPS 7u
void mouse_fail_at(uint32_t step);
uint32_t mouse_fail_step(void);

/* ---- IRQ12 ---- */

enum mouse_irq_action {
    MOUSE_IRQ_NONE,             /* nothing waiting (an edge latched while masked) */
    MOUSE_IRQ_AUX,              /* a port 2 byte: read it */
    MOUSE_IRQ_KEYBOARD,         /* a port 1 byte: leave it for IRQ1 */
};

enum mouse_irq_action mouse_irq_action(uint8_t status);

/* ---- The byte queue: IRQ12 pushes, thread 0 pops (with interrupts
 * off around each pop). Full: the byte is dropped and counted. ---- */

#define MOUSE_QUEUE_SIZE 256u

struct mouse_queue {
    uint8_t bytes[MOUSE_QUEUE_SIZE];
    uint32_t head;              /* the oldest byte */
    uint32_t count;
    uint32_t dropped;
};

void mouse_queue_init(struct mouse_queue *q);
bool mouse_queue_push(struct mouse_queue *q, uint8_t byte);
bool mouse_queue_pop(struct mouse_queue *q, uint8_t *out);

/* ---- The decoder ---- */

struct mouse_event {
    int32_t dx;                 /* screen motion: right positive */
    int32_t dy;                 /* screen motion: down positive */
    int32_t wheel;              /* 1.5 stage 7: notches, negative up (away from the user); 0 on 3-byte packets */
    uint8_t buttons;            /* held now (MOUSE_B0_LEFT | ...) */
    uint8_t pressed;            /* pressed since the last packet */
    uint8_t released;           /* released since the last packet */
    bool overflow;              /* an overflow bit was set: the motion is dropped (0, 0); the wheel is kept */
};

struct mouse_decoder {
    uint8_t bytes[4];
    uint32_t have;              /* bytes of the current packet so far */
    bool wheel;                 /* 1.5 stage 7: 4-byte packets (else 3) */
    uint8_t buttons;            /* held after the last packet */
    uint32_t packets;
    uint32_t bad_bytes;         /* bytes dropped while looking for a packet's first byte */
    uint32_t overflows;
};

/* 3-byte packets until mouse_decoder_wheel. */
void mouse_decoder_init(struct mouse_decoder *d);

/* 1.5 stage 7: 4-byte packets from now on (a packet begun is dropped). */
void mouse_decoder_wheel(struct mouse_decoder *d);

/* Takes one byte; true when it ends a packet (then *out is filled). A
 * first byte without bit 3 is dropped and counted, and the next byte is
 * taken as a first byte again (resynchronisation; the same rule for
 * 3-byte and 4-byte packets). */
bool mouse_decode(struct mouse_decoder *d, uint8_t byte, struct mouse_event *out);

#endif
