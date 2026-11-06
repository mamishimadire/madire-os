#ifndef MADIRE_FS_EXT2_DISK_H
#define MADIRE_FS_EXT2_DISK_H

/*
 * The disk's ext2 volume at /home (1.4 stage 8; docs/plans/1.4.md, C8,
 * C9, D11, D12, D16): called once at boot by the AHCI driver (ahci.c),
 * after the write guard, for the partition the guard allowed (a marked
 * test disk); never for a disk the guard refused (Live mode, as before).
 * The volume is read through the block cache (blk.h), the partition's
 * bytes only, and mounted READ ONLY on the RAM folder /home through the
 * mount table (mount_core.h), under the VFS lock; nothing is written.
 *
 * D16 in stage 8:
 *   not ext2, a superblock that fails its checks, an unknown INCOMPAT flag
 *       -> not mounted (Live mode): one ext2: line with the reason;
 *   an unknown RO_COMPAT flag, the error bit, or not cleanly unmounted
 *       -> mounted read-only, with one ext2: line each (the D12 warning
 *          for not clean; the recount comes with read-write in stage 10);
 *   an unknown COMPAT flag -> reading allowed, one ext2: line (the rule
 *       for writes comes in stage 10);
 *   marked and supported -> mounted read-only (stage 8 has no write path).
 *
 * Its lines, logged after the work (never while the lock is held), all
 * between the guard's lines and the Storage: line:
 *   ext2: port P partition I: revision R, B-byte blocks, ...; state ...
 *   ext2: port P partition I: not mounted: <reason>
 *   ext2: unknown COMPAT flags 0x...: ... / unknown RO_COMPAT ... /
 *         the error bit ... / warning: not cleanly unmounted ...
 *   mount: ext2 '<label>' on /home (filesystem N), read-only: <why>; ...
 *   mount: file-size limits from each filesystem's ops table: ...
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Mounts the ext2 volume of partition `partition` (LBA `first`, `sectors`
 * sectors) of the disk on port `port`, attached to the block cache. Fills
 * `tail` with the Storage: line's end ("; ext2 '...' mounted read-only on
 * /home" or "; read only, not mounted: <reason>", without the mode in
 * parentheses). true if mounted. */
bool ext2_disk_mount(uint32_t port, uint32_t partition, uint64_t first, uint64_t sectors, char *tail,
                     size_t tail_size);

/* TSC cycles in microseconds, by the calibration the mount made (0 on a
 * boot without a mounted disk); for report-only figures (console.c). */
uint64_t ext2_disk_us(uint64_t cycles);

#endif
