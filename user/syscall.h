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

#define SYS_OPEN        2               /* 1.0 stage 5 */
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSTAT       6
#define SYS_LSEEK       8
#define SYS_FTRUNCATE   77
#define SYS_OPENAT      257
#define SYS_NEWFSTATAT  262

#define SYS_GETCWD      79              /* 1.0 stage 6 */
#define SYS_CHDIR       80
#define SYS_RENAME      82
#define SYS_MKDIR       83
#define SYS_RMDIR       84
#define SYS_UNLINK      87
#define SYS_GETDENTS64  217
#define SYS_MKDIRAT     258
#define SYS_UNLINKAT    263

#define ENOENT       2
#define EBADF        9
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define ENFILE       23
#define EMFILE       24
#define EFBIG        27
#define ENOSPC       28
#define ESPIPE       29
#define ERANGE       34
#define ENAMETOOLONG 36
#define ENOSYS       38
#define ENOTEMPTY    39

/* 1.0 stage 5: open flags, *at flags, lseek, st_mode (x86-64) */
#define O_RDONLY    0x0
#define O_WRONLY    0x1
#define O_RDWR      0x2
#define O_CREAT     0x40
#define O_EXCL      0x80
#define O_TRUNC     0x200
#define O_APPEND    0x400
#define O_DIRECTORY 0x10000
#define O_CLOEXEC   0x80000
#define AT_FDCWD            (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR        0x200
#define AT_NO_AUTOMOUNT     0x800
#define AT_EMPTY_PATH       0x1000
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define S_IFMT  0170000u
#define S_IFDIR 0040000u
#define S_IFCHR 0020000u
#define S_IFREG 0100000u
#define DT_DIR 4                        /* 1.0 stage 6: getdents64's d_type */
#define DT_REG 8

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

/* 1.0 stage 5: struct stat (x86-64), 144 bytes */
struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime_sec, st_atime_nsec;
    int64_t st_mtime_sec, st_mtime_nsec;
    int64_t st_ctime_sec, st_ctime_nsec;
    int64_t reserved[3];
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

/* 1.0 stage 5 */
static inline long sys_open(const char *path, uint64_t flags, uint64_t mode)
{
    return syscall6(SYS_OPEN, (uint64_t)(uintptr_t)path, flags, mode, 0, 0, 0);
}

static inline long sys_openat(uint64_t dirfd, const char *path, uint64_t flags, uint64_t mode)
{
    return syscall6(SYS_OPENAT, dirfd, (uint64_t)(uintptr_t)path, flags, mode, 0, 0);
}

static inline long sys_stat(const char *path, struct stat *st)
{
    return syscall6(SYS_STAT, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)st, 0, 0, 0, 0);
}

static inline long sys_lstat(const char *path, struct stat *st)
{
    return syscall6(SYS_LSTAT, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)st, 0, 0, 0, 0);
}

static inline long sys_fstat(uint64_t fd, struct stat *st)
{
    return syscall6(SYS_FSTAT, fd, (uint64_t)(uintptr_t)st, 0, 0, 0, 0);
}

static inline long sys_newfstatat(uint64_t dirfd, const char *path, struct stat *st,
                                  uint64_t flags)
{
    return syscall6(SYS_NEWFSTATAT, dirfd, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)st,
                    flags, 0, 0);
}

static inline long sys_lseek(uint64_t fd, int64_t off, uint64_t whence)
{
    return syscall6(SYS_LSEEK, fd, (uint64_t)off, whence, 0, 0, 0);
}

static inline long sys_ftruncate(uint64_t fd, int64_t length)
{
    return syscall6(SYS_FTRUNCATE, fd, (uint64_t)length, 0, 0, 0, 0);
}

/* 1.0 stage 6 */
static inline long sys_getcwd(char *buf, uint64_t size)
{
    return syscall6(SYS_GETCWD, (uint64_t)(uintptr_t)buf, size, 0, 0, 0, 0);
}

static inline long sys_chdir(const char *path)
{
    return syscall6(SYS_CHDIR, (uint64_t)(uintptr_t)path, 0, 0, 0, 0, 0);
}

static inline long sys_rename(const char *oldpath, const char *newpath)
{
    return syscall6(SYS_RENAME, (uint64_t)(uintptr_t)oldpath, (uint64_t)(uintptr_t)newpath, 0, 0,
                    0, 0);
}

static inline long sys_mkdir(const char *path, uint64_t mode)
{
    return syscall6(SYS_MKDIR, (uint64_t)(uintptr_t)path, mode, 0, 0, 0, 0);
}

static inline long sys_rmdir(const char *path)
{
    return syscall6(SYS_RMDIR, (uint64_t)(uintptr_t)path, 0, 0, 0, 0, 0);
}

static inline long sys_unlink(const char *path)
{
    return syscall6(SYS_UNLINK, (uint64_t)(uintptr_t)path, 0, 0, 0, 0, 0);
}

static inline long sys_getdents64(uint64_t fd, void *dirp, uint64_t count)
{
    return syscall6(SYS_GETDENTS64, fd, (uint64_t)(uintptr_t)dirp, count, 0, 0, 0);
}

static inline long sys_mkdirat(uint64_t dirfd, const char *path, uint64_t mode)
{
    return syscall6(SYS_MKDIRAT, dirfd, (uint64_t)(uintptr_t)path, mode, 0, 0, 0);
}

static inline long sys_unlinkat(uint64_t dirfd, const char *path, uint64_t flags)
{
    return syscall6(SYS_UNLINKAT, dirfd, (uint64_t)(uintptr_t)path, flags, 0, 0, 0);
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
