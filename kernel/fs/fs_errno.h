#ifndef MADIRE_FS_FS_ERRNO_H
#define MADIRE_FS_FS_ERRNO_H

/*
 * The errno values the filesystem answers with (1.0): Linux's numbers
 * (asm-generic, the same on x86-64), from the documented interface. Every
 * filesystem call returns 0 (or a count) on success and -FS_E... on error,
 * as the system calls will (stages 4-6). The FS_ prefix keeps them apart
 * from a host's <errno.h> in the unit tests.
 */

#define FS_ENOENT        2
#define FS_EBADF         9
#define FS_ENOMEM       12
#define FS_EFAULT       14
#define FS_EBUSY        16
#define FS_EEXIST       17
#define FS_ENOTDIR      20
#define FS_EISDIR       21
#define FS_EINVAL       22
#define FS_ENFILE       23
#define FS_EMFILE       24
#define FS_EFBIG        27
#define FS_ENOSPC       28
#define FS_ESPIPE       29
#define FS_ERANGE       34
#define FS_ENAMETOOLONG 36
#define FS_ENOSYS       38
#define FS_ENOTEMPTY    39

/* Its name ("ENOENT"), or "E?" for any other value (positive or negative). */
const char *fs_errno_name(int err);

#endif
