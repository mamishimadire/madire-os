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
#include "kernel/proc/proc_core.h"
#include "kernel/proc/userentry.h"
#include "kernel/proc/userimg.h"

/* The built-in images (build/user/images.asm, scripts/user-images.sh). */
extern const struct userimg user_images[];
extern const uint64_t user_images_count;

/* Pages a load mapped, by kind. */
struct process_load_stats {
    uint32_t text, data, stack;
};

/* Checks the image (userimg_check) and maps it into the address space,
 * which the calling thread must have entered: its code read + execute at
 * VM_USER_TEXT, its data and bss read + write + no-execute at VM_USER_DATA,
 * and a zeroed stack (read + write + no-execute) ending at
 * VM_USER_STACK_TOP. Every frame is filled before it is mapped. false: a
 * bad image or no memory (what was mapped stays for uspace_destroy). */
bool process_load(struct uspace *as, const struct userimg *img, struct process_load_stats *st);

/* userenter.asm: starts the program in ring 3 with RDI = arg. */
void enter_user(const struct user_iret *frame, uint64_t arg) __attribute__((noreturn));

/* Starts the built-in image `name` as a new process with RDI = arg.
 * Returns its process ID (> 0), or -1: no such image, the process table
 * is full, or no thread could be created. */
int32_t process_start(const char *name, uint64_t arg);

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
void process_exit_current(int32_t status) __attribute__((noreturn));

/* The thread of process `pid` (alive or not yet waited for), or -1. */
int32_t process_tid(int32_t pid);

/* Processes alive or not yet waited for. */
uint32_t process_live(void);

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
