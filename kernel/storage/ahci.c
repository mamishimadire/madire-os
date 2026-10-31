/* The AHCI driver at boot (1.4 stage 4): see ahci.h. Read only. */
#include "kernel/storage/ahci.h"
#include "kernel/kprintf.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/storage/ahci_core.h"
#include "kernel/storage/blk.h"
#include "kernel/timer.h"

typedef unsigned long long ull;

static struct ahci_hba ahci_hba;
static volatile uint8_t *ahci_regs;          /* the controller's registers (BAR5) */
static uint32_t ahci_dma_slots[VM_STORAGE_DMA_PAGES / 32];  /* VM_STORAGE_BASE's pages in use */
static uint32_t ahci_dma_pages;             /* DMA pages mapped now */

static uint32_t ahci_reg_read(void *ctx, uint32_t off)
{
    (void)ctx;
    return *(volatile uint32_t *)(ahci_regs + off);
}

static void ahci_reg_write(void *ctx, uint32_t off, uint32_t value)
{
    (void)ctx;
    *(volatile uint32_t *)(ahci_regs + off) = value;
}

static uint64_t ahci_clock(void *ctx)
{
    (void)ctx;
    return timer_ticks();
}

/* A zeroed page of free RAM, mapped write-back at the next free page of
 * the storage area's DMA part. */
static bool ahci_page(void *ctx, volatile uint8_t **virt, uint32_t *phys)
{
    (void)ctx;
    uint32_t slot = 0;
    while (slot < VM_STORAGE_DMA_PAGES && ((ahci_dma_slots[slot / 32] >> (slot % 32)) & 1u))
        slot++;
    if (slot == VM_STORAGE_DMA_PAGES)
        return false;
    uint32_t frame = memory_alloc_page();
    if (!frame)
        return false;
    uint64_t va = VM_STORAGE_BASE + (uint64_t)slot * VM_PAGE_SIZE;
    if (paging_map(va, frame, PTE_WRITE | paging_nx_flag()) != VMM_OK) {
        memory_free_page(frame);
        return false;
    }
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)va;
    for (uint32_t i = 0; i < VM_PAGE_SIZE; i++)
        p[i] = 0;
    ahci_dma_slots[slot / 32] |= 1u << (slot % 32);
    ahci_dma_pages++;
    *virt = p;
    *phys = frame;
    return true;
}

static void ahci_unpage(void *ctx, volatile uint8_t *virt, uint32_t phys)
{
    (void)ctx;
    uint64_t va = (uint64_t)(uintptr_t)virt, got = 0;
    uint32_t slot = (uint32_t)((va - VM_STORAGE_BASE) / VM_PAGE_SIZE);
    if (paging_unmap(va, &got) != VMM_OK || got != phys)
        return;                             /* not ours: never freed */
    memory_free_page(phys);
    ahci_dma_slots[slot / 32] &= ~(1u << (slot % 32));
    ahci_dma_pages--;
}

static const struct ahci_ops ahci_kernel_ops = {
    ahci_reg_read, ahci_reg_write, ahci_clock, ahci_page, ahci_unpage, 0, TIMER_HZ,
};

/* Maps `pages` pages of registers from physical page `phys` at
 * VM_DEVREG_BASE + `first` pages, uncached. */
static bool ahci_map_regs(uint32_t phys, uint32_t first, uint32_t pages)
{
    for (uint32_t i = first; i < pages; i++)
        if (paging_map(VM_DEVREG_BASE + (uint64_t)i * VM_PAGE_SIZE, phys + i * VM_PAGE_SIZE,
                       PTE_WRITE | PTE_UNCACHED | paging_nx_flag()) != VMM_OK)
            return false;
    return true;
}

/* "64 MiB", "30 GiB", "1.5 GiB": the size of `sectors` 512-byte sectors. */
static void ahci_size(char *buf, size_t size, uint64_t sectors)
{
    uint64_t mib = sectors >> 11, tenths = (sectors * 10u) >> 21;
    if (mib < 10240u)
        ksnprintf(buf, size, "%llu MiB", (ull)mib);
    else if (tenths % 10u == 0)
        ksnprintf(buf, size, "%llu GiB", (ull)(tenths / 10u));
    else
        ksnprintf(buf, size, "%llu.%llu GiB", (ull)(tenths / 10u), (ull)(tenths % 10u));
}

static void ahci_log_port(uint32_t p)
{
    const struct ahci_port *pt = &ahci_hba.port[p];
    char size[24];
    switch (pt->state) {
    case AHCI_PORT_NONE:
    case AHCI_PORT_EMPTY:
        return;
    case AHCI_PORT_STUCK:
        klogf("ahci: port %u: %s (PxCMD 0x%08x)\n", p, ahci_state_str(pt->state), pt->cmd_stuck);
        return;
    case AHCI_PORT_ATAPI:
    case AHCI_PORT_OTHER:
        klogf("ahci: port %u: signature 0x%08x: %s\n", p, pt->sig, ahci_state_str(pt->state));
        return;
    case AHCI_PORT_DISK:
    case AHCI_PORT_REFUSED:
    case AHCI_PORT_FAILED:
        break;
    }
    if (!pt->disk.sectors && pt->state == AHCI_PORT_FAILED) {
        klogf("ahci: port %u: signature 0x%08x (ATA disk): %s: %s\n", p, pt->sig, ahci_state_str(pt->state),
              pt->why ? pt->why : "unknown");
        return;
    }
    ahci_size(size, sizeof(size), pt->disk.sectors);
    klogf("ahci: port %u: signature 0x%08x (ATA disk); IDENTIFY DEVICE: model '%s', %llu sectors of %u bytes "
          "(%s), LBA48 %s, DMA %s; %s%s%s\n", p, pt->sig, pt->disk.model, (ull)pt->disk.sectors,
          pt->disk.sector_size, size, pt->disk.lba48 ? "yes" : "no", pt->disk.dma ? "yes" : "no",
          pt->state == AHCI_PORT_DISK ? "used read only" : ahci_state_str(pt->state),
          pt->why ? ": " : "", pt->why ? pt->why : "");
}

/* The one line about the last error of port p. */
static void ahci_log_error(uint32_t p)
{
    const struct ahci_port *pt = &ahci_hba.port[p];
    const struct ahci_error *e = &pt->last;
    klogf("ahci: port %u: %s LBA %llu, %u sectors failed: %s (PxIS 0x%08x, PxTFD 0x%08x, PxSERR 0x%08x, "
          "PxCI 0x%08x); %s; %s; error %u of %u, then EIO\n", p, ahci_op_str(e->op), (ull)e->lba, e->sectors,
          ahci_end_str(e->end), e->is, e->tfd, e->serr, e->ci,
          e->reset ? "port reset (COMRESET) once" : "no port reset",
          e->recovered ? (pt->state == AHCI_PORT_DISK ? "port running again" : "disk marked failed")
                       : "port not recovered: disk marked failed",
          pt->errors, AHCI_ERRORS_FAILED);
}

int ahci_disk_read(uint32_t port, uint64_t lba, uint32_t sectors, const uint32_t *phys, uint32_t pages)
{
    if (port >= AHCI_PORTS)
        return -AHCI_EIO;
    uint32_t errors = ahci_hba.port[port].errors;
    int r = ahci_read(&ahci_hba, port, lba, sectors, phys, pages);
    if (ahci_hba.port[port].errors != errors)
        ahci_log_error(port);
    return r;
}

void ahci_figures(uint64_t *commands, uint64_t *polls_longest, uint64_t *ticks_longest)
{
    *commands = ahci_hba.commands;
    *polls_longest = ahci_hba.polls_longest;
    *ticks_longest = ahci_hba.ticks_longest;
}

void ahci_boot(const struct pci_ahci *a)
{
    char where[12], size[24];
    ksnprintf(where, sizeof(where), "%02x:%02x.%x", a->bus, a->dev, a->fn);
    if (!paging_enabled()) {
        klogf("Storage: AHCI %s, not used: paging is off (Live mode)\n", where);
        return;
    }
    uint32_t tables0 = paging_stats()->page_tables, page = a->base & ~(uint32_t)(VM_PAGE_SIZE - 1);
    uint32_t off = a->base - page;
    if (!ahci_map_regs(page, 0, 1)) {
        klogf("Storage: AHCI %s, not used: its registers could not be mapped (Live mode)\n", where);
        return;
    }
    ahci_regs = (volatile uint8_t *)(uintptr_t)(VM_DEVREG_BASE + off);
    uint32_t pi = ahci_reg_read(0, AHCI_REG_PI);
    uint32_t pages = (uint32_t)((off + ahci_regs_bytes(pi) + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE);
    if (pages > VM_DEVREG_PAGES || !ahci_map_regs(page, 1, pages)) {
        klogf("Storage: AHCI %s, not used: its registers (%u pages) could not be mapped (Live mode)\n", where,
              pages);
        return;
    }
    uint64_t pte_phys = 0, pte_flags = 0;
    paging_translate(VM_DEVREG_BASE, &pte_phys, &pte_flags);
    klogf("ahci: slot 508: registers 0x%08x at 0x%016llx (%u pages, uncached: PCD and PWT); the first page's "
          "entry: frame 0x%08llx, flags 0x%llx\n", a->base, (ull)(VM_DEVREG_BASE + off), pages, (ull)pte_phys,
          (ull)pte_flags);

    ahci_start(&ahci_hba, &ahci_kernel_ops);
    const struct ahci_hba *h = &ahci_hba;
    uint32_t implemented = 0, empty = 0;
    for (uint32_t p = 0; p < AHCI_PORTS; p++) {
        implemented += (h->pi >> p) & 1u;
        empty += h->port[p].state == AHCI_PORT_EMPTY;
    }
    klogf("ahci: AHCI version 0x%08x, CAP 0x%08x, CAP2 0x%08x, %u ports implemented (PI 0x%08x); BIOS hand-off: "
          "%s; GHC 0x%08x -> 0x%08x (AHCI mode on, interrupts off); polling\n", h->vs, h->cap, h->cap2, implemented,
          h->pi,
          !h->handoff ? "not offered" : ahci_end_str(h->handoff_end), h->ghc_before, h->ghc);
    for (uint32_t p = 0; p < h->ports; p++)
        ahci_log_port(p);
    klogf("ahci: %u of %u implemented ports empty (no device)\n", empty, implemented);

    int32_t disk = -1, refused = -1;
    uint32_t disks = 0;
    for (uint32_t p = 0; p < h->ports; p++) {
        if (h->port[p].state == AHCI_PORT_DISK) {
            if (disk < 0)
                disk = (int32_t)p;
            disks++;
        } else if (refused < 0 && (h->port[p].state == AHCI_PORT_REFUSED || h->port[p].state == AHCI_PORT_FAILED)) {
            refused = (int32_t)p;
        }
    }
    if (disk >= 0)
        blk_attach((uint32_t)disk, h->port[disk].disk.sectors);
    uint32_t tables, mapped;
    paging_slot_figures(VM_STORAGE_INDEX, &tables, &mapped);
    klogf("ahci: slot 508 now: %u page tables, %u pages mapped (walked); %u page tables added, %u DMA pages, "
          "%u cache pages\n", tables, mapped, paging_stats()->page_tables - tables0, ahci_dma_pages,
          blk_pages_made());
    if (disk >= 0) {
        const struct ahci_port *pt = &h->port[disk];
        ahci_size(size, sizeof(size), pt->disk.sectors);
        klogf("Storage: AHCI %s, port %d disk %s %s, read only, not mounted%s (Live mode)\n", where, disk,
              pt->disk.model, size, disks > 1 ? "; more disks not used" : "");
    } else if (refused >= 0) {
        klogf("Storage: AHCI %s, no usable disk (port %d: %s) (Live mode)\n", where, refused,
              h->port[refused].why ? h->port[refused].why : "unknown");
    } else {
        klogf("Storage: AHCI %s, no disk (Live mode)\n", where);
    }
}
