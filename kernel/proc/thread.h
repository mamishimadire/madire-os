#ifndef MADIRE_PROC_THREAD_H
#define MADIRE_PROC_THREAD_H

/*
 * Kernel threads (0.9). The bookkeeping is kernel/proc/sched_core.c (pure);
 * this layer owns the stacks, does the context switches (switch.asm) and
 * turns scheduler errors into panics.
 *
 * Thread 0 is kernel_main's flow: the boot sequence, then the console and
 * idle loop. It never blocks or exits; when it joins a live thread it polls
 * (yield, or hlt when nothing else can run).
 *
 * Every other thread has a 16 KiB stack in a slot of PML4 slot 507, with a
 * 16 KiB unmapped guard below it. The stack is painted when the thread is
 * created (stack high-water mark) and freed, with its frames, when the
 * thread is reaped: by its joiner, or by thread 0 for detached threads.
 *
 * Scheduling is preemptive round robin: a thread that does not give up the
 * CPU is switched out at the end of an interrupt once its slice
 * (SCHED_SLICE_TICKS, 50 ms) is used up and another thread is ready, or
 * when a sleeper wakes. Never while preemption is disabled (preempt.h):
 * the switch then waits for the next interrupt after preempt_enable().
 *
 * Rules (each breach panics with a message naming the thread):
 *   - yield, sleep, join and exit only with preemption enabled;
 *   - only thread 0 writes to the screen; other threads use klogf;
 *   - every scheduler call runs with interrupts disabled (the timer tick
 *     updates the same state from IRQ context).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/sched_core.h"

#define THREAD_NAME_MAX 16

typedef int32_t (*thread_fn)(void *arg);

/* Sets up thread 0 ("kernel"). Before this, thread_current() is 0. */
void thread_init(void);

/* Creates a thread that runs fn(arg) and exits with its return value.
 * Returns its tid, or -1 if there is no free thread slot, stack slot or
 * memory (or paging is off). */
int32_t thread_create(const char *name, thread_fn fn, void *arg, bool detached);

/* Gives the CPU to the next ready thread (if any). */
void thread_yield(void);

/* Waits for thread `tid` to exit, frees it, and returns its exit code. */
int32_t thread_join(int32_t tid);

/* Ends the current thread (never thread 0). */
void thread_exit(int32_t code) __attribute__((noreturn));

/* Blocks the current thread for `ticks` timer ticks (10 ms each); it runs
 * again at the first tick at or after now + ticks. Thread 0 waits by
 * polling (yield, or hlt when nothing else can run). 0 ticks: a yield. */
void thread_sleep(uint32_t ticks);

/* Thread 0: frees one detached thread that has exited. True if it did. */
bool thread_reap(void);

/* Thread 0's idle step: frees exited detached threads, then yields if
 * another thread is ready, else waits for the next interrupt (hlt). */
void thread_idle(void);

/* Interrupt hooks (kernel/arch/x86/interrupts.c), IRQ context, after the
 * EOI: the timer tick (slice accounting, waking sleepers), then the
 * preemption point (switch if one is due and preemption is enabled). */
void thread_tick(void);
void thread_preempt_point(void);

int32_t thread_current(void);
const char *thread_name(int32_t tid);
enum sched_state thread_state(int32_t tid);
uint32_t thread_live(void);             /* threads not free, thread 0 included */
uint32_t thread_stacks_in_use(void);
uint64_t thread_switches(void);
uint64_t thread_preemptions(void);      /* switches made at the preemption point */
uint64_t thread_ticks_run(int32_t tid); /* timer ticks charged to the thread */
bool thread_need_resched(void);         /* a switch is pending */

/* The thread using stack slot `slot`, or -1 (double-fault diagnosis). */
int32_t thread_slot_owner(int slot);

/* Stack high-water mark of a live thread, in bytes of its stack size
 * (thread 0: the boot stack, kernel_stack_peak()). Each exiting thread
 * logs its final peak: "Thread N 'name' exited: code C, stack peak P of
 * 16384"; thread_log_exits(false) turns that line off (thread churn). */
uint32_t thread_stack_peak(int32_t tid);
uint32_t thread_stack_size(int32_t tid);
void thread_log_exits(bool on);
uint32_t thread_peak_max(void);         /* highest final peak since the last reset */

/* Longest turn (0.9, known limitation of the deferred switch): the most
 * timer ticks any thread other than thread 0 (which is also the idle
 * thread) ran without a switch. thread_turn_reset() restarts both this and
 * thread_peak_max(); each scheduler diagnostic resets them at its start. */
void thread_turn_reset(void);
uint64_t thread_turn_max(void);

/* kputc's check: true while only thread 0 runs, or during a panic. */
bool thread_screen_allowed(void);
void thread_screen_violation(void) __attribute__((noreturn));

/* switch.asm, for test=threads: loads known values into the callee-saved
 * registers, yields, and returns a mask of those that changed (0 = none). */
uint32_t switch_test_regs(uint64_t seed);

#endif
