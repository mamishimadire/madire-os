#ifndef MADIRE_STORAGE_BLK_CORE_SAVE_H
#define MADIRE_STORAGE_BLK_CORE_SAVE_H

/*
 * The block layer's write-back part and its save order (1.4 stage 9;
 * docs/plans/1.4.md, C5 with R1-R4, D8, D9, section D). PURE: no hardware
 * access, allocation or printing. NOT LINKED into the kernel in stage 9
 * (the Makefile leaves it out of the kernel's sources; madire.kernel stays
 * byte-identical to stage 8's); stage 10 joins it to the page cache of
 * blk_core.c and to AHCI writes. Host tests: tests/unit/test_blk_core_save.c
 * and the host cut sweep (tests/ext2write.c, part static).
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
 *   4 folder blocks the disk already uses
 *   5 group descriptors and the superblock
 * A save writes class 1, then FLUSH CACHE, class 2, FLUSH, ... class 5,
 * FLUSH; a class with nothing dirty writes nothing and needs no FLUSH. Within
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
#define BLK_SV_CLASSES      5u
#define BLK_SV_RUN_MAX      16u     /* chunks per write command (16 KiB) */
#define BLK_SV_FULL         28      /* no room for more dirty chunks: save first */

enum blk_sv_class {
    BLK_SV_DATA = 1,
    BLK_SV_BITMAP = 2,
    BLK_SV_INODE = 3,
    BLK_SV_FOLDER = 4,
    BLK_SV_SUPER = 5,
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
    uint8_t cls;            /* 1..5; 0 while being removed */
};

struct blk_sv {
    const struct blk_sv_ops *ops;
    struct blk_sv_ent *ents;    /* sorted by chunk; ents[i]'s bytes at data + i * 1024 */
    uint8_t *data;
    uint8_t *run;               /* BLK_SV_RUN_MAX chunks: one command's bytes */
    uint32_t cap, n;
    uint64_t chunks;            /* the partition's whole chunks */
    bool failed;
    /* figures (report only) */
    uint64_t writes, flushes, saves, failed_saves, puts;
    uint64_t class_writes[BLK_SV_CLASSES + 1];
};

void blk_sv_init(struct blk_sv *s, const struct blk_sv_ops *ops, struct blk_sv_ent *ents, uint8_t *data,
                 uint32_t cap, uint8_t *run, uint64_t part_bytes);

/* Reads [off, off + len) of the partition, dirty chunks over the disk:
 * 0 or -BLK_EIO (beyond the partition, or the disk's read failed). */
int blk_sv_read(struct blk_sv *s, uint64_t off, uint32_t len, uint8_t *buf);

/* Marks [off, off + len) dirty with these bytes and class `cls` (off and
 * len multiples of 1024, cls 1..5). All or nothing: -BLK_EIO (bad range or
 * class), -BLK_SV_FULL (not enough room; nothing changed), or 0. A chunk
 * already dirty takes the new bytes and the new class. */
int blk_sv_put(struct blk_sv *s, uint64_t off, uint32_t len, const uint8_t *buf, uint32_t cls);

/* Dirty chunks now. */
uint32_t blk_sv_dirty(const struct blk_sv *s);

/* True if `chunks` more fit while no more than half of the room is dirty
 * (C5: a save runs when more than half the cache is dirty). */
bool blk_sv_room(const struct blk_sv *s, uint32_t chunks);

/* The save: classes 1 to 5, a FLUSH after each class that wrote. 0, or
 * -BLK_EIO (the layer is failed from then on). */
int blk_sv_save(struct blk_sv *s);

#endif
