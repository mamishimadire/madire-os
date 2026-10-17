/* The PS/2 "set lights" exchange (see ps2leds.h). */
#include "drivers/keyboard/ps2leds.h"

uint8_t leds_begin(struct leds_exchange *x, uint8_t state)
{
    x->state = (uint8_t)(state & LEDS_STATE_MASK);
    x->byte = LEDS_CMD;
    x->resends = 0;
    x->outcome = LEDS_SEND;
    x->failed_byte = 0;
    x->reply = 0;
    x->timed_out = false;
    return LEDS_CMD;
}

static enum leds_step fail(struct leds_exchange *x, int reply)
{
    x->failed_byte = x->byte;
    x->timed_out = reply == LEDS_TIMEOUT;
    x->reply = x->timed_out ? 0 : (uint8_t)reply;
    x->outcome = LEDS_FAILED;
    return LEDS_FAILED;
}

enum leds_step leds_reply(struct leds_exchange *x, int reply, uint8_t *send)
{
    if (x->outcome != LEDS_SEND)
        return (enum leds_step)x->outcome;
    if (reply == (int)LEDS_ACK) {
        if (x->byte != LEDS_CMD) {
            x->outcome = LEDS_DONE;
            return LEDS_DONE;
        }
        x->byte = x->state;
        x->resends = 0;
        *send = x->byte;
        return LEDS_SEND;
    }
    if (reply == (int)LEDS_RESEND && x->resends < LEDS_MAX_RESENDS) {
        x->resends++;
        *send = x->byte;
        return LEDS_SEND;
    }
    return fail(x, reply);
}
