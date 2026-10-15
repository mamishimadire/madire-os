#ifndef MADIRE_PROC_FDTABLE_CORE_H
#define MADIRE_PROC_FDTABLE_CORE_H

/*
 * File descriptors (1.0, stage 4), the pure part: the system-wide table of
 * open files, each process's descriptor table, and the decisions of close,
 * dup and dup2 with their Linux errno values (docs/plans/1.0.md, B1, B3).
 * No locking, printing or scheduler call here (host unit tests:
 * tests/unit/test_fdtable.c); the kernel side, kernel/proc/fd.c, calls it
 * with preemption disabled (one CPU).
 *
 * The idea of an open file shared by descriptors, with its own offset and
 * reference count (what dup shares), follows xv6's file table; the code is
 * Madire's own.
 *
 * An open file is a console input (fd 0 at start: read only, reads return
 * 0), a console output (fds 1 and 2 at start share one: write only, the
 * 0.10 write path) or a file (a VFS node, opaque here). refs counts the
 * descriptors on it, in any process, and the kernel's own holds; it is
 * freed when refs reaches 0, and the node it held is handed back to the
 * caller, which drops it under the VFS lock.
 *
 * Each process has FD_MAX descriptors (0..15); a descriptor holds the
 * index of an open file or FD_NONE. Descriptor numbers are compared as
 * full 64-bit values: fd 2^32 + 1 is not open (EBADF). Linux truncates fd
 * to an unsigned int (it would be fd 1): a documented deviation, stricter
 * than Linux; real programs pass int descriptors (owner, stage 4, S4-2).
 *
 * The busy mark (Linux's per-file position lock, plan B2): a read or write
 * on a file holds its open file's mark for the whole call, so a second
 * call on the same open file (a dup, or another process sharing it) waits
 * until the first ends and its offset follows the first call's end. Like
 * the VFS lock it is a FIFO ticket mark (the stage 3 lesson: try-and-yield
 * starves): callers hold it in the order they asked, waiting by yielding.
 * Console open files have no offset and no mark.
 */

#include <stdbool.h>
#include <stdint.h>

#define FD_MAX         16u              /* descriptors per process (D8, B3) */
#define FILE_MAX       256u             /* open files system-wide (D8, B3) */
#define FD_NONE        0xFFFFu          /* a descriptor that is not open */
#define FD_RW_MAX      0x7FFFF000ull    /* bytes per read or write (Linux's MAX_RW_COUNT) */
#define FD_CHUNK       4096u            /* bytes per hold of the VFS lock (plan B2) */
#define FD_TICKET_NONE (-1)

enum ofile_kind {
    OFILE_FREE = 0,                     /* a zeroed table is all free */
    OFILE_CONSOLE_IN,
    OFILE_CONSOLE_OUT,
    OFILE_FILE,
};

#define OF_READ  1u
#define OF_WRITE 2u

struct ofile {
    uint8_t kind;                       /* enum ofile_kind */
    uint8_t access;                     /* OF_READ | OF_WRITE */
    uint32_t refs;
    uint64_t offset;                    /* files only */
    void *node;                         /* files only: the struct vnode held */
    /* The busy mark: tickets, its holder, and its figures. */
    uint32_t busy_next, busy_serving;
    int32_t busy_owner;                 /* thread id, or FD_TICKET_NONE */
    uint64_t busy_takes, busy_contended, busy_waits;
    uint32_t busy_ahead_max;            /* most tickets ahead of a caller's (holder included) */
};

struct ofile_table {
    struct ofile f[FILE_MAX];
    uint32_t used;                      /* open files now */
};

struct fd_table {
    uint16_t fd[FD_MAX];                /* an index into the ofile table, or FD_NONE */
};

/* errno values (Linux x86-64), returned as -errno */
#define FDT_EBADF  9
#define FDT_ENFILE 23
#define FDT_EMFILE 24

void ofile_table_init(struct ofile_table *t);

/* A new open file with one reference: 0 and *idx, or -ENFILE (nothing
 * changed). node must be NULL for a console kind. */
int ofile_alloc(struct ofile_table *t, enum ofile_kind kind, uint8_t access, void *node,
                uint32_t *idx);

/* One more reference to open file idx (a descriptor or a kernel hold). */
void ofile_get(struct ofile_table *t, uint32_t idx);

/* One reference less; true if that freed the open file: then *node_out is
 * the node it held (NULL for a console), for the caller to drop. */
bool ofile_put(struct ofile_table *t, uint32_t idx, void **node_out);

void fd_table_init(struct fd_table *d);

/* The open file of descriptor fd: 0 and *idx, or -EBADF (*idx untouched). */
int fd_lookup(const struct fd_table *d, uint64_t fd, uint32_t *idx);

/* 0 if open file idx allows the access (OF_READ or OF_WRITE), else -EBADF. */
int ofile_access(const struct ofile_table *t, uint32_t idx, uint8_t want);

/* Descriptors open in d. */
uint32_t fd_count(const struct fd_table *d);

/* Puts open file idx (one reference already taken for it) at the lowest
 * free descriptor: that number, or -EMFILE (nothing changed). */
int fd_install(struct fd_table *d, uint32_t idx);

/* Puts open file idx at descriptor fd (0..FD_MAX-1), which must be free;
 * one reference already taken. false: fd out of range or open. */
bool fd_install_at(struct fd_table *d, uint64_t fd, uint32_t idx);

/* Closes descriptor fd: 0 or -EBADF. *node_out is the node to drop when
 * that freed a file's open file, else NULL. */
int fd_close(struct fd_table *d, struct ofile_table *t, uint64_t fd, void **node_out);

/* dup: the lowest free descriptor on oldfd's open file, or -EBADF / -EMFILE. */
int fd_dup(struct fd_table *d, struct ofile_table *t, uint64_t oldfd);

/* dup2 (Linux): oldfd not open, or newfd outside 0..FD_MAX-1: -EBADF;
 * oldfd == newfd: newfd, nothing done; newfd open: closed first (its node
 * to drop in *node_out, else NULL). Returns newfd. */
int fd_dup2(struct fd_table *d, struct ofile_table *t, uint64_t oldfd, uint64_t newfd,
            void **node_out);

/* Closes every descriptor; the nodes to drop go into nodes[] (at most
 * FD_MAX). Returns the number of descriptors closed. */
uint32_t fd_close_all(struct fd_table *d, struct ofile_table *t, void *nodes[FD_MAX],
                      uint32_t *nnodes);

/* The bytes one read or write call handles: count clamped to FD_RW_MAX. */
uint64_t fd_rw_count(uint64_t count);

/* The bytes of the next chunk once `done` of `total` are done. */
uint32_t fd_chunk(uint64_t done, uint64_t total);

/*
 * The busy mark of open file idx, for thread tid. ofile_busy_ticket takes
 * the next ticket (the tickets ahead go into busy_ahead_max);
 * ofile_busy_try returns true when that ticket is served (then tid holds
 * the mark; `waited` is the wait steps it made, for the figures);
 * ofile_busy_release serves the next ticket (false: tid did not hold it).
 */
uint32_t ofile_busy_ticket(struct ofile_table *t, uint32_t idx);
bool ofile_busy_try(struct ofile_table *t, uint32_t idx, uint32_t ticket, int32_t tid,
                    uint32_t waited);
bool ofile_busy_release(struct ofile_table *t, uint32_t idx, int32_t tid);

/* Tickets ahead of `ticket` on idx's mark (the holder's included). */
uint32_t ofile_busy_ahead(const struct ofile_table *t, uint32_t idx, uint32_t ticket);

#endif
