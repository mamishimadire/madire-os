/* Unit tests for kernel/storage/pci_core.c (the PCI scan, 1.4 stage 3)
 * against a fake configuration space: a plain bus (QEMU's pc machine), a
 * bridge with a device behind it, multi-function devices and a device that
 * answers for every function number, absent slots, bridge loops and a
 * chain through all 256 buses, every kind of BAR5 (32-bit, 64-bit, I/O,
 * reserved, unassigned, a bridge header), mass-storage functions that are
 * not AHCI, and two AHCI controllers. Every read is recorded: the scan
 * reads only the registers it needs, each bus once, and it cannot write
 * (the access table has no write function). The module is included
 * directly. */
#include <stdbool.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/storage/pci_core.c"

#define FAKE_MAX 300

struct fake_fn {
    uint8_t bus, dev, fn;
    bool alias;             /* answers for every function number of its device */
    uint32_t regs[16];      /* configuration registers 0x00-0x3C */
};

struct fake {
    struct fake_fn f[FAKE_MAX];
    int n;
    uint32_t reads;
    uint32_t bad_offsets;           /* reads of a register the scan never needs */
    uint32_t bus_scans[PCI_MAX_BUSES];  /* reads of device 0's ID, per bus */
};

static struct fake fk;

static uint32_t fake_read(void *ctx, uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset)
{
    struct fake *f = ctx;
    f->reads++;
    if (offset != PCI_REG_ID && offset != PCI_REG_CLASS && offset != PCI_REG_HEADER &&
        offset != PCI_REG_BUSES && offset != PCI_REG_BAR5)
        f->bad_offsets++;
    if (offset == PCI_REG_ID && dev == 0 && fn == 0)
        f->bus_scans[bus]++;
    for (int i = 0; i < f->n; i++) {
        const struct fake_fn *e = &f->f[i];
        if (e->bus == bus && e->dev == dev && (e->fn == fn || e->alias))
            return e->regs[offset / 4];
    }
    return 0xFFFFFFFFu;
}

static const struct pci_access fake_acc = { fake_read, &fk };

static void fake_reset(void)
{
    memset(&fk, 0, sizeof fk);
}

/* A function; header is the header type byte (layout, bit 7 multi-function). */
static struct fake_fn *fake_add(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t vendor, uint16_t device,
                           uint8_t cls, uint8_t sub, uint8_t pif, uint8_t header)
{
    struct fake_fn *e = &fk.f[fk.n++];
    e->bus = bus;
    e->dev = dev;
    e->fn = fn;
    e->regs[PCI_REG_ID / 4] = (uint32_t)device << 16 | vendor;
    e->regs[PCI_REG_CLASS / 4] = (uint32_t)cls << 24 | (uint32_t)sub << 16 | (uint32_t)pif << 8 | 0x02u;
    e->regs[PCI_REG_HEADER / 4] = (uint32_t)header << 16;
    return e;
}

/* A PCI-to-PCI bridge on `bus` whose secondary bus is `secondary`. */
static struct fake_fn *fake_bridge(uint8_t bus, uint8_t dev, uint8_t secondary)
{
    struct fake_fn *e = fake_add(bus, dev, 0, 0x1b36, 0x0001, 0x06, 0x04, 0x00, PCI_LAYOUT_BRIDGE);
    e->regs[PCI_REG_BUSES / 4] = (uint32_t)secondary << 16 | (uint32_t)secondary << 8 | bus;
    return e;
}

/* An ICH9 AHCI controller with BAR5 = bar5. */
static struct fake_fn *fake_ahci(uint8_t bus, uint8_t dev, uint8_t fn, uint32_t bar5)
{
    struct fake_fn *e = fake_add(bus, dev, fn, 0x8086, 0x2922, 0x01, 0x06, 0x01, PCI_LAYOUT_DEVICE);
    e->regs[PCI_REG_BAR5 / 4] = bar5;
    return e;
}

/* QEMU's default pc machine (i440FX): 00:00.0 host bridge, 00:01.0-3 the
 * PIIX3 (multi-function: ISA, IDE, ACPI; 00:01.2 absent), 00:02.0 VGA,
 * 00:03.0 the network card. */
static void fake_pc_machine(void)
{
    fake_add(0, 0, 0, 0x8086, 0x1237, 0x06, 0x00, 0x00, 0x00);
    fake_add(0, 1, 0, 0x8086, 0x7000, 0x06, 0x01, 0x00, PCI_HEADER_MULTI);
    fake_add(0, 1, 1, 0x8086, 0x7010, 0x01, 0x01, 0x80, 0x00);
    fake_add(0, 1, 3, 0x8086, 0x7113, 0x06, 0x80, 0x00, 0x00);
    fake_add(0, 2, 0, 0x1234, 0x1111, 0x03, 0x00, 0x00, 0x00);
    fake_add(0, 3, 0, 0x8086, 0x100e, 0x02, 0x00, 0x00, 0x00);
}

static void fake_scan(struct pci_scan *s)
{
    memset(s, 0xA5, sizeof *s);         /* every field must be set by the scan */
    pci_scan(&fake_acc, s);
}

static void test_plain_bus(void)
{
    struct pci_scan s;
    fake_reset();
    fake_pc_machine();
    fake_scan(&s);
    CHECK_EQ(s.buses, 1);
    CHECK_EQ(s.devices, 4);
    CHECK_EQ(s.functions, 6);
    CHECK_EQ(s.bridges, 0);
    CHECK_EQ(s.refused, 0);
    CHECK_EQ(s.ahci, 0);
    CHECK_EQ(s.storage_other, 1);       /* the PIIX3 IDE controller, 01.01.80 */
    CHECK(!s.found);
    CHECK_EQ(s.first_ahci.base, 0);
    CHECK_EQ(s.first_ahci.bar5, 0);
    CHECK_EQ(fk.bad_offsets, 0);
    CHECK_EQ(fk.bus_scans[0], 1);
    /* 32 IDs on bus 0; 4 x (header, class); 7 IDs of 00:01.1-7, 2 x (header, class) */
    CHECK_EQ(fk.reads, 32 + 4 * 2 + 7 + 2 * 2);
}

static void test_ahci_on_bus0(void)
{
    struct pci_scan s;
    fake_reset();
    fake_pc_machine();
    fake_ahci(0, 5, 0, 0xfebf1000u);
    fake_scan(&s);
    CHECK_EQ(s.functions, 7);
    CHECK_EQ(s.ahci, 1);
    CHECK_EQ(s.storage_other, 1);
    CHECK(s.found);
    CHECK_EQ(s.first_ahci.bus, 0);
    CHECK_EQ(s.first_ahci.dev, 5);
    CHECK_EQ(s.first_ahci.fn, 0);
    CHECK_EQ(s.first_ahci.vendor, 0x8086);
    CHECK_EQ(s.first_ahci.device, 0x2922);
    CHECK_EQ(s.first_ahci.bar5, 0xfebf1000u);
    CHECK_EQ(s.first_ahci.base, 0xfebf1000u);
    CHECK_EQ(s.first_ahci.state, PCI_BAR_OK);
    CHECK_EQ(fk.bad_offsets, 0);
}

static void test_bridge(void)
{
    struct pci_scan s;
    fake_reset();
    fake_pc_machine();
    fake_bridge(0, 6, 1);
    fake_ahci(1, 2, 0, 0xfe800000u);
    fake_add(1, 3, 0, 0x8086, 0x100e, 0x02, 0x00, 0x00, 0x00);
    fake_scan(&s);
    CHECK_EQ(s.buses, 2);
    CHECK_EQ(s.devices, 7);
    CHECK_EQ(s.functions, 9);
    CHECK_EQ(s.bridges, 1);
    CHECK_EQ(s.refused, 0);
    CHECK(s.found);
    CHECK_EQ(s.first_ahci.bus, 1);
    CHECK_EQ(s.first_ahci.dev, 2);
    CHECK_EQ(s.first_ahci.base, 0xfe800000u);
    CHECK_EQ(fk.bus_scans[0], 1);
    CHECK_EQ(fk.bus_scans[1], 1);
    CHECK_EQ(fk.bus_scans[2], 0);
    CHECK_EQ(fk.bad_offsets, 0);
    /* A device on a bus no bridge names is never seen. */
    fake_reset();
    fake_pc_machine();
    fake_ahci(2, 0, 0, 0xfe800000u);
    fake_scan(&s);
    CHECK_EQ(s.buses, 1);
    CHECK(!s.found);
    CHECK_EQ(fk.bus_scans[2], 0);
}

static void test_bridges_nested(void)
{
    /* 0 -> 1 -> 2 and 0 -> 3: bus 0, then 1 and 3 (in the order found), then 2. */
    struct pci_scan s;
    fake_reset();
    fake_bridge(0, 1, 1);
    fake_bridge(0, 2, 3);
    fake_bridge(1, 0, 2);
    fake_ahci(2, 4, 0, 0xfe000000u);
    fake_ahci(3, 4, 0, 0xfd000000u);
    fake_scan(&s);
    CHECK_EQ(s.buses, 4);
    CHECK_EQ(s.bridges, 3);
    CHECK_EQ(s.ahci, 2);
    /* Bus 3 is scanned before bus 2, so its controller is the first. */
    CHECK_EQ(s.first_ahci.bus, 3);
    CHECK_EQ(s.first_ahci.base, 0xfd000000u);
}

static void test_multifunction(void)
{
    struct pci_scan s;
    /* A multi-function device: functions 0, 2 and 7 present, the others absent. */
    fake_reset();
    fake_add(0, 4, 0, 0x8086, 0x2918, 0x06, 0x01, 0x00, PCI_HEADER_MULTI);
    fake_add(0, 4, 2, 0x8086, 0x2922, 0x01, 0x06, 0x01, 0x00)->regs[PCI_REG_BAR5 / 4] = 0xfebd5000u;
    fake_add(0, 4, 7, 0x8086, 0x2930, 0x0c, 0x05, 0x00, 0x00);
    fake_scan(&s);
    CHECK_EQ(s.devices, 1);
    CHECK_EQ(s.functions, 3);
    CHECK(s.found);
    CHECK_EQ(s.first_ahci.fn, 2);
    CHECK_EQ(s.first_ahci.base, 0xfebd5000u);
    /* Without bit 7 only function 0 is read, even if others answer. */
    fake_reset();
    fake_add(0, 4, 0, 0x8086, 0x2918, 0x06, 0x01, 0x00, 0x00);
    fake_ahci(0, 4, 2, 0xfebd5000u);
    fake_scan(&s);
    CHECK_EQ(s.functions, 1);
    CHECK(!s.found);
    /* A single-function device that answers for every function number is
     * counted once (functions 1-7 are not read). */
    fake_reset();
    fake_ahci(0, 7, 0, 0xfebd0000u)->alias = true;
    fake_scan(&s);
    CHECK_EQ(s.devices, 1);
    CHECK_EQ(s.functions, 1);
    CHECK_EQ(s.ahci, 1);
    CHECK_EQ(fk.reads, 32 + 2 + 1);     /* the IDs, its header and class, its BAR5 */
    /* A multi-function bit on a function other than 0 is ignored. */
    fake_reset();
    fake_add(0, 4, 0, 0x8086, 0x2918, 0x06, 0x01, 0x00, PCI_HEADER_MULTI);
    fake_add(0, 4, 1, 0x8086, 0x2919, 0x06, 0x01, 0x00, PCI_HEADER_MULTI);
    fake_scan(&s);
    CHECK_EQ(s.functions, 2);
    CHECK_EQ(s.devices, 1);
}

static void test_absent(void)
{
    struct pci_scan s;
    /* An empty bus: 32 IDs read, nothing found. */
    fake_reset();
    fake_scan(&s);
    CHECK_EQ(s.buses, 1);
    CHECK_EQ(s.devices, 0);
    CHECK_EQ(s.functions, 0);
    CHECK(!s.found);
    CHECK_EQ(fk.reads, 32);
    /* Function 0 absent: the device is absent, its function 1 never read. */
    fake_reset();
    fake_ahci(0, 9, 1, 0xfebf1000u);
    fake_scan(&s);
    CHECK_EQ(s.devices, 0);
    CHECK(!s.found);
    /* A vendor of 0xFFFF with another device ID is still absent. */
    fake_reset();
    fake_add(0, 9, 0, 0xFFFF, 0x2922, 0x01, 0x06, 0x01, 0x00);
    fake_scan(&s);
    CHECK_EQ(s.devices, 0);
    /* Gaps between present devices; device 31 is scanned. */
    fake_reset();
    fake_add(0, 0, 0, 0x8086, 0x29c0, 0x06, 0x00, 0x00, 0x00);
    fake_ahci(0, 31, 0, 0xfebf1000u);
    fake_scan(&s);
    CHECK_EQ(s.devices, 2);
    CHECK(s.found);
    CHECK_EQ(s.first_ahci.dev, 31);
}

static void test_bridge_loops(void)
{
    struct pci_scan s;
    /* A bridge whose secondary bus is its own bus, one naming bus 0, and two
     * naming the same bus: each refused bus is counted, every bus scanned once. */
    fake_reset();
    fake_bridge(0, 1, 1);
    fake_bridge(1, 0, 1);            /* itself */
    fake_bridge(1, 1, 0);            /* back to bus 0 */
    fake_bridge(0, 2, 1);            /* bus 1 again */
    fake_bridge(1, 2, 2);
    fake_bridge(2, 0, 1);            /* a loop 1 -> 2 -> 1 */
    fake_scan(&s);
    CHECK_EQ(s.buses, 3);
    CHECK_EQ(s.bridges, 6);
    CHECK_EQ(s.refused, 4);
    CHECK_EQ(fk.bus_scans[0], 1);
    CHECK_EQ(fk.bus_scans[1], 1);
    CHECK_EQ(fk.bus_scans[2], 1);
    /* A chain through every bus number, 0 -> 1 -> ... -> 255, and bus 255's
     * bridge back to 0: 256 buses, each scanned once, the scan ends. */
    fake_reset();
    for (uint32_t b = 0; b < 255; b++)
        fake_bridge((uint8_t)b, 0, (uint8_t)(b + 1));
    fake_bridge(255, 0, 0);
    fake_ahci(255, 1, 0, 0xfeb00000u);
    fake_scan(&s);
    CHECK_EQ(s.buses, 256);
    CHECK_EQ(s.bridges, 256);
    CHECK_EQ(s.refused, 1);
    CHECK(s.found);
    CHECK_EQ(s.first_ahci.bus, 255);
    uint32_t once = 0;
    for (uint32_t b = 0; b < PCI_MAX_BUSES; b++)
        once += fk.bus_scans[b] == 1;
    CHECK_EQ(once, 256);
    CHECK_EQ(fk.bad_offsets, 0);
    /* The queue's guard: the queue never takes more than PCI_MAX_BUSES. */
    struct pci_walk w;
    for (uint32_t i = 0; i < PCI_MAX_BUSES / 32; i++)
        w.seen[i] = 0;
    w.head = 0;
    w.tail = 0;
    w.seen[0] = 1u;
    uint32_t taken = 0;
    for (uint32_t b = 0; b < 300; b++)
        taken += pci_queue_bus(&w, b);
    CHECK_EQ(taken, 255);
    CHECK_EQ(w.tail, 255);
    CHECK(!pci_queue_bus(&w, 256));
}

static void test_bar5(void)
{
    struct pci_scan s;
    CHECK_EQ(pci_bar5_state(0xfebf1000u), PCI_BAR_OK);
    CHECK_EQ(pci_bar5_state(0xfebf1008u), PCI_BAR_OK);     /* prefetchable bit: still 32-bit memory */
    CHECK_EQ(pci_bar5_state(0x00000000u), PCI_BAR_UNASSIGNED);
    CHECK_EQ(pci_bar5_state(0x00000008u), PCI_BAR_UNASSIGNED);
    CHECK_EQ(pci_bar5_state(0xfebf1004u), PCI_BAR_64BIT);
    CHECK_EQ(pci_bar5_state(0x0000000cu), PCI_BAR_64BIT);
    CHECK_EQ(pci_bar5_state(0x0000c001u), PCI_BAR_IO);
    CHECK_EQ(pci_bar5_state(0xfebf1002u), PCI_BAR_RESERVED);
    CHECK_EQ(pci_bar5_state(0xfebf1006u), PCI_BAR_RESERVED);
    CHECK(strcmp(pci_bar_reason(PCI_BAR_OK), "") == 0);
    CHECK(strcmp(pci_bar_reason(PCI_BAR_64BIT), "a 64-bit BAR (its address may lie above 4 GiB)") == 0);
    CHECK(strcmp(pci_bar_reason(PCI_BAR_IO), "an I/O BAR, not memory") == 0);
    CHECK(strcmp(pci_bar_reason(PCI_BAR_RESERVED), "a BAR of the reserved memory type") == 0);
    CHECK(strcmp(pci_bar_reason(PCI_BAR_UNASSIGNED), "no address assigned") == 0);
    CHECK(strcmp(pci_bar_reason(PCI_BAR_NOT_DEVICE), "not a device header (layout 0)") == 0);
    CHECK(strcmp(pci_bar_reason((enum pci_bar)99), "unknown") == 0);
    /* A 64-bit BAR5, its upper half (register 0x28) naming an address above
     * 4 GiB: refused without reading 0x28; the register kept for the log. */
    static const uint32_t refused[] = { 0xfebf100cu, 0x0000c001u, 0xfebf1002u, 0x00000000u };
    static const enum pci_bar states[] = { PCI_BAR_64BIT, PCI_BAR_IO, PCI_BAR_RESERVED, PCI_BAR_UNASSIGNED };
    for (int i = 0; i < 4; i++) {
        fake_reset();
        struct fake_fn *e = fake_ahci(0, 5, 0, refused[i]);
        e->regs[0x28 / 4] = 0x00000001u;
        fake_scan(&s);
        CHECK(s.found);
        CHECK_EQ(s.first_ahci.state, states[i]);
        CHECK_EQ(s.first_ahci.bar5, refused[i]);
        CHECK_EQ(s.first_ahci.base, 0);
        CHECK_EQ(fk.bad_offsets, 0);
    }
    /* An AHCI class code on a bridge header: no BAR5 read. */
    fake_reset();
    fake_add(0, 5, 0, 0x8086, 0x2922, 0x01, 0x06, 0x01, PCI_LAYOUT_BRIDGE)->regs[PCI_REG_BAR5 / 4] = 0xfebf1000u;
    fake_scan(&s);
    CHECK(s.found);
    CHECK_EQ(s.first_ahci.state, PCI_BAR_NOT_DEVICE);
    CHECK_EQ(s.first_ahci.bar5, 0);
    CHECK_EQ(s.first_ahci.base, 0);
    CHECK_EQ(s.bridges, 1);
}

static void test_not_ahci(void)
{
    /* Mass storage that is not AHCI: IDE, SATA with a vendor interface (00),
     * NVMe (01.08.02), SCSI (01.00.00); and an AHCI interface under another
     * class (a SATA-looking 0x0c bus controller). None is the controller. */
    struct pci_scan s;
    fake_reset();
    fake_add(0, 1, 0, 0x8086, 0x7010, 0x01, 0x01, 0x80, 0x00);
    fake_add(0, 2, 0, 0x8086, 0x2920, 0x01, 0x06, 0x00, 0x00)->regs[PCI_REG_BAR5 / 4] = 0xfebf1000u;
    fake_add(0, 3, 0, 0x1b36, 0x0010, 0x01, 0x08, 0x02, 0x00);
    fake_add(0, 4, 0, 0x1000, 0x0012, 0x01, 0x00, 0x00, 0x00);
    fake_add(0, 5, 0, 0x8086, 0x2922, 0x0c, 0x06, 0x01, 0x00)->regs[PCI_REG_BAR5 / 4] = 0xfebf2000u;
    fake_add(0, 6, 0, 0x8086, 0x2922, 0x01, 0x05, 0x01, 0x00)->regs[PCI_REG_BAR5 / 4] = 0xfebf3000u;
    fake_scan(&s);
    CHECK_EQ(s.functions, 6);
    CHECK_EQ(s.ahci, 0);
    CHECK_EQ(s.storage_other, 5);
    CHECK(!s.found);
    CHECK_EQ(fk.reads, 32 + 6 * 2);     /* no BAR5 read */
}

static void test_two_ahci(void)
{
    struct pci_scan s;
    fake_reset();
    fake_ahci(0, 8, 0, 0xfe000000u);
    fake_ahci(0, 3, 0, 0xfd000000u);
    fake_scan(&s);
    CHECK_EQ(s.ahci, 2);
    CHECK_EQ(s.first_ahci.dev, 3);      /* the first in scan order */
    CHECK_EQ(s.first_ahci.base, 0xfd000000u);
    CHECK_EQ(fk.reads, 32 + 2 * 2 + 1); /* only the first one's BAR5 is read */
}

int main(void)
{
    printf("test_pci_core\n");
    RUN(test_plain_bus);
    RUN(test_ahci_on_bus0);
    RUN(test_bridge);
    RUN(test_bridges_nested);
    RUN(test_multifunction);
    RUN(test_absent);
    RUN(test_bridge_loops);
    RUN(test_bar5);
    RUN(test_not_ahci);
    RUN(test_two_ahci);
    return unit_report("pci_core");
}
