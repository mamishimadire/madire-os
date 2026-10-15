#ifndef MADIRE_USER_SYSCALL_H
#define MADIRE_USER_SYSCALL_H

/*
 * System-call wrappers for the built-in user programs (0.10). Linux x86-64
 * conventions (kernel/proc/syscall_core.h): number in RAX, arguments in
 * RDI, RSI, RDX, R10, R8, R9, result in RAX (-errno on error); SYSCALL
 * overwrites RCX and R11.
 */

#include <stdint.h>

#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_CLOSE       3
#define SYS_DUP         32
#define SYS_DUP2        33
#define SYS_SCHED_YIELD 24
#define SYS_NANOSLEEP   35
#define SYS_GETPID      39
#define SYS_EXIT        60
#define SYS_EXIT_GROUP  231

#define EBADF  9
#define EFAULT 14
#define EISDIR 21
#define EINVAL 22
#define EMFILE 24
#define ENOSYS 38

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static inline long syscall6(uint64_t n, uint64_t a, uint64_t b, uint64_t c,
                            uint64_t d, uint64_t e, uint64_t f)
{
    long r;
    register uint64_t r10 __asm__("r10") = d;
    register uint64_t r8 __asm__("r8") = e;
    register uint64_t r9 __asm__("r9") = f;
    __asm__ volatile ("syscall"
                      : "=a"(r)
                      : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                      : "rcx", "r11", "memory");
    return r;
}

static inline long sys_write(uint64_t fd, const void *buf, uint64_t count)
{
    return syscall6(SYS_WRITE, fd, (uint64_t)(uintptr_t)buf, count, 0, 0, 0);
}

/* 1.0 stage 4 */
static inline long sys_read(uint64_t fd, void *buf, uint64_t count)
{
    return syscall6(SYS_READ, fd, (uint64_t)(uintptr_t)buf, count, 0, 0, 0);
}

static inline long sys_close(uint64_t fd)
{
    return syscall6(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
}

static inline long sys_dup(uint64_t oldfd)
{
    return syscall6(SYS_DUP, oldfd, 0, 0, 0, 0, 0);
}

static inline long sys_dup2(uint64_t oldfd, uint64_t newfd)
{
    return syscall6(SYS_DUP2, oldfd, newfd, 0, 0, 0, 0);
}

static inline long sys_sched_yield(void)
{
    return syscall6(SYS_SCHED_YIELD, 0, 0, 0, 0, 0, 0);
}

static inline long sys_nanosleep(const struct timespec *req, struct timespec *rem)
{
    return syscall6(SYS_NANOSLEEP, (uint64_t)(uintptr_t)req, (uint64_t)(uintptr_t)rem, 0, 0, 0, 0);
}

static inline long sys_getpid(void)
{
    return syscall6(SYS_GETPID, 0, 0, 0, 0, 0, 0);
}

static inline __attribute__((noreturn)) void sys_exit(uint64_t status)
{
    syscall6(SYS_EXIT, status, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

static inline __attribute__((noreturn)) void sys_exit_group(uint64_t status)
{
    syscall6(SYS_EXIT_GROUP, status, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

#endif
