#ifndef MADIRE_STORAGE_AHCI_CORE_H
#define MADIRE_STORAGE_AHCI_CORE_H

/*
 * The AHCI driver, the pure part (1.4 stage 4; docs/plans/1.4.md, D1, D2,
 * C2, C3 and C5's errors). No port or memory-mapped access, printing or
 * allocation of its own: the controller's registers, the clock and the DMA
 * pages come through struct ahci_ops, so the host unit tests
 * (tests/unit/test_ahci_core.c) run it against a fake controller with
 * injected errors and timeouts. kernel/storage/ahci.c gives the real ones
 * and logs what this part found.
 *
 * READ ONLY in 1.4 stage 4: the only ATA commands this code can build are
 * IDENTIFY DEVICE (0xEC) and READ DMA EXT (0x25). Their opcodes are the two
 * bytes of one table, ahci_opcodes (ahci_core.c), read through a volatile
 * access so they stay in the kernel's read-only data, where part static's
 * check reads them back from madire.elf; a command header's "write" bit is
 * never set. WRITE DMA EXT and FLUSH CACHE EXT come in stage 10, behind the
 * write guard (C4).
 *
 * What it does, in this order (AHCI 1.3.1):
 *   1. BIOS/OS hand-off, only if the controller offers it (CAP2.BOH): the OS
 *      ownership bit set, then the BIOS's bit waited for (25 ms) and its
 *      busy bit (2 s); if the BIOS does not let go in time the start goes
 *      on and the result says so.
 *   2. AHCI mode on and interrupts off: GHC is written once with AE alone
 *      (HR, the controller reset, is never written: no HBA-wide reset is
 *      needed, since every port is stopped and set up below).
 *   3. Every implemented port (PI) is stopped first: ST off and CR waited
 *      for, then FRE off and FR waited for (500 ms each). A BIOS can leave
 *      a port running with its command list and received-FIS area in
 *      memory Madire now hands out; once stopped, the port does no DMA. A
 *      port that does not stop is left alone ("stuck").
 *   4. A port with a device (PxSSTS.DET = 3) gets one DMA page for its
 *      command list (1 KiB at 0) and received-FIS area (256 bytes at
 *      1 KiB), errors and interrupt bits cleared, interrupts off (PxIE 0),
 *      FIS receive on; its signature (PxSIG, waited for up to 1 s while it
 *      reads 0xFFFFFFFF) decides: an ATA disk (0x00000101) is used; any
 *      other device (ATAPI 0xEB140101, an enclosure, a port multiplier) is
 *      never sent a command: FIS receive goes off again and its page is
 *      returned. Ports without a device get nothing.
 *   5. A disk gets a second page, the command table (128-byte aligned:
 *      the FIS at 0, up to 16 physical-region entries at 0x80, and a
 *      512-byte buffer at 2 KiB for IDENTIFY), its engine started (ST on,
 *      after BSY and DRQ are clear) and IDENTIFY DEVICE: the model, the
 *      size, LBA48, DMA and the logical sector size. A disk without LBA48
 *      or DMA, or whose logical sector is not 512 bytes (C5), is refused:
 *      its port is stopped and its pages returned.
 *
 * One command at a time, in command slot 0 (D1); polling only (D2): every
 * wait reads registers until done, bounded by the clock (5 s for IDENTIFY
 * and a read, 1 s for a port reset or for a device to become ready, 500 ms
 * for an engine to stop) and by a count of register reads
 * (AHCI_POLLS_PER_MS for each millisecond of the limit), which also ends a
 * wait if the clock stops. A command that fails or times out (D9): the
 * port's registers are kept for one log line (the caller's), the engine is
 * stopped and, only if the port is still busy or does not stop, reset once
 * (COMRESET); then the port is started again and the command ends with
 * -AHCI_EIO. It is never retried here. After AHCI_ERRORS_FAILED errors the
 * disk is marked failed and every later read ends with -AHCI_EIO at once.
 *
 * The idea of splitting the controller from its ports, and a port from its
 * disk, follows SerenityOS's AHCI driver (docs/plans/1.4.md, section M,
 * ideas only); the registers and commands are from the AHCI 1.3.1 and ATA
 * ACS-3 specifications; the code is Madire's own.
 */

#include <stdbool.h>
#include <stdint.h>

#define AHCI_PORTS          32u
#define AHCI_EIO            5           /* reads end with -AHCI_EIO on an error */

/* The controller's registers (offsets from BAR5, each 32 bits). */
#define AHCI_REG_CAP        0x00u
#define AHCI_REG_GHC        0x04u
#define AHCI_REG_PI         0x0Cu
#define AHCI_REG_VS         0x10u
#define AHCI_REG_CAP2       0x24u
#define AHCI_REG_BOHC       0x28u

#define AHCI_GHC_IE         0x00000002u
#define AHCI_GHC_AE         0x80000000u
#define AHCI_CAP2_BOH       0x00000001u
#define AHCI_BOHC_BOS       0x00000001u
#define AHCI_BOHC_OOS       0x00000002u
#define AHCI_BOHC_BB        0x00000010u

/* A port's registers: port p's at AHCI_PORT_BASE(p) + offset. */
#define AHCI_PORT_BASE(p)   (0x100u + 0x80u * (uint32_t)(p))
#define AHCI_PX_CLB         0x00u
#define AHCI_PX_CLBU        0x04u
#define AHCI_PX_FB          0x08u
#define AHCI_PX_FBU         0x0Cu
#define AHCI_PX_IS          0x10u
#define AHCI_PX_IE          0x14u
#define AHCI_PX_CMD         0x18u
#define AHCI_PX_TFD         0x20u
#define AHCI_PX_SIG         0x24u
#define AHCI_PX_SSTS        0x28u
#define AHCI_PX_SCTL        0x2Cu
#define AHCI_PX_SERR        0x30u
#define AHCI_PX_CI          0x38u

#define AHCI_CMD_ST         0x00000001u
#define AHCI_CMD_FRE        0x00000010u
#define AHCI_CMD_FR         0x00004000u
#define AHCI_CMD_CR         0x00008000u
/* PxIS bits that end a command with an error: task-file error, host bus
 * fatal error, host bus data error, interface fatal error, interface
 * non-fatal error, overflow. */
#define AHCI_IS_ERRORS      0x7D000000u
#define AHCI_TFD_ERR        0x01u
#define AHCI_TFD_DRQ        0x08u
#define AHCI_TFD_BSY        0x80u
#define AHCI_SSTS_DET       0x0000000Fu
#define AHCI_DET_PRESENT    3u          /* a device, and communication established */
#define AHCI_SCTL_DET       0x0000000Fu

#define AHCI_SIG_ATA        0x00000101u
#define AHCI_SIG_ATAPI      0xEB140101u
#define AHCI_SIG_NONE       0xFFFFFFFFu

/* The DMA pages' layout (C3). */
#define AHCI_LIST_FIS       1024u       /* the received-FIS area in the list page */
#define AHCI_TABLE_PRDT     0x80u       /* the physical-region entries in the table page */
#define AHCI_PRD_MAX        16u         /* entries per command: 16 pages, 64 KiB */
#define AHCI_TABLE_BUF      2048u       /* IDENTIFY's 512-byte buffer in the table page */

/* The time limits (D2), in milliseconds, and the register reads allowed
 * per millisecond of a limit. */
#define AHCI_LIMIT_CMD_MS   5000u
#define AHCI_LIMIT_RESET_MS 1000u
#define AHCI_LIMIT_READY_MS 1000u
#define AHCI_LIMIT_STOP_MS  500u
#define AHCI_LIMIT_SIG_MS   1000u
#define AHCI_LIMIT_BOS_MS   25u
#define AHCI_LIMIT_BB_MS    2000u
#define AHCI_POLLS_PER_MS   2000u
#define AHCI_ERRORS_FAILED  3u

struct ahci_ops {
    uint32_t (*read)(void *ctx, uint32_t off);          /* a 32-bit controller register */
    void (*write)(void *ctx, uint32_t off, uint32_t value);
    uint64_t (*ticks)(void *ctx);                       /* the clock: hz ticks a second */
    /* A zeroed 4 KiB DMA page below 4 GiB: the CPU's address and the
     * physical one; false if none can be had. */
    bool (*page)(void *ctx, volatile uint8_t **virt, uint32_t *phys);
    void (*unpage)(void *ctx, volatile uint8_t *virt, uint32_t phys);
    void *ctx;
    uint32_t hz;
};

/* How a wait or a command ended. */
enum ahci_end {
    AHCI_END_DONE,
    AHCI_END_TIME,      /* its time limit passed */
    AHCI_END_CAP,       /* its count of register reads ran out (the clock did not advance) */
    AHCI_END_ERROR,     /* the device reported an error (PxIS or the task file) */
    AHCI_END_SHORT,     /* the controller moved fewer bytes than asked */
    AHCI_END_BUSY,      /* the port was not ready for a command */
    AHCI_END_REFUSED,   /* not sent: a bad request, or the disk is not usable */
};

enum ahci_port_state {
    AHCI_PORT_NONE,     /* not implemented (PI) */
    AHCI_PORT_EMPTY,    /* no device (PxSSTS.DET not 3) */
    AHCI_PORT_STUCK,    /* its engine did not stop: left alone */
    AHCI_PORT_ATAPI,    /* an ATAPI device: ignored, never sent a command */
    AHCI_PORT_OTHER,    /* another signature: ignored */
    AHCI_PORT_DISK,     /* an ATA disk, in use (read only) */
    AHCI_PORT_REFUSED,  /* an ATA disk Madire does not use (see why) */
    AHCI_PORT_FAILED,   /* a disk whose set-up failed, or with too many errors */
};

enum ahci_op {
    AHCI_OP_IDENTIFY,   /* IDENTIFY DEVICE */
    AHCI_OP_READ,       /* READ DMA EXT */
    AHCI_OPS,
};

struct ahci_disk {
    char model[41];         /* IDENTIFY words 27-46, trailing spaces removed */
    uint64_t sectors;       /* LBA48 count (words 100-103), else words 60-61 */
    uint32_t sector_size;   /* logical sector size in bytes */
    bool lba48, dma;
};

/* The registers at the last error, kept for one log line. */
struct ahci_error {
    enum ahci_op op;
    enum ahci_end end;
    uint64_t lba;
    uint32_t sectors;
    uint32_t is, tfd, serr, ci;
    bool reset;             /* the port was reset (COMRESET) */
    bool recovered;         /* the port was started again */
};

struct ahci_port {
    enum ahci_port_state state;
    uint32_t sig, ssts;     /* as read when the port was probed */
    uint32_t cmd_stuck;     /* PxCMD of a stuck port */
    volatile uint8_t *list, *table;
    uint32_t list_phys, table_phys;
    struct ahci_disk disk;
    const char *why;        /* REFUSED, FAILED: the reason (static text) */
    uint32_t errors, resets;
    uint32_t commands;      /* commands sent to this port */
    struct ahci_error last;
};

struct ahci_hba {
    const struct ahci_ops *ops;
    uint32_t cap, cap2, pi, vs, ghc_before, ghc;
    bool handoff;           /* CAP2.BOH: the hand-off was done */
    enum ahci_end handoff_end;
    uint32_t ports;         /* the highest implemented port + 1 */
    struct ahci_port port[AHCI_PORTS];
    /* figures (report only) */
    uint64_t commands;
    uint64_t polls_longest; /* the most register reads one command's wait took */
    uint64_t ticks_longest; /* the most clock ticks one command took */
    uint32_t ends_time, ends_cap;
};

/* The bytes of register space the implemented ports `pi` need. */
uint32_t ahci_regs_bytes(uint32_t pi);

/* Steps 1-5 above. `h` is set from scratch; ops must stay valid. */
void ahci_start(struct ahci_hba *h, const struct ahci_ops *ops);

/* READ DMA EXT of `sectors` sectors from `lba` on port `p`'s disk into
 * `pages` pages (physical addresses phys[0..pages-1], 4 KiB each; the last
 * may be partly filled: the sectors must need exactly `pages` pages), at
 * most AHCI_PRD_MAX pages. 0, or -AHCI_EIO (the request is refused, the
 * disk is not usable or the command failed; p->last says how). */
int ahci_read(struct ahci_hba *h, uint32_t p, uint64_t lba, uint32_t sectors, const uint32_t *phys,
              uint32_t pages);

/* IDENTIFY DEVICE's 256 words: the disk's model, size and features; false
 * (with *why) if Madire does not use it. */
bool ahci_parse_identify(const uint16_t *w, struct ahci_disk *d, const char **why);

/* The opcode Madire sends for `op` (0 for none). */
uint8_t ahci_opcode(enum ahci_op op);

const char *ahci_end_str(enum ahci_end end);
const char *ahci_state_str(enum ahci_port_state state);
const char *ahci_op_str(enum ahci_op op);

#endif
