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

/* 1.0 stage 6: the calling process's current folder (NULL in its table is
 * the root). Only its own thread changes it (chdir) or drops it (its
 * teardown), and it is held meanwhile, so it is read without the lock. */
static struct vnode *cwd_node(void)
{
    struct vnode *c = process_current_fds()->cwd;
    return c ? c : vfs_root()->root;
}

/* The start folder of a relative path (see fsys.h); *held: the open file
 * referenced for the call (fd_put), or NO_FILE. */
static int start_folder(uint64_t dirfd, struct vnode **start, uint32_t *held)
{
    *held = NO_FILE;
    if (dirfd == FS_AT_FDCWD) {
        *start = cwd_node();                    /* stage 6: the current folder */
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
        node_facts(cwd_node(), &f);             /* stage 6: the current folder */
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

/* ---- 1.0 stage 6: the folder calls ---- */

/* A path call's start: the path copied in (into PATH_MAX_BYTES at path)
 * and the folder a relative one starts at; *held as start_folder. The
 * empty path: -ENOENT (no AT_EMPTY_PATH for these calls). */
static int path_start(uint64_t dirfd, uint64_t upath, char *path, struct vnode **start,
                      uint32_t *held)
{
    *held = NO_FILE;
    *start = vfs_root()->root;
    int r = path_in(upath, path);
    if (r < 0)
        return r;
    if (path[0] == '\0')
        return -FS_ENOENT;
    if (!fsys_absolute(path))
        return start_folder(dirfd, start, held);
    return 0;
}

int64_t fsys_sys_mkdirat(uint64_t dirfd, uint64_t upath, uint64_t mode)
{
    char path[PATH_MAX_BYTES];
    struct vnode *start = NULL;
    uint32_t held = NO_FILE;
    int r = path_start(dirfd, upath, path, &start, &held);
    if (r == 0) {
        fd_vfs_acquire();
        r = vfs_create(vfs_root(), start, path, VNODE_DIR, fsys_mkdir_mode(mode), NULL);
        fd_vfs_release();
    }
    if (held != NO_FILE)
        fd_put(held);
    return r;
}

int64_t fsys_sys_unlinkat(uint64_t dirfd, uint64_t upath, uint64_t flags)
{
    bool removedir = false;
    int r = fsys_unlinkat_flags(flags, &removedir);    /* EINVAL first, as Linux */
    if (r < 0)
        return r;
    char path[PATH_MAX_BYTES];
    struct vnode *start = NULL, *victim = NULL;
    uint32_t held = NO_FILE;
    r = path_start(dirfd, upath, path, &start, &held);
    if (r == 0) {
        struct vfs *v = vfs_root();
        fd_vfs_acquire();
        if (removedir) {
            r = vfs_rmdir(v, start, path);              /* a folder has no pages */
        } else {
            /* The file is held across the unlink, so that a last hold
             * frees its pages in steps (fd_node_drop), not in this hold. */
            struct vnode *n = NULL;
            if (vfs_lookup(v, start, path, &n) == 0 && n->type == VNODE_FILE) {
                vfs_hold(n);
                victim = n;
            }
            r = vfs_unlink(v, start, path);
        }
        fd_vfs_release();
    }
    if (victim)
        fd_node_drop(victim);
    if (held != NO_FILE)
        fd_put(held);
    return r;
}

int64_t fsys_sys_rename(uint64_t uold, uint64_t unew)
{
    char opath[PATH_MAX_BYTES], npath[PATH_MAX_BYTES];
    int r = path_in(uold, opath);
    if (r < 0)
        return r;
    r = path_in(unew, npath);
    if (r < 0)
        return r;
    struct vfs *v = vfs_root();
    struct vnode *start = cwd_node(), *n = NULL, *victim = NULL;
    fd_vfs_acquire();
    /* A file it may replace is held across the rename (as in unlinkat). */
    if (vfs_lookup(v, start, npath, &n) == 0 && n->type == VNODE_FILE) {
        vfs_hold(n);
        victim = n;
    }
    r = vfs_rename(v, start, opath, start, npath);
    fd_vfs_release();
    if (victim)
        fd_node_drop(victim);
    return r;
}

int64_t fsys_sys_chdir(uint64_t upath)
{
    char path[PATH_MAX_BYTES];
    int r = path_in(upath, path);
    if (r < 0)
        return r;
    struct fd_table *d = process_current_fds();
    struct vfs *v = vfs_root();
    struct vnode *n = NULL;
    fd_vfs_acquire();
    r = vfs_lookup(v, cwd_node(), path, &n);
    if (r == 0 && n->type != VNODE_DIR)
        r = -FS_ENOTDIR;
    if (r == 0) {
        struct vnode *old = d->cwd;
        if (n == v->root)
            n = NULL;                           /* the root needs no hold */
        else
            vfs_hold(n);                        /* the new one first: it may be the old */
        d->cwd = n;
        if (old)
            vfs_drop(v, old);                   /* a removed folder is freed now */
    }
    fd_vfs_release();
    return r;
}

int64_t fsys_sys_getcwd(uint64_t ubuf, uint64_t size)
{
    char path[PATH_MAX_BYTES];
    fd_vfs_acquire();
    int64_t len = fsys_cwd_path(cwd_node(), vfs_root()->root, path, sizeof(path));
    fd_vfs_release();
    if (len < 0)
        return len;                             /* ENOENT, ENAMETOOLONG */
    if ((uint64_t)len > size)
        return -FS_ERANGE;                      /* also size 0 (the system call, S6-1) */
    preempt_disable();
    bool ok = copy_to_user(ubuf, path, (uint64_t)len);
    preempt_enable();
    return ok ? len : -FS_EFAULT;
}

int64_t fsys_sys_getdents64(uint64_t fd, uint64_t ubuf, uint64_t count)
{
    uint32_t file = 0;
    uint8_t kind = 0, access = 0;
    struct vnode *n = NULL;
    if (fd_get(process_current_fds(), fd, &file, &kind, &access, &n) < 0)
        return -FS_EBADF;
    int64_t r = kind == OFILE_FILE ? fd_file_getdents(file, ubuf, count) : -FS_ENOTDIR;
    fd_put(file);
    return r;
}
