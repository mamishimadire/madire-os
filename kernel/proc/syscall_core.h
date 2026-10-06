#ifndef MADIRE_PROC_SYSCALL_CORE_H
#define MADIRE_PROC_SYSCALL_CORE_H

/*
 * System calls (0.10): the numbers, the dispatch decision and the argument
 * rules, without hardware access (tests/unit/test_syscall.c). The kernel
 * side (kernel/proc/syscall.c) copies user memory and does the work.
 *
 * ABI: the Linux x86-64 conventions for the subset below, so that Linux
 * programs can be ported later. The number is in RAX, the arguments in RDI,
 * RSI, RDX, R10, R8 and R9, the result in RAX; an error is -errno. RCX and
 * R11 are not preserved (they come back holding the return address and
 * RFLAGS, as SYSRET would leave them); every other register is preserved.
 * Our own table: the numbers and meanings are taken from the documented
 * Linux interface, no Linux header is used.
 *
 *   1    write(fd, buf, count)        fd 1 or 2; at most SYSCALL_WRITE_MAX
 *                                     bytes per call (a short count beyond)
 *   24   sched_yield()                0
 *   35   nanosleep(req, rem)          rounded up to whole timer ticks; rem is
 *                                     never written (nothing interrupts a
 *                                     sleep: there are no signals)
 *   39   getpid()                     the process ID
 *   60   exit(status)                 ends the process; status & 0xFF
 *   231  exit_group(status)           the same (one thread per process)
 *
 * Every other number, compared as the full 64-bit RAX (so the x32 bit
 * 0x40000000, values of 2^32 and above, and -1 are all unknown), returns
 * -ENOSYS. Numbers 0x10000000-0x1000FFFF are reserved for Madire's own
 * calls; none is implemented in 0.10.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYS_WRITE       1u
#define SYS_SCHED_YIELD 24u
#define SYS_NANOSLEEP   35u
#define SYS_GETPID      39u
#define SYS_EXIT        60u
#define SYS_EXIT_GROUP  231u

#define SYS_MADIRE_FIRST 0x10000000ull
#define SYS_MADIRE_LAST  0x1000FFFFull

/* errno values (Linux x86-64); a call returns -errno */
#define MADIRE_EBADF  9
#define MADIRE_EFAULT 14
#define MADIRE_EINVAL 22
#define MADIRE_ENOSYS 38

#define SYSCALL_WRITE_MAX 4096u     /* bytes accepted per write call */
#define SYSCALL_LINE_MAX  100u      /* characters of user text per log line */

enum syscall_id {
    SC_UNKNOWN,                     /* -ENOSYS */
    SC_WRITE,
    SC_SCHED_YIELD,
    SC_NANOSLEEP,
    SC_GETPID,
    SC_EXIT,
    SC_EXIT_GROUP,
};

enum syscall_id syscall_lookup(uint64_t nr);
bool syscall_is_madire(uint64_t nr);       /* in the reserved Madire range */
const char *syscall_name(enum syscall_id id);

/* write: 0 if fd is 1 or 2 (then *accepted = the bytes that will be taken,
 * min(count, SYSCALL_WRITE_MAX)), else -EBADF. */
int64_t syscall_write_check(uint64_t fd, uint64_t count, uint64_t *accepted);

/* nanosleep: a struct timespec {int64 tv_sec; int64 tv_nsec} to timer ticks
 * at `hz`, rounded up (0 stays 0, which is a yield); -EINVAL if tv_sec < 0
 * or tv_nsec is outside 0..999999999. Clamped to UINT32_MAX ticks. */
int64_t syscall_timespec_ticks(int64_t sec, int64_t nsec, uint32_t hz, uint32_t *ticks);

/* exit / exit_group: the status a waiter sees. */
int32_t syscall_exit_status(uint64_t status);

/*
 * write's output, as log lines: each line is "[pid N] " followed by user
 * text with every byte outside 0x20..0x7E replaced by '?' (so a program
 * cannot write control sequences or forge a kernel log line). The text is
 * split at each '\n' (which is not emitted) and after SYSCALL_LINE_MAX
 * characters; text after the last '\n' still ends its own line. emit gets
 * each line without a line ending; returns the number of lines.
 */
typedef void (*syscall_emit_fn)(void *ctx, const char *line, size_t len);
uint32_t syscall_write_lines(int32_t pid, const uint8_t *buf, size_t n,
                             syscall_emit_fn emit, void *ctx);

/* RFLAGS for the return to user mode: the user-visible flags (CF, PF, AF,
 * ZF, SF, TF, DF, OF, AC, ID) kept, IOPL, NT, RF, VM, VIF and VIP cleared,
 * IF and the reserved bit 1 set. */
uint64_t syscall_user_rflags(uint64_t rflags);

#endif
