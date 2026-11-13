#ifndef MADIRE_PROC_DISKLOCK_CORE_H
#define MADIRE_PROC_DISKLOCK_CORE_H

/*
 * The per-disk I/O lock (1.5 stage 4; docs/plans/1.5.md, C1, decision 1),
 * the pure part: the state, the rules and the figures, with no scheduler
 * call, printing or panic (host unit tests: tests/unit/test_disklock.c).
 * The kernel side is kernel/proc/disklock.c.
 *
 * A FIFO ticket lock with the ticket logic of klock_core (a new kind,
 * not a klock): a thread takes the next ticket and holds the lock when its
 * ticket is the one served; a release serves the next ticket. Tickets are
 * 32-bit, compared by equality, distances are unsigned differences (the
 * wrap is harmless); each thread holds at most one ticket, so at most
 * DISKLOCK_TICKETS are out, the thread of each kept in
 * ticket_tid[ticket % DISKLOCK_TICKETS].
 *
 * Unlike a klock, a holder keeps preemption ON and may yield (while it
 * waits for the disk), and a waiter yields until its ticket is served.
 *
 * The order check (disklock_core_check), the rules L1, L3, L4 and L6 of
 * C1, in this order:
 *   L4  never taken from an interrupt handler (interrupts off);
 *   L1  never taken while the VFS lock is held (the disk lock comes first);
 *   L3  one owner, who never takes it again (nor while its ticket is out);
 *   L6  never taken in Live mode (the lock is on only in Disk mode).
 * L5 (the AHCI port and the read cache used only by the owner) is
 * disklock_core_io_ok; the AHCI wait loop's yield rule is
 * disklock_core_may_yield.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/sched_core.h"

#define DISKLOCK_FREE    (-1)
#define DISKLOCK_TICKETS SCHED_MAX_THREADS

struct disklock {
    bool on;                    /* Disk mode: a disk is mounted (L6) */
    int32_t owner;              /* the holder's thread id, or DISKLOCK_FREE */
    uint32_t next;              /* the next ticket to hand out */
    uint32_t serving;           /* the ticket allowed to hold the lock */
    int32_t ticket_tid[DISKLOCK_TICKETS];
    uint64_t since;             /* the tick it was taken at */
    uint64_t takes;             /* times it was taken */
    uint64_t contended;         /* takes that waited first */
    uint64_t yields;            /* wait steps (yields) of every waiter */
    uint64_t yields0;           /* of thread 0 */
    uint64_t hold_longest;      /* ticks, from a take to its release */
    uint64_t wait_longest;      /* ticks, from the ticket to the take, any thread */
    uint64_t wait_longest0;     /* the same, thread 0 only */
};

enum disklock_result {
    DISKLOCK_OK = 0,            /* the order check passed */
    DISKLOCK_TAKEN,             /* the caller holds it now */
    DISKLOCK_BUSY,              /* not its turn yet: yield, try again */
    DISKLOCK_QUEUED,            /* a ticket was handed out */
    DISKLOCK_RELEASED,
    DISKLOCK_ERR_IRQ,           /* L4: interrupts off (an interrupt handler) */
    DISKLOCK_ERR_ORDER,         /* L1: the VFS lock is held */
    DISKLOCK_ERR_RECURSIVE,     /* L3: the caller holds it (or a ticket) already */
    DISKLOCK_ERR_LIVE,          /* L6: Live mode (the lock is off) */
    DISKLOCK_ERR_NOT_OWNER,     /* release by a thread that does not hold it */
    DISKLOCK_ERR_BAD_TID,       /* not a thread id */
    DISKLOCK_ERR_FULL,          /* more tickets out than threads exist */
    DISKLOCK_ERR_NOT_TICKET,    /* a try with a ticket that is not the caller's */
};

/* Free, off (Live mode), every figure 0. */
void disklock_core_init(struct disklock *l);

/* The order check of thread `tid` before a take: `vfs_held` (the caller
 * holds the VFS lock), `irq_on` (interrupts on). DISKLOCK_OK, or the first
 * rule broken in the order L4, L1, L3, L6. Changes nothing. */
enum disklock_result disklock_core_check(const struct disklock *l, int32_t tid, bool vfs_held, bool irq_on);

/* Thread `tid` takes the next ticket (*ticket): DISKLOCK_QUEUED. */
enum disklock_result disklock_core_ticket(struct disklock *l, int32_t tid, uint32_t *ticket);

/* One try by thread `tid` with its `ticket` at tick `now`: DISKLOCK_TAKEN
 * when the ticket is served (and the lock free), else DISKLOCK_BUSY.
 * `first_try` is the tick it took its ticket and `yields` the wait steps
 * since; on DISKLOCK_TAKEN they go into the figures. */
enum disklock_result disklock_core_try(struct disklock *l, int32_t tid, uint32_t ticket, uint64_t now,
                                       uint64_t first_try, uint32_t yields);

/* Release by thread `tid` at tick `now`; serves the next ticket. */
enum disklock_result disklock_core_release(struct disklock *l, int32_t tid, uint64_t now);

/* One wait step of thread `tid`, counted before it yields. */
void disklock_core_waited(struct disklock *l, int32_t tid);

/* Tickets ahead of `ticket` (the holder's included). */
uint32_t disklock_core_ahead(const struct disklock *l, uint32_t ticket);

/* The first thread holding the lock or a ticket ahead of `ticket` whose
 * state (from `state`) is not READY, with that state in *bad_state; or
 * DISKLOCK_FREE if each of them can take its turn. A holder that yields
 * stays READY; NEW, BLOCKED, ZOMBIE and FREE mean it never could. A
 * ticket ahead whose slot names no thread gives DISKLOCK_NO_THREAD (not a
 * thread id: the caller must not name it), with SCHED_FREE. */
#define DISKLOCK_NO_THREAD (DISKLOCK_FREE - 1)
int32_t disklock_core_ahead_bad(const struct disklock *l, uint32_t ticket,
                                enum sched_state (*state)(int32_t tid), enum sched_state *bad_state);

bool disklock_core_held_by(const struct disklock *l, int32_t tid);

/* True if thread `tid` holds the lock or a ticket not yet served (the
 * check at syscall return and at thread exit: it must hold neither). */
bool disklock_core_involved(const struct disklock *l, int32_t tid);

/* L5: thread `tid` may use the AHCI port and the read cache: in Live mode
 * (the lock off: the boot's probe, a disk with nothing mounted), or as the
 * lock's owner. */
bool disklock_core_io_ok(const struct disklock *l, int32_t tid);

/* The AHCI wait loop's rule: it may yield between status reads only when
 * thread `tid` owns the disk lock, its preemption count `pc` is 0 and
 * interrupts are on (`irq_on`). Inside a VFS hold (count above 0: a
 * cache-miss read) it keeps polling and never yields. */
bool disklock_core_may_yield(const struct disklock *l, int32_t tid, uint32_t pc, bool irq_on);

const char *disklock_result_str(enum disklock_result r);

#endif
