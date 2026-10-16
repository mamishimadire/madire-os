#ifndef MADIRE_PROC_FDTEST_H
#define MADIRE_PROC_FDTEST_H

/*
 * Descriptor diagnostics (1.0, stage 4), QEMU boot options only (plan B7,
 * D7; test=fdsys and test=selfcheckcopylock are approved additions to
 * B7's list).
 *
 * test=fdsys: the system calls read, write, close, dup and dup2 from the
 * program fdops: the console descriptors, close, dup, dup2, EMFILE, the
 * console's short count; files the kernel gives the program at its start
 * (process_start_fds): a read and a write in chunks with the full count,
 * dup'd descriptors sharing one offset, access errors, EFAULT; the busy
 * mark (a second reader waits for the first call, FIFO, with three
 * sharers for the fairness figures); the chunk loop's VFS lock holds;
 * nothing left behind.
 *
 * test=fdkill: programs (fdhold) killed, faulting and exiting with 16
 * descriptors open, a kill before the first entry to ring 3, a kill
 * requested between the first and second chunk of a 48 KiB read; an
 * unlinked file freed at its last close; the open-file table filled to
 * ENFILE; nothing leaked.
 *
 * test=selfcheckcopylock: a user copy while thread 0 holds the VFS lock;
 * must panic (kernel/memory/usercopy.h).
 *
 * test=selfcheckyieldlock (1.0 stage 7b): a yield while thread 0 holds the
 * VFS lock; must panic (kernel/proc/klock.h, klock_check_none).
 */

#include <stdbool.h>

bool fdtest_sys(void);
bool fdtest_kill(void);
void fdtest_selfcheck_copylock(void) __attribute__((noreturn));
void fdtest_selfcheck_yieldlock(void) __attribute__((noreturn));

#endif
