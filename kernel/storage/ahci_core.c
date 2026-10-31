/* The AHCI driver, the pure part (1.4 stage 4): see ahci_core.h. Read
 * only: IDENTIFY DEVICE and READ DMA EXT are the only commands. */
#include "kernel/storage/ahci_core.h"

/* The only ATA opcodes this driver sends (D1, read only in stage 4),
 * indexed by enum ahci_op. Read through a volatile access (ahci_opcode),
 * so the table stays in .rodata, where part static reads it back from
 * madire.elf: its two bytes are every command the kernel can send. */
static const uint8_t ahci_opcodes[AHCI_OPS] __attribute__((used)) = {
    0xECu,      /* IDENTIFY DEVICE */
    0x25u,      /* READ DMA EXT */
};

#define AHCI_FIS_H2D        0x27u       /* register FIS, host to device */
#define AHCI_FIS_COMMAND    0x80u       /* its C bit: the command register */
#define AHCI_DEVICE_LBA     0x40u       /* the device register's LBA bit */
#define AHCI_CFL_H2D        5u          /* the FIS's length in dwords (20 bytes) */
#define AHCI_SECTOR         512u
#define AHCI_PAGE           4096u

uint8_t ahci_opcode(enum ahci_op op)
{
    if ((unsigned)op >= AHCI_OPS)
        return 0;
    return *(const volatile uint8_t *)&ahci_opcodes[op];
}

static uint32_t ahci_rd(const struct ahci_hba *h, uint32_t off)
{
    return h->ops->read(h->ops->ctx, off);
}

static void ahci_wr(const struct ahci_hba *h, uint32_t off, uint32_t value)
{
    h->ops->write(h->ops->ctx, off, value);
}

static uint64_t ahci_now(const struct ahci_hba *h)
{
    return h->ops->ticks(h->ops->ctx);
}

/* The clock ticks a limit of `ms` takes, at least one. */
static uint64_t ahci_limit_ticks(const struct ahci_hba *h, uint32_t ms)
{
    uint64_t t = (uint64_t)ms * h->ops->hz / 1000u;
    return t ? t : 1u;
}

/* Reads register `off` until (value & mask) == want (or, with `differs`,
 * until the value is not `want`), at most `ms` milliseconds by the clock
 * and ms * AHCI_POLLS_PER_MS reads. A mask of 0 with want 1 never matches:
 * a pause of `ms` (ending AHCI_END_TIME, or AHCI_END_CAP). */
static enum ahci_end ahci_wait(struct ahci_hba *h, uint32_t off, uint32_t mask, uint32_t want, bool differs,
                               uint32_t ms)
{
    uint64_t limit = ahci_limit_ticks(h, ms), cap = (uint64_t)ms * AHCI_POLLS_PER_MS;
    uint64_t t0 = ahci_now(h);
    for (uint64_t n = 1;; n++) {
        uint32_t v = ahci_rd(h, off);
        if (differs ? v != want : (v & mask) == want)
            return AHCI_END_DONE;
        if (ahci_now(h) - t0 > limit)
            return AHCI_END_TIME;
        if (n >= cap)
            return AHCI_END_CAP;
    }
}

uint32_t ahci_regs_bytes(uint32_t pi)
{
    uint32_t top = 0;
    for (uint32_t p = 0; p < AHCI_PORTS; p++)
        if (pi & (1u << p))
            top = p + 1;
    return AHCI_PORT_BASE(top);         /* the registers end after the last port's */
}

/* ---- ports ---- */

/* ST off and CR waited for, then FRE off and FR waited for. */
static bool ahci_port_stop(struct ahci_hba *h, uint32_t p)
{
    uint32_t base = AHCI_PORT_BASE(p), cmd = ahci_rd(h, base + AHCI_PX_CMD);
    if (cmd & (AHCI_CMD_ST | AHCI_CMD_CR)) {
        ahci_wr(h, base + AHCI_PX_CMD, cmd & ~AHCI_CMD_ST);
        if (ahci_wait(h, base + AHCI_PX_CMD, AHCI_CMD_CR, 0, false, AHCI_LIMIT_STOP_MS) != AHCI_END_DONE)
            return false;
        cmd = ahci_rd(h, base + AHCI_PX_CMD);
    }
    if (cmd & (AHCI_CMD_FRE | AHCI_CMD_FR)) {
        ahci_wr(h, base + AHCI_PX_CMD, cmd & ~AHCI_CMD_FRE);
        if (ahci_wait(h, base + AHCI_PX_CMD, AHCI_CMD_FR, 0, false, AHCI_LIMIT_STOP_MS) != AHCI_END_DONE)
            return false;
    }
    return true;
}

/* The engine on, once the device is ready (BSY and DRQ clear). */
static bool ahci_port_run(struct ahci_hba *h, uint32_t p)
{
    uint32_t base = AHCI_PORT_BASE(p);
    if (ahci_wait(h, base + AHCI_PX_TFD, AHCI_TFD_BSY | AHCI_TFD_DRQ, 0, false, AHCI_LIMIT_READY_MS) !=
        AHCI_END_DONE)
        return false;
    ahci_wr(h, base + AHCI_PX_CMD, ahci_rd(h, base + AHCI_PX_CMD) | AHCI_CMD_ST);
    return true;
}

/* Returns a port's pages (only once it is stopped: no DMA can reach them). */
static void ahci_port_release(struct ahci_hba *h, struct ahci_port *pt)
{
    if (pt->table)
        h->ops->unpage(h->ops->ctx, pt->table, pt->table_phys);
    if (pt->list)
        h->ops->unpage(h->ops->ctx, pt->list, pt->list_phys);
    pt->table = pt->list = 0;
    pt->table_phys = pt->list_phys = 0;
}

/* A port that is used no more: stopped, its pages returned if it stopped. */
static void ahci_port_drop(struct ahci_hba *h, uint32_t p, enum ahci_port_state state, const char *why)
{
    struct ahci_port *pt = &h->port[p];
    pt->state = state;
    pt->why = why;
    if (ahci_port_stop(h, p))
        ahci_port_release(h, pt);
}

/* COMRESET (AHCI 1.3.1 section 10.4.2): DET = 1 for at least 1 ms, then
 * 0, and the device present again; the errors cleared. ST must be off. */
static bool ahci_port_reset(struct ahci_hba *h, uint32_t p)
{
    uint32_t base = AHCI_PORT_BASE(p), sctl = ahci_rd(h, base + AHCI_PX_SCTL);
    h->port[p].resets++;
    ahci_wr(h, base + AHCI_PX_SCTL, (sctl & ~AHCI_SCTL_DET) | 1u);
    ahci_wait(h, base + AHCI_PX_SSTS, 0, 1, false, 2);      /* a pause: at least 1 ms */
    ahci_wr(h, base + AHCI_PX_SCTL, sctl & ~AHCI_SCTL_DET);
    enum ahci_end e = ahci_wait(h, base + AHCI_PX_SSTS, AHCI_SSTS_DET, AHCI_DET_PRESENT, false,
                                AHCI_LIMIT_RESET_MS);
    ahci_wr(h, base + AHCI_PX_SERR, 0xFFFFFFFFu);
    return e == AHCI_END_DONE;
}

/* After a failed command (D9): the engine stopped (which clears PxCI);
 * only if it does not stop, or the device stays busy, one COMRESET; the
 * errors cleared; the engine started again. Never a retry. */
static void ahci_port_recover(struct ahci_hba *h, uint32_t p)
{
    struct ahci_port *pt = &h->port[p];
    uint32_t base = AHCI_PORT_BASE(p);
    bool ok = true;
    uint32_t cmd = ahci_rd(h, base + AHCI_PX_CMD);
    ahci_wr(h, base + AHCI_PX_CMD, cmd & ~AHCI_CMD_ST);
    bool stopped = ahci_wait(h, base + AHCI_PX_CMD, AHCI_CMD_CR, 0, false, AHCI_LIMIT_STOP_MS) == AHCI_END_DONE;
    ahci_wr(h, base + AHCI_PX_SERR, 0xFFFFFFFFu);
    ahci_wr(h, base + AHCI_PX_IS, 0xFFFFFFFFu);
    if (!stopped || (ahci_rd(h, base + AHCI_PX_TFD) & (AHCI_TFD_BSY | AHCI_TFD_DRQ))) {
        pt->last.reset = true;
        ok = ahci_port_reset(h, p) &&
             ahci_wait(h, base + AHCI_PX_CMD, AHCI_CMD_CR, 0, false, AHCI_LIMIT_STOP_MS) == AHCI_END_DONE;
        ahci_wr(h, base + AHCI_PX_IS, 0xFFFFFFFFu);
    }
    ok = ok && ahci_port_run(h, p);
    pt->last.recovered = ok;
    if (!ok) {
        ahci_port_drop(h, p, AHCI_PORT_FAILED, "the port did not recover after an error");
        return;
    }
    if (pt->errors >= AHCI_ERRORS_FAILED) {
        pt->state = AHCI_PORT_FAILED;
        pt->why = "too many errors";
    }
}

/* One command in slot 0: the header, the FIS, `n` regions of bytes[i]
 * bytes at phys[i]; issued and waited for (5 s). */
static enum ahci_end ahci_command(struct ahci_hba *h, uint32_t p, enum ahci_op op, uint64_t lba, uint32_t count,
                                  const uint32_t *phys, const uint32_t *bytes, uint32_t n)
{
    struct ahci_port *pt = &h->port[p];
    uint32_t base = AHCI_PORT_BASE(p), total = 0;
    uint8_t code = ahci_opcode(op);
    if (!code || n == 0 || n > AHCI_PRD_MAX || !pt->list || !pt->table)
        return AHCI_END_REFUSED;
    if (ahci_wait(h, base + AHCI_PX_TFD, AHCI_TFD_BSY | AHCI_TFD_DRQ, 0, false, AHCI_LIMIT_READY_MS) !=
            AHCI_END_DONE ||
        (ahci_rd(h, base + AHCI_PX_CI) & 1u))
        return AHCI_END_BUSY;

    volatile uint32_t *hdr = (volatile uint32_t *)pt->list;            /* command header 0 */
    hdr[0] = AHCI_CFL_H2D | (n << 16);  /* the FIS length, PRDTL; W (bit 6) clear: from the device */
    hdr[1] = 0;                         /* PRDBC: the controller counts the bytes moved */
    hdr[2] = pt->table_phys;
    hdr[3] = 0;
    for (uint32_t i = 4; i < 8; i++)
        hdr[i] = 0;
    volatile uint8_t *fis = pt->table;
    for (uint32_t i = 0; i < AHCI_TABLE_PRDT; i++)
        fis[i] = 0;
    fis[0] = AHCI_FIS_H2D;
    fis[1] = AHCI_FIS_COMMAND;
    fis[2] = code;
    if (op == AHCI_OP_READ) {
        fis[4] = (uint8_t)lba;
        fis[5] = (uint8_t)(lba >> 8);
        fis[6] = (uint8_t)(lba >> 16);
        fis[7] = AHCI_DEVICE_LBA;
        fis[8] = (uint8_t)(lba >> 24);
        fis[9] = (uint8_t)(lba >> 32);
        fis[10] = (uint8_t)(lba >> 40);
        fis[12] = (uint8_t)count;
        fis[13] = (uint8_t)(count >> 8);
    }
    volatile uint32_t *prd = (volatile uint32_t *)(pt->table + AHCI_TABLE_PRDT);
    for (uint32_t i = 0; i < n; i++) {
        prd[4 * i] = phys[i];
        prd[4 * i + 1] = 0;
        prd[4 * i + 2] = 0;
        prd[4 * i + 3] = bytes[i] - 1;  /* the byte count - 1; bit 31 (interrupt) clear */
        total += bytes[i];
    }
    ahci_wr(h, base + AHCI_PX_IS, 0xFFFFFFFFu);
    __asm__ volatile ("" ::: "memory");    /* the header and table are written before the issue */
    ahci_wr(h, base + AHCI_PX_CI, 1u);
    pt->commands++;
    h->commands++;

    uint64_t limit = ahci_limit_ticks(h, AHCI_LIMIT_CMD_MS), cap = (uint64_t)AHCI_LIMIT_CMD_MS * AHCI_POLLS_PER_MS;
    uint64_t t0 = ahci_now(h), polls = 0;
    enum ahci_end e = AHCI_END_DONE;
    for (;;) {
        polls++;
        uint32_t is = ahci_rd(h, base + AHCI_PX_IS);
        if (is & AHCI_IS_ERRORS) {
            e = AHCI_END_ERROR;
            break;
        }
        if (!(ahci_rd(h, base + AHCI_PX_CI) & 1u)) {
            e = AHCI_END_DONE;
            break;
        }
        if (ahci_now(h) - t0 > limit) {
            e = AHCI_END_TIME;
            h->ends_time++;
            break;
        }
        if (polls >= cap) {
            e = AHCI_END_CAP;
            h->ends_cap++;
            break;
        }
    }
    uint64_t took = ahci_now(h) - t0;
    if (polls > h->polls_longest)
        h->polls_longest = polls;
    if (took > h->ticks_longest)
        h->ticks_longest = took;
    __asm__ volatile ("" ::: "memory");    /* the data is read after the wait */
    uint32_t tfd = ahci_rd(h, base + AHCI_PX_TFD);
    if (e == AHCI_END_DONE && (tfd & (AHCI_TFD_ERR | AHCI_TFD_BSY | AHCI_TFD_DRQ)))
        e = AHCI_END_ERROR;
    if (e == AHCI_END_DONE && hdr[1] != total)
        e = AHCI_END_SHORT;
    if (e == AHCI_END_DONE)
        return e;

    pt->errors++;
    pt->last.op = op;
    pt->last.end = e;
    pt->last.lba = lba;
    pt->last.sectors = count;
    pt->last.is = ahci_rd(h, base + AHCI_PX_IS);
    pt->last.tfd = tfd;
    pt->last.serr = ahci_rd(h, base + AHCI_PX_SERR);
    pt->last.ci = ahci_rd(h, base + AHCI_PX_CI);
    pt->last.reset = false;
    pt->last.recovered = false;
    ahci_port_recover(h, p);
    return e;
}

/* ---- IDENTIFY DEVICE (ATA ACS-3, section 7.12) ---- */

bool ahci_parse_identify(const uint16_t *w, struct ahci_disk *d, const char **why)
{
    uint32_t n = 0;
    for (uint32_t i = 27; i <= 46; i++) {           /* two characters a word, the first in the high byte */
        char c[2] = { (char)(w[i] >> 8), (char)w[i] };
        for (uint32_t k = 0; k < 2; k++)
            d->model[n++] = c[k] >= 0x20 && c[k] < 0x7F ? c[k] : (c[k] ? '?' : ' ');
    }
    while (n && d->model[n - 1] == ' ')
        n--;
    d->model[n] = 0;
    d->lba48 = (w[83] & (1u << 10)) != 0;
    d->dma = (w[49] & (1u << 8)) != 0;
    if (d->lba48)
        d->sectors = (uint64_t)w[100] | (uint64_t)w[101] << 16 | (uint64_t)w[102] << 32 | (uint64_t)w[103] << 48;
    else
        d->sectors = (uint64_t)w[60] | (uint64_t)w[61] << 16;
    d->sector_size = AHCI_SECTOR;
    /* Word 106 valid (bits 15-14 = 01) with bit 12: the logical sector is
     * words 117-118 words long. */
    if ((w[106] & 0xC000u) == 0x4000u && (w[106] & (1u << 12)))
        d->sector_size = ((uint32_t)w[117] | (uint32_t)w[118] << 16) * 2u;
    *why = 0;
    if (!d->lba48)
        *why = "no 48-bit addressing (LBA48)";
    else if (!d->dma)
        *why = "no DMA";
    else if (d->sector_size != AHCI_SECTOR)
        *why = "its logical sector is not 512 bytes";
    else if (d->sectors == 0)
        *why = "it reports no sectors";
    return *why == 0;
}

static void ahci_identify(struct ahci_hba *h, uint32_t p)
{
    struct ahci_port *pt = &h->port[p];
    const char *why;
    uint32_t phys = pt->table_phys + AHCI_TABLE_BUF, bytes = AHCI_SECTOR;
    enum ahci_end e = ahci_command(h, p, AHCI_OP_IDENTIFY, 0, 0, &phys, &bytes, 1);
    if (e != AHCI_END_DONE) {
        if (pt->state == AHCI_PORT_DISK)
            ahci_port_drop(h, p, AHCI_PORT_FAILED, "IDENTIFY DEVICE failed");
        return;
    }
    uint16_t w[256];
    volatile const uint8_t *buf = pt->table + AHCI_TABLE_BUF;
    for (uint32_t i = 0; i < 256; i++)
        w[i] = (uint16_t)(buf[2 * i] | (uint16_t)buf[2 * i + 1] << 8);
    if (!ahci_parse_identify(w, &pt->disk, &why))
        ahci_port_drop(h, p, AHCI_PORT_REFUSED, why);
}

/* Step 4 and 5 for one stopped port. */
static void ahci_probe(struct ahci_hba *h, uint32_t p)
{
    struct ahci_port *pt = &h->port[p];
    uint32_t base = AHCI_PORT_BASE(p);
    pt->ssts = ahci_rd(h, base + AHCI_PX_SSTS);
    pt->sig = ahci_rd(h, base + AHCI_PX_SIG);
    if ((pt->ssts & AHCI_SSTS_DET) != AHCI_DET_PRESENT) {
        pt->state = AHCI_PORT_EMPTY;
        return;
    }
    if (!h->ops->page(h->ops->ctx, &pt->list, &pt->list_phys)) {
        pt->list = 0;
        pt->state = AHCI_PORT_FAILED;
        pt->why = "no free page for its command list";
        return;
    }
    ahci_wr(h, base + AHCI_PX_CLB, pt->list_phys);
    ahci_wr(h, base + AHCI_PX_CLBU, 0);
    ahci_wr(h, base + AHCI_PX_FB, pt->list_phys + AHCI_LIST_FIS);
    ahci_wr(h, base + AHCI_PX_FBU, 0);
    ahci_wr(h, base + AHCI_PX_SERR, 0xFFFFFFFFu);
    ahci_wr(h, base + AHCI_PX_IS, 0xFFFFFFFFu);
    ahci_wr(h, base + AHCI_PX_IE, 0);
    ahci_wr(h, base + AHCI_PX_CMD, ahci_rd(h, base + AHCI_PX_CMD) | AHCI_CMD_FRE);
    if (pt->sig == AHCI_SIG_NONE &&
        ahci_wait(h, base + AHCI_PX_SIG, 0, AHCI_SIG_NONE, true, AHCI_LIMIT_SIG_MS) == AHCI_END_DONE)
        pt->sig = ahci_rd(h, base + AHCI_PX_SIG);
    if (pt->sig != AHCI_SIG_ATA) {
        ahci_port_drop(h, p, pt->sig == AHCI_SIG_ATAPI ? AHCI_PORT_ATAPI : AHCI_PORT_OTHER, 0);
        return;
    }
    if (!h->ops->page(h->ops->ctx, &pt->table, &pt->table_phys)) {
        pt->table = 0;
        ahci_port_drop(h, p, AHCI_PORT_FAILED, "no free page for its command table");
        return;
    }
    pt->state = AHCI_PORT_DISK;
    if (!ahci_port_run(h, p)) {
        ahci_port_drop(h, p, AHCI_PORT_FAILED, "the device stayed busy");
        return;
    }
    ahci_identify(h, p);
}

void ahci_start(struct ahci_hba *h, const struct ahci_ops *ops)
{
    h->ops = ops;
    h->handoff = false;
    h->handoff_end = AHCI_END_DONE;
    h->commands = h->polls_longest = h->ticks_longest = 0;
    h->ends_time = h->ends_cap = 0;
    for (uint32_t p = 0; p < AHCI_PORTS; p++) {
        struct ahci_port *pt = &h->port[p];
        pt->state = AHCI_PORT_NONE;
        pt->sig = pt->ssts = pt->cmd_stuck = 0;
        pt->list = pt->table = 0;
        pt->list_phys = pt->table_phys = 0;
        pt->disk.model[0] = 0;
        pt->disk.sectors = 0;
        pt->disk.sector_size = 0;
        pt->disk.lba48 = pt->disk.dma = false;
        pt->why = 0;
        pt->errors = pt->resets = pt->commands = 0;
        pt->last.op = AHCI_OP_IDENTIFY;
        pt->last.end = AHCI_END_DONE;
        pt->last.lba = 0;
        pt->last.sectors = 0;
        pt->last.is = pt->last.tfd = pt->last.serr = pt->last.ci = 0;
        pt->last.reset = pt->last.recovered = false;
    }
    h->cap = ahci_rd(h, AHCI_REG_CAP);
    h->vs = ahci_rd(h, AHCI_REG_VS);
    h->cap2 = ahci_rd(h, AHCI_REG_CAP2);
    h->pi = ahci_rd(h, AHCI_REG_PI);
    h->ports = ahci_regs_bytes(h->pi) > AHCI_PORT_BASE(0) ? (ahci_regs_bytes(h->pi) - AHCI_PORT_BASE(0)) / 0x80u : 0;

    if (h->cap2 & AHCI_CAP2_BOH) {                  /* step 1 */
        h->handoff = true;
        ahci_wr(h, AHCI_REG_BOHC, ahci_rd(h, AHCI_REG_BOHC) | AHCI_BOHC_OOS);
        h->handoff_end = ahci_wait(h, AHCI_REG_BOHC, AHCI_BOHC_BOS, 0, false, AHCI_LIMIT_BOS_MS);
        if (h->handoff_end == AHCI_END_DONE && (ahci_rd(h, AHCI_REG_BOHC) & AHCI_BOHC_BB))
            h->handoff_end = ahci_wait(h, AHCI_REG_BOHC, AHCI_BOHC_BB, 0, false, AHCI_LIMIT_BB_MS);
    }
    h->ghc_before = ahci_rd(h, AHCI_REG_GHC);       /* step 2 */
    ahci_wr(h, AHCI_REG_GHC, AHCI_GHC_AE);
    h->ghc = ahci_rd(h, AHCI_REG_GHC);

    for (uint32_t p = 0; p < h->ports; p++) {       /* step 3 */
        if (!(h->pi & (1u << p)))
            continue;
        h->port[p].state = AHCI_PORT_EMPTY;
        if (!ahci_port_stop(h, p)) {
            h->port[p].state = AHCI_PORT_STUCK;
            h->port[p].cmd_stuck = ahci_rd(h, AHCI_PORT_BASE(p) + AHCI_PX_CMD);
        }
    }
    for (uint32_t p = 0; p < h->ports; p++)         /* steps 4 and 5 */
        if (h->port[p].state == AHCI_PORT_EMPTY)
            ahci_probe(h, p);
}

int ahci_read(struct ahci_hba *h, uint32_t p, uint64_t lba, uint32_t sectors, const uint32_t *phys,
              uint32_t pages)
{
    if (p >= AHCI_PORTS || h->port[p].state != AHCI_PORT_DISK)
        return -AHCI_EIO;
    struct ahci_port *pt = &h->port[p];
    uint32_t per = AHCI_PAGE / AHCI_SECTOR, bytes[AHCI_PRD_MAX];
    if (sectors == 0 || pages == 0 || pages > AHCI_PRD_MAX || sectors > pages * per ||
        sectors <= (pages - 1) * per || lba >= pt->disk.sectors || sectors > pt->disk.sectors - lba)
        return -AHCI_EIO;
    for (uint32_t i = 0; i < pages; i++)
        bytes[i] = i + 1 < pages ? AHCI_PAGE : (sectors - (pages - 1) * per) * AHCI_SECTOR;
    return ahci_command(h, p, AHCI_OP_READ, lba, sectors, phys, bytes, pages) == AHCI_END_DONE ? 0 : -AHCI_EIO;
}

const char *ahci_end_str(enum ahci_end end)
{
    switch (end) {
    case AHCI_END_DONE:    return "done";
    case AHCI_END_TIME:    return "timed out";
    case AHCI_END_CAP:     return "ended by its read count (the clock did not advance)";
    case AHCI_END_ERROR:   return "the device reported an error";
    case AHCI_END_SHORT:   return "fewer bytes moved than asked";
    case AHCI_END_BUSY:    return "the port was not ready";
    case AHCI_END_REFUSED: return "refused";
    }
    return "unknown";
}

const char *ahci_state_str(enum ahci_port_state state)
{
    switch (state) {
    case AHCI_PORT_NONE:    return "not implemented";
    case AHCI_PORT_EMPTY:   return "empty";
    case AHCI_PORT_STUCK:   return "its engine did not stop: left alone";
    case AHCI_PORT_ATAPI:   return "ATAPI device, ignored (no command sent)";
    case AHCI_PORT_OTHER:   return "not an ATA disk, ignored (no command sent)";
    case AHCI_PORT_DISK:    return "disk";
    case AHCI_PORT_REFUSED: return "disk not used";
    case AHCI_PORT_FAILED:  return "disk failed";
    }
    return "unknown";
}

const char *ahci_op_str(enum ahci_op op)
{
    switch (op) {
    case AHCI_OP_IDENTIFY: return "IDENTIFY DEVICE";
    case AHCI_OP_READ:     return "READ DMA EXT";
    case AHCI_OPS:         break;
    }
    return "none";
}
