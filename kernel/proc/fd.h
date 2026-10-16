#ifndef MADIRE_PROC_FD_H
#define MADIRE_PROC_FD_H

/*
 * File descriptors, the kernel side (1.0, stage 4; the rules are in
 * fdtable_core.h and docs/plans/1.0.md, B2 and B3).
 *
 * Every process starts with fd 0 (console input, read only: reads return
 * 0), and fds 1 and 2 sharing one console output (write only: the 0.10
 * write path, at most 4096 bytes per call). The kernel may give a program
 * more descriptors at its start (process_start_fds): open files the
 * kernel opened itself. No system call reaches that path; it is how the
 * diagnostics give programs files before open() exists, and how a shell
 * redirection can give a program a file as fd 1 later.
 *
 * The descriptor and open-file tables change only with preemption
 * disabled (one CPU). A file's data and its node are reached only under
 * the VFS lock, one chunk of at most 4096 bytes per hold, and user memory
 * only outside it, through a kernel buffer of one chunk. Lock order: an
 * open file's busy mark, then the VFS lock (never the other way round: no
 * thread waits for a busy mark while holding the VFS lock).
 *
 * The busy mark is held for a whole read or write on a file and released
 * before the call returns, so it is never held at a return to ring 3,
 * where a kill takes effect (always-on self-check: fd_busy_check).
 *
 * 1.0 stage 5: open, lseek, ftruncate and fstat's kernel side. A write
 * (and a truncate) also holds its node's write mark for the whole call
 * (fsys_core.h), so two writes to one file never interleave chunk by
 * chunk; O_APPEND claims the end once, under the mark. Lock order: the
 * open file's busy mark, the node's write mark, the VFS lock. A truncate
 * gives pages back at most FSYS_TRUNC_STEP per hold of the VFS lock. A
 * buffer that turns bad part-way: the bytes before its first bad page are
 * moved and counted (an error only when there are none). The console
 * keeps its own write path (kernel/proc/syscall.c), unchanged.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/fdtable_core.h"

struct vnode;

/* A descriptor the kernel gives a program at its start: open file `file`
 * (one the kernel holds, from fd_kernel_open) as descriptor `fd`. */
struct fd_grant {
    uint32_t fd;
    uint32_t file;
};

/* fds 0, 1 and 2 on the console for a new process: 0 or -ENFILE (nothing
 * taken). Only preemption is disabled inside: callable with interrupts off. */
int fd_start_console(struct fd_table *d);

/* The grants, each at its number (a console descriptor there is closed
 * first), each open file taking one more reference. false: a number out
 * of range, two grants for one number, or a file not open. */
bool fd_start_grants(struct fd_table *d, const struct fd_grant *g, uint32_t n);

/* A process's end: every descriptor closed, the nodes of the open files
 * freed by it dropped under the VFS lock. Returns the descriptors closed. */
uint32_t fd_teardown(struct fd_table *d);

/* The kernel's own open files (diagnostics; a shell redirection later):
 * fd_kernel_open is called with the VFS lock held (it holds the node);
 * fd_kernel_close without it (it may drop the node). -ENFILE when full. */
int fd_kernel_open(struct vnode *n, uint8_t access, uint32_t *file);
int fd_kernel_console(uint32_t *file);  /* a console-input open file (test=fdkill's ENFILE) */
void fd_kernel_close(uint32_t file);

/* The open file of descriptor fd if it allows `want` (OF_READ or
 * OF_WRITE): 0, its index and kind; else -EBADF. */
int fd_resolve(const struct fd_table *d, uint64_t fd, uint8_t want, uint32_t *file,
               uint8_t *kind);

/* read / write on a file's open file (fd_resolve'd): the in-kernel chunk
 * loop (plan B2, F18). The full count; a short count only at the end of
 * the file (read), at a limit (write: EFBIG, ENOSPC) or at a bad buffer
 * after some progress; the error itself when nothing was done. */
int64_t fd_file_read(uint32_t file, uint64_t buf, uint64_t count);
int64_t fd_file_write(uint32_t file, uint64_t buf, uint64_t count);

/* 1.0 stage 5. open: fd_open_begin checks for a free descriptor (-EMFILE)
 * and takes an open file of `access` with no node yet (-ENFILE);
 * fd_open_node gives it its node (the VFS lock held: the node is held);
 * fd_open_finish installs it at the lowest free descriptor (with
 * close-on-exec if asked) and returns that number; fd_open_abort gives it
 * back (dropping its node if it had one). */
int fd_open_begin(const struct fd_table *d, uint8_t access, uint32_t *file);
void fd_open_node(uint32_t file, struct vnode *n);
int fd_open_finish(struct fd_table *d, uint32_t file, bool cloexec);
void fd_open_abort(uint32_t file);

/* The open file of descriptor fd, with one reference taken for the call
 * (fd_put gives it back): 0, or -EBADF. */
int fd_get(const struct fd_table *d, uint64_t fd, uint32_t *file, uint8_t *kind,
           uint8_t *access, struct vnode **node);
void fd_put(uint32_t file);

/* A file's new size, under its write mark, in steps (the caller holds the
 * node): 0, -EISDIR (not a file) or -EFBIG. */
int fd_node_truncate(struct vnode *n, uint64_t length);

/* lseek on a file's open file, under its busy mark: the new offset or
 * -EINVAL. */
int64_t fd_file_lseek(uint32_t file, int64_t off, uint64_t whence);

/*
 * 1.0 stage 6. fd_file_getdents: getdents64 on a folder's open file, under
 * its busy mark (Linux's position lock): whole struct linux_dirent64
 * records from the open file's offset (fsys_core.h), at most 4096 bytes
 * of them per hold of the VFS lock, copied out after each hold; the bytes
 * written; -ENOTDIR (not a folder), -ENOENT (the folder was removed),
 * -EINVAL (the next record does not fit), -EFAULT (no whole record could
 * be written); after some records, the bytes so far. count is read from
 * its low 32 bits (Linux's unsigned int; recorded with F17: descriptors
 * stay strict 64-bit values, S4-2).
 *
 * fd_node_drop: gives back one hold of node n (no VFS lock held), as a
 * close does: a file left with no name and no other hold gives its pages
 * back in steps of at most FSYS_TRUNC_STEP per hold first (S6-6 (a); the
 * same path as every last close and every teardown).
 *
 * The current folder (struct fd_table's cwd) is dropped by fd_teardown
 * with the descriptors.
 */
int64_t fd_file_getdents(uint32_t file, uint64_t buf, uint64_t count);
void fd_node_drop(struct vnode *n);

int64_t fd_sys_close(struct fd_table *d, uint64_t fd);
int64_t fd_sys_dup(struct fd_table *d, uint64_t oldfd);
int64_t fd_sys_dup2(struct fd_table *d, uint64_t oldfd, uint64_t newfd);

/* Panics if the calling thread holds a busy mark (before every return to
 * ring 3 from a system call, and at a process's end). */
void fd_busy_check(const char *where);

/* Figures (the boot line, test=fdsys, test=fdkill, test=killrace). */
uint32_t fd_open_files(void);
struct fd_figures {
    uint64_t teardown_closed;           /* descriptors closed by process teardowns */
    uint64_t chunks;                    /* file chunks moved under the VFS lock */
    uint64_t hold_longest;              /* ticks, the longest such hold */
    uint64_t busy_wait_longest;         /* ticks, the longest wait for a busy mark */
    /* 1.0 stage 5: every hold of the chunk loop, the truncates and lseek */
    uint64_t holds;
    uint64_t holds_ticked;              /* of them, holds of 1 tick or more */
    uint64_t holds_switched;            /* of them, holds during which another thread ran */
    uint64_t trunc_holds, trunc_hold_longest;
    uint64_t wmark_wait_longest;        /* ticks, the longest wait for a write mark */
    /* The write marks of every node (kept here: the node holds only the
     * mark itself, in its padding). */
    uint64_t wmark_takes, wmark_contended;
    uint32_t wmark_ahead_max;           /* most tickets ahead of a caller's (holder included) */
    uint64_t cwd_dropped;               /* 1.0 stage 6: current folders dropped by teardowns */
};
void fd_figures(struct fd_figures *out);

/* 1.0 stage 5: the figures from now on (chunks, holds, the longest hold
 * and waits, and every thread's VFS-lock waits); a diagnostic's start. */
void fd_figures_reset(void);

/* Thread tid's VFS-lock takes after waiting, and its longest wait in
 * ticks, through fd_vfs_acquire (the chunk loop, the truncates, lseek,
 * the file system calls, node drops, and test=filecontend's thread 0). */
void fd_thread_lock(int32_t tid, uint64_t *waited, uint64_t *longest);

/* 1.0 stage 5: the VFS lock with its figures. fd_vfs_acquire takes it
 * (the wait steps it needed); fd_vfs_release notes the tick and releases
 * it. A take that waited is a hand-off: its gap is the ticks from the
 * release that served its ticket (with the FIFO lock, the last release)
 * to the take, the time the next owner took to be scheduled. Counted
 * only while every user of the lock goes through these two. */
uint32_t fd_vfs_acquire(void);
void fd_vfs_release(void);
struct fd_handoff {
    uint64_t count;                     /* takes that waited */
    uint64_t ticked;                    /* of them, gaps of 1 tick or more */
    uint64_t longest;                   /* ticks */
    int32_t longest_tid;                /* the thread that waited it, or -1 */
};
void fd_handoffs(struct fd_handoff *out);

/* A copy of open file `file` (its kind, refs, offset, busy figures). */
void fd_file_info(uint32_t file, struct ofile *out);

#endif
