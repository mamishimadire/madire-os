#ifndef MADIRE_PROC_KLOCK_H
#define MADIRE_PROC_KLOCK_H

/*
 * The kernel lock that threads wait on (1.0, stage 3, D10; the rules are in
 * docs/plans/1.0.md, B2). The state and its figures are the pure
 * klock_core.c; this side takes the lock, waits and checks the rules.
 *
 * klock_acquire tries; while another thread holds the lock it yields
 * (thread_yield) and tries again. That is the only scheduler call here:
 * a waiter stays on the run queue, so thread 0 (the console) never blocks
 * on a lock, and the holder, which is ready, runs within one round of the
 * run queue. Holders run with preemption on and do bounded work; they may
 * be preempted or yield, never sleep, join or exit while holding.
 *
 * Lock order: a klock is taken outermost, with the preemption count 0 and
 * interrupts on (else panic); the preempt_disable() sections (heap, page
 * allocator, window, klogf) nest inside a hold, never the other way round.
 * Never from an interrupt handler (interrupts are off there: panic).
 *
 * Always-on self-checks (each a panic naming the thread):
 *   - taken twice by one thread; released by a thread not holding it;
 *   - a waiter finds the owner not ready (sleeping, joining, exited, gone);
 *   - thread 0: a wait method other than yielding (a blocking wait), or
 *     thread 0 not running while it waits.
 */

#include <stdint.h>
#include "kernel/proc/klock_core.h"

void klock_init(struct klock *l);

/* Takes the lock; returns the wait steps (yields) it needed (0: free). */
uint32_t klock_acquire(struct klock *l);

void klock_release(struct klock *l);

/* True if the calling thread holds it. */
bool klock_held(const struct klock *l);

/* Thread 0's self-check: the wait steps of thread 0 it checked. */
uint64_t klock_thread0_checks(void);

#endif
