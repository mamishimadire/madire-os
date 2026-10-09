#ifndef MADIRE_PROC_SYSCALL_H
#define MADIRE_PROC_SYSCALL_H

/*
 * System calls, the kernel side (0.10). The numbers and rules are in
 * syscall_core.h (pure); the entry is kernel/proc/syscall_entry.asm.
 *
 * Stage 5a: exit (60), exit_group (231), getpid (39) and sched_yield (24).
 * Stage 5b: write (1) and nanosleep (35), whose user pointers are checked
 * (kernel/memory/usercopy.h) before any use: a bad one gives -EFAULT, and
 * a refused write logs nothing.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/arch/x86/interrupts.h"
#include "kernel/proc/syscall_core.h"

#define MSR_STAR   0xC0000081u
#define MSR_LSTAR  0xC0000082u
#define MSR_SFMASK 0xC0000084u
#define EFER_SCE   0x1ull

/* RFLAGS bits cleared on entry: TF, IF, DF, IOPL, NT, AC. */
#define SYSCALL_SFMASK (0x100ull | 0x200ull | 0x400ull | 0x3000ull | 0x4000ull | 0x40000ull)

/* Sets EFER.SCE, STAR, LSTAR and SFMASK, reads them back and logs them.
 * false: a value did not read back as written. */
bool syscall_init(void);

/* syscall_entry.asm, with interrupts off; returns with interrupts off. */
void syscall_dispatch(struct interrupt_frame *frame);

/* Calls since boot, by syscall_core's id (SC_UNKNOWN: every -ENOSYS). */
struct syscall_counts {
    uint64_t by_id[SC_EXIT_GROUP + 1];
    uint64_t enosys;                /* calls answered -ENOSYS */
};
void syscall_counts(struct syscall_counts *out);

/* nanosleep (test=userio): calls, and the last one's ticks asked for and
 * actually slept (timer ticks from before the sleep to after it). */
struct syscall_sleep_stats {
    uint64_t calls;
    uint32_t last_ticks;
    uint64_t last_slept;
};
void syscall_sleep_stats(struct syscall_sleep_stats *out);

#endif
