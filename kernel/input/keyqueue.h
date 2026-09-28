#ifndef MADIRE_INPUT_KEYQUEUE_H
#define MADIRE_INPUT_KEYQUEUE_H

/*
 * Fixed-size queue of key events: one producer (the IRQ1 handler), one
 * consumer (normal kernel code). Pure module, unit-tested on the host
 * (tests/unit/test_keyqueue.c).
 *
 * When full, the NEWEST event is dropped and counted, so the events
 * already queued keep their order. The consumer must pop with interrupts
 * disabled (keyboard_get_event() does this); the producer runs in IRQ
 * context, where interrupts are already disabled.
 */

#include <stdbool.h>
#include <stdint.h>
#include "drivers/keyboard/scancode.h"

#define KEYQUEUE_SIZE 64u           /* power of two */

struct keyqueue {
    struct key_event items[KEYQUEUE_SIZE];
    volatile uint32_t head;         /* total pushed (free-running) */
    volatile uint32_t tail;         /* total popped (free-running) */
    volatile uint32_t dropped;      /* events lost because the queue was full */
};

void keyqueue_init(struct keyqueue *q);
bool keyqueue_push(struct keyqueue *q, const struct key_event *e);   /* false = dropped */
bool keyqueue_pop(struct keyqueue *q, struct key_event *out);        /* false = empty */
uint32_t keyqueue_count(const struct keyqueue *q);
uint32_t keyqueue_dropped(const struct keyqueue *q);

#endif
