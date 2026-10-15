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
 *   0    read(fd, buf, count)         1.0 stage 4: through the descriptor table
 *                                     (kernel/proc/fd.h): fd 0 (the console)
 *                                     returns 0; a file the full count
 *   1    write(fd, buf, count)        through the descriptor table (1.0 stage
 *                                     4): the console (fds 1 and 2 at start)
 *                                     at most SYSCALL_WRITE_MAX bytes per call
 *                                     (a short count beyond); a file the full
 *                                     count
 *   2    open(path, flags, mode)      1.0 stage 5 (kernel/fs/fsys.c): openat(AT_FDCWD, ...)
 *   3    close(fd)                    1.0 stage 4
 *   4    stat(path, st)               1.0 stage 5
 *   5    fstat(fd, st)                1.0 stage 5
 *   6    lstat(path, st)              1.0 stage 5: as stat (no symbolic links)
 *   8    lseek(fd, off, whence)       1.0 stage 5
 *   24   sched_yield()                0
 *   35   nanosleep(req, rem)          rounded up to whole timer ticks; rem is
 *                                     never written (nothing interrupts a
 *                                     sleep: there are no signals)
 *   32   dup(oldfd)                   1.0 stage 4: the lowest free descriptor
 *   33   dup2(oldfd, newfd)           1.0 stage 4
 *   39   getpid()                     the process ID
 *   60   exit(status)                 ends the process; status & 0xFF
 *   77   ftruncate(fd, length)        1.0 stage 5
 *   231  exit_group(status)           the same (one thread per process)
 *   257  openat(dirfd, path, flags, mode)        1.0 stage 5
 *   262  newfstatat(dirfd, path, st, flags)      1.0 stage 5
 *
 * Every other number, compared as the full 64-bit RAX (so the x32 bit
 * 0x40000000, values of 2^32 and above, and -1 are all unknown), returns
 * -ENOSYS. Numbers 0x10000000-0x1000FFFF are reserved for Madire's own
 * calls; none is implemented in 0.10.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYS_READ        0u
#define SYS_WRITE       1u
#define SYS_CLOSE       3u
#define SYS_DUP         32u
#define SYS_DUP2        33u
#define SYS_SCHED_YIELD 24u
#define SYS_NANOSLEEP   35u
#define SYS_GETPID      39u
#define SYS_EXIT        60u
#define SYS_EXIT_GROUP  231u
#define SYS_OPEN        2u              /* 1.0 stage 5 */
#define SYS_STAT        4u
#define SYS_FSTAT       5u
#define SYS_LSTAT       6u
#define SYS_LSEEK       8u
#define SYS_FTRUNCATE   77u
#define SYS_OPENAT      257u
#define SYS_NEWFSTATAT  262u

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
    SC_READ,                        /* 1.0 stage 4 */
    SC_CLOSE,
    SC_DUP,
    SC_DUP2,
    SC_OPEN,                        /* 1.0 stage 5 */
    SC_STAT,
    SC_FSTAT,
    SC_LSTAT,
    SC_LSEEK,
    SC_FTRUNCATE,
    SC_OPENAT,
    SC_NEWFSTATAT,
    SC_COUNT                        /* the number of ids */
};

enum syscall_id syscall_lookup(uint64_t nr);
bool syscall_is_madire(uint64_t nr);       /* in the reserved Madire range */
const char *syscall_name(enum syscall_id id);

/* write to the console: the bytes taken, min(count, SYSCALL_WRITE_MAX).
 * (1.0 stage 4: which descriptors are open, and for writing, is decided by
 * the descriptor table, kernel/proc/fdtable_core.h.) */
uint64_t syscall_console_count(uint64_t count);

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
