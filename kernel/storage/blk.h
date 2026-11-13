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
 * only), and "diskread: DONE" or "diskread: FAILED ...". It writes nothing.
 */

#include <stdbool.h>
#include <stdint.h>

/* The disk on AHCI port `port`, of `sectors` 512-byte sectors. */
void blk_attach(uint32_t port, uint64_t sectors);
bool blk_attached(void);
/* The cache's pages made so far (mapped in slot 508). */
uint32_t blk_pages_made(void);

/* Sectors [lba, lba + count) into dst: 0 or -BLK_EIO (blk_core.h). One
 * caller at a time: 1.5 stage 4 (C1, L5), in Disk mode the disk lock's
 * owner (else a panic, disklock_check_io). */
int blk_read(uint64_t lba, uint32_t count, uint8_t *dst);

/* test=diskread (thread 0, at boot). */
void blk_diskread(void);

/* 1.4 stage 10: one WRITE DMA EXT of `sectors` sectors at `lba` from the
 * frames phys[0..pages-1] (ahci_disk_write: refused unless the guard allows),
 * then the cached pages it reached are dropped (blk_cache_forget); and
 * FLUSH CACHE EXT. 0 or -BLK_EIO. One caller at a time: since 1.5 stage 4
 * (C1, L5) the disk lock's owner in Disk mode (a save piece holds only the
 * disk lock). Each call made while the caller holds the VFS lock is
 * counted (blk_vfs_commands; C1: it must stay 0, the boot mount included). */
int blk_write(uint64_t lba, uint32_t sectors, const uint32_t *phys, uint32_t pages);
int blk_flush(void);
/* The attached disk's AHCI port. */
uint32_t blk_port_number(void);

/* 1.5 stage 4: write and FLUSH commands asked for while the caller held
 * the VFS lock, since boot. */
uint64_t blk_vfs_commands(void);

/*
 * 1.5 stage 4, test=blkslow=<ms> (C1; a slow disk): after each write or
 * FLUSH command has finished, the caller waits <ms> more by yielding, if
 * it may (the disk lock's owner with preemption on: disklock_may_yield);
 * one "blk: test=blkslow:" line per such wait names the command and the
 * thread, and counts what ran meanwhile: thread 0's idle loop
 * (blk_slow_idle, called once per pass) and a ticker thread (started by
 * blk_slow_start, one count per tick it wakes), with blk_vfs_commands.
 * blk_slow is set before the disk is attached (kernel.c).
 */
void blk_slow(uint32_t ms);
void blk_slow_start(void);
void blk_slow_idle(void);

#endif
