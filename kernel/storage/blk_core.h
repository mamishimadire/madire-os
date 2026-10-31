#ifndef MADIRE_STORAGE_BLK_CORE_H
#define MADIRE_STORAGE_BLK_CORE_H

/*
 * The block cache, the pure part (1.4 stage 4: reads only; docs/plans/
 * 1.4.md, D8, C5). One disk; its sectors are 512 bytes and are cached in
 * 4 KiB pages of 8 sectors each, page n holding sectors 8n to 8n + 7 (the
 * last page of a disk whose size is not a multiple of 8 sectors is read in
 * part, and the rest of it is zero). Each page of the disk is in the cache
 * at most once. No hardware access, allocation or printing here (host unit
 * tests: tests/unit/test_blk_core.c, against a fake disk with an injected
 * error); kernel/storage/blk.c gives the pages and the disk.
 *
 * Slots: at most `cap` pages (2 MiB, 512 pages, or 1/16 of the free pages,
 * the smaller: blk_cache_pages()). A slot's page is made on its first use
 * (blk_ops.ready), so a boot that reads little takes little memory; if one
 * cannot be made, the cache stays at the slots it has. When every slot is
 * in use, the least recently used page is replaced (stage 4 has no dirty
 * page: writing back comes in stage 9).
 *
 * A read of [lba, lba + count): every sector must exist (lba + count <=
 * the disk's sectors), else -BLK_EIO and nothing is read. Pages in the
 * cache are copied out; each run of up to BLK_RUN_MAX pages that are not is
 * read with one disk command into slots chosen first (so a run never
 * replaces a page of itself), then copied out. A failed command (-BLK_EIO
 * from blk_ops.read) leaves none of the run's slots in use and ends the
 * read with -BLK_EIO; it is never retried here (D9).
 *
 * blk_crc32: the CRC32 of GPT, zlib and PNG (the reflected polynomial
 * 0xEDB88320, initial value and final XOR 0xFFFFFFFF), by nibbles; the
 * test=diskread lines carry it, compared with the image's on the host.
 *
 * The shape of the cache (one copy of each block, least-recently-used
 * reuse) follows xv6's bio.c (docs/plans/1.4.md, section M, ideas only);
 * the code is Madire's own.
 */

#include <stdbool.h>
#include <stdint.h>

#define BLK_SECTOR          512u
#define BLK_PAGE            4096u
#define BLK_PER_PAGE        8u          /* sectors in a page */
#define BLK_RUN_MAX         16u         /* pages per disk command (AHCI_PRD_MAX) */
#define BLK_CACHE_MAX       512u        /* 2 MiB */
#define BLK_EIO             5

struct blk_ops {
    /* Slot `slot`'s page exists (made now if needed); false if it cannot be. */
    bool (*ready)(void *ctx, uint32_t slot);
    uint8_t *(*data)(void *ctx, uint32_t slot);
    /* Reads `sectors` sectors from `lba` into the pages of slots[0..n-1]
     * (the sectors fill n pages, the last perhaps in part): 0 or -BLK_EIO. */
    int (*read)(void *ctx, uint64_t lba, uint32_t sectors, const uint32_t *slots, uint32_t n);
    void *ctx;
};

struct blk_slot {
    uint64_t page;          /* the disk's page held (valid only) */
    uint64_t stamp;         /* the last use: the least recently used is replaced */
    bool valid;
};

struct blk_cache {
    const struct blk_ops *ops;
    struct blk_slot *slots;
    uint32_t cap;           /* slots at most */
    uint32_t made;          /* slots whose page exists (0..cap) */
    uint64_t sectors;       /* the disk's */
    uint64_t clock;
    /* figures (report only) */
    uint64_t reads, hits, misses, commands, replaced, errors, refused;
    bool short_of_pages;    /* a slot's page could not be made: the cache stays at `made` */
};

/* The cache's size in pages: BLK_CACHE_MAX or free_pages / 16, the smaller. */
uint32_t blk_cache_pages(uint32_t free_pages);

void blk_cache_init(struct blk_cache *c, const struct blk_ops *ops, struct blk_slot *slots, uint32_t cap,
                    uint64_t sectors);

/* Reads sectors [lba, lba + count) into dst (count * 512 bytes): 0 or
 * -BLK_EIO. A count of 0 reads nothing and returns 0. */
int blk_cache_read(struct blk_cache *c, uint64_t lba, uint32_t count, uint8_t *dst);

/* The CRC32 of n bytes, continuing from crc (0 to start). */
uint32_t blk_crc32(uint32_t crc, const uint8_t *p, uint32_t n);

#endif
