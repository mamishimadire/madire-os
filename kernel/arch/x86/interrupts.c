/*
 * Common C entry point for every interrupt and CPU exception (see isr.asm).
 *
 * Policy:
 *   - breakpoint (int3): counted and resumed; proves the full entry/return path;
 *   - any other CPU exception: fatal, shown on the crash screen;
 *   - hardware IRQs (vectors 32-47): handed to irq_dispatch(), which calls
 *     the registered driver handler and sends EOI (kernel/arch/x86/irq.c).
 *
 * 0.9: after irq_dispatch() has returned (so the EOI is already sent and
 * the PIC can deliver the next IRQ0 to whichever thread runs next), the
 * timer tick is charged to the scheduler and the preemption point runs: if
 * a switch is due and preemption is enabled, the interrupted thread is put
 * back on the run queue and another one continues. The interrupted thread
 * later resumes here and returns through isr_common's iretq, which restores
 * its own RFLAGS (including IF). Exceptions never switch threads.
 */
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/panic.h"
#include "kernel/proc/thread.h"

static volatile uint32_t breakpoint_count;

void interrupt_dispatch(struct interrupt_frame *frame)
{
    if (frame->vector == EXCEPTION_BREAKPOINT) {
        breakpoint_count++;
        return;
    }

    if (frame->vector < IRQ_BASE_VECTOR)
        panic_exception(frame);

    uint8_t irq = (uint8_t)(frame->vector - IRQ_BASE_VECTOR);
    irq_dispatch(irq);
    if (irq == IRQ_TIMER)
        thread_tick();
    thread_preempt_point();
}

uint32_t interrupts_breakpoint_count(void)
{
    return breakpoint_count;
}
