/*
 * Common C entry point for every interrupt and CPU exception (see isr.asm).
 *
 * Policy:
 *   - breakpoint (int3): counted and resumed; proves the full entry/return path;
 *   - any other CPU exception: fatal, shown on the crash screen;
 *   - hardware IRQs (vectors 32-47): handed to irq_dispatch(), which calls
 *     the registered driver handler and sends EOI (kernel/arch/x86/irq.c).
 */
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/panic.h"

static volatile uint32_t breakpoint_count;

void interrupt_dispatch(struct interrupt_frame *frame)
{
    if (frame->vector == EXCEPTION_BREAKPOINT) {
        breakpoint_count++;
        return;
    }

    if (frame->vector < IRQ_BASE_VECTOR)
        panic_exception(frame);

    irq_dispatch((uint8_t)(frame->vector - IRQ_BASE_VECTOR));
}

uint32_t interrupts_breakpoint_count(void)
{
    return breakpoint_count;
}
