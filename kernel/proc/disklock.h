#ifndef MADIRE_PROC_DISKLOCK_H
#define MADIRE_PROC_DISKLOCK_H

/*
 * The per-disk I/O lock (1.5 stage 4; docs/plans/1.5.md, C1, decision 1).
 * The state and its rules are the pure disklock_core.c; this side takes
 * the lock, waits and turns a broken rule into a panic.
 *
 * Disk mode only: the lock is switched on by the boot-time mount
 * (disklock_start, thread 0, before any other thread makes a file call)
 * and off again if the mount fails (disklock_stop); in Live mode it is
 * never taken (L6) and nothing changes.
 *
 * The lock order (L1): the disk lock first, then the VFS lock. In Disk
 * mode fd_vfs_acquire() takes both and fd_vfs_release() gives both back
 * (kernel/proc/fd.c), so every path that gives the VFS lock back to wait
 * (a busy or write mark, an ext2 save before a retry) gives the disk lock
 * back too. A save piece holds the disk lock only
 * (kernel/fs/ext2_disk.c).
 *
 * L2: a holder keeps preemption on and may yield (the AHCI wait loop
 * yields between status reads: disklock_may_yield); a waiter yields until
 * its ticket is served and never spins with preemption off. VFS holders
 * are unchanged: preemption off, never yielding (klock.h). L2 has no
 * panic of its own; it is enforced by design and by the existing checks:
 * a take inside a VFS hold is L1's panic; a take inside any other
 * preempt_disable() section would wait with thread_yield(), which panics
 * with preemption off (thread.c, require_preemption) and while the VFS
 * lock is held (klock_check_none); and the AHCI loop asks
 * disklock_may_yield(), true only for the owner at preemption count 0
 * with interrupts on.
 *
 * Always-on self-checks (each a panic whose first line is the rule's text,
 * the second naming the function and the thread):
 *   "disk lock taken while the VFS lock is held (L1)"
 *   "disk lock taken again by its owner (L3)"
 *   "disk lock taken in an interrupt handler (L4)"
 *   "AHCI used without the disk lock (L5)"      (disklock_check_io)
 *   "disk lock taken in Live mode (L6)"
 *   "disk lock still held at syscall return"   (disklock_check_none)
 *   "disk lock still held at thread exit"      (disklock_check_none)
 * and, as klock's: a release by a thread that does not hold it, more
 * tickets than threads, and a waiter behind a holder or ticket whose
 * thread can never take its turn (not ready), or behind a ticket slot
 * that names no thread.
 *
 * Why no deadlock (one CPU): a VFS holder never yields and never waits for
 * the disk lock (L1), so it runs to its release; a disk-lock holder waits
 * only for the disk (bounded by the driver's time limits) and for nothing
 * a waiter holds; a waiter holds neither lock (busy and write marks are
 * waited for with no lock held). Every wait therefore ends.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/disklock_core.h"

/* Disk mode on (the mount, thread 0) and off (a mount that failed). */
void disklock_start(void);
void disklock_stop(void);

/* True in Disk mode. */
bool disklock_on(void);

/* Takes the disk lock (the order check first, then a ticket, yielding
 * until it is served); returns the wait steps. Preemption stays on. */
uint32_t disklock_acquire(void);

void disklock_release(void);

/* True if the calling thread holds it. */
bool disklock_held(void);

/* The AHCI wait loop's rule (disklock_core_may_yield) for the caller. */
bool disklock_may_yield(void);

/* L5: panics, naming `where` and the thread, if the caller uses the AHCI
 * port or the read cache in Disk mode without holding the disk lock. */
void disklock_check_io(const char *where);

/* Always-on: panics if the calling thread holds the disk lock or a ticket
 * at `at` ("syscall return": syscall.c; "thread exit": thread.c), with
 * the first line "disk lock still held at <at>" and the second naming
 * `fn` and the thread. */
void disklock_check_none(const char *at, const char *fn);

/* The figures (report only): takes, takes that waited, wait steps (all,
 * thread 0's), the longest hold and wait in ticks (thread 0's wait too). */
struct disklock_figs {
    uint64_t takes, contended, yields, yields0;
    uint64_t hold_longest, wait_longest, wait_longest0;
};
void disklock_figures(struct disklock_figs *f);

/* test=selfcheckdisklock and test=selfcheckdiskrelock (Disk mode, thread
 * 0): break L1 (the VFS lock, then the disk lock) or L3 (the disk lock
 * twice); each must panic with its rule's text. */
void disklock_selfcheck_order(void);
void disklock_selfcheck_relock(void);

#endif
