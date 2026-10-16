#ifndef MADIRE_PROC_KLOCK_H
#define MADIRE_PROC_KLOCK_H

/*
 * The kernel lock that threads wait on (1.0, stage 3, D10; the rules are in
 * docs/plans/1.0.md, B2). The state and its figures are the pure
 * klock_core.c; this side takes the lock, waits and checks the rules.
 *
 * klock_acquire takes a ticket (a FIFO ticket lock, the stage 3 fix);
 * until its ticket is served it yields (thread_yield) and tries again.
 * That is the only scheduler call here: a waiter stays on the run queue,
 * so thread 0 (the console) never blocks on a lock, and the threads ahead
 * of it, which are ready, run in turn. Threads hold the lock in the order
 * they asked, so a waiter waits for at most the holds of the tickets
 * ahead of it (at most one per other thread).
 *
 * 1.0 stage 7b (an owner-approved step outside the plan; it supersedes
 * B2's "holders run with preemption on ... may be preempted or yield"):
 * a holder runs with preemption DISABLED from its take to its release.
 * The take leaves the preemption count at 1 (the hold's own) and the
 * release brings it back to 0, which is where a switch that came due
 * during the hold is taken (policy (b), preempt.h). Before its ticket an
 * acquire passes a switch point (a switch already due is taken then), and
 * the ticket and the first try are one preemption-off step, so the kernel
 * never leaves a ticket out while it is switched away. Holders do bounded
 * work and never yield, sleep, join or exit while holding (always-on
 * self-check, klock_check_none, called by thread_yield, thread_sleep,
 * thread_join and thread_exit). On one CPU the kernel's own code then
 * never finds the lock held: queues form only where a test builds them
 * with a reserved turn (below). Waiters still wait by yielding, with
 * preemption on, and thread 0 never blocks. Process kills take effect only
 * at the return to ring 3, so no ticket is ever abandoned.
 *
 * Lock order: a klock is taken outermost, with the preemption count 0 and
 * interrupts on (else panic); the preempt_disable() sections (heap, page
 * allocator, window, klogf) nest inside a hold, never the other way round.
 * Never from an interrupt handler (interrupts are off there: panic).
 *
 * Always-on self-checks (each a panic naming the thread):
 *   - taken twice by one thread; released by a thread not holding it;
 *     released with preemption on; requeued with a preemption count
 *     other than the hold's own 1;
 *   - more tickets out than threads;
 *   - a waiter finds the owner, or the thread of a ticket ahead of its
 *     own, not ready (sleeping, joining, exited, gone, not started);
 *   - thread 0: a wait method other than yielding (a blocking wait), or
 *     thread 0 not running while it waits;
 *   - stage 7b: a yield, sleep, join or exit while holding a klock;
 *     a reserved turn outside a test= diagnostic.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/klock_core.h"

void klock_init(struct klock *l);

/* Takes the lock; returns the wait steps (yields) it needed (0: free).
 * Returns with preemption disabled until klock_release. */
uint32_t klock_acquire(struct klock *l);

void klock_release(struct klock *l);

/* 1.0 stage 4: releases the lock and takes a new ticket in one step (no
 * other ticket can come between), so every thread already waiting holds
 * the lock once before the caller has it again; returns the wait steps.
 * The caller must hold it, with the preemption count exactly 1 (the
 * hold's own; stage 7b). (test=fdkill uses it to take the lock between
 * two chunks of a program's read.) */
uint32_t klock_requeue(struct klock *l);

/*
 * Stage 7b, TEST CODE ONLY: a reserved turn. klock_test_reserve waits for
 * the caller's turn as klock_acquire does but does not take the lock:
 * every ticket behind it waits, while the caller, which does not hold the
 * lock, keeps preemption on and may yield (a test builds a queue this
 * way, e.g. until a program waits behind it). klock_test_take then takes
 * the lock (preemption disabled until klock_release). The figures count
 * the hold from the reservation. Both panic unless a test= diagnostic is
 * running (klock_test_allow, called only by the test= dispatch in
 * kernel/kernel.c, never on a normal boot): production code never
 * reserves a turn.
 */
uint32_t klock_test_reserve(struct klock *l);
void klock_test_take(struct klock *l);
void klock_test_allow(void);

/* Stage 7b: panics, naming fn and the thread, if the calling thread holds
 * a klock (the VFS lock is the only one). */
void klock_check_none(const char *fn);

/* True if the calling thread holds it. */
bool klock_held(const struct klock *l);

/* Thread 0's self-check: the wait steps of thread 0 it checked. */
uint64_t klock_thread0_checks(void);

#endif
