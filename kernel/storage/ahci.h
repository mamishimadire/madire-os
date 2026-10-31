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
 *   Storage: AHCI bb:dd.f, port P disk <model> <size>, read only, not
 *            mounted (Live mode)
 *   Storage: AHCI bb:dd.f, no disk (Live mode)
 *   Storage: AHCI bb:dd.f, no usable disk (port P: <reason>) (Live mode)
 *   Storage: AHCI bb:dd.f, not used: <reason> (Live mode)
 *
 * Every other line of the driver starts with "ahci: " (blk.c's with
 * "blk: "), and they all come between the PCI: line and the Storage: line.
 * READ ONLY: no command that writes exists in stage 4 (ahci_core.h).
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

/* The driver's figures (report only): commands sent, the longest
 * command's register reads and ticks. */
void ahci_figures(uint64_t *commands, uint64_t *polls_longest, uint64_t *ticks_longest);

#endif
