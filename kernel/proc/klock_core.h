#ifndef MADIRE_PROC_KLOCK_CORE_H
#define MADIRE_PROC_KLOCK_CORE_H

/*
 * The kernel lock that threads wait on (1.0, stage 3, D10), the pure part:
 * the state, its rules and its figures, with no scheduler call, printing or
 * panic (host unit tests: tests/unit/test_klock.c). The kernel side is
 * kernel/proc/klock.c; the VFS has one such lock for the whole filesystem
 * (kernel/fs/vfs.c).
 *
 * A FIFO ticket lock (stage 3 fix): a thread takes the next ticket and may
 * hold the lock when its ticket is the one served; a release serves the
 * next ticket. So threads hold it in the order they asked, and a waiter
 * waits for at most the holds of the tickets ahead of it (the plain
 * try-and-yield lock of the first version let a thread that took it again
 * at once starve the others). Waiting is by yielding: there is no sleep
 * queue in 1.0, so no waiter is ever taken off the run queue
 * (klock_core_wait_method).
 *
 * Tickets are 32-bit and wrap: they are compared by equality only and
 * distances are unsigned differences, so the wrap is harmless. Each thread
 * holds at most one ticket, so at most KLOCK_TICKETS are outstanding; the
 * thread of each is kept in ticket_tid[ticket % KLOCK_TICKETS] (2^32 is a
 * multiple of it).
 *
 * Errors the kernel side turns into panics: taking it twice from one
 * thread, a release by a thread not holding it, more tickets than threads,
 * and a ticket ahead whose thread can never take its turn (only a READY
 * thread may hold the lock or a ticket ahead of a waiter:
 * klock_core_ahead_bad).
 *
 * The figures (takes, waits, the longest hold and wait in ticks, the most
 * tickets ahead of thread 0, the lowest and highest preemption count seen
 * at a release) are what test=klock checks.
 *
 * 1.0 stage 7b: a holder keeps preemption disabled from its take to its
 * release (kernel/proc/klock.h), so the count at a release is the hold's
 * own, 1; klock_core_requeue refuses any other count. A reserved turn
 * (test code only, while a test= diagnostic runs): the caller's ticket is
 * served but it does not hold the lock yet; every ticket behind waits; it
 * takes the lock later (klock_core_take_reserved). A hold that began as a
 * reserved turn is timed from the reservation.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/sched_core.h"

#define KLOCK_FREE    (-1)
#define KLOCK_TICKETS SCHED_MAX_THREADS

struct klock {
    int32_t owner;              /* the holder's thread id, or KLOCK_FREE */
    int32_t reserved;           /* stage 7b: the thread whose turn is reserved, or KLOCK_FREE */
    uint32_t next;              /* the next ticket to hand out */
    uint32_t serving;           /* the ticket allowed to hold the lock */
    int32_t ticket_tid[KLOCK_TICKETS];
    uint64_t since;             /* the tick it was taken at */
    uint64_t takes;             /* times it was taken */
    uint64_t contended;         /* takes that waited first */
    uint64_t yields;            /* wait steps (yields) of every waiter */
    uint64_t yields0;           /* of thread 0 */
    uint64_t hold_longest;      /* ticks, from a take to its release */
    uint64_t wait_longest;      /* ticks, from the ticket to the take, any thread */
    uint64_t wait_longest0;     /* the same, thread 0 only */
    uint32_t ahead_max0;        /* most tickets ahead of thread 0's ticket (holder included) */
    uint32_t release_pc_max;    /* highest preemption count at a release */
    uint32_t release_pc_min;    /* stage 7b: lowest (UINT32_MAX before the first release) */
    uint64_t reserves;          /* stage 7b: reserved turns */
};

enum klock_result {
    KLOCK_TAKEN = 0,            /* the caller holds it now */
    KLOCK_BUSY,                 /* not its turn yet: wait, try again */
    KLOCK_QUEUED,               /* a ticket was handed out */
    KLOCK_RELEASED,
    KLOCK_ERR_RECURSIVE,        /* the caller already holds it */
    KLOCK_ERR_NOT_OWNER,        /* release by a thread that does not hold it */
    KLOCK_ERR_BAD_TID,          /* not a thread id */
    KLOCK_ERR_FULL,             /* more tickets out than threads exist */
    KLOCK_ERR_NOT_TICKET,       /* a try with a ticket that is not the caller's */
    KLOCK_RESERVED,             /* stage 7b: the caller's turn is reserved */
    KLOCK_ERR_COUNT,            /* stage 7b: a requeue with a preemption count other than 1 */
    KLOCK_ERR_NOT_DIAG,         /* stage 7b: a reserved turn outside a test= diagnostic */
    KLOCK_ERR_NOT_RESERVED,     /* stage 7b: a take without a reserved turn */
};

/* How a waiter waits. 1.0 has no sleep queue: every thread, thread 0
 * above all, waits by yielding. KLOCK_WAIT_BLOCK exists only so that the
 * kernel side can refuse it (its thread-0 self-check). */
enum klock_wait {
    KLOCK_WAIT_YIELD = 0,
    KLOCK_WAIT_BLOCK,
};

void klock_core_init(struct klock *l);

/* Thread `tid` takes the next ticket (*ticket): KLOCK_QUEUED. For thread
 * 0, the tickets ahead of it go into ahead_max0. */
enum klock_result klock_core_ticket(struct klock *l, int32_t tid, uint32_t *ticket);

/* One try by thread `tid` with its `ticket` at tick `now`: KLOCK_TAKEN
 * when the ticket is served (and the lock is free), else KLOCK_BUSY.
 * `first_try` is the tick it took its ticket and `yields` the wait steps
 * it has made since; on KLOCK_TAKEN they go into the figures. */
enum klock_result klock_core_try(struct klock *l, int32_t tid, uint32_t ticket, uint64_t now,
                                 uint64_t first_try, uint32_t yields);

/* Release by thread `tid` at tick `now`; serves the next ticket. `pc` is
 * the preemption count the caller had when it asked (recorded: the
 * lowest and the highest; 1 expected since stage 7b, the hold's own). */
enum klock_result klock_core_release(struct klock *l, int32_t tid, uint64_t now, uint32_t pc);

/* 1.0 stage 4: the holder `tid` releases at tick `now` (as
 * klock_core_release, `pc` recorded the same way) and takes the next
 * ticket (*ticket, as klock_core_ticket) in one step: no other ticket can
 * come between, so it goes behind every ticket already out and holds the
 * lock again only after each of them. KLOCK_QUEUED, or the release's
 * error with nothing changed (kernel/proc/klock.c, klock_requeue). Stage
 * 7b: `pc` must be exactly 1 (the hold's own count), else KLOCK_ERR_COUNT
 * with nothing changed. */
enum klock_result klock_core_requeue(struct klock *l, int32_t tid, uint64_t now, uint32_t pc,
                                     uint32_t *ticket);

/* Stage 7b, test code only. klock_core_reserve_try: as klock_core_try,
 * but when the ticket is served the caller's turn is reserved
 * (KLOCK_RESERVED: the lock stays without an owner, every ticket behind is
 * busy, the hold's time starts now). klock_core_take_reserved: the
 * reserving thread takes the lock (KLOCK_TAKEN), or KLOCK_ERR_NOT_RESERVED.
 * Both return KLOCK_ERR_NOT_DIAG with nothing changed unless `diag` (a
 * test= diagnostic is running). */
enum klock_result klock_core_reserve_try(struct klock *l, int32_t tid, uint32_t ticket,
                                         uint64_t now, uint64_t first_try, uint32_t yields,
                                         bool diag);
enum klock_result klock_core_take_reserved(struct klock *l, int32_t tid, bool diag);

/* One wait step of thread `tid`, counted before it yields. */
void klock_core_waited(struct klock *l, int32_t tid);

/* Tickets ahead of `ticket` (the holder's included). */
uint32_t klock_core_ahead(const struct klock *l, uint32_t ticket);

/* The first thread holding the lock or a ticket ahead of `ticket` whose
 * state (from `state`) is not READY, with that state in *bad_state; or
 * KLOCK_FREE if all of them can take their turn. */
int32_t klock_core_ahead_bad(const struct klock *l, uint32_t ticket,
                             enum sched_state (*state)(int32_t tid), enum sched_state *bad_state);

enum klock_wait klock_core_wait_method(int32_t tid);

/* True if a thread in this state may hold the lock or a ticket ahead of a
 * waiter: only READY (on the run queue). On one CPU only the waiter itself
 * runs; NEW, BLOCKED, ZOMBIE and FREE mean it can never take its turn. */
bool klock_core_owner_ok(enum sched_state state);

bool klock_core_held_by(const struct klock *l, int32_t tid);

const char *klock_result_str(enum klock_result r);

#endif
