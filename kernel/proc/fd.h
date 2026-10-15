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
};
void fd_figures(struct fd_figures *out);

/* A copy of open file `file` (its kind, refs, offset, busy figures). */
void fd_file_info(uint32_t file, struct ofile *out);

#endif
