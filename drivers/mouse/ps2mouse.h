#ifndef MADIRE_DRIVERS_MOUSE_PS2MOUSE_H
#define MADIRE_DRIVERS_MOUSE_PS2MOUSE_H

/*
 * PS/2 mouse driver (1.2 stage 7; the pure part is mouse_core.h). Started
 * only on framebuffer boots and by the diagnostic test=mouse (decision 10);
 * every other boot leaves port 2 as before 1.2.
 *
 * The start order (after the keyboard and its "IRQ mask" line):
 *   IRQ1 masked -> mouse_start (polled: the aux port enabled and tested,
 *   the mouse's defaults set, its reporting on) -> the IRQ12 handler
 *   registered -> port 2 owned (i8042_set_aux_owned: IRQ1 leaves port 2
 *   bytes for IRQ12 from now on) -> the configuration with IRQ12 on ->
 *   port 1 on -> IRQ12 and the cascade IRQ2 unmasked (verified) -> IRQ1
 *   unmasked again.
 * Before the ownership flag is set, IRQ1 still reads and drops port 2
 * bytes, so the polled start runs with IRQ1 masked: no reply is lost.
 *
 * IRQ12 context only reads one byte and queues it (bounded, a full queue
 * counted); thread 0 takes the bytes with mouse_next_event().
 */

#include <stdbool.h>
#include <stdint.h>
#include "drivers/mouse/mouse_core.h"

#define IRQ_MOUSE   12
#define IRQ_CASCADE 2

/* Starts the mouse and logs the "Mouse: ..." lines. False (one line
 * logged, port 2 left as it was) if the aux port is absent or fails; the
 * boot goes on without a mouse. A PIC mask that does not change as asked
 * is a kernel bug and panics, as for IRQ0 and IRQ1. */
bool mouse_init(void);

bool mouse_present(void);

/* Thread 0: the next decoded packet from the queued bytes, if a whole
 * one is there. */
bool mouse_next_event(struct mouse_event *out);

struct mouse_counts {
    uint32_t packets, bad_bytes, overflows;     /* the decoder's */
    uint32_t queued, dropped;                   /* bytes into the queue; dropped when full */
    uint32_t irq_bytes, irq_empty, irq_keyboard; /* IRQ12: a byte read, none waiting, a port 1 byte left */
    uint32_t irqs;                              /* irq_count(IRQ12) */
};

void mouse_counts(struct mouse_counts *out);

#endif
