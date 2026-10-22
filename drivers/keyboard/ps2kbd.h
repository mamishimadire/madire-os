#ifndef MADIRE_DRIVERS_KEYBOARD_PS2KBD_H
#define MADIRE_DRIVERS_KEYBOARD_PS2KBD_H

/*
 * PS/2 keyboard on the i8042 controller, port 1, IRQ1.
 *
 * "Light touch" set-up: no controller self-test and no keyboard reset
 * (either can disturb the BIOS configuration). Every wait is bounded, so
 * a missing controller or keyboard gives an error status, never a hang.
 */

#include <stdbool.h>
#include <stdint.h>
#include "drivers/keyboard/scancode.h"

enum kbd_status {
    KBD_OK,
    KBD_NO_CONTROLLER,  /* status port reads 0xFF: no i8042 */
    KBD_TIMEOUT,        /* controller did not accept or answer a command */
    KBD_CONFIG,         /* configuration byte did not read back as written */
    KBD_NO_KEYBOARD,    /* no echo reply from a keyboard */
    KBD_IRQ,            /* IRQ1 could not be registered or unmasked */
};

/* Probes and configures the controller (IRQ1 on, translation to set 1 on,
 * port 2 IRQ off), checks a keyboard answers ECHO, registers the IRQ1
 * handler, clears pending bytes, then unmasks IRQ1 (in that order). */
enum kbd_status keyboard_init(void);

struct kbd_selftest {
    bool decoder_ok;        /* known byte sequences decode correctly */
    bool queue_ok;          /* push/pop order and overflow on a private queue */
    bool irq_delivered;     /* ECHO reply arrived through IRQ1 */
};

/* Runs after keyboard_init() with interrupts enabled. Waits are bounded by
 * the timer when it is verified, otherwise by a spin limit. */
bool keyboard_selftest(struct kbd_selftest *out);

/*
 * The keyboard lights (1.1). keyboard_init() sets them once, polled, after
 * the ECHO check and before IRQ1 is unmasked; a failure never changes its
 * status (the keyboard works without lights).
 */
struct kbd_lights {
    uint8_t state;          /* bit 0 Scroll Lock, bit 1 Num Lock, bit 2 Caps Lock */
    bool ok;                /* acknowledged */
    uint8_t failed_byte;    /* if not ok: 0xED or the state byte */
    uint8_t reply;          /* if not ok and not timed_out: the keyboard's reply */
    bool timed_out;         /* if not ok: no reply in time */
};

/* The boot result; NULL if keyboard_init() did not reach the exchange. */
const struct kbd_lights *keyboard_lights(void);

enum kbd_sync {
    KBD_SYNC_NONE,          /* nothing sent: unchanged, lights off, or no timer */
    KBD_SYNC_DONE,          /* the new state was acknowledged (*out filled) */
    KBD_SYNC_FAILED,        /* not acknowledged (*out filled); lights off until restart */
};

/* Normal context only, never from an interrupt handler or with preemption
 * off. If the decoder's lock state differs from the lights last set, sets
 * them: each reply comes through the IRQ1 handler and is waited for with
 * cli / check / sti; hlt, at most 3 timer ticks per reply. Only when the
 * timer is verified. After one failure the lights stay off until restart. */
enum kbd_sync keyboard_lights_sync(struct kbd_lights *out);

/* Normal-context consumer: next key event, false if none. */
bool keyboard_get_event(struct key_event *out);

uint32_t keyboard_queued(void);         /* events waiting in the queue now */
uint32_t keyboard_dropped(void);        /* events lost to a full queue */
uint32_t keyboard_bytes(void);          /* bytes read in the IRQ handler */

const char *kbd_status_str(enum kbd_status status);

#endif
