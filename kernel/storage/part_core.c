/* Partition tables, the pure part (1.4 stage 5): see part_core.h. Read
 * only. */
#include "kernel/storage/part_core.h"
#include "kernel/storage/blk_core.h"

/* GPT's "Linux filesystem data" type, 0FC63DAF-8483-4772-8E79-3D69D8477DE4,
 * as stored (the first three fields little-endian). */
static const uint8_t part_linux_guid[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4,
};

static const uint8_t part_gpt_sig[8] = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };

static uint32_t part_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t part_le64(const uint8_t *p)
{
    return (uint64_t)part_le32(p) | (uint64_t)part_le32(p + 4) << 32;
}

static bool part_same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

static bool part_zero(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i])
            return false;
    return true;
}

bool part_guid_linux(const uint8_t *guid)
{
    return part_same(guid, part_linux_guid, 16);
}

static void part_bad(struct part_table *t, const char *why)
{
    if (t->kind != PART_KIND_BAD) {
        t->kind = PART_KIND_BAD;
        t->why = why;
    }
    t->n = 0;
}

static bool part_get(struct part_table *t, const struct part_ops *ops, uint64_t lba)
{
    return lba < t->disk_sectors && ops->read(ops->ctx, lba, t->sec) == 0;
}

/* Adds a partition [first, first + sectors) unless it overlaps one kept:
 * the new entry, or 0 (with *why) if it overlaps or there is no room. */
static struct part_entry *part_add(struct part_table *t, uint64_t first, uint64_t sectors, const char **why,
                                   const char *overlap, const char *full)
{
    for (uint32_t i = 0; i < t->n; i++) {
        const struct part_entry *o = &t->e[i];
        if (first < o->first + o->sectors && o->first < first + sectors) {
            *why = overlap;
            return 0;
        }
    }
    if (t->n == PART_MAX) {
        *why = full;
        return 0;
    }
    struct part_entry *e = &t->e[t->n++];
    e->index = 0;
    e->first = first;
    e->sectors = sectors;
    e->mbr_type = 0;
    for (uint32_t i = 0; i < 16; i++)
        e->type_guid[i] = 0;
    e->name[0] = 0;
    e->linux_fs = false;
    return e;
}

/* ---- GPT ---- */

/* One complete entry (t->ent, its first 128 bytes) of number `no`. */
static void part_gpt_entry(struct part_table *t, uint32_t no, const char **why)
{
    const uint8_t *x = t->ent;
    if (*why || part_zero(x, 16))
        return;                                 /* unused (or the array already failed) */
    uint64_t first = part_le64(x + 32), last = part_le64(x + 40);
    if (first > last || first < t->gpt_first_usable || last > t->gpt_last_usable) {
        *why = "a GPT partition lies outside the usable range";
        return;
    }
    struct part_entry *e = part_add(t, first, last - first + 1, why, "two GPT partitions overlap",
                                    "more than 16 GPT partitions");
    if (!e)
        return;
    e->index = no + 1;
    for (uint32_t i = 0; i < 16; i++)
        e->type_guid[i] = x[i];
    e->linux_fs = part_guid_linux(x);
    uint32_t k = 0;
    for (; k < PART_NAME_MAX; k++) {
        uint32_t u = (uint32_t)x[56 + 2 * k] | (uint32_t)x[57 + 2 * k] << 8;
        if (!u)
            break;
        e->name[k] = u >= 0x20u && u < 0x7Fu ? (char)u : '?';
    }
    e->name[k] = 0;
}

/* Streams the primary entry array: its CRC32 and its entries. False (with
 * the table BAD) on a read error. */
static bool part_gpt_array(struct part_table *t, const struct part_ops *ops, uint32_t *crc, const char **why)
{
    uint32_t total = t->gpt_entries * t->gpt_entry_size, done = 0, off = 0, no = 0;
    uint32_t c = 0;
    for (uint64_t lba = t->gpt_entries_lba; done < total; lba++) {
        if (!part_get(t, ops, lba)) {
            part_bad(t, "a sector of the GPT entry array could not be read");
            return false;
        }
        uint32_t take = total - done < PART_SECTOR ? total - done : PART_SECTOR;
        c = blk_crc32(c, t->sec, take);
        for (uint32_t j = 0; j < take; j++) {
            if (off < 128u)
                t->ent[off] = t->sec[j];
            if (off == 127u)
                part_gpt_entry(t, no, why);
            if (++off == t->gpt_entry_size) {
                off = 0;
                no++;
            }
        }
        done += take;
    }
    *crc = c;
    return true;
}

/* The header in t->sec: its CRC32 matches (computed over `size` bytes with
 * the CRC32 field as zero). Changes t->sec's CRC32 field to zero. */
static bool part_gpt_crc_ok(struct part_table *t, uint32_t size, uint32_t *stored)
{
    *stored = part_le32(t->sec + 16);
    for (uint32_t i = 16; i < 20; i++)
        t->sec[i] = 0;
    return blk_crc32(0, t->sec, size) == *stored;
}

/* The backup header: checked and reported only. */
static void part_gpt_backup(struct part_table *t, const struct part_ops *ops)
{
    uint64_t alt = t->gpt_alternate, last = t->disk_sectors - 1;
    uint32_t stored = 0, size = 0;
    uint64_t asec = ((uint64_t)t->gpt_entries * t->gpt_entry_size + PART_SECTOR - 1) / PART_SECTOR;
    t->backup_ok = false;
    if (alt != last) {
        t->backup_why = "the primary header's alternate LBA is not the disk's last";
        return;
    }
    if (!part_get(t, ops, alt)) {
        t->backup_why = "it could not be read";
        return;
    }
    size = part_le32(t->sec + 12);
    if (!part_same(t->sec, part_gpt_sig, 8)) {
        t->backup_why = "no \"EFI PART\" signature";
    } else if (part_le32(t->sec + 8) != PART_GPT_REVISION) {
        t->backup_why = "its revision is not 1.0";
    } else if (size < 92u || size > PART_SECTOR) {
        t->backup_why = "its header size is not 92 to 512 bytes";
    } else if (!part_gpt_crc_ok(t, size, &stored)) {
        t->backup_why = "its header CRC32 does not match";
    } else if (part_le64(t->sec + 24) != alt || part_le64(t->sec + 32) != 1) {
        t->backup_why = "its own and alternate LBAs are not the last LBA and 1";
    } else if (part_le64(t->sec + 40) != t->gpt_first_usable || part_le64(t->sec + 48) != t->gpt_last_usable ||
               !part_same(t->sec + 56, t->gpt_disk_guid, 16)) {
        t->backup_why = "its usable range or disk GUID differs from the primary's";
    } else if (part_le32(t->sec + 80) != t->gpt_entries || part_le32(t->sec + 84) != t->gpt_entry_size ||
               part_le32(t->sec + 88) != t->gpt_array_crc) {
        t->backup_why = "its entry count, entry size or entry-array CRC32 differs from the primary's";
    } else if (part_le64(t->sec + 72) <= t->gpt_last_usable || part_le64(t->sec + 72) + asec > alt) {
        t->backup_why = "its entry array is not between the usable range and the backup header";
    } else {
        t->backup_ok = true;
        t->backup_why = 0;
    }
}

static void part_gpt(struct part_table *t, const struct part_ops *ops)
{
    uint32_t stored = 0, crc = 0;
    const char *why = 0;
    if (t->disk_sectors < 3u) {
        part_bad(t, "the disk is too small for GPT");
        return;
    }
    if (!part_get(t, ops, 1)) {
        part_bad(t, "LBA 1 (the GPT header) could not be read");
        return;
    }
    if (!part_same(t->sec, part_gpt_sig, 8)) {
        part_bad(t, "a protective MBR (type 0xEE) but no GPT header at LBA 1");
        return;
    }
    t->gpt_revision = part_le32(t->sec + 8);
    t->gpt_header_size = part_le32(t->sec + 12);
    if (t->gpt_revision != PART_GPT_REVISION) {
        part_bad(t, "the GPT header's revision is not 1.0");
        return;
    }
    if (t->gpt_header_size < 92u || t->gpt_header_size > PART_SECTOR) {
        part_bad(t, "the GPT header's size is not 92 to 512 bytes");
        return;
    }
    if (!part_gpt_crc_ok(t, t->gpt_header_size, &stored)) {
        part_bad(t, "the GPT header's CRC32 does not match");
        return;
    }
    t->gpt_header_crc = stored;
    t->gpt_alternate = part_le64(t->sec + 32);
    t->gpt_first_usable = part_le64(t->sec + 40);
    t->gpt_last_usable = part_le64(t->sec + 48);
    for (uint32_t i = 0; i < 16; i++)
        t->gpt_disk_guid[i] = t->sec[56 + i];
    t->gpt_entries_lba = part_le64(t->sec + 72);
    t->gpt_entries = part_le32(t->sec + 80);
    t->gpt_entry_size = part_le32(t->sec + 84);
    t->gpt_array_crc = part_le32(t->sec + 88);
    if (part_le64(t->sec + 24) != 1) {
        part_bad(t, "the GPT header's own LBA is not 1");
        return;
    }
    if (t->gpt_entry_size < 128u || t->gpt_entry_size % 8u) {
        part_bad(t, "the GPT entry size is not a multiple of 8 of at least 128 bytes");
        return;
    }
    if (t->gpt_entries == 0 || (uint64_t)t->gpt_entries * t->gpt_entry_size > PART_GPT_ARRAY_MAX) {
        part_bad(t, "the GPT entry count is 0 or its entry array is larger than 64 KiB");
        return;
    }
    uint64_t asec = ((uint64_t)t->gpt_entries * t->gpt_entry_size + PART_SECTOR - 1) / PART_SECTOR;
    if (t->gpt_first_usable > t->gpt_last_usable || t->gpt_last_usable >= t->disk_sectors) {
        part_bad(t, "the GPT usable range is not inside the disk");
        return;
    }
    if (t->gpt_entries_lba < 2u || t->gpt_entries_lba > t->gpt_first_usable ||
        asec > t->gpt_first_usable - t->gpt_entries_lba) {
        part_bad(t, "the GPT entry array is not between the header and the usable range");
        return;
    }
    if (!part_gpt_array(t, ops, &crc, &why))
        return;
    if (crc != t->gpt_array_crc) {
        part_bad(t, "the GPT entry array's CRC32 does not match");
        return;
    }
    if (why) {
        part_bad(t, why);
        return;
    }
    t->kind = PART_KIND_GPT;
    part_gpt_backup(t, ops);
}

/* ---- MBR ---- */

void part_read(struct part_table *t, const struct part_ops *ops, uint64_t disk_sectors)
{
    t->kind = PART_KIND_NONE;
    t->disk_sectors = disk_sectors;
    t->n = 0;
    t->why = 0;
    t->mbr_signature = 0;
    t->gpt_revision = t->gpt_header_size = t->gpt_entries = t->gpt_entry_size = 0;
    t->gpt_alternate = t->gpt_first_usable = t->gpt_last_usable = t->gpt_entries_lba = 0;
    t->gpt_header_crc = t->gpt_array_crc = 0;
    for (uint32_t i = 0; i < 16; i++)
        t->gpt_disk_guid[i] = 0;
    t->backup_ok = false;
    t->backup_why = 0;

    if (!part_get(t, ops, 0)) {
        part_bad(t, "LBA 0 could not be read");
        return;
    }
    if (t->sec[510] != 0x55u || t->sec[511] != 0xAAu)
        return;                                 /* PART_KIND_NONE */
    t->mbr_signature = part_le32(t->sec + 440);
    for (uint32_t i = 0; i < 4; i++)
        if (t->sec[446 + 16 * i + 4] == PART_MBR_PROTECTIVE) {
            part_gpt(t, ops);
            return;
        }
    for (uint32_t i = 0; i < 4; i++) {
        const uint8_t *x = t->sec + 446 + 16 * i;
        uint8_t type = x[4];
        if (!type)
            continue;
        uint64_t first = part_le32(x + 8), count = part_le32(x + 12);
        const char *why = 0;
        if (x[0] != 0x00u && x[0] != 0x80u) {
            part_bad(t, "an MBR entry's status byte is not 0x00 or 0x80");
            return;
        }
        if (first == 0 || count == 0) {
            part_bad(t, "an MBR partition starts at LBA 0 or has no sectors");
            return;
        }
        if (first + count > t->disk_sectors) {
            part_bad(t, "an MBR partition lies beyond the end of the disk");
            return;
        }
        struct part_entry *e = part_add(t, first, count, &why, "two MBR partitions overlap", "no room");
        if (!e) {
            part_bad(t, why);
            return;
        }
        e->index = i + 1;
        e->mbr_type = type;
        e->linux_fs = type == PART_MBR_LINUX;
    }
    t->kind = PART_KIND_MBR;
}

const char *part_kind_str(enum part_kind kind)
{
    switch (kind) {
    case PART_KIND_NONE: return "no partition table";
    case PART_KIND_MBR:  return "MBR";
    case PART_KIND_GPT:  return "GPT";
    case PART_KIND_BAD:  return "partition table not usable";
    }
    return "unknown";
}
