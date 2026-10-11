#ifndef MADIRE_PROC_SCHEDTEST_H
#define MADIRE_PROC_SCHEDTEST_H

/*
 * Scheduler self-test and thread diagnostics (0.9). Everything is logged
 * on serial only (other threads may not write to the screen); kernel.c
 * shows the one-line result.
 */

#include <stdbool.h>

/* Boot self-test, cooperative part: two threads ping-pong through yield in
 * strict order, are joined, and every frame and stack slot comes back. */
bool schedtest_boot(void);

bool schedtest_threads(void);       /* test=threads */
bool schedtest_join(void);          /* test=join */
bool schedtest_ifstate(void);       /* test=ifstate */
bool schedtest_preempt(void);       /* test=preempt: fairness of preemption */
bool schedtest_preemptoff(void);    /* test=preemptoff: the switch at the release */
bool schedtest_sleep(void);         /* test=sleep */
bool schedtest_heapthreads(void);   /* test=heapthreads */
bool schedtest_framethreads(void);  /* test=framethreads */
bool schedtest_preemptlong(void);   /* test=preemptlong: policy (b), 0.11 */
bool schedtest_threadleak(void);    /* test=threadleak: 2000 threads, stack peaks */
bool schedtest_pingpong_preempt(void); /* test=pingpongpreempt: relaxed order branch */
bool schedtest_pingpong_bad(void);  /* test=pingpongbad: a wrong order still fails */
/* test=waitstall (0.12 stage 3): with the timer stopped (interrupts off), a
 * bounded wait and thread 0's bounded sleep end as stalls, each logged. */
bool schedtest_waitstall(void);

/* Deliberate kernel bugs; each must end on the crash screen. */
void schedtest_preempt_yield(void); /* test=preemptyield */
void schedtest_preempt_join(void);  /* test=preemptjoin */
void schedtest_preempt_sleep(void); /* test=preemptsleep */
void schedtest_threadoverflow(void);/* test=threadoverflow: #DF on IST1, named */

#endif
