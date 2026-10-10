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
 * Pending switch: when a timer tick asks for a switch while the count is
 * above zero, the request stays pending (need_resched). 0.9 policy (a):
 * the switch waited for the first interrupt after the count was back at
 * zero, and a thread that spent almost all its time in guarded sections
 * could keep the CPU for about 100 ticks, because tick after tick landed
 * inside a section. 0.11 policy (b) (an approved deviation): the
 * preempt_enable() that brings the count to zero switches at once
 * (preempt_resched, kernel/proc/thread.c), if interrupts are on, the
 * scheduler is ready and no panic is in progress. Interrupt handlers run
 * with interrupts off, so they never switch here; anywhere else this
 * applies, a timer interrupt could already have switched, so no new
 * interleaving appears. The end of an IRQ (interrupts.c) stays a switch
 * point too. preempt_enable() is still safe with interrupts off (it does
 * not switch then). test=preemptoff measures both.
 *
 * Yielding, sleeping, joining or exiting with the count above zero is a
 * kernel bug and panics (kernel/proc/thread.c). preempt_enable() at zero
 * panics too.
 */

#include <stdint.h>

extern volatile uint32_t preempt_count_value;     /* kernel/proc/thread.c */
void preempt_underflow(void);                     /* panics */
/* 0.11, policy (b): a switch is due (a copy of the scheduler's need_resched,
 * set at each tick), and the switch itself (kernel/proc/thread.c). */
extern volatile uint8_t preempt_resched_pending;
void preempt_resched(void);

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
    if (preempt_count_value == 0 && preempt_resched_pending)
        preempt_resched();                        /* 0.11: policy (b) */
}

static inline uint32_t preempt_count(void)
{
    return preempt_count_value;
}

#endif
