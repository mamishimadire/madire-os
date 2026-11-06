#ifndef MADIRE_STORAGE_PART_CORE_H
#define MADIRE_STORAGE_PART_CORE_H

/*
 * Partition tables, the pure part (1.4 stage 5; docs/plans/1.4.md, D10,
 * C6). READ ONLY: this code reads a disk's partition table through
 * struct part_ops and never writes anything. No hardware access, printing
 * or allocation here (host unit tests: tests/unit/test_part_core.c);
 * kernel/storage/part.c reads the sectors through the block cache and logs
 * what this part found.
 *
 * The MBR (LBA 0): the signature 0x55AA at bytes 510-511, then four
 * primary entries of 16 bytes at byte 446 (status, CHS, type, CHS, first
 * LBA, sector count). No signature: no partition table (an empty disk).
 * An entry of type 0 is unused; a used entry must have the status 0x00 or
 * 0x80, a first LBA of at least 1, at least one sector, lie inside the
 * disk and overlap no other. Extended partitions are listed by their type
 * only (logical partitions are not read in 1.4). An entry of type 0xEE (a
 * protective MBR) means GPT: the table is then GPT's alone.
 *
 * GPT (UEFI 2.11, chapter 5): the header at LBA 1: the signature
 * "EFI PART", revision 1.0 (0x00010000), a header size of 92 to 512 bytes,
 * its CRC32 (over the header size, the CRC32 field taken as zero), its own
 * LBA 1, a usable range inside the disk, an entry size that is a multiple
 * of 8 and at least 128 bytes, an entry count of at least 1 with the whole
 * entry array at most PART_GPT_ARRAY_MAX bytes, the array between the
 * header and the usable range, and the array's CRC32 (over count * size
 * bytes). A used entry (type GUID not zero) must lie inside the usable
 * range and overlap no other; at most PART_MAX are kept. Names are
 * UTF-16LE (36 code units); they are kept as ASCII ('?' for any other
 * character). The backup header (at the primary's alternate LBA) is
 * checked and REPORTED only (backup_ok, backup_why): its signature,
 * revision, size and CRC32, its own LBA (the disk's last), its alternate
 * LBA (1), the same usable range, disk GUID, entry count, size and array
 * CRC32, and its array after the usable range; it never decides anything.
 *
 * Anything malformed: PART_KIND_BAD, no partitions, `why` the first
 * problem found (static text, for one log line); never a panic. A sector
 * that cannot be read is the same (PART_KIND_BAD, "... could not be
 * read").
 *
 * The CRC32 is blk_crc32 (blk_core.h), GPT's own CRC32. The idea of
 * finding the partitions first and handing each to a filesystem as its
 * own range follows SerenityOS's storage code (docs/plans/1.4.md, section
 * M, ideas only); the formats are from the specification; the code is
 * Madire's own.
 */

#include <stdbool.h>
#include <stdint.h>

#define PART_SECTOR         512u
#define PART_MAX            16u         /* partitions kept */
#define PART_GPT_ARRAY_MAX  65536u      /* bytes of a GPT entry array at most (128 sectors) */
#define PART_NAME_MAX       36u         /* a GPT name's UTF-16 code units */
#define PART_MBR_LINUX      0x83u
#define PART_MBR_PROTECTIVE 0xEEu
#define PART_GPT_REVISION   0x00010000u

enum part_kind {
    PART_KIND_NONE,     /* no partition table (no 0x55AA signature in LBA 0) */
    PART_KIND_MBR,
    PART_KIND_GPT,
    PART_KIND_BAD,      /* a table that is not usable: no partitions (see why) */
};

struct part_entry {
    uint32_t index;                 /* 1-based: the MBR entry, or the GPT entry number */
    uint64_t first, sectors;
    uint8_t mbr_type;               /* MBR: the type byte; GPT: 0 */
    uint8_t type_guid[16];          /* GPT: as stored (mixed-endian); MBR: zero */
    char name[PART_NAME_MAX + 1];   /* GPT: ASCII; MBR: empty */
    bool linux_fs;                  /* MBR type 0x83, or GPT "Linux filesystem data" */
};

/* Reads one 512-byte sector: 0, or not 0 if it cannot be read. */
struct part_ops {
    int (*read)(void *ctx, uint64_t lba, uint8_t *sector);
    void *ctx;
};

struct part_table {
    enum part_kind kind;
    uint64_t disk_sectors;
    uint32_t n;                     /* partitions in e[] */
    struct part_entry e[PART_MAX];
    const char *why;                /* PART_KIND_BAD: the first problem (static text) */
    uint32_t mbr_signature;         /* the MBR's disk signature (bytes 440-443) */
    /* GPT, the primary header's fields */
    uint32_t gpt_revision, gpt_header_size, gpt_entries, gpt_entry_size;
    uint64_t gpt_alternate, gpt_first_usable, gpt_last_usable, gpt_entries_lba;
    uint32_t gpt_header_crc, gpt_array_crc;     /* as stored (and found valid) */
    uint8_t gpt_disk_guid[16];
    /* GPT, the backup header (report only) */
    bool backup_ok;
    const char *backup_why;         /* !backup_ok: why (static text) */
    /* scratch */
    uint8_t sec[PART_SECTOR];
    uint8_t ent[128];
};

/* The partition table of a disk of `disk_sectors` sectors. `t` is set from
 * scratch. */
void part_read(struct part_table *t, const struct part_ops *ops, uint64_t disk_sectors);

/* True if the 16 bytes are GPT's "Linux filesystem data" type GUID
 * (0FC63DAF-8483-4772-8E79-3D69D8477DE4, as stored on the disk). */
bool part_guid_linux(const uint8_t *guid);

const char *part_kind_str(enum part_kind kind);

#endif
