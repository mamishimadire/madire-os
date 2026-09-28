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

/* Normal-context consumer: next key event, false if none. */
bool keyboard_get_event(struct key_event *out);

uint32_t keyboard_queued(void);         /* events waiting in the queue now */
uint32_t keyboard_dropped(void);        /* events lost to a full queue */
uint32_t keyboard_bytes(void);          /* bytes read in the IRQ handler */

const char *kbd_status_str(enum kbd_status status);

#endif
