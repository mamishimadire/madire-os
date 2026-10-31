#ifndef MADIRE_STORAGE_PCI_H
#define MADIRE_STORAGE_PCI_H

/*
 * The PCI scan at boot (1.4 stage 3, docs/plans/1.4.md, sections C2 and
 * C9): configuration mechanism 1 (ports 0xCF8 and 0xCFC), the pure scan
 * of pci_core.c, and two lines on every boot, logged after the
 * filesystem's self-test and before System ready:
 *
 *   PCI: <n> buses, <n> devices, <n> functions, <n> PCI-to-PCI bridges
 *        (<n> secondary buses refused); storage: <n> AHCI, <n> other;
 *        configuration read only
 *   Storage: no AHCI controller (Live mode)
 *   Storage: AHCI controller at bb:dd.f (vvvv:dddd, BAR5 0x<register>
 *        refused: <reason>), not used (Live mode)
 *
 * READ ONLY in stage 3: no configuration register is written (the only
 * port write is the address to 0xCF8, which selects a register); no
 * command-register change, no bus mastering, no BAR sizing, nothing
 * mapped. 1.4 stage 4: the PCI: line still describes the scan, which
 * stays read only. After it, for a controller with a usable BAR5 only:
 * the "ahci: controller at ..." line, the one configuration write (the
 * controller's command register: memory space and bus master on, INTx
 * off; logged on its own "ahci:" line), and the AHCI driver's start,
 * which logs the Storage: line (kernel/storage/ahci.h). No BAR is sized.
 */

/* Scans PCI and logs the PCI: and Storage: lines. */
void pci_boot(void);

#endif
