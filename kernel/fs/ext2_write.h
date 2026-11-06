#ifndef MADIRE_FS_EXT2_WRITE_H
#define MADIRE_FS_EXT2_WRITE_H

/*
 * ext2, the write core (1.4 stage 9; docs/plans/1.4.md, C5 with R1-R4, C7,
 * D, D9, D11, D12). PURE: no hardware access, printing or allocation. NOT
 * LINKED into the kernel in stage 9 (the Makefile leaves it out of the
 * kernel's sources; madire.kernel stays byte-identical to stage 8's, and
 * stage 8's check that madire.elf holds no ext2 write-type symbol is
 * unchanged); stage 10 links it behind the write guard. Host tests:
 * tests/unit/test_ext2_write.c and tests/ext2write.c (the host sequences and
 * the cut sweep against e2fsck -fn and debugfs, part static).
 *
 * It reads through the read core (ext2_core.h), whose reads go through the
 * block layer's write-back part (kernel/storage/blk_core_save.h), so every
 * read sees the changes not yet saved; every block it writes also updates
 * the read core's kept blocks (the group descriptors', the inode table's,
 * the folder's and the indirect levels'), so they never go stale.
 *
 * THE SAVE DESIGN (C5 with the owner's R1-R4, stage 9):
 *  R1 A block allocated since the last finished save (a "fresh" block:
 *     file data, a new indirect block, a new folder block) is written whole
 *     in class 1: nothing on the disk points to it yet.
 *  R2 An indirect block the disk already uses is never changed in place:
 *     the changed copy goes to a fresh block (class 1), the levels above are
 *     copied the same way, and the inode's one write (class 3) switches the
 *     whole map, so size, i_blocks and the map always agree on the disk (for
 *     writes and for truncate). A fresh indirect block is changed in place.
 *  R3 Removing a name: save E writes the folder without the entry (class
 *     4); after save E the inode is written as deleted (links 0, dtime) or,
 *     for a hard-linked file, with its link count one lower (class 3 of save
 *     E+1); after save E+1 the bitmap bits of a deleted inode and its blocks
 *     are cleared (class 2 of save E+2). A block or inode is never reused
 *     before the save that removes its last pointer has finished. Blocks
 *     freed by truncate or by R2's copies: pointers removed in save E, bits
 *     cleared in save E+1 (the owner's C5 rule). A shared attribute block's
 *     reference count is lowered in save E+2 (after the inode's deletion).
 *  R4 An entry is added only to a folder block the disk already uses. When
 *     none has room, the core adds an empty block to the folder (class 1 and
 *     the folder's inode, class 3) and returns EXT2_EAGAIN; the caller saves
 *     and tries again. The same answer comes when the disk (or the room for
 *     changes) is full but pending frees or a save would make room. The
 *     caller saves at most 3 times for one operation, then gives up with
 *     EXT2_ENOSPC (stage 10: a save inside an operation must keep the lock
 *     hold limits, in pieces of about 8 commands).
 *  Save 0: a read-write mount writes the volume's "not clean" state at once
 *  (its own save). A full save (ext2_write_sync) repeats saves until nothing
 *  is dirty or pending; e2fsck -fn then reports nothing.
 *
 * The feature policy for writes (D11 and the owner's rule): an unknown
 * RO_COMPAT flag, ANY unknown COMPAT flag, or the error bit: read-only (the
 * reason in ro_why). The error state (D9): a failed save makes the volume
 * read-only; the error bit is then written once if writing still works. A
 * volume not cleanly unmounted (D12): read-write after recounting the free
 * blocks, free inodes and folders of every group from the bitmaps and the
 * inode tables. resize_inode: inode 7 and the reserved descriptor blocks
 * are never written (they are in use in the bitmaps, so never allocated).
 * dir_index: a folder Madire changes has its index flag cleared.
 *
 * Ideas from the references (section M; ideas only, no code): a new block
 * zeroed before use, a fixed most-blocks reserve per step (xv6's fs.c and
 * log.c); allocation starting in a preferred group, the free counts kept in
 * both the group descriptor and the superblock, the used-folders count, the
 * moved folder's ".." repointed and both parents' link counts changed
 * (SerenityOS's Ext2FS).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/fs/ext2_core.h"
#include "kernel/storage/blk_core_save.h"

/* The write core's error codes (beyond ext2_core.h's). */
enum ext2_werr {
    EXT2_EEXIST = -20,
    EXT2_ENOSPC = -21,
    EXT2_EAGAIN = -22,      /* save, then try again (R4) */
    EXT2_EROFS = -23,
    EXT2_ENOTEMPTY = -24,
    EXT2_EISDIR = -25,
    EXT2_EINVAL = -26,
    EXT2_EFBIG = -27,
    EXT2_EMLINK = -28,
    EXT2_ENAMETOOLONG = -29,
};

#define EXT2_W_PEND_MAX     4096u   /* pending frees and deferred steps (ranges) */
#define EXT2_W_FRESH_MAX    1024u   /* fresh indirect and folder blocks per save */
#define EXT2_W_POOL_MAX     8u      /* blocks reserved for one step */
#define EXT2_W_ROOM_CHUNKS  160u    /* dirty chunks one step may add at most */
#define EXT2_W_LINK_MAX     32000u
#define EXT2_W_SAVE_TRIES   3u      /* saves for one operation before EXT2_ENOSPC */

enum ext2_w_pend_kind {
    EXT2_W_PEND_BLOCKS = 1,     /* clear the bits of blocks num..num+count-1 */
    EXT2_W_PEND_INODE = 2,      /* clear the bit of inode num */
    EXT2_W_PEND_NAME = 3,       /* a name of inode num was removed: delete it or lower its links */
    EXT2_W_PEND_ATTR = 4,       /* lower attribute block num's reference count */
};

struct ext2_w_pend {
    uint32_t num, count;
    uint8_t kind;
};

struct ext2_w_ops {
    uint32_t (*now)(void *ctx);                             /* seconds, for the times */
    void (*touch)(void *ctx, uint32_t save, uint32_t ino);  /* an inode changed (may be 0) */
    void *ctx;
};

struct ext2_wfs {
    struct ext2_fs fs;          /* the read core, reading through `sv` */
    struct blk_sv *sv;
    struct ext2_w_ops wops;
    bool rw;                    /* writes allowed */
    bool failed;                /* a save failed (D9) */
    bool recounted;             /* D12's recount ran at the mount */
    const char *ro_why;         /* why read-only (static text), or 0 */
    uint32_t saves_done;        /* finished saves; changes now go into save number saves_done */
    uint64_t file_max;
    uint32_t gen;
    uint32_t pend_n, fresh_n, pool_n;
    uint32_t pend_floor;        /* entries below it are being carried out: never merged into */
    struct ext2_w_pend pend[EXT2_W_PEND_MAX];
    uint32_t fresh[EXT2_W_FRESH_MAX];
    uint32_t pool[EXT2_W_POOL_MAX];
    /* figures (report only) */
    uint32_t blocks_alloc, inodes_alloc, blocks_cleared, inodes_cleared, copies, eagain;
    uint32_t leaks;             /* pending frees dropped (table full): left marked in use */
    /* scratch blocks */
    uint8_t bm[EXT2_BLOCK_MAX];     /* bitmaps */
    uint8_t gb[EXT2_BLOCK_MAX];     /* the group descriptor table */
    uint8_t ib[EXT2_BLOCK_MAX];     /* the inode table */
    uint8_t db[EXT2_BLOCK_MAX];     /* folders */
    uint8_t xb[EXT2_BLOCK_MAX];     /* file data, attribute blocks */
    uint8_t mb[4][EXT2_BLOCK_MAX];  /* the block map's levels */
};

/* Mounts the volume in a partition of `part_bytes` bytes through `sv`.
 * `want_rw`: read-write unless the policy says read-only (w->rw, w->ro_why);
 * a read-write mount recounts if not clean (D12) and runs save 0. EXT2_OK or
 * the read core's error (not mounted). */
int ext2_write_mount(struct ext2_wfs *w, struct blk_sv *sv, uint64_t part_bytes, const struct ext2_w_ops *ops,
                     bool want_rw);

/* Every operation: EXT2_OK (or a count), EXT2_EROFS if read-only,
 * EXT2_EAGAIN (save, then try again: R4), or an error. Names are `len`
 * bytes, 1 to 255, without '/' or NUL, not "." or "..". */
int ext2_write_create(struct ext2_wfs *w, uint32_t dir, const char *name, uint32_t len, uint32_t mode,
                      uint32_t *ino);
int ext2_write_mkdir(struct ext2_wfs *w, uint32_t dir, const char *name, uint32_t len, uint32_t mode,
                     uint32_t *ino);
int ext2_write_unlink(struct ext2_wfs *w, uint32_t dir, const char *name, uint32_t len);
int ext2_write_rmdir(struct ext2_wfs *w, uint32_t dir, const char *name, uint32_t len);
int ext2_write_rename(struct ext2_wfs *w, uint32_t odir, const char *oname, uint32_t olen, uint32_t ndir,
                      const char *nname, uint32_t nlen);
/* Bytes written (fewer than `len` if a save is needed first: call again
 * for the rest), or an error with nothing written. */
int64_t ext2_write_pwrite(struct ext2_wfs *w, uint32_t ino, uint64_t off, const uint8_t *buf, uint32_t len);
int ext2_write_truncate(struct ext2_wfs *w, uint32_t ino, uint64_t size);

/* One save (C5's classes), then the steps of R3 that it allows. EXT2_OK or
 * EXT2_EIO (read-only from then on, D9). */
int ext2_write_save(struct ext2_wfs *w);
/* A full save: saves until nothing is dirty or pending. */
int ext2_write_sync(struct ext2_wfs *w);
/* A full save, then the clean state written (its own save). */
int ext2_write_unmount(struct ext2_wfs *w);
/* True if something is dirty or pending. */
bool ext2_write_busy(const struct ext2_wfs *w);
/* D12: the free blocks, free inodes and folders of every group recounted
 * from the bitmaps and inode tables, the descriptors and the superblock set. */
int ext2_write_recount(struct ext2_wfs *w);

const char *ext2_write_err_str(int err);

#endif
