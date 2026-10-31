#ifndef MADIRE_STORAGE_BLK_H
#define MADIRE_STORAGE_BLK_H

/*
 * The block cache in the kernel (1.4 stage 4: reads only; docs/plans/
 * 1.4.md, D8, C3, C5): the pure cache of blk_core.h over the AHCI disk
 * (ahci.h), its pages frames of free RAM mapped write-back from
 * VM_BLK_CACHE_BASE in slot 508 when first used. The cache's size is fixed
 * when the disk is attached, after vfs_boot() (C3): 2 MiB or 1/16 of the
 * free pages then, the smaller.
 *
 * At attach the disk's first and last sectors are read (the read check,
 * one "blk:" line with their CRC32s): every boot with a disk reads it,
 * also on VirtualBox (the stage 4 probe), and stage 5 reads the partition
 * tables there.
 *
 * test=diskread (blk_diskread): a reader thread reads chosen sectors (the
 * first, the middle and the last, runs of whole pages, unaligned runs
 * across pages, a run larger than the cache, the partial last page, and
 * reads beyond the end, which must end with EIO), each read with
 * preemption off as a holder of the VFS lock will read (risk 26), and logs
 * one "diskread:" line per read with the CRC32 of the sectors read; the
 * test compares them with the image's (tests/disk_image.py). Then the
 * reader's longest turn, the read speed and the cache's figures (report
 * only), and "diskread: DONE" or "diskread: FAILED ...". No write exists.
 */

#include <stdbool.h>
#include <stdint.h>

/* The disk on AHCI port `port`, of `sectors` 512-byte sectors. */
void blk_attach(uint32_t port, uint64_t sectors);
bool blk_attached(void);
/* The cache's pages made so far (mapped in slot 508). */
uint32_t blk_pages_made(void);

/* Sectors [lba, lba + count) into dst: 0 or -BLK_EIO (blk_core.h). One
 * caller at a time. */
int blk_read(uint64_t lba, uint32_t count, uint8_t *dst);

/* test=diskread (thread 0, at boot). */
void blk_diskread(void);

#endif
