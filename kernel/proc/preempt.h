#ifndef MADIRE_PROC_PREEMPT_H
#define MADIRE_PROC_PREEMPT_H

/*
 * Preemption-disable count (0.9). While it is above zero the scheduler
 * never switches threads, even when a timer tick asks for it; the switch
 * waits for the next safe point with the count back at zero. Interrupts
 * keep running (this is not cli): the timer, the keyboard and uptime are
 * unaffected.
 *
 * Kernel subsystems that keep shared state and have no locking (the heap,
 * the frame allocator, paging and the physical-access window, kprintf /
 * klogf) wrap each operation in preempt_disable() / preempt_enable(). On
 * one CPU that is enough: no other thread can run inside the section, and
 * interrupt handlers never use these subsystems (kernel/arch/x86/irq.h).
 *
 * The count nests. An interrupt handler that used it would leave it as it
 * found it, so the read-modify-write below needs no atomic instruction.
 * The empty asm statements are compiler barriers: nothing the section
 * protects is moved outside it.
 *
 * Deferred switch (approved policy (a)): when a timer tick asks for a switch
 * while the count is above zero, the request stays pending (need_resched)
 * and preempt_enable() does NOT switch. The switch happens at the first
 * interrupt after the count is back at zero, at the end of that interrupt
 * (kernel/arch/x86/interrupts.c): at most one timer tick (10 ms) later, or
 * sooner if another interrupt comes first. There is a single switch point
 * (the end of an IRQ, after the EOI), and preempt_enable() stays safe to
 * call anywhere, including with interrupts off. test=preemptoff measures
 * the bound.
 *
 * Yielding, sleeping, joining or exiting with the count above zero is a
 * kernel bug and panics (kernel/proc/thread.c). preempt_enable() at zero
 * panics too.
 */

#include <stdint.h>

extern volatile uint32_t preempt_count_value;     /* kernel/proc/thread.c */
void preempt_underflow(void);                     /* panics */

static inline void preempt_disable(void)
{
    preempt_count_value = preempt_count_value + 1;
    __asm__ volatile ("" ::: "memory");
}

static inline void preempt_enable(void)
{
    __asm__ volatile ("" ::: "memory");
    if (preempt_count_value == 0)
        preempt_underflow();
    preempt_count_value = preempt_count_value - 1;
}

static inline uint32_t preempt_count(void)
{
    return preempt_count_value;
}

#endif
