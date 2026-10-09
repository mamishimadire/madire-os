/*
 * Common C entry point for every interrupt and CPU exception (see isr.asm).
 *
 * Policy:
 *   - breakpoint (int3): counted and resumed; proves the full entry/return path;
 *   - any other CPU exception: fatal, shown on the crash screen; except
 *     (0.10) one raised by a process's code in ring 3, which ends only
 *     that process (process_fault);
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
#include "kernel/arch/x86/gdt_desc.h"
#include "kernel/arch/x86/interrupts.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/panic.h"
#include "kernel/proc/process.h"
#include "kernel/proc/thread.h"

static volatile uint32_t breakpoint_count;

/* 0.10, test=clac: while armed, every interrupt records whether RFLAGS.AC
 * is set inside the handler (isr.asm clears it when SMAP is on). */
static volatile bool ac_probe;
static volatile uint32_t ac_set, ac_clear;

/* 0.10, test=userspin: see interrupts.h. Changed only with interrupts off. */
static volatile bool user_probe;
static struct user_irq_probe up;
static void user_probe_record(const struct interrupt_frame *frame);

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

    /* 0.10: an exception in a process's ring-3 code ends that process
     * (process_fault does not return then); anything else is fatal. */
    if (frame->vector < IRQ_BASE_VECTOR) {
        if ((frame->cs & 3) == 3)
            process_fault(frame);
        panic_exception(frame);
    }

    uint8_t irq = (uint8_t)(frame->vector - IRQ_BASE_VECTOR);
    irq_dispatch(irq);
    bool user_tick = user_probe && irq == IRQ_TIMER && (frame->cs & 3) == 3;
    if (irq == IRQ_TIMER)
        thread_tick();
    if (user_tick)
        user_probe_record(frame);
    uint64_t preemptions = thread_preemptions();
    thread_preempt_point();
    /* Back here: either no switch, or this thread runs again; with ring 3
     * interrupted, isr_common's iretq now returns there. */
    if (user_tick && thread_preemptions() != preemptions)
        up.preempted++;
    /* 0.10: a process with a kill pending ends here instead of returning
     * to ring 3 (process_return_to_user never returns then). */
    if ((frame->cs & 3) == 3)
        process_return_to_user(frame);
}

static void user_probe_record(const struct interrupt_frame *frame)
{
    uint64_t at = (uint64_t)(uintptr_t)frame;
    uint64_t rsp0 = gdt_rsp0(), top = thread_kstack_top(thread_current());
    if (up.irqs == 0) {
        up.tid = thread_current();
        up.first = *frame;
        up.first_at = at;
        up.first_rsp0 = rsp0;
        up.first_top = top;
        up.rip_min = up.rip_max = frame->rip;
        up.rsp_min = up.rsp_max = frame->rsp;
    }
    up.irqs++;
    if (up.preempted)
        up.after_preempt++;
    if (frame->cs != GDT_USER_CODE_SEL)
        up.cs_bad++;
    if (frame->ss != GDT_USER_DATA_SEL)
        up.ss_bad++;
    if (!(frame->rflags & 0x200))
        up.if_clear++;
    if (frame->rflags & 0x3000)
        up.iopl_set++;
    if (at + sizeof(*frame) != rsp0 || rsp0 != top)
        up.stack_bad++;
    if (thread_current() != up.tid)
        up.other_thread++;
    if (frame->rip < up.rip_min)
        up.rip_min = frame->rip;
    if (frame->rip > up.rip_max)
        up.rip_max = frame->rip;
    if (frame->rsp < up.rsp_min)
        up.rsp_min = frame->rsp;
    if (frame->rsp > up.rsp_max)
        up.rsp_max = frame->rsp;
}

void interrupts_user_probe_start(void)
{
    uint32_t f = irq_save();
    struct user_irq_probe zero = { 0 };
    up = zero;
    user_probe = true;
    irq_restore(f);
}

void interrupts_user_probe_stop(struct user_irq_probe *out)
{
    uint32_t f = irq_save();
    user_probe = false;
    *out = up;
    irq_restore(f);
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
