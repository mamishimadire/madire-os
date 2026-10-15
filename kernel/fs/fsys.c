/* The file system calls, the kernel side (see fsys.h). */
#include <stdbool.h>
#include <stddef.h>
#include "kernel/fs/fsys.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/path_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/memory/usercopy.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/process.h"

#define NO_FILE 0xFFFFFFFFu

/* The NUL-terminated path at user address u into buf (PATH_MAX_BYTES):
 * 0, -EFAULT (a byte before its NUL is not readable) or -ENAMETOOLONG (no
 * NUL within PATH_MAX_BYTES). Read page by page, never past the page
 * holding the NUL. */
static int path_in(uint64_t u, char *buf)
{
    uint64_t done = 0;
    while (done < PATH_MAX_BYTES) {
        uint64_t n = fsys_page_step(u + done, PATH_MAX_BYTES - done);
        preempt_disable();
        bool ok = copy_from_user(buf + done, u + done, n);
        preempt_enable();
        if (!ok)
            return -FS_EFAULT;
        for (uint64_t i = done; i < done + n; i++)
            if (buf[i] == '\0')
                return 0;
        done += n;
    }
    return -FS_ENAMETOOLONG;
}

/* The start folder of a relative path (see fsys.h); *held: the open file
 * referenced for the call (fd_put), or NO_FILE. */
static int start_folder(uint64_t dirfd, struct vnode **start, uint32_t *held)
{
    *held = NO_FILE;
    if (dirfd == FS_AT_FDCWD) {
        *start = vfs_root()->root;              /* no current folder until stage 6 */
        return 0;
    }
    uint32_t file = 0;
    uint8_t kind = 0, access = 0;
    struct vnode *n = NULL;
    if (fd_get(process_current_fds(), dirfd, &file, &kind, &access, &n) < 0)
        return -FS_EBADF;
    if (kind != OFILE_FILE || n->type != VNODE_DIR) {
        fd_put(file);
        return -FS_ENOTDIR;
    }
    *start = n;
    *held = file;
    return 0;
}

/* What open finds or makes at path (the VFS lock held): 0 and *out. */
static int open_node(const struct fsys_open *o, struct vnode *start, const char *path,
                     uint64_t mode, struct vnode **out)
{
    struct vfs *v = vfs_root();
    struct vnode *n = NULL;
    int r;
    if (o->create) {
        r = vfs_create(v, start, path, VNODE_FILE, (uint32_t)(mode & 07777u), &n);
        if (r == 0) {
            *out = n;                           /* a new, empty file */
            return 0;
        }
        if (r != -FS_EEXIST)
            return r;
        if (fsys_trailing_slash(path))
            return -FS_EISDIR;                  /* O_CREAT with a trailing '/' (Linux) */
        if (o->excl)
            return -FS_EEXIST;
        r = vfs_lookup(v, start, path, &n);
        if (r < 0)
            return r;
        if (n->type == VNODE_DIR)
            return -FS_EISDIR;                  /* O_CREAT on a folder */
    } else {
        r = vfs_lookup(v, start, path, &n);
        if (r < 0)
            return r;
    }
    r = fsys_open_existing(o, n->type);
    if (r < 0)
        return r;
    *out = n;
    return 0;
}

int64_t fsys_sys_openat(uint64_t dirfd, uint64_t upath, uint64_t flags, uint64_t mode)
{
    struct fsys_open o;
    int r = fsys_open_flags(flags, &o);         /* EINVAL first, as Linux */
    if (r < 0)
        return r;
    char path[PATH_MAX_BYTES];
    r = path_in(upath, path);
    if (r < 0)
        return r;
    struct fd_table *d = process_current_fds();
    uint32_t file = 0;
    r = fd_open_begin(d, fsys_open_access(&o), &file);     /* EMFILE, then ENFILE */
    if (r < 0)
        return r;
    if (path[0] == '\0') {
        fd_open_abort(file);
        return -FS_ENOENT;
    }
    struct vnode *start = vfs_root()->root, *n = NULL;
    uint32_t held = NO_FILE;
    if (!fsys_absolute(path)) {
        r = start_folder(dirfd, &start, &held);
        if (r < 0) {
            fd_open_abort(file);
            return r;
        }
    }
    fd_vfs_acquire();
    r = open_node(&o, start, path, mode, &n);
    if (r == 0)
        fd_open_node(file, n);
    fd_vfs_release();
    if (held != NO_FILE)
        fd_put(held);
    if (r < 0) {
        fd_open_abort(file);
        return r;
    }
    if (o.trunc && n->type == VNODE_FILE)
        fd_node_truncate(n, 0);                 /* also with O_RDONLY, as Linux */
    return fd_open_finish(d, file, o.cloexec);
}

/* ---- the stat calls ---- */

/* A node's facts (the VFS lock held). */
static void node_facts(const struct vnode *n, struct fsys_facts *f)
{
    f->kind = n->type == VNODE_DIR ? FSYS_DIR : FSYS_FILE;
    f->ino = n->ino;
    f->size = n->size;
    f->mode = n->mode;
    f->nlink = n->nlink;
    f->subdirs = 0;
    if (n->type == VNODE_DIR)
        for (const struct vnode *e = ramfs_first(n); e; e = ramfs_next(e))
            if (e->type == VNODE_DIR)
                f->subdirs++;
    f->pages = ramfs_file_pages(n);
    f->atime = n->atime;
    f->mtime = n->mtime;
    f->ctime = n->ctime;
}

static void console_facts(struct fsys_facts *f)
{
    f->kind = FSYS_CONSOLE;
    f->ino = 0;
    f->size = 0;
    f->mode = FSYS_CONSOLE_MODE;
    f->nlink = 1;
    f->subdirs = 0;
    f->pages = 0;
    f->atime = f->mtime = f->ctime = vfs_boot_epoch();
}

static int64_t put_stat(const struct fsys_facts *f, uint64_t ust)
{
    struct fs_stat st;
    fsys_stat_fill(&st, f);
    preempt_disable();
    bool ok = copy_to_user(ust, &st, sizeof(st));
    preempt_enable();
    return ok ? 0 : -FS_EFAULT;
}

/* fstat of descriptor fd (and of AT_FDCWD's folder when fdcwd). */
static int64_t stat_fd(uint64_t fd, uint64_t ust, bool fdcwd)
{
    struct fsys_facts f = { 0 };
    if (fdcwd && fd == FS_AT_FDCWD) {
        fd_vfs_acquire();
        node_facts(vfs_root()->root, &f);
        fd_vfs_release();
        return put_stat(&f, ust);
    }
    uint32_t file = 0;
    uint8_t kind = 0, access = 0;
    struct vnode *n = NULL;
    if (fd_get(process_current_fds(), fd, &file, &kind, &access, &n) < 0)
        return -FS_EBADF;
    if (kind == OFILE_FILE) {
        fd_vfs_acquire();
        node_facts(n, &f);
        fd_vfs_release();
    } else {
        console_facts(&f);
    }
    fd_put(file);
    return put_stat(&f, ust);
}

int64_t fsys_sys_fstat(uint64_t fd, uint64_t ust)
{
    return stat_fd(fd, ust, false);
}

int64_t fsys_sys_fstatat(uint64_t dirfd, uint64_t upath, uint64_t ust, uint64_t flags)
{
    char path[PATH_MAX_BYTES];
    int r = path_in(upath, path);
    if (r < 0)
        return r;
    bool empty_path = false;
    r = fsys_at_flags(flags, &empty_path);
    if (r < 0)
        return r;
    if (path[0] == '\0')
        return empty_path ? stat_fd(dirfd, ust, true) : -FS_ENOENT;
    struct vnode *start = vfs_root()->root, *n = NULL;
    uint32_t held = NO_FILE;
    if (!fsys_absolute(path)) {
        r = start_folder(dirfd, &start, &held);
        if (r < 0)
            return r;
    }
    struct fsys_facts f = { 0 };
    fd_vfs_acquire();
    r = vfs_lookup(vfs_root(), start, path, &n);
    if (r == 0)
        node_facts(n, &f);
    fd_vfs_release();
    if (held != NO_FILE)
        fd_put(held);
    if (r < 0)
        return r;
    return put_stat(&f, ust);
}

/* ---- lseek and ftruncate ---- */

int64_t fsys_sys_lseek(uint64_t fd, uint64_t off, uint64_t whence)
{
    uint32_t file = 0;
    uint8_t kind = 0, access = 0;
    struct vnode *n = NULL;
    if (fd_get(process_current_fds(), fd, &file, &kind, &access, &n) < 0)
        return -FS_EBADF;
    int64_t r = kind == OFILE_FILE ? fd_file_lseek(file, (int64_t)off, whence) : -FS_ESPIPE;
    fd_put(file);
    return r;
}

int64_t fsys_sys_ftruncate(uint64_t fd, uint64_t length)
{
    if ((int64_t)length < 0)
        return -FS_EINVAL;                      /* before the descriptor, as Linux */
    uint32_t file = 0;
    uint8_t kind = 0, access = 0;
    struct vnode *n = NULL;
    if (fd_get(process_current_fds(), fd, &file, &kind, &access, &n) < 0)
        return -FS_EBADF;
    int64_t r;
    if (kind != OFILE_FILE || n->type != VNODE_FILE || !(access & OF_WRITE))
        r = -FS_EINVAL;                         /* not a file open for writing */
    else if (length > RAMFS_FILE_MAX)
        r = -FS_EFBIG;
    else
        r = fd_node_truncate(n, length);
    fd_put(file);
    return r;
}
