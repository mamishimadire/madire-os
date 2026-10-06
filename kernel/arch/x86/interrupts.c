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

/* 0.10, test=clac: while armed, every interrupt records whether RFLAGS.AC
 * is set inside the handler (isr.asm clears it when SMAP is on). */
static volatile bool ac_probe;
static volatile uint32_t ac_set, ac_clear;

void interrupt_dispatch(struct interrupt_frame *frame)
{
    if (ac_probe) {
        uint64_t f;
        __asm__ volatile ("pushfq; pop %0" : "=r"(f));
        if (f & 0x40000ull)
            ac_set++;
        else
            ac_clear++;
    }
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

void interrupts_ac_probe_start(void)
{
    ac_set = ac_clear = 0;
    ac_probe = true;
}

void interrupts_ac_probe_stop(uint32_t *with_ac, uint32_t *without_ac)
{
    ac_probe = false;
    *with_ac = ac_set;
    *without_ac = ac_clear;
}
