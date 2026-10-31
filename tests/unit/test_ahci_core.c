/* Unit tests for kernel/storage/ahci_core.c (the AHCI driver's pure part,
 * 1.4 stage 4) against a fake controller: the registers of an HBA and its
 * ports, a clock that advances with register reads (or stops), DMA pages,
 * and a fake disk and ATAPI device. The fake carries out each command it
 * is issued: it reads the command header, the FIS and the physical-region
 * entries from the DMA pages, fills the buffers, sets PxIS, PxTFD and the
 * header's byte count, and records every opcode and every header's write
 * bit. Injected: a task-file error, a command that never completes (with
 * the device left busy until a COMRESET), a short transfer, an engine that
 * does not stop, a device that stays busy, a late or missing signature,
 * a BIOS that keeps the controller, and a stopped clock. Every test also
 * proves that only 0xEC and 0x25 were ever sent and that no header asked
 * for a write. The module is included directly. */
#include <stdbool.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/storage/ahci_core.c"

#define FK_PAGES    16
#define FK_PHYS0    0x00200000u

struct fk_port {
    bool device, atapi, sig_late, sig_never, stuck, busy_forever;
    uint32_t sig;
    uint32_t clb, fb, is, ie, cmd, tfd, ssts, sctl, serr, ci;
    uint32_t commands, resets, sctl_reset;
    int fail_next, hang_next, short_next;
    bool busy_until_reset;
    uint64_t sectors;
    uint16_t ident[256];
};

struct fk {
    uint32_t cap, cap2, ghc, pi, vs, bohc;
    int bos_delay;              /* reads of BOHC until the BIOS lets go; -1 never */
    bool oos_written;
    uint32_t ghc_writes, hr_written;
    struct fk_port port[AHCI_PORTS];
    uint64_t ticks;
    uint32_t reads_per_tick;    /* 0: the clock is stopped */
    uint64_t reads;
    uint32_t opcodes[256];
    uint32_t write_bits, bad_dma, bad_prd;
    uint32_t prd_bytes[AHCI_PRD_MAX], prd_n;
    uint8_t mem[FK_PAGES][4096] __attribute__((aligned(4096)));
    bool used[FK_PAGES];
    uint32_t pages_out, pages_taken;
    bool no_pages;
};

static struct fk fk;

static uint8_t *fk_phys(uint32_t phys, uint32_t len)
{
    if (phys < FK_PHYS0)
        return 0;
    uint32_t i = (phys - FK_PHYS0) / 4096u, off = (phys - FK_PHYS0) % 4096u;
    if (i >= FK_PAGES || !fk.used[i] || off + len > 4096u)
        return 0;
    return &fk.mem[i][off];
}

static uint8_t fk_byte(uint64_t lba, uint32_t i)
{
    return (uint8_t)(lba * 31u + i * 7u + (lba >> 8) + 1u);
}

static void fk_ident(struct fk_port *p, const char *model, uint64_t sectors, bool lba48, bool dma, uint32_t ssize)
{
    for (int i = 0; i < 256; i++)
        p->ident[i] = 0;
    char m[40];
    size_t n = strlen(model);
    for (int i = 0; i < 40; i++)
        m[i] = (size_t)i < n ? model[i] : ' ';
    for (int i = 0; i < 20; i++)
        p->ident[27 + i] = (uint16_t)((uint8_t)m[2 * i] << 8 | (uint8_t)m[2 * i + 1]);
    if (dma)
        p->ident[49] = 1u << 8;
    if (lba48)
        p->ident[83] = 1u << 10;
    p->ident[60] = (uint16_t)(sectors > 0x0FFFFFFFu ? 0xFFFF : sectors);
    p->ident[61] = (uint16_t)(sectors > 0x0FFFFFFFu ? 0x0FFF : sectors >> 16);
    for (int i = 0; i < 4; i++)
        p->ident[100 + i] = (uint16_t)(sectors >> (16 * i));
    if (ssize != 512) {
        p->ident[106] = 0x4000u | (1u << 12);
        p->ident[117] = (uint16_t)(ssize / 2);
        p->ident[118] = (uint16_t)((ssize / 2) >> 16);
    }
    p->sectors = sectors;
}

static void fk_disk(uint32_t i, uint64_t sectors)
{
    struct fk_port *p = &fk.port[i];
    p->device = true;
    p->sig = AHCI_SIG_ATA;
    p->ssts = 0x113u;
    p->tfd = 0x50u;
    fk_ident(p, "QEMU HARDDISK", sectors, true, true, 512);
}

static void fk_cd(uint32_t i)
{
    struct fk_port *p = &fk.port[i];
    p->device = p->atapi = true;
    p->sig = AHCI_SIG_ATAPI;
    p->ssts = 0x113u;
    p->tfd = 0x50u;
}

/* A QEMU-like ich9-ahci: 6 ports, nothing attached; then the test adds. */
static void fk_reset(void)
{
    memset(&fk, 0, sizeof(fk));
    fk.cap = 0x40141F05u;           /* S64A, ... 6 ports (NP 5) */
    fk.pi = 0x3Fu;
    fk.vs = 0x00010000u;
    fk.bos_delay = -1;
    fk.reads_per_tick = 10;
    for (uint32_t i = 0; i < AHCI_PORTS; i++) {
        fk.port[i].sig = AHCI_SIG_NONE;
        fk.port[i].tfd = 0x7Fu;
    }
}

static uint32_t fk_cmd(const struct fk_port *p)
{
    uint32_t c = p->cmd & (AHCI_CMD_ST | AHCI_CMD_FRE);
    if ((p->cmd & AHCI_CMD_ST) || p->stuck)
        c |= AHCI_CMD_CR;
    if (p->cmd & AHCI_CMD_FRE)
        c |= AHCI_CMD_FR;
    return c;
}

static uint32_t fk_read(void *ctx, uint32_t off)
{
    (void)ctx;
    fk.reads++;
    if (fk.reads_per_tick && fk.reads % fk.reads_per_tick == 0)
        fk.ticks++;
    if (off < 0x100u) {
        switch (off) {
        case AHCI_REG_CAP:  return fk.cap;
        case AHCI_REG_GHC:  return fk.ghc;
        case AHCI_REG_PI:   return fk.pi;
        case AHCI_REG_VS:   return fk.vs;
        case AHCI_REG_CAP2: return fk.cap2;
        case AHCI_REG_BOHC:
            if (fk.oos_written && fk.bos_delay >= 0 && fk.bos_delay-- == 0)
                fk.bohc &= ~AHCI_BOHC_BOS;
            return fk.bohc;
        }
        return 0;
    }
    struct fk_port *p = &fk.port[(off - 0x100u) / 0x80u];
    switch ((off - 0x100u) % 0x80u) {
    case AHCI_PX_CLB:  return p->clb;
    case AHCI_PX_FB:   return p->fb;
    case AHCI_PX_IS:   return p->is;
    case AHCI_PX_IE:   return p->ie;
    case AHCI_PX_CMD:  return fk_cmd(p);
    case AHCI_PX_TFD:  return p->busy_forever ? 0x80u : p->tfd;
    case AHCI_PX_SIG:
        if (p->sig_never || (p->sig_late && !(p->cmd & AHCI_CMD_FRE)))
            return AHCI_SIG_NONE;
        return p->device ? p->sig : AHCI_SIG_NONE;
    case AHCI_PX_SSTS: return p->device ? p->ssts : 0;
    case AHCI_PX_SCTL: return p->sctl;
    case AHCI_PX_SERR: return p->serr;
    case AHCI_PX_CI:   return p->ci;
    }
    return 0;
}

/* Carries out the command in slot 0. */
static void fk_execute(struct fk_port *p)
{
    uint32_t *hdr = (uint32_t *)fk_phys(p->clb, 32);
    if (!hdr || !fk_phys(p->fb, 256)) {
        fk.bad_dma++;
        return;
    }
    uint32_t dw0 = hdr[0], n = dw0 >> 16;
    if (dw0 & (1u << 6))
        fk.write_bits++;
    uint8_t *t = fk_phys(hdr[2], AHCI_TABLE_PRDT + 16u * n);
    if (!t || (hdr[2] & 0x7Fu) || (dw0 & 0x1Fu) != 5u) {
        fk.bad_dma++;
        return;
    }
    uint8_t op = t[2];
    fk.opcodes[op]++;
    p->commands++;
    if (p->hang_next > 0) {
        p->hang_next--;
        p->ci |= 1u;
        if (p->busy_until_reset)
            p->tfd = 0xD0u;                 /* BSY */
        return;
    }
    if (p->fail_next > 0) {
        p->fail_next--;
        p->is |= 1u << 30;                  /* TFES */
        p->tfd = 0x0451u;                   /* ABRT, DRDY, ERR */
        p->ci |= 1u;
        return;
    }
    uint64_t lba = (uint64_t)t[4] | (uint64_t)t[5] << 8 | (uint64_t)t[6] << 16 | (uint64_t)t[8] << 24 |
                   (uint64_t)t[9] << 32 | (uint64_t)t[10] << 40;
    uint32_t count = (uint32_t)t[12] | (uint32_t)t[13] << 8, want = op == 0xECu ? 512u : count * 512u, moved = 0;
    uint32_t *prd = (uint32_t *)(t + AHCI_TABLE_PRDT);
    fk.prd_n = n;
    for (uint32_t i = 0; i < n && moved < want; i++) {
        uint32_t bytes = (prd[4 * i + 3] & 0x3FFFFFu) + 1u;
        if (i < AHCI_PRD_MAX)
            fk.prd_bytes[i] = bytes;
        if (prd[4 * i + 3] & 0x80000000u)
            fk.bad_prd++;
        uint8_t *dst = fk_phys(prd[4 * i], bytes);
        if (!dst) {
            fk.bad_dma++;
            return;
        }
        for (uint32_t k = 0; k < bytes && moved < want; k++, moved++) {
            if (op == 0xECu)
                dst[k] = (uint8_t)(p->ident[moved / 2] >> (8 * (moved % 2)));
            else
                dst[k] = fk_byte(lba + moved / 512u, moved % 512u);
        }
    }
    if (op == 0x25u && (lba + count > p->sectors || t[7] != 0x40u))
        fk.bad_prd++;
    if (p->short_next > 0) {
        p->short_next--;
        moved -= 512u;
    }
    hdr[1] = moved;
    p->tfd = 0x50u;
    p->is |= 1u;                            /* D2H register FIS */
    p->ci &= ~1u;
}

static void fk_write(void *ctx, uint32_t off, uint32_t v)
{
    (void)ctx;
    if (off < 0x100u) {
        if (off == AHCI_REG_GHC) {
            fk.ghc_writes++;
            if (v & 1u)
                fk.hr_written++;
            fk.ghc = v & (AHCI_GHC_AE | AHCI_GHC_IE);
        } else if (off == AHCI_REG_BOHC && (v & AHCI_BOHC_OOS)) {
            fk.oos_written = true;
            fk.bohc |= AHCI_BOHC_OOS;
        }
        return;
    }
    struct fk_port *p = &fk.port[(off - 0x100u) / 0x80u];
    switch ((off - 0x100u) % 0x80u) {
    case AHCI_PX_CLB:  p->clb = v; break;
    case AHCI_PX_FB:   p->fb = v; break;
    case AHCI_PX_IS:   p->is &= ~v; break;
    case AHCI_PX_IE:   p->ie = v; break;
    case AHCI_PX_SERR: p->serr &= ~v; break;
    case AHCI_PX_CMD:
        if ((p->cmd & AHCI_CMD_ST) && !(v & AHCI_CMD_ST))
            p->ci = 0;                      /* clearing ST clears PxCI */
        p->cmd = v & (AHCI_CMD_ST | AHCI_CMD_FRE);
        break;
    case AHCI_PX_SCTL:
        if ((v & 0xFu) == 1u)
            p->sctl_reset = 1;
        else if (p->sctl_reset) {
            p->sctl_reset = 0;
            p->resets++;
            p->tfd = 0x50u;
            p->ci = 0;
        }
        p->sctl = v;
        break;
    case AHCI_PX_CI:
        if ((v & 1u) && (p->cmd & AHCI_CMD_ST))
            fk_execute(p);
        break;
    }
}

static uint64_t fk_ticks(void *ctx)
{
    (void)ctx;
    return fk.ticks;
}

static bool fk_page(void *ctx, volatile uint8_t **virt, uint32_t *phys)
{
    (void)ctx;
    if (fk.no_pages)
        return false;
    for (int i = 0; i < FK_PAGES; i++) {
        if (fk.used[i])
            continue;
        fk.used[i] = true;
        memset(fk.mem[i], 0, 4096);
        *virt = fk.mem[i];
        *phys = FK_PHYS0 + (uint32_t)i * 4096u;
        fk.pages_out++;
        fk.pages_taken++;
        return true;
    }
    return false;
}

static void fk_unpage(void *ctx, volatile uint8_t *virt, uint32_t phys)
{
    (void)ctx;
    uint32_t i = (phys - FK_PHYS0) / 4096u;
    if (i < FK_PAGES && fk.used[i] && virt == fk.mem[i]) {
        fk.used[i] = false;
        fk.pages_out--;
    }
}

static const struct ahci_ops fk_ops = { fk_read, fk_write, fk_ticks, fk_page, fk_unpage, 0, 100 };
static struct ahci_hba hba;

/* A page for a read's data (as the block cache's pages). */
static uint32_t fk_data_page(void)
{
    volatile uint8_t *v;
    uint32_t phys = 0;
    CHECK(fk_page(0, &v, &phys));
    return phys;
}

static bool fk_data_ok(uint32_t phys, uint64_t lba, uint32_t bytes)
{
    const uint8_t *d = fk_phys(phys, bytes);
    for (uint32_t k = 0; d && k < bytes; k++)
        if (d[k] != fk_byte(lba + k / 512u, k % 512u))
            return false;
    return d != 0;
}

/* After every test: only the two read-only opcodes, never a write bit,
 * every DMA address one of the fake's pages. */
static void check_read_only(void)
{
    uint32_t other = 0;
    for (int i = 0; i < 256; i++)
        if (i != 0xEC && i != 0x25)
            other += fk.opcodes[i];
    CHECK_EQ(other, 0);
    CHECK_EQ(fk.write_bits, 0);
    CHECK_EQ(fk.bad_dma, 0);
    CHECK_EQ(fk.bad_prd, 0);
    CHECK_EQ(fk.hr_written, 0);
}

static void test_opcodes(void)
{
    CHECK_EQ(ahci_opcode(AHCI_OP_IDENTIFY), 0xEC);
    CHECK_EQ(ahci_opcode(AHCI_OP_READ), 0x25);
    CHECK_EQ(ahci_opcode(AHCI_OPS), 0);
    CHECK_EQ(sizeof(ahci_opcodes), 2);
    CHECK_EQ(ahci_regs_bytes(0), 0x100);
    CHECK_EQ(ahci_regs_bytes(0x3Fu), 0x400);
    CHECK_EQ(ahci_regs_bytes(0x3FFFFFFFu), 0x1000);
    CHECK_EQ(ahci_regs_bytes(0x80000000u), 0x1100);
    CHECK_EQ(ahci_regs_bytes(0x5u), 0x280);
}

/* As QEMU's ich9-ahci with a disk on port 0 and a CD on port 1, both left
 * running by the BIOS with its own command lists. */
static void test_start_qemu_like(void)
{
    fk_reset();
    fk_disk(0, 131075);
    fk_cd(1);
    fk.port[0].cmd = fk.port[1].cmd = AHCI_CMD_ST | AHCI_CMD_FRE;
    fk.port[0].clb = fk.port[1].clb = 0x9F000u;          /* the BIOS's */
    fk.ghc = AHCI_GHC_AE | AHCI_GHC_IE;
    ahci_start(&hba, &fk_ops);
    CHECK_EQ(hba.ports, 6);
    CHECK(!hba.handoff);
    CHECK_EQ(hba.ghc_before, AHCI_GHC_AE | AHCI_GHC_IE);
    CHECK_EQ(hba.ghc, AHCI_GHC_AE);                       /* AHCI mode on, interrupts off */
    CHECK_EQ(fk.ghc_writes, 1);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_DISK);
    CHECK(strcmp(hba.port[0].disk.model, "QEMU HARDDISK") == 0);
    CHECK_EQ(hba.port[0].disk.sectors, 131075);
    CHECK_EQ(hba.port[0].disk.sector_size, 512);
    CHECK(hba.port[0].disk.lba48 && hba.port[0].disk.dma);
    CHECK_EQ(hba.port[1].state, AHCI_PORT_ATAPI);
    CHECK_EQ(fk.port[1].commands, 0);                     /* the CD: never a command */
    CHECK_EQ(fk.port[1].cmd, 0);                          /* stopped, FIS receive off again */
    CHECK_EQ(fk.port[1].ie, 0);
    for (uint32_t p = 2; p < 6; p++) {
        CHECK_EQ(hba.port[p].state, AHCI_PORT_EMPTY);
        CHECK_EQ(fk.port[p].clb, 0);                      /* no device: nothing set */
    }
    CHECK_EQ(hba.port[6].state, AHCI_PORT_NONE);
    CHECK_EQ(fk.port[0].commands, 1);
    CHECK_EQ(fk.opcodes[0xEC], 1);
    CHECK_EQ(fk.port[0].cmd, AHCI_CMD_ST | AHCI_CMD_FRE);
    CHECK_EQ(fk.port[0].clb, hba.port[0].list_phys);      /* the BIOS's list replaced */
    CHECK_EQ(fk.port[0].fb, hba.port[0].list_phys + 1024u);
    CHECK_EQ(fk.port[0].ie, 0);
    CHECK_EQ(fk.pages_out, 2);                            /* port 0's two; the CD's returned */
    CHECK_EQ(fk.pages_taken, 3);
    CHECK_EQ(hba.commands, 1);
    check_read_only();
}

static void test_read(void)
{
    fk_reset();
    fk_disk(0, 131075);
    ahci_start(&hba, &fk_ops);
    uint32_t a = fk_data_page(), b = fk_data_page(), c = fk_data_page();
    uint32_t pages[3] = { a, b, c };
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, pages, 1), 0);
    CHECK(fk_data_ok(a, 0, 4096));
    CHECK_EQ(ahci_read(&hba, 0, 1001, 20, pages, 3), 0);   /* 2 whole pages and 4 sectors */
    CHECK_EQ(fk.prd_n, 3);
    CHECK_EQ(fk.prd_bytes[0], 4096);
    CHECK_EQ(fk.prd_bytes[1], 4096);
    CHECK_EQ(fk.prd_bytes[2], 2048);
    CHECK(fk_data_ok(a, 1001, 4096) && fk_data_ok(b, 1009, 4096) && fk_data_ok(c, 1017, 2048));
    CHECK_EQ(ahci_read(&hba, 0, 131072, 3, pages, 1), 0);  /* the last 3 sectors */
    CHECK(fk_data_ok(a, 131072, 1536));
    uint32_t sent = fk.port[0].commands;
    CHECK_EQ(ahci_read(&hba, 0, 131073, 3, pages, 1), -AHCI_EIO);   /* past the end */
    CHECK_EQ(ahci_read(&hba, 0, 131075, 1, pages, 1), -AHCI_EIO);
    CHECK_EQ(ahci_read(&hba, 0, 0xFFFFFFFFFFFFull, 1, pages, 1), -AHCI_EIO);
    CHECK_EQ(ahci_read(&hba, 0, 0, 9, pages, 1), -AHCI_EIO);        /* more than its pages hold */
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, pages, 2), -AHCI_EIO);        /* a page too many */
    CHECK_EQ(ahci_read(&hba, 0, 0, 0, pages, 1), -AHCI_EIO);
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, pages, 17), -AHCI_EIO);
    CHECK_EQ(ahci_read(&hba, 1, 0, 8, pages, 1), -AHCI_EIO);        /* no disk there */
    CHECK_EQ(ahci_read(&hba, 32, 0, 8, pages, 1), -AHCI_EIO);
    CHECK_EQ(fk.port[0].commands, sent);                            /* none of them sent */
    CHECK_EQ(hba.port[0].errors, 0);
    check_read_only();
}

/* The device reports an error: EIO, logged registers, no reset (the
 * engine stopped and the device is not busy), running again. */
static void test_error_recovered(void)
{
    fk_reset();
    fk_disk(0, 4096);
    ahci_start(&hba, &fk_ops);
    uint32_t a = fk_data_page();
    fk.port[0].fail_next = 1;
    CHECK_EQ(ahci_read(&hba, 0, 16, 8, &a, 1), -AHCI_EIO);
    CHECK_EQ(hba.port[0].errors, 1);
    CHECK_EQ(hba.port[0].last.end, AHCI_END_ERROR);
    CHECK_EQ(hba.port[0].last.op, AHCI_OP_READ);
    CHECK_EQ(hba.port[0].last.lba, 16);
    CHECK_EQ(hba.port[0].last.sectors, 8);
    CHECK(hba.port[0].last.is & (1u << 30));
    CHECK_EQ(hba.port[0].last.tfd & 0xFFu, 0x51);
    CHECK(!hba.port[0].last.reset);
    CHECK(hba.port[0].last.recovered);
    CHECK_EQ(fk.port[0].resets, 0);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_DISK);
    CHECK_EQ(fk.port[0].cmd, AHCI_CMD_ST | AHCI_CMD_FRE);
    CHECK_EQ(fk.port[0].is, 0);
    uint32_t sent = fk.port[0].commands;
    CHECK_EQ(ahci_read(&hba, 0, 16, 8, &a, 1), 0);         /* the next read works; never a retry */
    CHECK_EQ(fk.port[0].commands, sent + 1);
    CHECK(fk_data_ok(a, 16, 4096));
    check_read_only();
}

/* A command that never completes, the device left busy: the time limit,
 * one COMRESET, running again. */
static void test_timeout_reset(void)
{
    fk_reset();
    fk_disk(0, 4096);
    ahci_start(&hba, &fk_ops);
    uint32_t a = fk_data_page();
    fk.port[0].hang_next = 1;
    fk.port[0].busy_until_reset = true;
    uint64_t t0 = fk.ticks;
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, &a, 1), -AHCI_EIO);
    CHECK(fk.ticks - t0 > 500);                             /* 5 s at 100 Hz */
    CHECK(fk.ticks - t0 < 700);
    CHECK_EQ(hba.port[0].last.end, AHCI_END_TIME);
    CHECK_EQ(hba.ends_time, 1);
    CHECK(hba.port[0].last.reset);
    CHECK_EQ(fk.port[0].resets, 1);                         /* at most one */
    CHECK_EQ(hba.port[0].resets, 1);
    CHECK(hba.port[0].last.recovered);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_DISK);
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, &a, 1), 0);
    CHECK(fk_data_ok(a, 0, 4096));
    check_read_only();
}

/* The clock stops: the count of register reads ends the wait. */
static void test_clock_stopped(void)
{
    fk_reset();
    fk_disk(0, 4096);
    ahci_start(&hba, &fk_ops);
    uint32_t a = fk_data_page();
    fk.reads_per_tick = 0;
    fk.port[0].hang_next = 1;
    uint64_t r0 = fk.reads;
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, &a, 1), -AHCI_EIO);
    CHECK_EQ(hba.port[0].last.end, AHCI_END_CAP);
    CHECK_EQ(hba.ends_cap, 1);
    CHECK(fk.reads - r0 >= (uint64_t)AHCI_LIMIT_CMD_MS * AHCI_POLLS_PER_MS);
    CHECK(fk.reads - r0 < (uint64_t)AHCI_LIMIT_CMD_MS * AHCI_POLLS_PER_MS * 3);
    CHECK(hba.polls_longest >= (uint64_t)AHCI_LIMIT_CMD_MS * AHCI_POLLS_PER_MS);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_DISK);
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, &a, 1), 0);
    check_read_only();
}

/* Three errors: the disk is marked failed; reads then end at once. */
static void test_three_errors(void)
{
    fk_reset();
    fk_disk(0, 4096);
    ahci_start(&hba, &fk_ops);
    uint32_t a = fk_data_page();
    fk.port[0].fail_next = 2;
    fk.port[0].short_next = 1;
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, &a, 1), -AHCI_EIO);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_DISK);
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, &a, 1), -AHCI_EIO);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_DISK);
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, &a, 1), -AHCI_EIO);  /* the short transfer */
    CHECK_EQ(hba.port[0].last.end, AHCI_END_SHORT);
    CHECK_EQ(hba.port[0].errors, 3);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_FAILED);
    CHECK(hba.port[0].why && strcmp(hba.port[0].why, "too many errors") == 0);
    uint32_t sent = fk.port[0].commands;
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, &a, 1), -AHCI_EIO);
    CHECK_EQ(fk.port[0].commands, sent);
    check_read_only();
}

static void test_handoff(void)
{
    fk_reset();
    fk_disk(0, 4096);
    fk.cap2 = AHCI_CAP2_BOH;
    fk.bohc = AHCI_BOHC_BOS;
    fk.bos_delay = 5;
    ahci_start(&hba, &fk_ops);
    CHECK(hba.handoff);
    CHECK(fk.oos_written);
    CHECK_EQ(hba.handoff_end, AHCI_END_DONE);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_DISK);
    fk_reset();
    fk_disk(0, 4096);
    fk.cap2 = AHCI_CAP2_BOH;
    fk.bohc = AHCI_BOHC_BOS;
    fk.bos_delay = -1;                                      /* the BIOS never lets go */
    ahci_start(&hba, &fk_ops);
    CHECK(hba.handoff);
    CHECK_EQ(hba.handoff_end, AHCI_END_TIME);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_DISK);            /* the start goes on */
    check_read_only();
}

static void test_stuck_and_busy(void)
{
    fk_reset();
    fk_disk(0, 4096);
    fk_disk(2, 4096);
    fk.port[0].cmd = AHCI_CMD_ST;
    fk.port[0].stuck = true;
    fk.port[0].clb = 0x9F000u;
    fk.port[2].busy_forever = true;
    ahci_start(&hba, &fk_ops);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_STUCK);
    CHECK(hba.port[0].cmd_stuck & AHCI_CMD_CR);
    CHECK_EQ(fk.port[0].clb, 0x9F000u);                    /* left alone */
    CHECK_EQ(fk.port[0].commands, 0);
    CHECK_EQ(hba.port[2].state, AHCI_PORT_FAILED);
    CHECK(hba.port[2].why && strcmp(hba.port[2].why, "the device stayed busy") == 0);
    CHECK_EQ(fk.port[2].commands, 0);
    CHECK_EQ(fk.port[2].cmd, 0);
    CHECK_EQ(fk.pages_out, 0);
    check_read_only();
}

static void test_signatures(void)
{
    fk_reset();
    fk_disk(0, 4096);
    fk.port[0].sig_late = true;                             /* known once FIS receive is on */
    fk_disk(1, 4096);
    fk.port[1].sig_never = true;
    fk_disk(2, 4096);
    fk.port[2].sig = 0x96690101u;                           /* a port multiplier */
    ahci_start(&hba, &fk_ops);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_DISK);
    CHECK_EQ(hba.port[0].sig, AHCI_SIG_ATA);
    CHECK_EQ(hba.port[1].state, AHCI_PORT_OTHER);
    CHECK_EQ(hba.port[1].sig, AHCI_SIG_NONE);
    CHECK_EQ(hba.port[2].state, AHCI_PORT_OTHER);
    CHECK_EQ(fk.port[1].commands + fk.port[2].commands, 0);
    CHECK_EQ(fk.pages_out, 2);
    check_read_only();
}

static void test_identify(void)
{
    struct ahci_disk d;
    const char *why;
    struct fk_port p;
    fk_ident(&p, "VBOX HARDDISK", 62914560ull, true, true, 512);
    CHECK(ahci_parse_identify(p.ident, &d, &why));
    CHECK(strcmp(d.model, "VBOX HARDDISK") == 0);
    CHECK_EQ(d.sectors, 62914560ull);
    fk_ident(&p, "BIG", 0x123456789ull, true, true, 512);
    CHECK(ahci_parse_identify(p.ident, &d, &why));
    CHECK_EQ(d.sectors, 0x123456789ull);
    fk_ident(&p, "4K", 1000, true, true, 4096);
    CHECK(!ahci_parse_identify(p.ident, &d, &why));
    CHECK_EQ(d.sector_size, 4096);
    CHECK(strcmp(why, "its logical sector is not 512 bytes") == 0);
    fk_ident(&p, "OLD", 1000, false, true, 512);
    CHECK(!ahci_parse_identify(p.ident, &d, &why));
    CHECK_EQ(d.sectors, 1000);                              /* words 60-61 */
    CHECK(strcmp(why, "no 48-bit addressing (LBA48)") == 0);
    fk_ident(&p, "PIO", 1000, true, false, 512);
    CHECK(!ahci_parse_identify(p.ident, &d, &why));
    CHECK(strcmp(why, "no DMA") == 0);
    fk_ident(&p, "EMPTY", 0, true, true, 512);
    CHECK(!ahci_parse_identify(p.ident, &d, &why));
    fk_ident(&p, "A\x01" "B", 10, true, true, 512);
    p.ident[46] = 0;                                        /* NULs at the end */
    CHECK(ahci_parse_identify(p.ident, &d, &why));
    CHECK(strcmp(d.model, "A?B") == 0);
    fk_ident(&p, "1234567890123456789012345678901234567890", 10, true, true, 512);
    CHECK(ahci_parse_identify(p.ident, &d, &why));
    CHECK_EQ(strlen(d.model), 40);

    /* A refused disk: its port stopped, its pages returned. */
    fk_reset();
    fk_disk(0, 1000);
    fk_ident(&fk.port[0], "4K DISK", 1000, true, true, 4096);
    ahci_start(&hba, &fk_ops);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_REFUSED);
    CHECK_EQ(fk.port[0].cmd, 0);
    CHECK_EQ(fk.pages_out, 0);
    uint32_t a = fk_data_page();
    CHECK_EQ(ahci_read(&hba, 0, 0, 8, &a, 1), -AHCI_EIO);
    CHECK_EQ(fk.port[0].commands, 1);                      /* IDENTIFY only */
    /* IDENTIFY fails: the disk failed, no retry. */
    fk_reset();
    fk_disk(0, 1000);
    fk.port[0].fail_next = 1;
    ahci_start(&hba, &fk_ops);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_FAILED);
    CHECK_EQ(hba.port[0].last.op, AHCI_OP_IDENTIFY);
    CHECK_EQ(fk.port[0].commands, 1);
    CHECK_EQ(fk.pages_out, 0);
    /* No page for the command list. */
    fk_reset();
    fk_disk(0, 1000);
    fk.no_pages = true;
    ahci_start(&hba, &fk_ops);
    CHECK_EQ(hba.port[0].state, AHCI_PORT_FAILED);
    CHECK_EQ(fk.port[0].commands, 0);
    check_read_only();
}

int main(void)
{
    printf("test_ahci_core\n");
    RUN(test_opcodes);
    RUN(test_start_qemu_like);
    RUN(test_read);
    RUN(test_error_recovered);
    RUN(test_timeout_reset);
    RUN(test_clock_stopped);
    RUN(test_three_errors);
    RUN(test_handoff);
    RUN(test_stuck_and_busy);
    RUN(test_signatures);
    RUN(test_identify);
    return unit_report("ahci_core");
}
