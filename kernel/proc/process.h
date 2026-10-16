#ifndef MADIRE_PROC_PROCESS_H
#define MADIRE_PROC_PROCESS_H

/*
 * User processes (0.10). A process is one thread running a built-in image
 * in ring 3, in its own address space. The thread builds the address space
 * itself, enters ring 3 (stage 4a), and at the end tears the address space
 * down itself and exits (stage 4b): the process table (proc_core.h) keeps
 * the result until process_wait collects it.
 *
 * A kill is only requested (process_kill); the process ends itself at its
 * next return to ring 3 (process_return_to_user, called at the end of
 * every interrupt that interrupted ring 3), or before it first enters
 * ring 3. Exit through a system call and faults come in later stages.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/arch/x86/interrupts.h"
#include "kernel/memory/uspace.h"
#include "kernel/proc/faultop.h"
#include "kernel/proc/loadplan.h"
#include "kernel/proc/proc_core.h"
#include "kernel/proc/userentry.h"
#include "kernel/proc/userimg.h"

/* The built-in images (build/user/images.asm, scripts/user-images.sh). */
extern const struct userimg user_images[];
extern const uint64_t user_images_count;

/* Pages a load mapped, by kind. */
struct process_load_stats {
    uint32_t text, data, stack;
    uint32_t in_modules;            /* 0.11: frames inside a boot module (must stay 0) */
};

/* Checks the image (userimg_check) and maps it into the address space,
 * which the calling thread must have entered: its code read + execute at
 * VM_USER_TEXT, its data and bss read + write + no-execute at VM_USER_DATA,
 * and a stack (read + write + no-execute) ending at VM_USER_STACK_TOP,
 * zeroed but for (1.0 stage 7) the default start block in its top page:
 * argv = { the image's name }, no environment (startblock_default_rsp).
 * Every frame is filled before it is mapped. false: a bad image or no
 * memory (what was mapped stays for uspace_destroy). */
bool process_load(struct uspace *as, const struct userimg *img, struct process_load_stats *st);

/* 0.11: the one loading path. process_load turns the image into a plan and
 * calls this; ELF programs come here too. Every page is a fresh frame
 * filled from the plan's bytes (never a page of the source); the stack is
 * added as before, its top page filled from top_page (1.0 stage 7: the
 * start block's STARTBLOCK_SIZE bytes; NULL: zeros). */
bool process_load_plan(struct uspace *as, const struct load_plan *plan, const uint8_t *top_page,
                       struct process_load_stats *st);

/* 0.11: what every load added up to since boot (test=elfrun). */
struct process_load_totals {
    uint32_t loads, pages, in_modules;
};
void process_load_totals(struct process_load_totals *out);

/* userenter.asm: starts the program in ring 3, every general register 0
 * (1.0 stage 7; RSP from the frame: the start block). */
void enter_user(const struct user_iret *frame) __attribute__((noreturn));

/* Starts the built-in image `name` as a new process. 1.0 stage 7: the
 * number reaches it as its one argument, argv = { name, "<arg>" } in
 * decimal (until then RDI = arg), with no environment. Returns its process
 * ID (> 0), or -1: no such image, the process table is full, no memory for
 * the start block, or no thread could be created. */
int32_t process_start(const char *name, uint64_t arg);

/* 1.0 stage 7: what a start returns when the start block (loadplan.h)
 * would not fit its 4096 bytes: -E2BIG; nothing was created. */
#define PROCESS_START_E2BIG (-STARTBLOCK_E2BIG)

/* 1.0 stage 7: the System V start with any arguments and environment: the
 * program module `name` (module) or the built-in image `name`, with
 * argv[0..argc-1] and the strings env_names[i] '=' env_values[i] as its
 * environment. Returns the process ID, -1 as process_start (or no such
 * module), or PROCESS_START_E2BIG. */
int32_t process_start_argv(const char *name, bool module, uint32_t argc, const char *const *argv,
                           uint32_t envc, const char *const *env_names,
                           const char *const *env_values);

/* 1.0 stage 8 (S6-5): process_start_argv for a program that starts in
 * folder cwd (its relative paths start there): the caller holds cwd (taken
 * under the VFS lock, vfs_hold) and hands that hold over to the process
 * only when this succeeds (a process ID > 0; its teardown drops it); on
 * any failure the hold is still the caller's. NULL is the root (no hold).
 * process_start_argv starts at the root. */
struct vnode;
int32_t process_start_argv_in(const char *name, bool module, uint32_t argc,
                              const char *const *argv, uint32_t envc,
                              const char *const *env_names, const char *const *env_values,
                              struct vnode *cwd);

/* 1.0 stage 7: start blocks allocated and not yet freed (each is freed
 * once its program is loaded, or when the process ends before that). */
uint32_t process_start_blocks_live(void);

/* 0.11: a process for an ELF file in kernel-reachable memory (a module,
 * through the physical view), argv as process_start's. The file is checked (again) by the
 * process's own thread before anything is mapped; a refused file ends the
 * process with PROC_END_LOAD_FAILED (code = the elf_check result) before
 * it has an address space. Returns the process ID or -1, as above. */
int32_t process_start_file(const char *name, const uint8_t *file, uint64_t size, uint64_t arg);

/* 0.11: the program module `name` (progmod_table), as above; -1 if no
 * such program was kept at boot. */
int32_t process_start_program(const char *name, uint64_t arg);

/* 1.0 stage 4: the built-in image `name`, as process_start, with more
 * descriptors given at its start (kernel/proc/fd.h): each grant's open
 * file at its number, replacing a console descriptor there. Kernel only:
 * no system call and no console command reaches it (the diagnostics use
 * it; a shell redirection may later). The caller keeps its own reference
 * to each granted file until this returns. -1 as process_start, or for a
 * bad grant or no open file left for the console descriptors. */
struct fd_grant;
int32_t process_start_fds(const char *name, uint64_t arg, const struct fd_grant *grants,
                          uint32_t ngrants);

/* Requests the end of process `pid`; it ends at its next return to ring 3
 * (result PROC_END_KILLED). */
enum proc_status process_kill(int32_t pid);

/* Waits for process `pid` to end (joins its thread), then gives back its
 * table slot and returns its result in *out. */
enum proc_status process_wait(int32_t pid, struct proc_result *out);

/* interrupts.c: an interrupt that interrupted ring 3 is about to return
 * there (EOI sent, preemption point passed). Ends the process if a kill is
 * pending; never returns in that case. */
void process_return_to_user(const struct interrupt_frame *frame);

/* interrupts.c: a CPU exception taken in ring 3. If the running thread is
 * a process, the process ends (result PROC_END_FAULT, code = the vector)
 * with a one-line message, and the kernel continues: never returns then.
 * Returns (and the caller panics, as before) for any other thread, and for
 * NMI, double fault and machine check, which are not the program's doing. */
void process_fault(const struct interrupt_frame *frame);

/* The last exception that ended a process (test=userfault). */
struct process_fault {
    int32_t pid;                    /* 0: none yet */
    uint32_t vector;
    uint64_t error_code, rip, cr2;  /* cr2: page faults only */
    struct faultop op;              /* the bytes at RIP (copy_from_user), or unreadable */
};
void process_last_fault(struct process_fault *out);

/* System calls (0.10, kernel/proc/syscall.c), from the process's own
 * thread: its process ID (panics if the thread is not a process), and
 * exit / exit_group (result PROC_END_EXIT with this status; never returns). */
int32_t process_current_pid(void);
/* True when the calling process has a pending kill request. */
bool process_current_kill_pending(void);
void process_exit_current(int32_t status) __attribute__((noreturn));

/* 1.0 stage 4: the calling process's descriptors (panics if the thread
 * is not a process), and the descriptors open in process `pid` (-1: no
 * such process). */
struct fd_table *process_current_fds(void);
int32_t process_open_fds(int32_t pid);

/* The thread of process `pid` (alive or not yet waited for), or -1. */
int32_t process_tid(int32_t pid);

/* Processes alive or not yet waited for. */
uint32_t process_live(void);

/* 0.11: one row of the process table, for the console's "ps" and "run". */
struct process_info {
    int32_t pid;
    int32_t tid;
    bool ended;                     /* ended, its result not yet collected */
    char name[PROC_NAME_MAX];
};

/* Copies at most `max` rows (alive or ended, in table order); returns how
 * many. */
uint32_t process_list(struct process_info *out, uint32_t max);

/* True once process `pid` has ended (its result waits for process_wait). */
bool process_ended(int32_t pid);

/* The address an exception ended `pid` at: CR2 for a page fault, RIP for
 * any other; 0 if it did not end by an exception. Valid until its result
 * is collected. */
uint64_t process_fault_address(int32_t pid);

/* What the processes' own teardowns gave back since boot (test=userkill). */
struct process_teardown_totals {
    uint32_t processes;             /* address spaces torn down */
    uint32_t pages, tables, bad_entries;
};
void process_teardown_totals(struct process_teardown_totals *out);

/* Interrupts from ring 3 after which `pid`'s thread returned to ring 3
 * (0 if unknown); the slot's count is kept until it is reaped. */
uint64_t process_user_returns(int32_t pid);

#endif
