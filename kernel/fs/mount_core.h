#ifndef MADIRE_FS_MOUNT_CORE_H
#define MADIRE_FS_MOUNT_CORE_H

/*
 * The mount table (1.4 stage 7), pure: tests/unit/test_mount_core.c. The
 * filesystems the VFS (vfs_core.c) reaches, each by its number: 0 is the
 * filesystem at "/" (the RAM filesystem in the kernel), and each other
 * one is mounted on a folder of one already mounted (docs/plans/1.4.md,
 * C8). A node carries its filesystem's number in struct vnode's fs_id
 * (what was the padding wr_spare), so finding a node's filesystem is an
 * index, not a search. The kernel mounts only "/" in 1.4 stage 7.
 *
 * Crossing (as Linux does):
 * - a walk that reaches a folder something is mounted on (a mount point)
 *   continues at the mounted filesystem's root (mount_enter);
 * - ".." at the root of a mounted filesystem goes to the parent of the
 *   folder it is mounted on (mount_parent); ".." of "/" is "/";
 * - a mount point cannot be removed or renamed, and nothing is renamed
 *   onto one (EBUSY), and a rename between two filesystems is EXDEV
 *   (vfs_core.c).
 *
 * Not locked: the caller holds the VFS lock.
 */

#include <stdbool.h>
#include <stdint.h>

struct vnode;
struct vfs_ops;

#define MOUNT_MAX 4u                    /* filesystems, "/" included */

struct mount {
    const struct vfs_ops *ops;
    void *fs;                           /* the filesystem's own state, passed to each op */
    struct vnode *root;                 /* its root folder */
    struct vnode *covered;              /* the folder it is mounted on; NULL for "/" */
};

struct mount_table {
    struct mount m[MOUNT_MAX];
    uint16_t count;                     /* filesystems mounted, "/" included */
};

/* The table with one filesystem, number 0, at "/": root becomes its own
 * parent and gets number 0. */
void mount_init(struct mount_table *t, const struct vfs_ops *ops, void *fs, struct vnode *root);

/* Mounts the filesystem whose root is root on folder `on` (a folder of a
 * filesystem already mounted). Its number (1 or more), or -FS_ENOTDIR
 * (`on` is a file), -FS_ENOENT (`on` was removed), -FS_EBUSY (something
 * is mounted on `on` already, or `on` is a filesystem's root, "/"
 * included), -FS_ENOSPC (the table is full); nothing changes on an
 * error. The mount holds `on` (vfs_hold), so it stays while mounted; root
 * becomes its own parent and gets the number. */
int mount_add(struct mount_table *t, const struct vfs_ops *ops, void *fs, struct vnode *root,
              struct vnode *on);

/* The filesystem node n belongs to. */
const struct mount *mount_of(const struct mount_table *t, const struct vnode *n);

/* True when a filesystem is mounted on n. */
bool mount_is_point(const struct mount_table *t, const struct vnode *n);

/* What a walk sees at n: the root mounted on n, or n itself. */
struct vnode *mount_enter(const struct mount_table *t, struct vnode *n);

/* The folder a mounted filesystem's root stands for (the one it is
 * mounted on), or n itself for any other node (also for "/"). */
const struct vnode *mount_covered(const struct mount_table *t, const struct vnode *n);

/* ".." of folder n: its parent, across a mount (the parent of the folder
 * a mounted root is mounted on); "/" for "/"; NULL for a removed folder. */
struct vnode *mount_parent(const struct mount_table *t, const struct vnode *n);

#endif
