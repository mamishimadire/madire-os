#ifndef MADIRE_STORAGE_GUARD_CORE_H
#define MADIRE_STORAGE_GUARD_CORE_H

/*
 * The write guard, the pure part (1.4 stage 5; docs/plans/1.4.md, D5, C4;
 * AGENTS.md section 6, "virtual disks only", in code). A disk may be
 * written only if ALL THREE hold for the partition used:
 *
 *   (a) the disk is virtual: IDENTIFY DEVICE's model, trimmed of spaces at
 *       both ends, is exactly "QEMU HARDDISK" or "VBOX HARDDISK";
 *   (b) the partition's first sector holds the Madire test marker
 *       (tests/disk_image.py): "MADIRE-TEST-DISK" (16 bytes), the format
 *       version 1 (u32 at 16), the record length 48 (u32 at 20), the
 *       partition's first LBA (u64 at 24) and sector count (u64 at 32),
 *       both EQUAL to the partition table's, the CRC32 of bytes 0-39 (u32
 *       at 40), and zero from byte 44 to the end of the sector;
 *   (c) the ext2 volume label is exactly "madire-test": in the superblock
 *       at the partition's byte 1024 only the magic (0xEF53, u16 at 56)
 *       and the 16-byte label (at 120: the name, then zero bytes) are read
 *       (the ext2 reader itself is stage 6).
 *
 * The conditions are checked in that order and the first that fails is
 * the result. No hardware access, printing or allocation here (host unit
 * tests: tests/unit/test_guard_core.c); kernel/storage/guard.c reads the
 * sectors, logs the result and hands the disk's decision, made once at
 * attach, to the lowest layer (ahci_guard_decide, ahci_core.h), the only
 * place that keeps it, and (1.4 stage 10) the one place that reads it: the
 * command builder refuses a write or a flush the decision does not allow.
 */

#include <stdbool.h>
#include <stdint.h>

#define GUARD_SECTOR        512u
#define GUARD_MARKER_VERSION 1u
#define GUARD_MARKER_LEN    48u
#define GUARD_EXT2_MAGIC    0xEF53u
#define GUARD_MIN_SECTORS   4u      /* the marker's sector, one more, the superblock's two */
#define GUARD_SB_SECTOR     2u      /* the superblock's first sector in the partition (byte 1024) */
#define GUARD_MODEL_MAX     40u
#define GUARD_LABEL_LEN     16u

enum guard_cond {
    GUARD_OK,           /* all three hold */
    GUARD_A_MODEL,      /* (a) not a virtual disk's model */
    GUARD_B_SMALL,      /* (b) the partition cannot hold the marker and the superblock */
    GUARD_B_MAGIC,      /* (b) no "MADIRE-TEST-DISK" */
    GUARD_B_VERSION,    /* (b) a version other than 1 (got) */
    GUARD_B_LENGTH,     /* (b) a record length other than 48 (got) */
    GUARD_B_CRC,        /* (b) the CRC32 (got: stored, want: computed) */
    GUARD_B_START,      /* (b) the first LBA (got: the marker's, want: the table's) */
    GUARD_B_SECTORS,    /* (b) the sector count (got: the marker's, want: the table's) */
    GUARD_B_TAIL,       /* (b) a byte not zero after the record */
    GUARD_C_MAGIC,      /* (c) no ext2 superblock (got: the magic read) */
    GUARD_C_LABEL,      /* (c) the label is not "madire-test" (label: what was read) */
};

struct guard_verdict {
    enum guard_cond cond;
    uint64_t got, want;                 /* the figures of the failed condition */
    char model[GUARD_MODEL_MAX + 1];    /* the model, trimmed */
    char label[GUARD_LABEL_LEN + 1];    /* the label read (ASCII, '?' for others), if (c) was reached */
};

/* (a) alone: the model, trimmed into `trimmed` (GUARD_MODEL_MAX + 1
 * bytes), is a virtual disk's. */
bool guard_model_virtual(const char *model, char *trimmed);

/* All three for the partition [first, first + sectors): `marker` is the
 * partition's first sector, `sb` its sector GUARD_SB_SECTOR (the
 * superblock's first 512 bytes); both may be 0 if (a) or the size already
 * fails. `v` is set from scratch; returns v->cond == GUARD_OK. */
bool guard_check(struct guard_verdict *v, const char *model, uint64_t first, uint64_t sectors,
                 const uint8_t *marker, const uint8_t *sb);

/* "(a)", "(b)", "(c)" or "" (GUARD_OK): the condition's letter. */
const char *guard_cond_letter(enum guard_cond cond);

#endif
