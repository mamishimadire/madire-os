#ifndef MADIRE_STORAGE_AHCI_H
#define MADIRE_STORAGE_AHCI_H

/*
 * The AHCI driver at boot (1.4 stage 4; docs/plans/1.4.md, D1, D2, D4,
 * C2, C3): maps the controller's registers (uncached) and its DMA pages in
 * slot 508 (vm_layout.h), runs the pure start (ahci_core.h), logs one line
 * per port that has something on it, attaches the first usable disk to
 * the block cache (blk.h), which reads its first and last sector, and ends
 * with the boot's Storage: line:
 *
 *   Storage: AHCI bb:dd.f, port P disk <model> <size>, <table>, writes
 *            would be ALLOWED on partition I | REFUSED: <condition>; read
 *            only, not mounted (Live mode)
 *   Storage: AHCI bb:dd.f, no disk (Live mode)
 *   Storage: AHCI bb:dd.f, no usable disk (port P: <reason>) (Live mode)
 *   Storage: AHCI bb:dd.f, not used: <reason> (Live mode)
 *
 * Every other line of the driver starts with "ahci: " (blk.c's with
 * "blk: ", 1.4 stage 5: part.c's with "part: ", guard.c's with "guard: "),
 * and they all come between the PCI: line and the Storage: line. Stage 5:
 * after the attach the disk's partition table is read and the write
 * guard decided (guard.h); the decision is kept here (ahci_disk_guard).
 * 1.4 stage 10: writes and flushes exist, and the pure core's one command
 * builder sends them only as that decision allows (ahci_core.h).
 * Disk commands are for one caller at a time (stage 4: thread 0 at boot,
 * then the test=diskread reader; from stage 8 the VFS lock's holder).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/storage/pci_core.h"

/* After the PCI scan found the controller `a` with a usable BAR5 and the
 * command register was written (pci.c). */
void ahci_boot(const struct pci_ahci *a);

/* READ DMA EXT on port `port` (see ahci_read in ahci_core.h); an error is
 * logged once ("ahci: port P: READ DMA EXT ... failed: ..."). 0 or
 * -AHCI_EIO. */
int ahci_disk_read(uint32_t port, uint64_t lba, uint32_t sectors, const uint32_t *phys, uint32_t pages);

/* 1.4 stage 5: the write guard's decision for port `port`'s disk, recorded
 * once (ahci_guard_decide, ahci_core.h): true if recorded. */
bool ahci_disk_guard(uint32_t port, bool allows, uint64_t first, uint64_t sectors);
/* The decision as kept: writes would be allowed on port `port`'s disk. */
bool ahci_disk_guard_allows(uint32_t port);

/* The driver's figures (report only): commands sent, the longest
 * command's register reads and ticks. */
void ahci_figures(uint64_t *commands, uint64_t *polls_longest, uint64_t *ticks_longest);

/* 1.4 stage 10: WRITE DMA EXT and FLUSH CACHE EXT on port `port` (ahci_write
 * and ahci_flush in ahci_core.h: refused, nothing sent, unless the guard
 * allows); an error is logged once, as for a read. 0 or -AHCI_EIO. */
int ahci_disk_write(uint32_t port, uint64_t lba, uint32_t sectors, const uint32_t *phys, uint32_t pages);
int ahci_disk_flush(uint32_t port);

/* Write figures (report only): writes and flushes sent, writes or flushes
 * the builder refused, the longest flush in ticks. */
void ahci_write_figures(uint64_t *writes, uint64_t *flushes, uint64_t *refused, uint64_t *flush_ticks);

/* True while port `port`'s disk is in use (not failed). */
bool ahci_disk_usable(uint32_t port);

/* test=blkdebug only (1.4 stage 10): the next read command fails once
 * (`read_once`), and every write command fails (`writes`), as if the device
 * reported an error (the status ERR bit in PxTFD, which the driver reads
 * once the command has ended, PxCI clear, never while the device still
 * moves data): the driver's own error path runs (logged, the port stopped,
 * reset only if needed, errors counted; 3 make the disk failed). */
void ahci_debug_arm(bool read_once, bool writes);
/* True while an armed read error has not been used by a read command. */
bool ahci_debug_read_pending(void);

/* test=diskwrite on a disk the guard refused (1.4 stage 10, C4): one
 * WRITE DMA EXT of one sector at `lba` from frame `phys`, then one FLUSH
 * CACHE EXT, through the same calls as every write; each must be refused
 * by the command builder (nothing sent). Their results. */
void ahci_write_attempt(uint32_t port, uint64_t lba, uint32_t phys, int *write_r, int *flush_r);

#endif
