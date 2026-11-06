#ifndef MADIRE_STORAGE_PART_H
#define MADIRE_STORAGE_PART_H

/*
 * Partition tables in the kernel (1.4 stage 5; docs/plans/1.4.md, D10,
 * C6): the pure reader of part_core.h over the block cache (blk.h). READ
 * ONLY. At attach the disk's table is read and logged, every line starting
 * with "part: ":
 *
 *   part: port P: no partition table (LBA 0 has no 0x55AA signature)
 *   part: port P: partition table not usable: <why>; no partitions
 *   part: port P: MBR (disk signature 0x........), N partition(s)
 *   part: port P: GPT (protective MBR 0xEE): revision 1.0, ... N partition(s)
 *   part: port P: partition I: MBR type 0x83 (Linux), LBA F, S sectors
 *   part: port P: partition I: GPT Linux filesystem data, LBA F, S sectors, name '...'
 *   part: port P: GPT backup header at LBA L: valid (report only)
 *   part: port P: GPT backup header: NOT valid: <why> (report only)
 */

#include <stdint.h>
#include "kernel/storage/part_core.h"

/* Reads and logs port `port`'s table (a disk of `sectors` sectors, attached
 * to the block cache). The table stays valid until the next call. */
const struct part_table *part_attach(uint32_t port, uint64_t sectors);

#endif
