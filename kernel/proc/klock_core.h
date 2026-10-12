#ifndef MADIRE_PROC_KLOCK_CORE_H
#define MADIRE_PROC_KLOCK_CORE_H

/*
 * The kernel lock that threads wait on (1.0, stage 3, D10), the pure part:
 * the state, its rules and its figures, with no scheduler call, printing or
 * panic (host unit tests: tests/unit/test_klock.c). The kernel side is
 * kernel/proc/klock.c; the VFS has one such lock for the whole filesystem
 * (kernel/fs/vfs.c).
 *
 * A lock is free or held by one thread (its owner). A thread that finds it
 * held waits by yielding and tries again: there is no sleep queue in 1.0,
 * so no waiter is ever taken off the run queue (klock_core_wait_method).
 * Taking it twice from one thread, or releasing it from a thread that does
 * not hold it, is an error (the kernel side panics). While a lock is held
 * its owner must be ready to run: a waiter finding the owner sleeping,
 * waiting for a thread, exited or gone (klock_core_owner_ok) has found a
 * kernel bug.
 *
 * The figures (takes, waits, the longest hold and wait in ticks, the
 * highest preemption count seen at a release) are what test=klock checks.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/sched_core.h"

#define KLOCK_FREE (-1)

struct klock {
    int32_t owner;              /* the holder's thread id, or KLOCK_FREE */
    uint64_t since;             /* the tick it was taken at */
    uint64_t takes;             /* times it was taken */
    uint64_t contended;         /* takes that waited first */
    uint64_t yields;            /* wait steps (yields) of every waiter */
    uint64_t yields0;           /* of thread 0 */
    uint64_t hold_longest;      /* ticks, from a take to its release */
    uint64_t wait_longest;      /* ticks, from the first try to the take, any thread */
    uint64_t wait_longest0;     /* the same, thread 0 only */
    uint32_t release_pc_max;    /* highest preemption count at a release */
};

enum klock_result {
    KLOCK_TAKEN = 0,            /* the caller holds it now */
    KLOCK_BUSY,                 /* another thread holds it: wait, try again */
    KLOCK_RELEASED,
    KLOCK_ERR_RECURSIVE,        /* the caller already holds it */
    KLOCK_ERR_NOT_OWNER,        /* release by a thread that does not hold it */
    KLOCK_ERR_BAD_TID,          /* not a thread id */
};

/* How a waiter waits. 1.0 has no sleep queue: every thread, thread 0
 * above all, waits by yielding. KLOCK_WAIT_BLOCK exists only so that the
 * kernel side can refuse it (its thread-0 self-check). */
enum klock_wait {
    KLOCK_WAIT_YIELD = 0,
    KLOCK_WAIT_BLOCK,
};

void klock_core_init(struct klock *l);

/* One try by thread `tid` at tick `now`. `first_try` is the tick of its
 * first try for this take and `yields` the wait steps it has made since;
 * on KLOCK_TAKEN they go into the figures. */
enum klock_result klock_core_try(struct klock *l, int32_t tid, uint64_t now, uint64_t first_try,
                                 uint32_t yields);

/* Release by thread `tid` at tick `now`; `pc` is the preemption count the
 * caller had when it asked (recorded, 0 expected). */
enum klock_result klock_core_release(struct klock *l, int32_t tid, uint64_t now, uint32_t pc);

/* One wait step of thread `tid`, counted before it yields. */
void klock_core_waited(struct klock *l, int32_t tid);

enum klock_wait klock_core_wait_method(int32_t tid);

/* True if the owner's scheduler state allows it to hold the lock while
 * another thread waits: only READY (on the run queue). On one CPU the
 * owner is never RUNNING while someone else tries; NEW, BLOCKED, ZOMBIE
 * and FREE mean it can never release. */
bool klock_core_owner_ok(enum sched_state owner_state);

bool klock_core_held_by(const struct klock *l, int32_t tid);

const char *klock_result_str(enum klock_result r);

#endif
