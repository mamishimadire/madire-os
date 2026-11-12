/* The PCI scan, the pure part (1.4 stage 3): see pci_core.h. Read only:
 * the access table has no write function. 1.4 stage 4: the command
 * register's value for the one write (pci.c makes it). */
#include "kernel/storage/pci_core.h"

/* The buses scanned or queued (one bit each) and the queue of buses still
 * to scan: each bus enters the queue at most once, so it never holds more
 * than PCI_MAX_BUSES entries. */
struct pci_walk {
    uint32_t seen[PCI_MAX_BUSES / 32];
    uint8_t queue[PCI_MAX_BUSES];
    uint32_t head, tail;
};

static bool pci_seen(const struct pci_walk *w, uint32_t bus)
{
    return (w->seen[bus / 32] >> (bus % 32)) & 1u;
}

/* Queues `bus` unless it is 0 (bus 0 is always the first) or already
 * scanned or queued; false if refused. */
static bool pci_queue_bus(struct pci_walk *w, uint32_t bus)
{
    if (bus == 0 || bus >= PCI_MAX_BUSES || pci_seen(w, bus) || w->tail >= PCI_MAX_BUSES)
        return false;
    w->seen[bus / 32] |= 1u << (bus % 32);
    w->queue[w->tail++] = (uint8_t)bus;
    return true;
}

enum pci_bar pci_bar5_state(uint32_t bar5)
{
    if (bar5 & 1u)
        return PCI_BAR_IO;
    switch ((bar5 >> 1) & 3u) {
    case 0:
        return (bar5 & ~0xFu) ? PCI_BAR_OK : PCI_BAR_UNASSIGNED;
    case 2:
        return PCI_BAR_64BIT;
    default:
        return PCI_BAR_RESERVED;
    }
}

const char *pci_bar_reason(enum pci_bar state)
{
    switch (state) {
    case PCI_BAR_OK:         return "";
    case PCI_BAR_64BIT:      return "a 64-bit BAR (its address may lie above 4 GiB)";
    case PCI_BAR_IO:         return "an I/O BAR, not memory";
    case PCI_BAR_RESERVED:   return "a BAR of the reserved memory type";
    case PCI_BAR_UNASSIGNED: return "no address assigned";
    case PCI_BAR_NOT_DEVICE: return "not a device header (layout 0)";
    }
    return "unknown";
}

uint16_t pci_ahci_command(uint16_t old)
{
    return (uint16_t)(old | PCI_CMD_MEMORY | PCI_CMD_MASTER | PCI_CMD_INTX_OFF);
}

uint32_t pci_command_write(uint16_t cmd)
{
    return (uint32_t)cmd;
}

/* One present function: counts it, queues a bridge's secondary bus, and
 * records the first AHCI controller. */
static void pci_function(const struct pci_access *acc, struct pci_walk *w, struct pci_scan *out,
                         uint8_t bus, uint8_t dev, uint8_t fn, uint32_t id, uint32_t header)
{
    uint32_t cls = acc->read32(acc->ctx, bus, dev, fn, PCI_REG_CLASS);
    uint32_t layout = (header >> 16) & 0x7Fu;
    uint8_t base = (uint8_t)(cls >> 24), sub = (uint8_t)(cls >> 16), pif = (uint8_t)(cls >> 8);

    out->functions++;
    if (layout == PCI_LAYOUT_BRIDGE) {
        out->bridges++;
        uint32_t buses = acc->read32(acc->ctx, bus, dev, fn, PCI_REG_BUSES);
        if (!pci_queue_bus(w, (buses >> 8) & 0xFFu))
            out->refused++;
    }
    if (base != PCI_CLASS_STORAGE)
        return;
    if (sub != PCI_SUB_SATA || pif != PCI_IF_AHCI) {
        out->storage_other++;
        return;
    }
    out->ahci++;
    if (out->found)
        return;
    out->found = true;
    struct pci_ahci *a = &out->first_ahci;
    a->bus = bus;
    a->dev = dev;
    a->fn = fn;
    a->vendor = (uint16_t)id;
    a->device = (uint16_t)(id >> 16);
    a->bar5 = 0;
    a->base = 0;
    if (layout != PCI_LAYOUT_DEVICE) {
        a->state = PCI_BAR_NOT_DEVICE;
        return;
    }
    a->bar5 = acc->read32(acc->ctx, bus, dev, fn, PCI_REG_BAR5);
    a->state = pci_bar5_state(a->bar5);
    if (a->state == PCI_BAR_OK)
        a->base = a->bar5 & ~0xFu;
}

static void pci_bus(const struct pci_access *acc, struct pci_walk *w, struct pci_scan *out, uint8_t bus)
{
    out->buses++;
    for (uint32_t d = 0; d < PCI_DEVICES; d++) {
        uint8_t dev = (uint8_t)d;
        uint32_t id = acc->read32(acc->ctx, bus, dev, 0, PCI_REG_ID);
        if ((id & 0xFFFFu) == PCI_VENDOR_NONE)
            continue;
        out->devices++;
        uint32_t header = acc->read32(acc->ctx, bus, dev, 0, PCI_REG_HEADER);
        pci_function(acc, w, out, bus, dev, 0, id, header);
        if (!(header & ((uint32_t)PCI_HEADER_MULTI << 16)))
            continue;
        for (uint32_t f = 1; f < PCI_FUNCTIONS; f++) {
            uint8_t fn = (uint8_t)f;
            uint32_t fid = acc->read32(acc->ctx, bus, dev, fn, PCI_REG_ID);
            if ((fid & 0xFFFFu) == PCI_VENDOR_NONE)
                continue;
            pci_function(acc, w, out, bus, dev, fn, fid,
                         acc->read32(acc->ctx, bus, dev, fn, PCI_REG_HEADER));
        }
    }
}

void pci_scan(const struct pci_access *acc, struct pci_scan *out)
{
    /* Field by field: no struct assignment, which could become a memset
     * (the kernel has none). */
    struct pci_walk w;
    for (uint32_t i = 0; i < PCI_MAX_BUSES / 32; i++)
        w.seen[i] = 0;
    for (uint32_t i = 0; i < PCI_MAX_BUSES; i++)
        w.queue[i] = 0;
    w.head = 0;
    w.tail = 0;
    out->buses = 0;
    out->devices = 0;
    out->functions = 0;
    out->bridges = 0;
    out->refused = 0;
    out->ahci = 0;
    out->storage_other = 0;
    out->found = false;
    out->first_ahci.bus = 0;
    out->first_ahci.dev = 0;
    out->first_ahci.fn = 0;
    out->first_ahci.vendor = 0;
    out->first_ahci.device = 0;
    out->first_ahci.bar5 = 0;
    out->first_ahci.base = 0;
    out->first_ahci.state = PCI_BAR_UNASSIGNED;
    w.seen[0] = 1u;                             /* bus 0: scanned first */
    pci_bus(acc, &w, out, 0);
    while (w.head < w.tail)
        pci_bus(acc, &w, out, w.queue[w.head++]);
}
