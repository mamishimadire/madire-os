/* The PCI scan at boot (1.4 stage 3): see pci.h. The scan is read only;
 * 1.4 stage 4: then the one configuration write, the AHCI controller's
 * command register, and the AHCI driver's start. */
#include "kernel/storage/pci.h"
#include "kernel/arch/x86/io.h"
#include "kernel/kprintf.h"
#include "kernel/proc/preempt.h"
#include "kernel/storage/ahci.h"
#include "kernel/storage/pci_core.h"

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu

static uint32_t pci_address(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset)
{
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 0x1Fu) << 11) |
           ((uint32_t)(fn & 0x7u) << 8) | (offset & 0xFCu);
}

/* Configuration mechanism 1: the enable bit, the bus, device, function
 * and register to 0xCF8 (a 32-bit write, which only selects), then the
 * register from 0xCFC. Preemption is off between the two, so no other
 * thread's access can come in between. */
static uint32_t pci_read32(void *ctx, uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset)
{
    (void)ctx;
    uint32_t address = pci_address(bus, dev, fn, offset);
    preempt_disable();
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t value = inl(PCI_CONFIG_DATA);
    preempt_enable();
    return value;
}

/* 1.4 stage 4: the only configuration write (docs/plans/1.4.md, C2), on
 * the AHCI function only: its command register (register 0x04) with
 * memory space and bus master on and INTx off (pci_ahci_command), in one
 * 32-bit write whose status half is zero (pci_command_write); then read
 * back. Logged on one line. */
static void pci_ahci_enable(const struct pci_ahci *a)
{
    uint16_t before = (uint16_t)pci_read32(0, a->bus, a->dev, a->fn, PCI_REG_COMMAND);
    uint16_t want = pci_ahci_command(before);
    preempt_disable();
    outl(PCI_CONFIG_ADDRESS, pci_address(a->bus, a->dev, a->fn, PCI_REG_COMMAND));
    outl(PCI_CONFIG_DATA, pci_command_write(want));
    preempt_enable();
    uint16_t after = (uint16_t)pci_read32(0, a->bus, a->dev, a->fn, PCI_REG_COMMAND);
    klogf("ahci: PCI %02x:%02x.%x command register 0x%04x -> 0x%04x (memory space on, bus master on, INTx off; "
          "read back %s); the only configuration write\n", a->bus, a->dev, a->fn, before, after,
          (after & want) == want ? "as written" : "DIFFERS");
}

void pci_boot(void)
{
    const struct pci_access acc = { pci_read32, 0 };
    struct pci_scan s;
    pci_scan(&acc, &s);
    klogf("PCI: %u buses, %u devices, %u functions, %u PCI-to-PCI bridges (%u secondary buses refused); "
          "storage: %u AHCI, %u other; configuration read only\n",
          s.buses, s.devices, s.functions, s.bridges, s.refused, s.ahci, s.storage_other);
    if (!s.found) {
        klogf("Storage: no AHCI controller (Live mode)\n");
        return;
    }
    const struct pci_ahci *a = &s.first_ahci;
    if (a->state != PCI_BAR_OK) {
        klogf("Storage: AHCI controller at %02x:%02x.%x (%04x:%04x, BAR5 0x%08x refused: %s), not used (Live mode)\n",
              a->bus, a->dev, a->fn, a->vendor, a->device, a->bar5, pci_bar_reason(a->state));
        return;
    }
    klogf("ahci: controller at %02x:%02x.%x (%04x:%04x, BAR5 0x%08x)\n", a->bus, a->dev, a->fn, a->vendor,
          a->device, a->base);
    pci_ahci_enable(a);
    ahci_boot(a);
}
