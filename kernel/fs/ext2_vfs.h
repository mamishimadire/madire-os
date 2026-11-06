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
 * 1.4 STAGE 10: READ-WRITE through a writer (struct ext2_vfs_writer, set by
 * ext2_vfs_set_writer; the kernel's is ext2_vfs_rw.h's, over the write
 * core). Without one, or while it says so, the volume is read-only exactly
 * as in stage 8. With one: create (files and folders), remove (unlink and
 * rmdir, through the VFS's remove op), move (rename, replacing a file or an
 * empty folder), write and truncate go to the writer, and the nodes follow:
 * a new entry gets its node at once (its byte in its folder), a removed one
 * leaves its folder (nlink 0, parent NULL) and is freed by release once
 * nothing holds it, a moved one takes its new folder, name and byte. A
 * node's inode copy is read again after every change through it and
 * before every read. Any call may answer -FS_EAGAIN (the write core needs
 * a save first, or one is in progress: nothing changed), and a write may
 * write fewer bytes than asked for the same reason (`again` set,
 * ext2_vfs_again): the caller gives the VFS lock back, saves and tries
 * again (ext2_disk.h).
 *
 * An open file whose last name is removed (unlink, or a rename that
 * replaces it) stays readable and writable until its last close, as on
 * Linux: the writer is asked to keep its inode (keep_ino; the node is an
 * "orphan"), and release asks it to forget the inode at the last close, so
 * R3's deletion runs only then. ext2_vfs_forget_orphans does it for every
 * orphan still open (before the save at Restart).
 *
 * EVICTION (risk 43): when the table is full, the least recently used node
 * that nothing needs is freed to make room: not the root, not open, not
 * the parent of another node, not one of the EXT2_VFS_RECENT most recently
 * used (the nodes the current VFS call still holds) and not the folder the
 * new node goes into. Its entry gets a new node if it is reached again.
 * Only when no node can be freed is the table full (-FS_ENOMEM, the error
 * op; getdents64 answers ENOMEM).
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
#define EXT2_VFS_RECENT 16u             /* stage 10: the most recently used nodes are never evicted */

struct ext2_vfs_mem {
    void *(*alloc)(size_t bytes);       /* NULL when out of memory */
    void (*free)(void *p);
    void (*full)(uint32_t max_nodes);   /* the first time the node table is full (may be NULL) */
    /* stage 10 (may be NULL): around every file data read (on, then off):
     * the kernel's test=blkdebug injects its read error there. */
    void (*reading)(bool on);
};

/* Stage 10: the changes, by inode numbers and names. Each returns 0 (or a
 * count) or -FS_E...; -FS_EAGAIN: nothing changed, save and try again. */
struct ext2_vfs_writer {
    int (*writable)(void *ctx);         /* 0, or -FS_EROFS */
    int (*create)(void *ctx, uint32_t dir, const char *name, uint32_t len, bool folder, uint32_t mode,
                  uint32_t *ino);
    /* unlink (or rmdir with folder); keep_ino: the removed inode is still
     * open (0: not): its deletion waits for forget. */
    int (*remove)(void *ctx, uint32_t dir, const char *name, uint32_t len, bool folder, uint32_t keep_ino);
    int (*rename)(void *ctx, uint32_t odir, const char *oname, uint32_t olen, uint32_t ndir, const char *nname,
                  uint32_t nlen, uint32_t keep_ino);
    /* Bytes written (fewer than len when a save is needed first). */
    int64_t (*write)(void *ctx, uint32_t ino, uint64_t off, const uint8_t *buf, uint32_t len);
    int (*truncate)(void *ctx, uint32_t ino, uint64_t size);
    void (*forget)(void *ctx, uint32_t ino);    /* the last close of a kept inode */
    void *ctx;
};

struct ext2_vfs_node {
    struct vnode vn;                    /* first: a vnode pointer is an ext2_vfs_node pointer */
    struct ext2_vfs_node *next;         /* every node of the volume, newest first */
    uint64_t pos;                       /* the entry's byte in its folder (0 for the root) */
    char *name;                         /* not NUL-terminated; NULL for the root */
    uint32_t name_len;
    struct ext2_inode in;               /* its inode, as read when the node was made (stage 10: or changed) */
    /* stage 10 */
    uint64_t stamp;                     /* its last use (eviction: the least recently used first) */
    uint32_t children;                  /* nodes whose parent it is (such a node is never evicted) */
    bool orphan;                        /* its last name was removed while it was open */
};

struct ext2_vfs {
    struct ext2_fs *fs;                 /* the mounted volume (ext2_mount done) */
    struct ext2_vfs_mem mem;
    struct ext2_vfs_node *root, *nodes;
    uint32_t count, max;                /* nodes made, the root included; at most */
    bool readonly;                      /* stage 8: always; stage 10: no writer */
    bool closed;                        /* stage 10: no more changes (the save before Restart) */
    const struct ext2_vfs_writer *wr;   /* stage 10: NULL: read-only */
    uint64_t file_max;                  /* the file-size limit (bytes) */
    uint64_t clock;                     /* stage 10: node uses so far */
    bool again;                         /* stage 10: a write stopped short: save first */
    /* figures (report only) */
    uint32_t full;                      /* lookups refused: max nodes reached */
    uint32_t errors;                    /* reads that failed (-FS_EIO) */
    uint32_t evicted;                   /* stage 10: nodes freed to make room */
    uint32_t changes;                   /* stage 10: changes made through the writer */
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

/* Stage 10: the writer (NULL: read-only, as stage 8). */
void ext2_vfs_set_writer(struct ext2_vfs *x, const struct ext2_vfs_writer *wr);
/* Stage 10: true once if a write stopped short since the last call (a save
 * is needed before the rest); read once, then cleared. */
bool ext2_vfs_again(struct ext2_vfs *x);
/* Stage 10: every orphan still open is forgotten now (its deletion runs
 * from the next save on); the count. */
uint32_t ext2_vfs_forget_orphans(struct ext2_vfs *x);

#endif
