#ifndef MADIRE_STORAGE_BLK_CORE_SAVE_H
#define MADIRE_STORAGE_BLK_CORE_SAVE_H

/*
 * The block layer's write-back part and its save order (1.4 stage 9;
 * docs/plans/1.4.md, C5 with R1-R4, D8, D9, section D). PURE: no hardware
 * access, allocation or printing. Linked into the kernel since stage 10
 * (kernel/fs/ext2_disk.c gives it the disk: reads through the page cache of
 * blk_core.c, writes and flushes through the AHCI driver behind the write
 * guard). Host tests: tests/unit/test_blk_core_save.c and the host cut
 * sweep (tests/ext2write.c, part static).
 *
 * Stage 10: a save may run in pieces (blk_sv_save_step: at most a given
 * number of disk commands per call, so a caller holding the VFS lock gives
 * it back between pieces, C5's "about 8 commands per hold"); the commands
 * and their order are exactly those of one whole save (blk_sv_save, which
 * is the same steps without a limit). While a save is in progress nothing
 * may be put (blk_sv_put answers -BLK_SV_FULL); the chunks keep their bytes
 * for reads until their class's FLUSH has finished. Each dirty chunk's
 * bytes stay in one slot of `data` while it is dirty (the sorted table holds
 * the slot's number), so adding a chunk never moves another's bytes.
 *
 * Changed bytes are kept in 1 KiB "chunks" of the partition (two sectors),
 * the smallest ext2 block: a 2 or 4 KiB block is 2 or 4 chunks, and the
 * superblock (bytes 1024-2047 of every ext2 volume) is always chunk 1 alone,
 * so writing it never rewrites the Madire test marker in the partition's
 * first sector, whatever the block size. Each dirty chunk carries the class
 * of the save it belongs to (C5):
 *   1 file data and every block allocated since the last finished save
 *     (R1: nothing on the disk points to it yet; new indirect blocks and new
 *     folder blocks included)
 *   2 bitmaps
 *   3 inodes (and attribute blocks' reference counts)
 *   4 folder blocks the disk already uses that receive the NEW name of a
 *     rename whose old name is in another block (R5, stage 11)
 *   5 the other folder blocks the disk already uses (a rename's old name
 *     removed, a moved folder's "..", every other change of a folder)
 *   6 group descriptors and the superblock
 * A save writes class 1, then FLUSH CACHE, class 2, FLUSH, ... class 6,
 * FLUSH; a class with nothing dirty writes nothing and needs no FLUSH. R5:
 * the FLUSH after class 4 puts every rename's new name on the disk before
 * its old name is removed, so a power cut never leaves a saved file or
 * folder under neither name (at worst under both). Within
 * a class the chunks go in ascending order, each run of up to BLK_SV_RUN_MAX
 * neighbouring chunks of the class in one write command. The host cut model
 * stops after any write command (command-granular, section D).
 *
 * A read sees the dirty chunks over the disk's bytes (read-your-writes). A
 * failed write or flush ends the save at once with -BLK_EIO and marks the
 * layer failed (D9: no retry here; the filesystem above becomes read-only).
 * Chunks already written stay written; the rest stay dirty.
 *
 * Ideas (docs/plans/1.4.md, section M; ideas only, no code): the dirty
 * blocks held until the commit writes them (xv6's log.c); writing back the
 * dirty superblock, descriptors and bitmaps as one flush (SerenityOS's
 * Ext2FS). The classes and the FLUSH between them are the plan's own.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/storage/blk_core.h"

#define BLK_SV_CHUNK        1024u
#define BLK_SV_CLASSES      6u
#define BLK_SV_RUN_MAX      16u     /* chunks per write command (16 KiB) */
#define BLK_SV_FULL         28      /* no room for more dirty chunks: save first */

enum blk_sv_class {
    BLK_SV_DATA = 1,
    BLK_SV_BITMAP = 2,
    BLK_SV_INODE = 3,
    BLK_SV_NAME = 4,                /* R5: a cross-block rename's new name */
    BLK_SV_FOLDER = 5,
    BLK_SV_SUPER = 6,
};

struct blk_sv_ops {
    /* `len` bytes at byte `off` of the partition: 0 or not 0. */
    int (*read)(void *ctx, uint64_t off, uint32_t len, uint8_t *buf);
    /* One write command of `len` bytes at byte `off` (both multiples of
     * 1024), all of class `cls`: 0 or not 0. */
    int (*write)(void *ctx, uint64_t off, uint32_t len, const uint8_t *buf, uint32_t cls);
    /* FLUSH CACHE: 0 or not 0. */
    int (*flush)(void *ctx);
    void *ctx;
};

struct blk_sv_ent {
    uint32_t chunk;
    uint32_t slot;          /* its bytes at data + slot * 1024 (stage 10) */
    uint8_t cls;            /* 1..6; 0 while being removed */
};

struct blk_sv {
    const struct blk_sv_ops *ops;
    /* ents[0..n-1] sorted by chunk; ents[n..cap-1] are unused and hold the
     * free slots, so the cap entries always hold every slot once. */
    struct blk_sv_ent *ents;
    uint8_t *data;
    uint8_t *run;               /* BLK_SV_RUN_MAX chunks: one command's bytes */
    uint32_t cap, n;
    uint64_t chunks;            /* the partition's whole chunks */
    bool failed;
    /* stage 10: a save in progress (blk_sv_save_step): its class, the next
     * entry to look at, and whether the class wrote (a FLUSH is due). */
    bool saving;
    uint32_t cur_cls, cur_i;
    bool cur_wrote;
    /* figures (report only) */
    uint64_t writes, flushes, saves, failed_saves, puts;
    uint64_t class_writes[BLK_SV_CLASSES + 1];
    uint32_t pieces;            /* calls of blk_sv_save_step that sent a command */
};

void blk_sv_init(struct blk_sv *s, const struct blk_sv_ops *ops, struct blk_sv_ent *ents, uint8_t *data,
                 uint32_t cap, uint8_t *run, uint64_t part_bytes);

/* Reads [off, off + len) of the partition, dirty chunks over the disk:
 * 0 or -BLK_EIO (beyond the partition, or the disk's read failed). */
int blk_sv_read(struct blk_sv *s, uint64_t off, uint32_t len, uint8_t *buf);

/* Marks [off, off + len) dirty with these bytes and class `cls` (off and
 * len multiples of 1024, cls 1..6). All or nothing: -BLK_EIO (bad range or
 * class), -BLK_SV_FULL (not enough room, or a save is in progress; nothing
 * changed), or 0. A chunk already dirty takes the new bytes and the new
 * class (the ext2 write core keeps a class-4 block in class 4, R5). */
int blk_sv_put(struct blk_sv *s, uint64_t off, uint32_t len, const uint8_t *buf, uint32_t cls);

/* Stage 11 (R5): the class of the dirty chunk holding partition byte `off`,
 * or 0 if that chunk is not dirty. */
uint32_t blk_sv_class(const struct blk_sv *s, uint64_t off);

/* Dirty chunks now. */
uint32_t blk_sv_dirty(const struct blk_sv *s);

/* True if `chunks` more fit while no more than half of the room is dirty
 * (C5: a save runs when more than half the cache is dirty). */
bool blk_sv_room(const struct blk_sv *s, uint32_t chunks);

/* The save: classes 1 to 6, a FLUSH after each class that wrote. 0, or
 * -BLK_EIO (the layer is failed from then on). A save already in progress
 * is finished. */
int blk_sv_save(struct blk_sv *s);

/* Stage 10: the save in pieces: starts a save if none is in progress, then
 * sends at most `max_cmds` commands (writes and flushes) of it. 0 with
 * *done true when the save has finished, 0 with *done false when commands
 * are left (call again), or -BLK_EIO (as blk_sv_save; no save in progress
 * then). */
int blk_sv_save_step(struct blk_sv *s, uint32_t max_cmds, bool *done);

#endif
