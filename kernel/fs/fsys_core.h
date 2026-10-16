#ifndef MADIRE_FS_FSYS_CORE_H
#define MADIRE_FS_FSYS_CORE_H

/*
 * The file system calls (1.0, stage 5), the pure part: open's flags and
 * decisions, the stat structure, lseek's arithmetic, newfstatat's flags,
 * the clock's epoch seconds, the node write mark and the user-buffer
 * prefix arithmetic. No locking, printing or hardware access here (host
 * unit tests: tests/unit/test_syscall_fs.c); the kernel side is
 * kernel/fs/fsys.c and kernel/proc/fd.c.
 *
 * Numbers, flags and layouts are Linux x86-64's, from the documented
 * interface (docs/plans/1.0.md, B1); no Linux header or code is used.
 * Arguments that Linux declares as int or unsigned int (open's flags,
 * newfstatat's flags, lseek's whence) are read from their low 32 bits, as
 * Linux does. Descriptors (and dirfd) are compared as full 64-bit values
 * (S4-2, F17): only the sign-extended -100 is AT_FDCWD.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/fs/vfs_core.h"
#include "kernel/rtc_time.h"

/* open flags (x86-64) */
#define FS_O_ACCMODE   0x3u
#define FS_O_WRONLY    0x1u
#define FS_O_RDWR      0x2u
#define FS_O_CREAT     0x40u
#define FS_O_EXCL      0x80u
#define FS_O_TRUNC     0x200u
#define FS_O_APPEND    0x400u
#define FS_O_DIRECTORY 0x10000u
#define FS_O_CLOEXEC   0x80000u

/* *at calls */
#define FS_AT_FDCWD             ((uint64_t)(int64_t)-100)
#define FS_AT_SYMLINK_NOFOLLOW  0x100u
#define FS_AT_NO_AUTOMOUNT      0x800u
#define FS_AT_EMPTY_PATH        0x1000u

/* lseek */
#define FS_SEEK_SET 0u
#define FS_SEEK_CUR 1u
#define FS_SEEK_END 2u

/* st_mode */
#define FS_S_IFMT   0170000u
#define FS_S_IFDIR  0040000u
#define FS_S_IFCHR  0020000u
#define FS_S_IFREG  0100000u

#define FSYS_DEV          1u            /* st_dev of the RAM filesystem (major 0, minor 1) */
#define FSYS_BLKSIZE      4096
#define FSYS_CONSOLE_RDEV 0x0501u       /* the console's st_rdev: 5:1 (/dev/console) */
#define FSYS_CONSOLE_MODE 0620u
#define FSYS_TRUNC_STEP   64u           /* pages given back per VFS-lock hold (K7 (b)) */

/* struct stat (x86-64), 144 bytes */
struct fs_stat {
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
    int64_t st_blocks;                  /* 512-byte units */
    int64_t st_atime_sec, st_atime_nsec;
    int64_t st_mtime_sec, st_mtime_nsec;
    int64_t st_ctime_sec, st_ctime_nsec;
    int64_t reserved[3];
};

_Static_assert(sizeof(struct fs_stat) == 144, "struct stat is 144 bytes on x86-64");

/* open's flags, decoded. */
struct fsys_open {
    uint8_t access;                     /* OF_READ and/or OF_WRITE (fdtable_core.h) */
    bool create, excl, trunc, append, directory, cloexec;
};

/* 0, or -FS_EINVAL for access mode 3 (plan B1). Unknown bits are ignored. */
int fsys_open_flags(uint64_t flags, struct fsys_open *o);

/* The open file's access bits: OF_READ / OF_WRITE, with OF_APPEND for
 * O_APPEND (fdtable_core.h). */
uint8_t fsys_open_access(const struct fsys_open *o);

/* What open does with the node a lookup found, of type `type` (an error
 * when it may not be opened so): 0 or -FS_ENOTDIR (O_DIRECTORY on a file),
 * -FS_EISDIR (a folder for writing, or with O_TRUNC). */
int fsys_open_existing(const struct fsys_open *o, enum vnode_type type);

/* True if the path ends in '/' after at least one other byte that is not
 * '/' ("a/", "/tmp/"; not "/" or "///"). */
bool fsys_trailing_slash(const char *path);

/* True if the path starts with '/'. */
bool fsys_absolute(const char *path);

/* What stat reports about a node or the console. */
enum fsys_kind { FSYS_CONSOLE = 0, FSYS_FILE = 1, FSYS_DIR = 2 };
struct fsys_facts {
    enum fsys_kind kind;
    uint64_t ino, size;
    uint32_t mode;                      /* the permission bits stored */
    uint32_t nlink;                     /* 1 with a name, 0 once removed */
    uint32_t subdirs;                   /* folders: its subfolders */
    uint32_t pages;                     /* files: its pages, the index page included */
    int64_t atime, mtime, ctime;
};

/* The 144-byte structure for those facts: dev 1, uid and gid 0, blksize
 * 4096, blocks = pages x 8; a file's links 1 (0 once removed), a folder's
 * 2 + its subfolders (0 once removed), size 0 for a folder; the console a
 * character device (mode 0620, rdev 5:1, node 0, size 0). Nanoseconds 0. */
void fsys_stat_fill(struct fs_stat *st, const struct fsys_facts *f);

/* lseek: the new offset from the current one and the size, or
 * -FS_EINVAL (whence not 0, 1 or 2 in its low 32 bits; a negative result;
 * an overflow past INT64_MAX). */
int64_t fsys_lseek(uint64_t cur, uint64_t size, int64_t off, uint64_t whence);

/* newfstatat's flags (low 32 bits): 0 and *empty_path, or -FS_EINVAL for
 * any bit but AT_SYMLINK_NOFOLLOW, AT_NO_AUTOMOUNT and AT_EMPTY_PATH. */
int fsys_at_flags(uint64_t flags, bool *empty_path);

/* Seconds since 1970-01-01 00:00 of a decoded RTC date (no time zone). */
int64_t fsys_epoch(const struct rtc_time *t);

/* The bytes from addr to the end of its page, at most left. */
uint64_t fsys_page_step(uint64_t addr, uint64_t left);

/*
 * The node write mark (plan B2: Linux's per-inode write lock): a FIFO
 * ticket mark in the node (vfs_core.h), held by a write or a truncate for
 * its whole call, so two writes to one file never interleave chunk by
 * chunk. Waiters yield (kernel/proc/fd.c). It lives in what was the
 * node's padding: 16-bit tickets, compared by equality, with distances
 * as 16-bit differences, so the wrap from 65535 to 0 is harmless (each
 * thread holds at most one ticket, far fewer than 65536). wr_holder is
 * the holder's thread id + 1 (0: free), so a zeroed node has a free mark.
 * Its figures (takes, waits, most tickets ahead) are kept by fd.c.
 */
uint16_t fsys_wmark_ticket(struct vnode *n, uint32_t *ahead);   /* ahead: holder included */
bool fsys_wmark_try(struct vnode *n, uint16_t ticket, int32_t tid);
bool fsys_wmark_release(struct vnode *n, int32_t tid);
int32_t fsys_wmark_holder(const struct vnode *n);              /* thread id, or -1 */
uint32_t fsys_wmark_waiting(const struct vnode *n);            /* tickets out, the holder's included */

/*
 * 1.0 stage 6: the folder calls' pure part (tests/unit/test_syscall_dir.c).
 */

#define FS_AT_REMOVEDIR 0x200u
#define FS_DT_DIR       4u
#define FS_DT_REG       8u
#define FSYS_DIRENT_HEAD 19u            /* d_ino 8, d_off 8, d_reclen 2, d_type 1 */

/* unlinkat's flags (int, the low 32 bits): 0 and *removedir, or -FS_EINVAL
 * for any bit but AT_REMOVEDIR (Linux). */
int fsys_unlinkat_flags(uint64_t flags, bool *removedir);

/* The mode bits mkdir stores: the permission bits and the sticky bit
 * (Linux keeps mode & 01777 for a folder; no umask in 1.0). */
uint32_t fsys_mkdir_mode(uint64_t mode);

/* A struct linux_dirent64 record's length for a name of len bytes: the
 * 19-byte head, the name, its NUL, rounded up to 8. */
uint32_t fsys_dirent_reclen(size_t len);

/* Writes one record at buf (room bytes free): its length, or 0 when it
 * does not fit (nothing written). The bytes after the NUL up to the
 * record's end are zero. */
uint32_t fsys_dirent_put(uint8_t *buf, uint32_t room, uint64_t ino, uint64_t off, uint8_t type,
                         const char *name, size_t len);

/*
 * getdents64's offsets on a folder (stable while entries come and go):
 * 0 is ".", 1 is "..", and 2 + s is the folder's first entry whose stamp
 * (ramfs_seq) is at least s. A record's d_off is the offset after it: 1
 * for ".", 2 for "..", stamp + 3 for an entry. An entry removed before it
 * is read is never returned; one added during a listing is returned once,
 * at the end (it takes the highest stamp); one renamed during a listing
 * gets a new stamp, so it may be returned again (POSIX leaves a rename
 * during a listing unspecified).
 *
 * fsys_dir_fill writes the records from offset `cursor` on into buf (at
 * most room bytes, whole records only), for folder dir (not removed: its
 * parent is known): the bytes written, *next the offset after the last
 * one written (cursor if none), and *more whether a record follows
 * (false at the end of the folder). The VFS lock held.
 */
uint32_t fsys_dir_fill(const struct vnode *dir, uint64_t cursor, uint8_t *buf, uint32_t room,
                       uint64_t *next, bool *more);

/* The offset after the last whole record within the first `valid` bytes of
 * the `used` bytes fsys_dir_fill wrote, and that record's end (the bytes
 * to keep: 0 if no whole record fits); cursor if none. */
uint64_t fsys_dir_keep(const uint8_t *buf, uint32_t used, uint32_t valid, uint64_t cursor,
                       uint32_t *keep);

/* getcwd: the path of folder cwd into buf (size bytes), with its NUL:
 * its length with the NUL, or -FS_ENOENT (cwd removed: no name left) or
 * -FS_ENAMETOOLONG (the path and its NUL need more than size bytes). The
 * VFS lock held. */
int64_t fsys_cwd_path(const struct vnode *cwd, const struct vnode *root, char *buf, size_t size);

#endif
