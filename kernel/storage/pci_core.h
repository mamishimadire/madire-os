#ifndef MADIRE_STORAGE_PCI_CORE_H
#define MADIRE_STORAGE_PCI_CORE_H

/*
 * The PCI scan (1.4 stage 3, docs/plans/1.4.md, section C2), the pure
 * part: which buses, devices and functions exist, the PCI-to-PCI bridges
 * and the buses behind them, and the first AHCI controller with its
 * registers' address (BAR5). No port access, printing or allocation here
 * (host unit tests: tests/unit/test_pci_core.c, against a fake
 * configuration space); the kernel side, kernel/storage/pci.c, gives the
 * read function (configuration mechanism 1) and logs the result.
 *
 * READ ONLY: the scan only reads configuration registers. The access
 * table has no write function at all, so the scan cannot change a
 * command register, turn on bus mastering or size a BAR (sizing needs a
 * write). Those writes come with the AHCI driver (1.4 stage 4).
 *
 * The order: bus 0 first, then each bridge's secondary bus in the order
 * the bridges are found (a queue); on each bus devices 0-31; on each
 * device function 0, and functions 1-7 only if function 0's header type
 * has bit 7 (multi-function) set. A function is absent if its vendor ID
 * reads 0xFFFF; a device is absent if its function 0 is. A bridge is a
 * function whose header layout (header type bits 0-6) is 1; its secondary
 * bus number (register 0x18, bits 15-8) is queued unless it is 0 or
 * already scanned or queued: so every bus is scanned at most once, a loop
 * of bridges ends, and at most PCI_MAX_BUSES (256, all an 8-bit bus
 * number can name) are scanned.
 *
 * An AHCI controller is class 01 (mass storage), subclass 06 (SATA),
 * programming interface 01 (AHCI 1.0). Its BAR5 (register 0x24 of a
 * header of layout 0) is read once and never sized; it is used only if
 * it is a 32-bit memory BAR with an address assigned (not 0). A 64-bit
 * BAR (whose address may lie above 4 GiB: its upper half is not read), an
 * I/O BAR, the reserved type and an unassigned BAR are refused.
 *
 * The idea of walking bus 0 and then each bridge's secondary bus follows
 * the PCI-to-PCI bridge architecture (the PCI configuration mechanism,
 * docs/plans/1.4.md, section M); the code is Madire's own.
 */

#include <stdbool.h>
#include <stdint.h>

#define PCI_MAX_BUSES     256u
#define PCI_DEVICES       32u
#define PCI_FUNCTIONS     8u

/* Configuration registers read (offsets, each a 32-bit register). */
#define PCI_REG_ID        0x00u     /* vendor ID (bits 15-0), device ID (31-16) */
#define PCI_REG_CLASS     0x08u     /* class (31-24), subclass (23-16), interface (15-8) */
#define PCI_REG_HEADER    0x0Cu     /* header type (23-16) */
#define PCI_REG_BUSES     0x18u     /* layout 1: primary (7-0), secondary (15-8) */
#define PCI_REG_BAR5      0x24u     /* layout 0: BAR5 (AHCI's ABAR) */

#define PCI_VENDOR_NONE   0xFFFFu
#define PCI_HEADER_MULTI  0x80u     /* header type bit 7: multi-function device */
#define PCI_LAYOUT_DEVICE 0x00u
#define PCI_LAYOUT_BRIDGE 0x01u

#define PCI_CLASS_STORAGE 0x01u
#define PCI_SUB_SATA      0x06u
#define PCI_IF_AHCI       0x01u

/* Reads the 32-bit configuration register at `offset` (a multiple of 4)
 * of bus:dev.fn; an absent function reads 0xFFFFFFFF. */
struct pci_access {
    uint32_t (*read32)(void *ctx, uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);
    void *ctx;
};

/* What the scan made of the first AHCI controller's BAR5. */
enum pci_bar {
    PCI_BAR_OK,             /* a 32-bit memory BAR, address not 0 */
    PCI_BAR_64BIT,          /* a 64-bit memory BAR: refused */
    PCI_BAR_IO,             /* an I/O BAR: refused */
    PCI_BAR_RESERVED,       /* memory type 01 (reserved): refused */
    PCI_BAR_UNASSIGNED,     /* a 32-bit memory BAR at address 0: refused */
    PCI_BAR_NOT_DEVICE,     /* the function's header is not of layout 0: no BAR5 */
};

struct pci_ahci {
    uint8_t bus, dev, fn;
    uint16_t vendor, device;
    uint32_t bar5;          /* the register as read */
    uint32_t base;          /* the registers' address (PCI_BAR_OK only, else 0) */
    enum pci_bar state;
};

struct pci_scan {
    uint32_t buses;         /* buses scanned */
    uint32_t devices;       /* devices present (function 0 present) */
    uint32_t functions;     /* functions present */
    uint32_t bridges;       /* PCI-to-PCI bridges (header layout 1) */
    uint32_t refused;       /* bridges whose secondary bus was 0 or already seen */
    uint32_t ahci;          /* AHCI controllers found */
    uint32_t storage_other; /* other mass-storage functions (class 01, not AHCI) */
    bool found;             /* first_ahci is valid */
    struct pci_ahci first_ahci;
};

/* Scans every bus reachable from bus 0 through `acc` (read only). */
void pci_scan(const struct pci_access *acc, struct pci_scan *out);

/* What a BAR5 register's value means (the rules above). */
enum pci_bar pci_bar5_state(uint32_t bar5);

/* Why a BAR state is refused, for the log ("" for PCI_BAR_OK). */
const char *pci_bar_reason(enum pci_bar state);

#endif
