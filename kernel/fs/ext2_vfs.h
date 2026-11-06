#ifndef MADIRE_FS_EXT2_VFS_H
#define MADIRE_FS_EXT2_VFS_H

/*
 * ext2 behind the VFS (1.4 stage 8; docs/plans/1.4.md, C8, C9, D11, D12,
 * D16), pure: tests/unit/test_ext2_vfs.c. The ext2 read core (ext2_core.h)
 * as a filesystem of the mount table (mount_core.h): its vfs_ops table,
 * ext2_vfs_ops, over one mounted volume. READ ONLY in stage 8: every call
 * that would change something (create, move, write, truncate) is
 * -FS_EROFS, and writable() is -FS_EROFS, so the VFS refuses unlink, rmdir
 * and rename at Linux's place before anything is looked up or changed
 * (vfs_core.h). Read-write comes in stage 10.
 *
 * Nodes. An ext2 file or folder is known to the VFS by a node (struct
 * ext2_vfs_node, its vnode first), made the first time a lookup or a
 * folder listing reaches it, from the allocator given at init (the kernel
 * heap in the kernel, malloc in the unit tests), and kept while the
 * volume is mounted: a node is the entry (its folder and its byte in that
 * folder), so a pointer the VFS holds stays valid, and two lookups of one
 * entry give one node. The node keeps a copy of its inode (read once:
 * nothing changes the volume while it is mounted read only). At most
 * max_nodes nodes, the root included: a lookup or listing that needs one
 * more fails with -FS_ENOMEM (the error op: the VFS answers ENOMEM, not
 * ENOENT), counted in `full`, and mem.full is called the first time (the
 * kernel logs one line). Nodes are never freed in stage 8 (a read-only
 * volume never loses an entry); eviction comes in stage 10.
 *
 * The error op: why the last lookup, entry_from or entry_next gave NULL:
 * 0 (no such entry, or the end of the folder), -FS_ENOMEM (no node could
 * be made) or -FS_EIO (a read failed); read once, then cleared.
 *
 * Folders are listed in their on-disk order ("." and ".." are not entries:
 * the VFS answers them from its parent pointers); an entry's position is
 * its byte in its folder, so positions grow along the folder and stay
 * stable. A folder's subfolder count comes from its link count (links -
 * 2), as Linux's stat shows it, without reading the folder.
 *
 * Every number from the disk is checked by the core (ext2_core.h); a read
 * that fails (the disk, or a number out of range) is -FS_EIO, counted in
 * `errors` with the core's reason in `why`.
 *
 * Not locked: the caller holds the VFS lock (as for every vfs_ops call).
 * Written from the plan (C8) and the ext2 specification; no reference
 * project's code was consulted for it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/fs/ext2_core.h"
#include "kernel/fs/vfs_core.h"

#define EXT2_VFS_NODES 1024u            /* the kernel's max_nodes (as RAMFS_MAX_NODES) */

struct ext2_vfs_mem {
    void *(*alloc)(size_t bytes);       /* NULL when out of memory */
    void (*free)(void *p);
    void (*full)(uint32_t max_nodes);   /* the first time the node table is full (may be NULL) */
};

struct ext2_vfs_node {
    struct vnode vn;                    /* first: a vnode pointer is an ext2_vfs_node pointer */
    struct ext2_vfs_node *next;         /* every node of the volume, newest first */
    uint64_t pos;                       /* the entry's byte in its folder (0 for the root) */
    char *name;                         /* not NUL-terminated; NULL for the root */
    uint32_t name_len;
    struct ext2_inode in;               /* its inode, as read when the node was made */
};

struct ext2_vfs {
    struct ext2_fs *fs;                 /* the mounted volume (ext2_mount done) */
    struct ext2_vfs_mem mem;
    struct ext2_vfs_node *root, *nodes;
    uint32_t count, max;                /* nodes made, the root included; at most */
    bool readonly;                      /* stage 8: always */
    uint64_t file_max;                  /* the file-size limit (bytes) */
    /* figures (report only) */
    uint32_t full;                      /* lookups refused: max nodes reached */
    uint32_t errors;                    /* reads that failed (-FS_EIO) */
    const char *why;                    /* the last failure's reason (static text), or 0 */
    int miss;                           /* why the last lookup or listing step gave NULL (error op) */
    bool full_said;                     /* mem.full called */
    struct ext2_dirent de;              /* scratch for a folder walk (not on the stack) */
};

/* The VFS view of a volume ext2_mount has mounted: the root node (inode
 * 2). 0, -FS_ENOMEM (no memory for the root) or -FS_EIO (its inode cannot
 * be read). Read only (stage 8). The file-size limit is what the block map
 * can address, at most 4 GiB - 1 (C7). */
int ext2_vfs_init(struct ext2_vfs *x, struct ext2_fs *fs, const struct ext2_vfs_mem *mem,
                  uint32_t max_nodes);

/* The filesystem's half of the VFS (fs is a struct ext2_vfs). */
extern const struct vfs_ops ext2_vfs_ops;

#endif
