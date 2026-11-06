/*
 * The VFS layer (1.0), pure: see vfs_core.h. The host unit tests build
 * this file too (tests/unit/test_vfs.c).
 *
 * A path is walked in a loop, one name at a time, from the root or the
 * start folder; "." stays, ".." goes to the parent (the root's parent is
 * the root). The walk stops before the last name, so that each operation
 * can decide what that name means for it.
 *
 * 1.4 stage 7: each node's filesystem is found by its fs_id in the mount
 * table, and every node a filesystem hands back is given its folder's
 * number (vfs_raw_lookup, vfs_entry_from, vfs_entry_next, vfs_create). A
 * step onto a mount point lands on the mounted root; ".." crosses back.
 * 1.4 stage 8: a read-only filesystem's changes end with EROFS
 * (vfs_writable, at the places vfs_core.h names).
 */
#include "kernel/fs/vfs_core.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/path_core.h"

void vfs_mount(struct vfs *v, const struct vfs_ops *ops, void *fs, struct vnode *root,
               int64_t (*now)(void))
{
    mount_init(&v->mounts, ops, fs, root);
    v->root = root;
    v->now = now;
}

/* The filesystem node n belongs to. */
static const struct mount *vfs_mnt(const struct vfs *v, const struct vnode *n)
{
    return mount_of(&v->mounts, n);
}

/* The entry called name in folder dir, raw (a mount point is not
 * entered), with its folder's filesystem number. */
static struct vnode *vfs_raw_lookup(struct vfs *v, struct vnode *dir, const char *name, size_t len)
{
    const struct mount *m = vfs_mnt(v, dir);
    struct vnode *n = m->ops->lookup(m->fs, dir, name, len);
    if (n)
        n->fs_id = dir->fs_id;
    return n;
}

static int64_t now(const struct vfs *v)
{
    return v->now ? v->now() : 0;
}

/* One step from folder cur by the name (not the last one of a path). */
static int step(struct vfs *v, struct vnode *cur, const char *name, size_t len,
                struct vnode **out)
{
    if (cur->type != VNODE_DIR)
        return -FS_ENOTDIR;
    switch (path_dot_kind(name, len)) {
    case PATH_DOT:
        *out = cur;
        return 0;
    case PATH_DOTDOT: {
        struct vnode *up = mount_parent(&v->mounts, cur);    /* across a mount */
        if (!up)
            return -FS_ENOENT;          /* a removed folder */
        *out = up;
        return 0;
    }
    default:
        break;
    }
    struct vnode *n = vfs_raw_lookup(v, cur, name, len);
    if (!n) {
        int e = vfs_lookup_error(v, cur);   /* 1.4 stage 8: ENOMEM or EIO, not ENOENT */
        return e ? e : -FS_ENOENT;
    }
    *out = mount_enter(&v->mounts, n);  /* a mount point: the mounted root */
    return 0;
}

struct last {
    struct vnode *dir;                  /* the folder the last name is looked up in */
    const char *name;
    size_t len;                         /* 0: the path names the root ("/") */
    bool trailing_slash;
};

/* Walks every name but the last. */
static int walk_parent(struct vfs *v, struct vnode *start, const char *path, struct last *l)
{
    struct path_iter it;
    int r = path_begin(&it, path);
    if (r < 0)
        return r;
    struct vnode *cur = it.absolute ? v->root : start;
    if (!it.absolute && cur->type != VNODE_DIR)
        return -FS_ENOTDIR;             /* an *at call's folder is a file */
    l->trailing_slash = it.trailing_slash;
    l->name = 0;
    l->len = 0;
    r = path_next(&it, &l->name, &l->len);
    if (r < 0)
        return r;
    while (r == 1 && !path_at_end(&it)) {
        r = step(v, cur, l->name, l->len, &cur);
        if (r < 0)
            return r;
        r = path_next(&it, &l->name, &l->len);
        if (r < 0)
            return r;
    }
    l->dir = cur;
    if (r == 0)
        l->len = 0;                     /* "/" (only an absolute path has no name) */
    return 0;
}

int vfs_lookup(struct vfs *v, struct vnode *start, const char *path, struct vnode **out)
{
    struct last l;
    int r = walk_parent(v, start, path, &l);
    if (r < 0)
        return r;
    struct vnode *n = l.dir;
    if (l.len) {
        r = step(v, l.dir, l.name, l.len, &n);
        if (r < 0)
            return r;
    }
    if (l.trailing_slash && n->type != VNODE_DIR)
        return -FS_ENOTDIR;
    *out = n;
    return 0;
}

int vfs_create(struct vfs *v, struct vnode *start, const char *path, enum vnode_type type,
               uint32_t mode, struct vnode **out)
{
    struct last l;
    int r = walk_parent(v, start, path, &l);
    if (r < 0)
        return r;
    if (l.len == 0 || path_dot_kind(l.name, l.len) != PATH_NAME)
        return -FS_EEXIST;
    if (l.dir->type != VNODE_DIR)
        return -FS_ENOTDIR;
    if (vfs_raw_lookup(v, l.dir, l.name, l.len))
        return -FS_EEXIST;
    r = vfs_lookup_error(v, l.dir);     /* 1.4 stage 8: not found, or not readable */
    if (r < 0)
        return r;
    if (type == VNODE_FILE && l.trailing_slash)
        return -FS_EISDIR;
    if (l.dir->nlink == 0)
        return -FS_ENOENT;              /* the folder was removed */
    r = vfs_writable(v, l.dir);         /* 1.4 stage 8: EROFS after EEXIST, as Linux */
    if (r < 0)
        return r;
    struct vnode *n;
    const struct mount *m = vfs_mnt(v, l.dir);
    r = m->ops->create(m->fs, l.dir, l.name, l.len, type, mode, &n);
    if (r < 0)
        return r;
    n->fs_id = l.dir->fs_id;
    n->atime = n->mtime = n->ctime = now(v);
    l.dir->mtime = l.dir->ctime = n->mtime;
    if (out)
        *out = n;
    return 0;
}

static void release_if_unused(struct vfs *v, struct vnode *n)
{
    if (n->nlink == 0 && n->opens == 0) {
        const struct mount *m = vfs_mnt(v, n);
        m->ops->release(m->fs, n);
    }
}

int vfs_unlink(struct vfs *v, struct vnode *start, const char *path)
{
    struct last l;
    int r = walk_parent(v, start, path, &l);
    if (r < 0)
        return r;
    if (l.len == 0 || path_dot_kind(l.name, l.len) != PATH_NAME)
        return -FS_EISDIR;
    if (l.dir->type != VNODE_DIR)
        return -FS_ENOTDIR;
    r = vfs_writable(v, l.dir);         /* 1.4 stage 8: before the lookup, as Linux */
    if (r < 0)
        return r;
    struct vnode *n = vfs_raw_lookup(v, l.dir, l.name, l.len);
    if (!n) {
        r = vfs_lookup_error(v, l.dir);
        return r ? r : -FS_ENOENT;
    }
    if (n->type == VNODE_DIR)
        return -FS_EISDIR;
    if (l.trailing_slash)
        return -FS_ENOTDIR;
    const struct mount *m = vfs_mnt(v, n);
    m->ops->detach(m->fs, n);
    l.dir->mtime = l.dir->ctime = n->ctime = now(v);
    release_if_unused(v, n);
    return 0;
}

int vfs_rmdir(struct vfs *v, struct vnode *start, const char *path)
{
    struct last l;
    int r = walk_parent(v, start, path, &l);
    if (r < 0)
        return r;
    if (l.len == 0)
        return -FS_EBUSY;
    switch (path_dot_kind(l.name, l.len)) {
    case PATH_DOT:
        return -FS_EINVAL;
    case PATH_DOTDOT:
        return -FS_ENOTEMPTY;
    default:
        break;
    }
    if (l.dir->type != VNODE_DIR)
        return -FS_ENOTDIR;
    r = vfs_writable(v, l.dir);         /* 1.4 stage 8: before the lookup, as Linux */
    if (r < 0)
        return r;
    struct vnode *n = vfs_raw_lookup(v, l.dir, l.name, l.len);
    if (!n) {
        r = vfs_lookup_error(v, l.dir);
        return r ? r : -FS_ENOENT;
    }
    if (n->type != VNODE_DIR)
        return -FS_ENOTDIR;
    if (mount_is_point(&v->mounts, n))
        return -FS_EBUSY;               /* something is mounted on it */
    const struct mount *m = vfs_mnt(v, n);
    if (m->ops->entries(m->fs, n))
        return -FS_ENOTEMPTY;
    m->ops->detach(m->fs, n);
    l.dir->mtime = l.dir->ctime = n->ctime = now(v);
    release_if_unused(v, n);
    return 0;
}

/* True when folder d is folder a or lies under it (within one
 * filesystem: the walk ends at its root, the one node that is its own
 * parent; for the filesystem at "/" that is "/", as before 1.4 stage 7). */
static bool within(const struct vnode *d, const struct vnode *a)
{
    for (;;) {
        if (d == a)
            return true;
        if (!d || d->parent == d)
            return false;
        d = d->parent;
    }
}

int vfs_rename(struct vfs *v, struct vnode *ostart, const char *opath,
               struct vnode *nstart, const char *npath)
{
    struct last o, t;
    int r = walk_parent(v, ostart, opath, &o);
    if (r < 0)
        return r;
    r = walk_parent(v, nstart, npath, &t);
    if (r < 0)
        return r;
    if (o.dir->fs_id != t.dir->fs_id)
        return -FS_EXDEV;               /* between two filesystems (Linux checks this first) */
    if (o.len == 0 || t.len == 0 || path_dot_kind(o.name, o.len) != PATH_NAME ||
        path_dot_kind(t.name, t.len) != PATH_NAME)
        return -FS_EBUSY;
    if (o.dir->type != VNODE_DIR || t.dir->type != VNODE_DIR)
        return -FS_ENOTDIR;
    r = vfs_writable(v, o.dir);         /* 1.4 stage 8: after EXDEV and EBUSY, as Linux */
    if (r < 0)
        return r;
    struct vnode *n = vfs_raw_lookup(v, o.dir, o.name, o.len);
    if (!n) {
        r = vfs_lookup_error(v, o.dir);
        return r ? r : -FS_ENOENT;
    }
    if (n->type != VNODE_DIR && (o.trailing_slash || t.trailing_slash))
        return -FS_ENOTDIR;
    struct vnode *victim = vfs_raw_lookup(v, t.dir, t.name, t.len);
    if (!victim && (r = vfs_lookup_error(v, t.dir)) < 0)
        return r;
    if (victim == n)
        return 0;                       /* the same node: nothing to do */
    if (n->type == VNODE_DIR && within(t.dir, n))
        return -FS_EINVAL;              /* a folder into its own subtree */
    const struct mount *m = vfs_mnt(v, n);
    if (victim) {
        if (n->type == VNODE_DIR && victim->type != VNODE_DIR)
            return -FS_ENOTDIR;
        if (n->type != VNODE_DIR && victim->type == VNODE_DIR)
            return -FS_EISDIR;
    }
    if (mount_is_point(&v->mounts, n) || (victim && mount_is_point(&v->mounts, victim)))
        return -FS_EBUSY;               /* something is mounted on one of them */
    if (victim && victim->type == VNODE_DIR && m->ops->entries(m->fs, victim))
        return -FS_ENOTEMPTY;
    if (t.dir->nlink == 0)
        return -FS_ENOENT;              /* into a removed folder */
    r = m->ops->move(m->fs, n, t.dir, t.name, t.len, victim);
    if (r < 0)
        return r;
    int64_t t_now = now(v);
    o.dir->mtime = o.dir->ctime = t.dir->mtime = t.dir->ctime = n->ctime = t_now;
    if (victim) {
        victim->ctime = t_now;
        release_if_unused(v, victim);
    }
    return 0;
}

void vfs_hold(struct vnode *n)
{
    n->opens++;
}

void vfs_drop(struct vfs *v, struct vnode *n)
{
    if (n->opens)
        n->opens--;
    release_if_unused(v, n);
}

/* ---- 1.4 stage 7: the table calls of the kernel's own paths ---- */

int64_t vfs_read(struct vfs *v, struct vnode *file, uint64_t off, void *buf, uint64_t len)
{
    const struct mount *m = vfs_mnt(v, file);
    return m->ops->read(m->fs, file, off, buf, len);
}

int64_t vfs_write(struct vfs *v, struct vnode *file, uint64_t off, const void *buf,
                  uint64_t len)
{
    const struct mount *m = vfs_mnt(v, file);
    return m->ops->write(m->fs, file, off, buf, len);
}

int vfs_truncate_step(struct vfs *v, struct vnode *file, uint64_t length, uint32_t max_pages)
{
    const struct mount *m = vfs_mnt(v, file);
    return m->ops->truncate_step(m->fs, file, length, max_pages);
}

uint32_t vfs_pages(const struct vfs *v, const struct vnode *n)
{
    const struct mount *m = vfs_mnt(v, n);
    return m->ops->pages(m->fs, n);
}

struct vnode *vfs_entry_from(const struct vfs *v, const struct vnode *dir, uint64_t pos)
{
    const struct mount *m = vfs_mnt(v, dir);
    struct vnode *e = m->ops->entry_from(m->fs, dir, pos);
    if (e)
        e->fs_id = dir->fs_id;
    return e;
}

struct vnode *vfs_entry_next(const struct vfs *v, const struct vnode *dir,
                             const struct vnode *e)
{
    const struct mount *m = vfs_mnt(v, dir);
    struct vnode *n = m->ops->entry_next(m->fs, dir, e);
    if (n)
        n->fs_id = dir->fs_id;
    return n;
}

const char *vfs_entry_name(const struct vfs *v, const struct vnode *n, size_t *len)
{
    const struct mount *m = vfs_mnt(v, n);
    return m->ops->name(m->fs, n, len);
}

uint64_t vfs_entry_pos(const struct vfs *v, const struct vnode *e)
{
    const struct mount *m = vfs_mnt(v, e);
    return m->ops->pos(m->fs, e);
}

uint32_t vfs_subdirs(const struct vfs *v, const struct vnode *dir)
{
    const struct mount *m = vfs_mnt(v, dir);
    if (m->ops->subdirs)
        return m->ops->subdirs(m->fs, dir);     /* 1.4 stage 8: ext2 knows it from its link count */
    uint32_t count = 0;
    for (const struct vnode *e = vfs_entry_from(v, dir, 0); e; e = vfs_entry_next(v, dir, e))
        if (e->type == VNODE_DIR)
            count++;
    return count;
}

int vfs_lookup_error(const struct vfs *v, const struct vnode *dir)
{
    const struct mount *m = vfs_mnt(v, dir);
    return m->ops->error ? m->ops->error(m->fs) : 0;
}

int vfs_writable(const struct vfs *v, const struct vnode *n)
{
    const struct mount *m = vfs_mnt(v, n);
    return m->ops->writable ? m->ops->writable(m->fs) : 0;
}

uint64_t vfs_file_max(const struct vfs *v, const struct vnode *n)
{
    const struct mount *m = vfs_mnt(v, n);
    return m->ops->file_max ? m->ops->file_max(m->fs) : 0;
}

struct vnode *vfs_enter(const struct vfs *v, struct vnode *n)
{
    return mount_enter(&v->mounts, n);
}

const struct vnode *vfs_covered(const struct vfs *v, const struct vnode *n)
{
    return mount_covered(&v->mounts, n);
}

struct vnode *vfs_parent(const struct vfs *v, const struct vnode *n)
{
    return mount_parent(&v->mounts, n);
}
