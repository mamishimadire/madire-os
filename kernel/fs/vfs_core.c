/*
 * The VFS layer (1.0), pure: see vfs_core.h. The host unit tests build
 * this file too (tests/unit/test_vfs.c).
 *
 * A path is walked in a loop, one name at a time, from the root or the
 * start folder; "." stays, ".." goes to the parent (the root's parent is
 * the root). The walk stops before the last name, so that each operation
 * can decide what that name means for it.
 */
#include "kernel/fs/vfs_core.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/path_core.h"

void vfs_mount(struct vfs *v, const struct vfs_ops *ops, void *fs, struct vnode *root,
               int64_t (*now)(void))
{
    v->ops = ops;
    v->fs = fs;
    v->root = root;
    v->now = now;
    root->parent = root;
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
    case PATH_DOTDOT:
        if (!cur->parent)
            return -FS_ENOENT;          /* a removed folder */
        *out = cur->parent;
        return 0;
    default:
        break;
    }
    struct vnode *n = v->ops->lookup(v->fs, cur, name, len);
    if (!n)
        return -FS_ENOENT;
    *out = n;
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
    if (v->ops->lookup(v->fs, l.dir, l.name, l.len))
        return -FS_EEXIST;
    if (type == VNODE_FILE && l.trailing_slash)
        return -FS_EISDIR;
    if (l.dir->nlink == 0)
        return -FS_ENOENT;              /* the folder was removed */
    struct vnode *n;
    r = v->ops->create(v->fs, l.dir, l.name, l.len, type, mode, &n);
    if (r < 0)
        return r;
    n->atime = n->mtime = n->ctime = now(v);
    l.dir->mtime = l.dir->ctime = n->mtime;
    if (out)
        *out = n;
    return 0;
}

static void release_if_unused(struct vfs *v, struct vnode *n)
{
    if (n->nlink == 0 && n->opens == 0)
        v->ops->release(v->fs, n);
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
    struct vnode *n = v->ops->lookup(v->fs, l.dir, l.name, l.len);
    if (!n)
        return -FS_ENOENT;
    if (n->type == VNODE_DIR)
        return -FS_EISDIR;
    if (l.trailing_slash)
        return -FS_ENOTDIR;
    v->ops->detach(v->fs, n);
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
    struct vnode *n = v->ops->lookup(v->fs, l.dir, l.name, l.len);
    if (!n)
        return -FS_ENOENT;
    if (n->type != VNODE_DIR)
        return -FS_ENOTDIR;
    if (v->ops->entries(v->fs, n))
        return -FS_ENOTEMPTY;
    v->ops->detach(v->fs, n);
    l.dir->mtime = l.dir->ctime = n->ctime = now(v);
    release_if_unused(v, n);
    return 0;
}

/* True when folder d is folder a or lies under it. */
static bool within(const struct vfs *v, const struct vnode *d, const struct vnode *a)
{
    for (;;) {
        if (d == a)
            return true;
        if (!d || d == v->root)
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
    if (o.len == 0 || t.len == 0 || path_dot_kind(o.name, o.len) != PATH_NAME ||
        path_dot_kind(t.name, t.len) != PATH_NAME)
        return -FS_EBUSY;
    if (o.dir->type != VNODE_DIR || t.dir->type != VNODE_DIR)
        return -FS_ENOTDIR;
    struct vnode *n = v->ops->lookup(v->fs, o.dir, o.name, o.len);
    if (!n)
        return -FS_ENOENT;
    if (n->type != VNODE_DIR && (o.trailing_slash || t.trailing_slash))
        return -FS_ENOTDIR;
    struct vnode *victim = v->ops->lookup(v->fs, t.dir, t.name, t.len);
    if (victim == n)
        return 0;                       /* the same node: nothing to do */
    if (n->type == VNODE_DIR && within(v, t.dir, n))
        return -FS_EINVAL;              /* a folder into its own subtree */
    if (victim) {
        if (n->type == VNODE_DIR && victim->type != VNODE_DIR)
            return -FS_ENOTDIR;
        if (n->type != VNODE_DIR && victim->type == VNODE_DIR)
            return -FS_EISDIR;
        if (victim->type == VNODE_DIR && v->ops->entries(v->fs, victim))
            return -FS_ENOTEMPTY;
    }
    if (t.dir->nlink == 0)
        return -FS_ENOENT;              /* into a removed folder */
    r = v->ops->move(v->fs, n, t.dir, t.name, t.len, victim);
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
