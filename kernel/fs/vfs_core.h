#ifndef MADIRE_FS_VFS_CORE_H
#define MADIRE_FS_VFS_CORE_H

/*
 * The VFS layer (1.0), pure: tests/unit/test_vfs.c. Nodes, lookup from a
 * start folder, and the operations with their errno decisions; the
 * filesystem underneath (the RAM filesystem, ramfs_core.c) is reached only
 * through its ops table, so a disk filesystem can be mounted later. One
 * filesystem, mounted at "/" (no mount call in 1.0).
 *
 * Every call returns 0 or -FS_E... (fs_errno.h), with the value Linux gives
 * in the same case (docs/plans/1.0.md, B1). Each takes a start folder for a
 * relative path (the current folder, or an *at call's folder); an absolute
 * path ignores it. A failed call changes nothing.
 *
 * A node is freed when it has no name left (nlink 0) and nothing holds it
 * open (opens 0): a removed file stays readable until its last close
 * (stages 4-6 hold nodes through vfs_hold / vfs_drop).
 *
 * Not locked: the caller holds the VFS lock (stage 3).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum vnode_type { VNODE_FILE = 1, VNODE_DIR = 2 };

struct vnode {
    enum vnode_type type;
    uint64_t ino;
    uint64_t size;                      /* bytes (files) */
    uint32_t mode;                      /* stored and shown, never checked */
    uint32_t nlink;                     /* 1 while it has a name, 0 once removed */
    uint32_t opens;                     /* open files and current folders on it */
    struct vnode *parent;               /* its folder; the root's is itself; NULL once removed */
    int64_t atime, mtime, ctime;        /* seconds */
};

struct vfs_ops {
    /* The entry called name in folder dir, or NULL. */
    struct vnode *(*lookup)(void *fs, struct vnode *dir, const char *name, size_t len);
    /* A new node under that name in dir: 0, -FS_ENOSPC (no node left) or
     * -FS_ENOMEM; nothing is created on an error. */
    int (*create)(void *fs, struct vnode *dir, const char *name, size_t len,
                  enum vnode_type type, uint32_t mode, struct vnode **out);
    /* Takes node out of its folder: nlink 0, parent NULL. */
    void (*detach)(void *fs, struct vnode *node);
    /* Gives node the name in dir, replacing victim (an entry of dir, or NULL,
     * which is detached): 0 or -FS_ENOMEM (nothing changed). */
    int (*move)(void *fs, struct vnode *node, struct vnode *dir, const char *name, size_t len,
                struct vnode *victim);
    /* The number of entries in a folder ("." and ".." not counted). */
    uint32_t (*entries)(void *fs, struct vnode *dir);
    /* Frees a node with nlink 0 and opens 0. */
    void (*release)(void *fs, struct vnode *node);
};

struct vfs {
    const struct vfs_ops *ops;
    void *fs;
    struct vnode *root;
    int64_t (*now)(void);               /* the time for new and changed nodes */
};

void vfs_mount(struct vfs *v, const struct vfs_ops *ops, void *fs, struct vnode *root,
               int64_t (*now)(void));

/* The node at path. A trailing '/' asks for a folder (ENOTDIR otherwise). */
int vfs_lookup(struct vfs *v, struct vnode *start, const char *path, struct vnode **out);

/* A new file or folder (type) at path: EEXIST if the name is taken (also
 * "/", "." and ".."); EISDIR for a file whose path ends in '/'; ENOENT in a
 * removed folder; out may be NULL. */
int vfs_create(struct vfs *v, struct vnode *start, const char *path, enum vnode_type type,
               uint32_t mode, struct vnode **out);

/* Removes a file's name (EISDIR for a folder, also "/", "." and ".."). */
int vfs_unlink(struct vfs *v, struct vnode *start, const char *path);

/* Removes an empty folder: EBUSY for "/", EINVAL for a last ".",
 * ENOTEMPTY for "..", or for a folder with entries. */
int vfs_rmdir(struct vfs *v, struct vnode *start, const char *path);

/* Moves or renames (each path with its own start folder, for renameat): a
 * file replaces a file, an empty folder an empty folder; EBUSY for "/",
 * "." or ".." on either side; EINVAL for a folder into its own subtree;
 * the same node: 0, nothing done. */
int vfs_rename(struct vfs *v, struct vnode *ostart, const char *opath,
               struct vnode *nstart, const char *npath);

/* An open file or current folder takes and gives back its hold here. */
void vfs_hold(struct vnode *n);
void vfs_drop(struct vfs *v, struct vnode *n);

#endif
