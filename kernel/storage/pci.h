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
 *   Storage: AHCI controller at bb:dd.f (vvvv:dddd, BAR5 0x<address>),
 *        not used yet (Live mode)
 *   Storage: AHCI controller at bb:dd.f (vvvv:dddd, BAR5 0x<register>
 *        refused: <reason>), not used (Live mode)
 *
 * READ ONLY in stage 3: no configuration register is written (the only
 * port write is the address to 0xCF8, which selects a register); no
 * command-register change, no bus mastering, no BAR sizing, nothing
 * mapped. The AHCI driver (stage 4) is the first to write.
 */

/* Scans PCI and logs the PCI: and Storage: lines. */
void pci_boot(void);

#endif
