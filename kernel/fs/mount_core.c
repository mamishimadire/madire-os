/*
 * The mount table (1.4 stage 7), pure: see mount_core.h. The host unit
 * tests build this file too (tests/unit/test_mount_core.c).
 */
#include "kernel/fs/mount_core.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/vfs_core.h"

void mount_init(struct mount_table *t, const struct vfs_ops *ops, void *fs, struct vnode *root)
{
    for (uint32_t i = 0; i < MOUNT_MAX; i++) {
        t->m[i].ops = 0;
        t->m[i].fs = 0;
        t->m[i].root = 0;
        t->m[i].covered = 0;
    }
    t->m[0].ops = ops;
    t->m[0].fs = fs;
    t->m[0].root = root;
    t->count = 1;
    root->fs_id = 0;
    root->parent = root;
}

const struct mount *mount_of(const struct mount_table *t, const struct vnode *n)
{
    return &t->m[n->fs_id];
}

/* The number of the filesystem mounted on n, or 0 if none (0 is "/",
 * which is mounted on nothing). */
static uint16_t mount_on(const struct mount_table *t, const struct vnode *n)
{
    for (uint16_t i = 1; i < t->count; i++)
        if (t->m[i].covered == n)
            return i;
    return 0;
}

bool mount_is_point(const struct mount_table *t, const struct vnode *n)
{
    return mount_on(t, n) != 0;
}

int mount_add(struct mount_table *t, const struct vfs_ops *ops, void *fs, struct vnode *root,
              struct vnode *on)
{
    if (on->type != VNODE_DIR)
        return -FS_ENOTDIR;
    if (on->nlink == 0 || !on->parent)
        return -FS_ENOENT;
    if (mount_is_point(t, on) || mount_of(t, on)->root == on)
        return -FS_EBUSY;
    if (t->count >= MOUNT_MAX)
        return -FS_ENOSPC;
    uint16_t id = t->count;
    t->m[id].ops = ops;
    t->m[id].fs = fs;
    t->m[id].root = root;
    t->m[id].covered = on;
    t->count = (uint16_t)(id + 1);
    root->fs_id = id;
    root->parent = root;
    vfs_hold(on);
    return id;
}

struct vnode *mount_enter(const struct mount_table *t, struct vnode *n)
{
    uint16_t id = t->count > 1 ? mount_on(t, n) : 0;
    return id ? t->m[id].root : n;
}

const struct vnode *mount_covered(const struct mount_table *t, const struct vnode *n)
{
    const struct mount *m = mount_of(t, n);
    return m->covered && m->root == n ? m->covered : n;
}

struct vnode *mount_parent(const struct mount_table *t, const struct vnode *n)
{
    return mount_covered(t, n)->parent;
}
