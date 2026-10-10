#ifndef MADIRE_PROC_PROC_CORE_H
#define MADIRE_PROC_PROC_CORE_H

/*
 * Process table (0.10), pure bookkeeping (tests/unit/test_proc.c). The
 * kernel side (kernel/proc/process.c) owns the address spaces and threads.
 *
 * A process is one user thread in its own address space. Its slot is taken
 * by proc_alloc (ALIVE), ends with proc_exit (ZOMBIE: only its result is
 * kept) and is given back by proc_reap, which hands the result to the
 * waiter. The table is static: no heap, so cleanup can be checked exactly.
 * Process IDs count up from 1 and are not reused within a boot.
 *
 * A kill is only requested here (proc_request_kill); the process ends
 * itself at its next return to user mode, so its address space is always
 * torn down by its own thread.
 */

#include <stdbool.h>
#include <stdint.h>

#define PROC_MAX      63            /* thread slots 1-63; thread 0 is the kernel */
#define PROC_NAME_MAX 16            /* including the terminating 0 */

enum proc_state {
    PROC_FREE,
    PROC_ALIVE,
    PROC_ZOMBIE,
};

enum proc_end {
    PROC_END_EXIT,                  /* exit / exit_group: code = status & 0xFF */
    PROC_END_FAULT,                 /* a CPU exception in user mode: code = vector */
    PROC_END_KILLED,                /* proc_request_kill: code = 0 */
    PROC_END_LOAD_FAILED,           /* 0.11: its file was refused at load (never ran):
                                     * code = the elf_check result */
};

struct proc_result {
    enum proc_end end;
    int32_t code;
};

struct proc_entry {
    enum proc_state state;
    int32_t pid;
    int32_t tid;                    /* its thread, -1 until set */
    char name[PROC_NAME_MAX];
    bool kill_pending;
    struct proc_result result;      /* valid once ZOMBIE */
};

struct proc_table {
    struct proc_entry e[PROC_MAX];
    int32_t next_pid;
    uint32_t live;                  /* ALIVE + ZOMBIE */
};

enum proc_status {
    PROC_OK,
    PROC_ERR_FULL,                  /* no free slot */
    PROC_ERR_PIDS,                  /* process IDs used up */
    PROC_ERR_NO_SUCH,               /* no process with that ID */
    PROC_ERR_NOT_ALIVE,             /* it has already ended */
    PROC_ERR_NOT_EXITED,            /* it is still running */
};

void proc_init(struct proc_table *t);

/* Takes a free slot (the lowest) for `name` (cut to PROC_NAME_MAX - 1);
 * *slot and *pid receive it. */
enum proc_status proc_alloc(struct proc_table *t, const char *name, int *slot, int32_t *pid);

/* The slot of process `pid` (ALIVE or ZOMBIE), or -1. */
int proc_find(const struct proc_table *t, int32_t pid);

void proc_set_tid(struct proc_table *t, int slot, int32_t tid);

enum proc_status proc_request_kill(struct proc_table *t, int32_t pid);
bool proc_kill_pending(const struct proc_table *t, int slot);

/* The process in `slot` has ended: ALIVE -> ZOMBIE with this result (for
 * PROC_END_EXIT the code is stored as given; syscall_exit_status() makes
 * it 0..255). False if the slot was not ALIVE. */
bool proc_exit(struct proc_table *t, int slot, enum proc_end end, int32_t code);

/* Gives back a ZOMBIE's slot and its result. */
enum proc_status proc_reap(struct proc_table *t, int32_t pid, struct proc_result *out);

uint32_t proc_live(const struct proc_table *t);
const char *proc_end_str(enum proc_end end);

#endif
