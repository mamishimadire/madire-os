/*
 * Hardware IRQ dispatch (see irq.h).
 *
 * EOI is sent exactly once per real IRQ, after the handler. Spurious IRQ 7/15
 * get no EOI from here (pic_is_spurious() handles the cascade EOI for 15).
 * An IRQ on a line without a handler is counted and acknowledged, never
 * fatal; normal (non-interrupt) code reports it.
 */
#include "kernel/arch/x86/irq.h"
#include "kernel/arch/x86/pic.h"

static irq_handler_t handlers[IRQ_LINES];
static volatile uint32_t counts[IRQ_LINES];
static volatile uint32_t spurious;
static volatile uint32_t unexpected;
static volatile uint8_t last_unexpected;

enum irq_result irq_register(uint8_t irq, irq_handler_t handler)
{
    if (irq >= IRQ_LINES)
        return IRQ_ERR_RANGE;
    if (!handler)
        return IRQ_ERR_NULL;
    if (handlers[irq])
        return IRQ_ERR_BUSY;
    handlers[irq] = handler;
    return IRQ_OK;
}

enum irq_result irq_enable_line(uint8_t irq)
{
    if (irq >= IRQ_LINES)
        return IRQ_ERR_RANGE;
    if (!handlers[irq])
        return IRQ_ERR_NO_HANDLER;
    pic_unmask(irq);
    return (pic_get_mask() & (1u << irq)) ? IRQ_ERR_MASK : IRQ_OK;
}

void irq_dispatch(uint8_t irq)
{
    if (irq >= IRQ_LINES)
        return;
    if (pic_is_spurious(irq)) {
        spurious++;
        return;
    }

    counts[irq]++;
    if (handlers[irq]) {
        handlers[irq]();
    } else {
        unexpected++;
        last_unexpected = irq;
    }
    pic_send_eoi(irq);
}

uint32_t irq_count(uint8_t irq)
{
    return irq < IRQ_LINES ? counts[irq] : 0;
}

uint32_t irq_spurious_count(void)
{
    return spurious;
}

uint32_t irq_unexpected_count(void)
{
    return unexpected;
}

uint8_t irq_last_unexpected(void)
{
    return last_unexpected;
}

const char *irq_result_str(enum irq_result result)
{
    switch (result) {
    case IRQ_OK:             return "ok";
    case IRQ_ERR_RANGE:      return "IRQ number out of range";
    case IRQ_ERR_NULL:       return "no handler given";
    case IRQ_ERR_BUSY:       return "IRQ line already has a handler";
    case IRQ_ERR_NO_HANDLER: return "IRQ line has no handler";
    case IRQ_ERR_MASK:       return "PIC mask did not change";
    }
    return "unknown IRQ error";
}
