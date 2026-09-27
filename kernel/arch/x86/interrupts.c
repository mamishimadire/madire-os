/*
 * Common C entry point for every interrupt and CPU exception (see isr.asm).
 *
 * Milestone 0.2 policy:
 *   - breakpoint (int3): counted and resumed; proves the full entry/return path;
 *   - any other CPU exception: fatal, shown on the crash screen;
 *   - hardware IRQs: all masked at the PIC, so only spurious ones can arrive;
 *     they are acknowledged and ignored. Drivers attach here from 0.4.
 */
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/pic.h"
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

    pic_acknowledge((uint8_t)(frame->vector - IRQ_BASE_VECTOR));
}

uint32_t interrupts_breakpoint_count(void)
{
    return breakpoint_count;
}
