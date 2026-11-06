#ifndef MADIRE_STORAGE_GUARD_H
#define MADIRE_STORAGE_GUARD_H

/*
 * The write guard at attach (1.4 stage 5; docs/plans/1.4.md, D5, C4,
 * C6). Once per disk, when it is attached: its partition table is read
 * and logged (part.h); if the model is a virtual disk's (condition (a)),
 * each Linux partition in table order gets conditions (b) and (c)
 * (guard_core.h; its first sector and the superblock's first sector read
 * through the block cache), and the first that passes all three is the
 * one writes would be allowed on (C6: one disk, one partition in 1.4).
 * The decision goes to the AHCI driver (ahci_disk_guard), the only place
 * that keeps it, and is read back from there for the log. Every line
 * starts with "guard: ":
 *
 *   guard: port P: writes would be REFUSED: <reason>          (the disk)
 *   guard: port P partition I: writes would be ALLOWED (...)
 *   guard: port P partition I: writes would be REFUSED: <first failed condition>
 *   guard: port P: decided once, kept by the AHCI driver: writes would be
 *          ALLOWED on partition I (LBA F, S sectors) | REFUSED; no write
 *          command exists in stage 5
 *
 * Stage 5 has no write path: nothing else reads the decision.
 */

#include <stdbool.h>
#include <stdint.h>

/* For the Storage: line. */
struct guard_summary {
    bool allows;            /* as the AHCI driver keeps it */
    uint32_t partition;     /* allows: its index (1-based) */
    char table[40];         /* "MBR, 1 partition", "no partition table", ... */
    char result[64];        /* "ALLOWED on partition 1", "REFUSED: (b) no valid test marker", ... */
};

void guard_attach(uint32_t port, const char *model, uint64_t sectors, struct guard_summary *s);

#endif
