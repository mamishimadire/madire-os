#ifndef MADIRE_FS_FSYS_H
#define MADIRE_FS_FSYS_H

/*
 * The file system calls (1.0, stages 5 and 6), the kernel side: open (2), stat
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
 * AT_FDCWD and the path calls the process's current folder (stage 6:
 * struct fd_table's cwd; "/" at a program's start). An absolute path
 * ignores dirfd. dirfd is compared as a full 64-bit value: only the
 * sign-extended -100 is AT_FDCWD (S4-2, F17).
 *
 * 1.0 stage 6, the folder calls: getcwd (79), chdir (80), rename (82),
 * mkdir (83), rmdir (84), unlink (87), getdents64 (217), mkdirat (258)
 * and unlinkat (263); mkdir, rmdir and unlink are mkdirat and unlinkat
 * with AT_FDCWD (one function each, plan B1). Each takes the VFS lock
 * once (rename: both paths in one hold); a file that unlink or rename
 * replaces is held across the call and its last hold frees its pages in
 * steps (fd_node_drop, S6-6 (a)). getcwd returns the length with the NUL
 * (the raw system call): -ENOENT if the current folder was removed,
 * -ENAMETOOLONG for a path over 4096 bytes with its NUL (S6-2), -ERANGE
 * when size is smaller (also 0: the system call's answer, S6-1). A
 * removed current folder's ".." is -ENOENT (stage 1's lookup; Linux
 * follows the old parent: a deviation, S6-4).
 */

#include <stdint.h>

int64_t fsys_sys_openat(uint64_t dirfd, uint64_t path, uint64_t flags, uint64_t mode);
int64_t fsys_sys_fstatat(uint64_t dirfd, uint64_t path, uint64_t st, uint64_t flags);
int64_t fsys_sys_fstat(uint64_t fd, uint64_t st);
int64_t fsys_sys_lseek(uint64_t fd, uint64_t off, uint64_t whence);
int64_t fsys_sys_ftruncate(uint64_t fd, uint64_t length);

/* 1.0 stage 6 */
int64_t fsys_sys_mkdirat(uint64_t dirfd, uint64_t path, uint64_t mode);
int64_t fsys_sys_unlinkat(uint64_t dirfd, uint64_t path, uint64_t flags);
int64_t fsys_sys_rename(uint64_t oldpath, uint64_t newpath);
int64_t fsys_sys_chdir(uint64_t path);
int64_t fsys_sys_getcwd(uint64_t buf, uint64_t size);
int64_t fsys_sys_getdents64(uint64_t fd, uint64_t dirp, uint64_t count);

#endif
