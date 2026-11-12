#ifndef MADIRE_FS_VFS_CORE_H
#define MADIRE_FS_VFS_CORE_H

/*
 * The VFS layer (1.0), pure: tests/unit/test_vfs.c. Nodes, lookup from a
 * start folder, and the operations with their errno decisions; the
 * filesystem underneath (the RAM filesystem, ramfs_core.c) is reached only
 * through its ops table, so a disk filesystem can be mounted later. One
 * filesystem, mounted at "/" (no mount call in 1.0).
 *
 * 1.4 stage 7: the ops table also carries the data and folder-listing
 * calls, and the kernel's own paths (fd.c, fsys*.c, console.c, vfs.c)
 * reach a node's filesystem only through it, by the vfs_read ... vfs_pages
 * calls below; struct vfs holds a mount table (mount_core.h) in place of
 * the one filesystem, and a node's filesystem is mount_of(its fs_id). The
 * walk crosses mounts (mount_core.h); the kernel still mounts only "/".
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
    /* 1.0 stage 5: the node write mark (fsys_core.h), held by a write or
     * a truncate for its whole call, in what was padding: 16-bit tickets
     * (at most 64 threads hold one each, so the wrap is harmless) and the
     * holder; its figures are kernel/proc/fd.c's, not per node. */
    uint16_t wr_next, wr_serving;
    uint64_t ino;
    uint64_t size;                      /* bytes (files) */
    uint32_t mode;                      /* stored and shown, never checked */
    uint32_t nlink;                     /* 1 while it has a name, 0 once removed */
    uint32_t opens;                     /* open files and current folders on it */
    uint16_t wr_holder;                 /* the write mark's holder: thread id + 1, 0: free */
    /* 1.4 stage 7: its filesystem's number in the mount table
     * (mount_core.h), in what was the padding wr_spare: 0 is the
     * filesystem at "/" (the RAM filesystem), as every node so far. */
    uint16_t fs_id;
    struct vnode *parent;               /* its folder; the root's is itself; NULL once removed */
    int64_t atime, mtime, ctime;        /* seconds */
};

/* A node is 72 bytes, and a RAM filesystem node record at most 128 (its
 * heap block stays 144 bytes, ramfs_core.h): a new field must fail the
 * build, not grow every node. */
_Static_assert(sizeof(struct vnode) == 72, "struct vnode is 72 bytes");

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

    /* 1.4 stage 7: the data calls, with the RAM filesystem's meanings
     * (ramfs_core.h: ramfs_read, ramfs_write, ramfs_truncate_step) and
     * its errors: a file's bytes, written or read at off (a count, or an
     * error when nothing moved), and a truncate in steps of at most
     * max_pages pages each (1 while steps are left, 0 when done). */
    int64_t (*read)(void *fs, struct vnode *file, uint64_t off, void *buf, uint64_t len);
    int64_t (*write)(void *fs, struct vnode *file, uint64_t off, const void *buf, uint64_t len);
    int (*truncate_step)(void *fs, struct vnode *file, uint64_t length, uint32_t max_pages);
    /* The 4 KiB pages a node's data takes (0 for a folder): stat's
     * st_blocks / 8, du. */
    uint32_t (*pages)(void *fs, const struct vnode *n);

    /* The folder's entries in their order (each with a position that only
     * grows along it, stable while entries come and go): the first whose
     * position is at least pos, and the entry after e in dir; NULL at the
     * end. "." and ".." are not entries. */
    struct vnode *(*entry_from)(void *fs, const struct vnode *dir, uint64_t pos);
    struct vnode *(*entry_next)(void *fs, const struct vnode *dir, const struct vnode *e);
    /* An entry's name in its folder (not NUL-terminated; len 0 for the
     * root) and its position there. */
    const char *(*name)(void *fs, const struct vnode *n, size_t *len);
    uint64_t (*pos)(void *fs, const struct vnode *e);

    /* 1.4 stage 8 (each may be NULL, with the meaning given; the RAM
     * filesystem leaves writable and subdirs NULL): */
    /* 0, or -FS_EROFS when nothing on the filesystem may change (NULL: 0). */
    int (*writable)(void *fs);
    /* The largest size a file may have, in bytes (NULL: no file may grow). */
    uint64_t (*file_max)(void *fs);
    /* A folder's entries that are folders (NULL: counted by entry_from and
     * entry_next). */
    uint32_t (*subdirs)(void *fs, const struct vnode *dir);
    /* Why the last lookup, entry_from or entry_next gave NULL: 0 (not
     * there, or the end), or -FS_ENOMEM / -FS_EIO; read once, then cleared
     * (NULL: always 0, as the RAM filesystem). */
    int (*error)(void *fs);

    /* 1.4 stage 10 (may be NULL): takes node out of its folder dir as
     * detach does (nlink 0, parent NULL), but may fail: 0, or an error with
     * nothing changed (ext2: -FS_EAGAIN, -FS_EIO, -FS_ENOSPC ...). NULL:
     * detach is used, which cannot fail (the RAM filesystem). */
    int (*remove)(void *fs, struct vnode *dir, struct vnode *node);
};

#include "kernel/fs/mount_core.h"

struct vfs {
    struct mount_table mounts;          /* 1.4 stage 7: number 0 is the filesystem at "/" */
    struct vnode *root;                 /* "/" (mounts.m[0].root) */
    int64_t (*now)(void);               /* the time for new and changed nodes */
};

/* The filesystem at "/" (number 0, the mount table's only entry). */
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
 * the same node: 0, nothing done. 1.4 stage 7: EXDEV when the two folders
 * are on different filesystems (checked first after the walks, as Linux);
 * EBUSY for a mount point on either side (also in vfs_rmdir).
 *
 * 1.4 stage 8: on a read-only filesystem (ops->writable) every change is
 * -FS_EROFS, at Linux's place: vfs_create after the checks that make it
 * EEXIST, EISDIR or ENOENT (a name that is taken stays EEXIST, as Linux's
 * mkdir); vfs_unlink and vfs_rmdir before the last name is looked up
 * (after EISDIR, EBUSY, EINVAL and ENOTEMPTY for "/", "." and ".."); and
 * vfs_rename after EXDEV and EBUSY, before the old name is looked up. */
int vfs_rename(struct vfs *v, struct vnode *ostart, const char *opath,
               struct vnode *nstart, const char *npath);

/* An open file or current folder takes and gives back its hold here. */
void vfs_hold(struct vnode *n);
void vfs_drop(struct vfs *v, struct vnode *n);

/*
 * 1.4 stage 7: the calls the kernel's own paths reach a node's filesystem
 * through (its ops table, by the node's fs_id; each as the op above). A
 * node got from vfs_entry_from or vfs_entry_next carries its folder's
 * filesystem number, as one from a lookup or a create does.
 */
int64_t vfs_read(struct vfs *v, struct vnode *file, uint64_t off, void *buf, uint64_t len);
int64_t vfs_write(struct vfs *v, struct vnode *file, uint64_t off, const void *buf,
                  uint64_t len);
int vfs_truncate_step(struct vfs *v, struct vnode *file, uint64_t length, uint32_t max_pages);
uint32_t vfs_pages(const struct vfs *v, const struct vnode *n);
struct vnode *vfs_entry_from(const struct vfs *v, const struct vnode *dir, uint64_t pos);
struct vnode *vfs_entry_next(const struct vfs *v, const struct vnode *dir,
                             const struct vnode *e);
const char *vfs_entry_name(const struct vfs *v, const struct vnode *n, size_t *len);
uint64_t vfs_entry_pos(const struct vfs *v, const struct vnode *e);

/* A folder's entries that are folders (stat's link count: 2 + these). */
uint32_t vfs_subdirs(const struct vfs *v, const struct vnode *dir);

/* 1.4 stage 8: 0, or -FS_EROFS when node n's filesystem is read-only;
 * and the largest size a file of n's filesystem may have (the RAM
 * filesystem's RAMFS_FILE_MAX, ext2's from its block map). */
int vfs_writable(const struct vfs *v, const struct vnode *n);
/* Why the last lookup or listing step in folder dir's filesystem gave
 * NULL: 0, or -FS_ENOMEM / -FS_EIO (ops->error; read once). A walk
 * answers that error instead of ENOENT. */
int vfs_lookup_error(const struct vfs *v, const struct vnode *dir);
uint64_t vfs_file_max(const struct vfs *v, const struct vnode *n);

/* What a walk sees at entry n (the root mounted on it, or n); the folder
 * a mounted root stands for (or n); ".." of folder n across mounts (NULL
 * for a removed folder). mount_core.h. */
struct vnode *vfs_enter(const struct vfs *v, struct vnode *n);
const struct vnode *vfs_covered(const struct vfs *v, const struct vnode *n);
struct vnode *vfs_parent(const struct vfs *v, const struct vnode *n);

#endif
