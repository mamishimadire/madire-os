#ifndef MADIRE_DRIVERS_KEYBOARD_PS2LEDS_H
#define MADIRE_DRIVERS_KEYBOARD_PS2LEDS_H

/*
 * The PS/2 "set lights" exchange (1.1): the command 0xED, then the state
 * byte (bit 0 Scroll Lock, bit 1 Num Lock, bit 2 Caps Lock), each answered
 * by the keyboard with ACK 0xFA. Pure module: no hardware access, no waits
 * and no printing; the driver sends the bytes and passes the replies in, so
 * it is unit-tested on the host (tests/unit/test_ps2leds.c).
 *
 *   0xFA          the byte was taken: 0xED -> the state byte -> done
 *   0xFE          resend the same byte, at most LEDS_MAX_RESENDS times per byte
 *   LEDS_TIMEOUT  no reply in time: failed
 *   anything else failed (the byte and the reply are kept for the log)
 */

#include <stdbool.h>
#include <stdint.h>

#define LEDS_CMD          0xEDu
#define LEDS_ACK          0xFAu
#define LEDS_RESEND       0xFEu
#define LEDS_STATE_MASK   0x07u
#define LEDS_MAX_RESENDS  3u
#define LEDS_TIMEOUT      0x100     /* leds_reply(): no reply arrived */

enum leds_step {
    LEDS_SEND,      /* send *send next, then pass its reply in */
    LEDS_DONE,      /* both bytes acknowledged */
    LEDS_FAILED,    /* see failed_byte, reply, timed_out */
};

struct leds_exchange {
    uint8_t state;          /* the light byte, masked to bits 0-2 */
    uint8_t byte;           /* the byte sent last (0xED or the state) */
    uint8_t resends;        /* 0xFE replies for that byte so far */
    uint8_t outcome;        /* LEDS_SEND while running, then the final step */
    uint8_t failed_byte;    /* on LEDS_FAILED: the byte that was not taken */
    uint8_t reply;          /* on LEDS_FAILED: its reply (unless timed_out) */
    bool timed_out;
};

/* Starts an exchange for <state> (bits above 2 are dropped); returns the
 * first byte to send, 0xED. */
uint8_t leds_begin(struct leds_exchange *x, uint8_t state);

/* Passes the reply to the byte sent last: 0x00-0xFF, or LEDS_TIMEOUT. On
 * LEDS_SEND, *send is the next byte to send. A finished exchange keeps its
 * final step: a later reply returns it again and changes nothing. */
enum leds_step leds_reply(struct leds_exchange *x, int reply, uint8_t *send);

#endif
