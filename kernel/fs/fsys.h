#ifndef MADIRE_FS_FSYS_H
#define MADIRE_FS_FSYS_H

/*
 * The file system calls (1.0, stage 5), the kernel side: open (2), stat
 * (4), fstat (5), lstat (6), lseek (8), ftruncate (77), openat (257) and
 * newfstatat (262). The decisions and layouts are the pure fsys_core.c;
 * descriptors and open files are kernel/proc/fd.c.
 *
 * A path is copied in from user memory first, page by page up to its NUL
 * (at most PATH_MAX bytes: -ENAMETOOLONG), into a buffer on the calling
 * thread's kernel stack; the stat structure is copied out last. Neither
 * copy happens while the VFS lock is held. One path lookup per hold.
 *
 * The start folder of a relative path: an *at call's dirfd (a folder's
 * open file: -EBADF if not open, -ENOTDIR if not a folder), or for
 * AT_FDCWD and the path calls the root folder "/": there is no current
 * folder until stage 6 (chdir). An absolute path ignores dirfd. dirfd is
 * compared as a full 64-bit value: only the sign-extended -100 is
 * AT_FDCWD (S4-2, F17).
 */

#include <stdint.h>

int64_t fsys_sys_openat(uint64_t dirfd, uint64_t path, uint64_t flags, uint64_t mode);
int64_t fsys_sys_fstatat(uint64_t dirfd, uint64_t path, uint64_t st, uint64_t flags);
int64_t fsys_sys_fstat(uint64_t fd, uint64_t st);
int64_t fsys_sys_lseek(uint64_t fd, uint64_t off, uint64_t whence);
int64_t fsys_sys_ftruncate(uint64_t fd, uint64_t length);

#endif
