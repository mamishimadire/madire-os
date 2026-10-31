/* The PCI scan at boot (1.4 stage 3): see pci.h. Read only. */
#include "kernel/storage/pci.h"
#include "kernel/arch/x86/io.h"
#include "kernel/kprintf.h"
#include "kernel/proc/preempt.h"
#include "kernel/storage/pci_core.h"

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu

/* Configuration mechanism 1: the enable bit, the bus, device, function
 * and register to 0xCF8 (a 32-bit write, which only selects), then the
 * register from 0xCFC. Preemption is off between the two, so no other
 * thread's access can come in between. */
static uint32_t pci_read32(void *ctx, uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset)
{
    (void)ctx;
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 0x1Fu) << 11) |
                       ((uint32_t)(fn & 0x7u) << 8) | (offset & 0xFCu);
    preempt_disable();
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t value = inl(PCI_CONFIG_DATA);
    preempt_enable();
    return value;
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
    if (a->state == PCI_BAR_OK)
        klogf("Storage: AHCI controller at %02x:%02x.%x (%04x:%04x, BAR5 0x%08x), not used yet (Live mode)\n",
              a->bus, a->dev, a->fn, a->vendor, a->device, a->base);
    else
        klogf("Storage: AHCI controller at %02x:%02x.%x (%04x:%04x, BAR5 0x%08x refused: %s), not used (Live mode)\n",
              a->bus, a->dev, a->fn, a->vendor, a->device, a->bar5, pci_bar_reason(a->state));
}
