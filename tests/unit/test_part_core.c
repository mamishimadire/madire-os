/* Unit tests for kernel/storage/part_core.c (partition tables, read only,
 * 1.4 stage 5) against a fake disk of 8192 sectors: no table (an empty
 * disk); MBR with one, several and no partitions; MBR garbage, a bad status
 * byte, overlapping entries, an entry beyond the disk or at LBA 0, a read
 * error; GPT read back with its header and entry-array CRC32s equal to
 * Python's zlib.crc32 of the same bytes (0x92F117AE and 0x6706DB40), a bad
 * header CRC32, a bad entry-array CRC32, overlapping entries, an entry
 * outside the usable range, a huge and a zero entry count, entry sizes
 * (127, 132 refused; 136, entries across sectors, read), the revision,
 * header size and own LBA, the array's place, a missing header, a read
 * error, more than 16 partitions, names (ASCII, '?' for others), and the
 * backup header checked and reported only. Every malformed table gives no
 * partitions. The modules are included directly. */
#include <stdbool.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/storage/blk_core.c"
#include "kernel/storage/part_core.c"

#define PD_SECTORS  8192u
#define PD_LAST     (PD_SECTORS - 1u)
#define PD_BACKUP_ARRAY (PD_LAST - 32u)     /* 8159 */

static uint8_t pd[PD_SECTORS][512];
static uint64_t pd_fail;
static uint32_t pd_reads;
static struct part_table t;

static int pd_read(void *ctx, uint64_t lba, uint8_t *sector)
{
    (void)ctx;
    pd_reads++;
    if (lba == pd_fail || lba >= PD_SECTORS)
        return -1;
    memcpy(sector, pd[lba], 512);
    return 0;
}

static const struct part_ops pd_ops = { pd_read, 0 };

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void pd_clear(void)
{
    memset(pd, 0, sizeof(pd));
    pd_fail = ~0ull;
    pd_reads = 0;
}

static void readt(void)
{
    part_read(&t, &pd_ops, PD_SECTORS);
}

static void bad(const char *why)
{
    readt();
    CHECK_EQ(t.kind, PART_KIND_BAD);
    CHECK_EQ(t.n, 0);
    CHECK(t.why != 0 && strstr(t.why, why) != 0);
    if (!t.why || !strstr(t.why, why))
        printf("    why: %s (expected: %s)\n", t.why ? t.why : "(none)", why);
}

/* ---- MBR ---- */

static void mbr_sig(void)
{
    pd[0][510] = 0x55;
    pd[0][511] = 0xAA;
    put32(pd[0] + 440, 0x4D414452u);
}

static void mbr_entry(int i, uint8_t status, uint8_t type, uint32_t first, uint32_t count)
{
    uint8_t *e = pd[0] + 446 + 16 * i;
    e[0] = status;
    e[4] = type;
    put32(e + 8, first);
    put32(e + 12, count);
}

static void test_crc_vectors(void)
{
    CHECK_EQ(blk_crc32(0, (const uint8_t *)"123456789", 9), 0xCBF43926u);
    static const uint8_t zeros[16384];
    CHECK_EQ(blk_crc32(0, zeros, sizeof(zeros)), 0xAB54D286u);     /* zlib.crc32(bytes(16384)) */
}

static void test_none(void)
{
    pd_clear();
    readt();
    CHECK_EQ(t.kind, PART_KIND_NONE);
    CHECK_EQ(t.n, 0);
    CHECK(t.why == 0);
    CHECK_EQ(pd_reads, 1);
    pd[0][510] = 0x55;                      /* half a signature */
    readt();
    CHECK_EQ(t.kind, PART_KIND_NONE);
}

static void test_mbr_one(void)
{
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0x00, 0x83, 2048, 4096);
    readt();
    CHECK_EQ(t.kind, PART_KIND_MBR);
    CHECK_EQ(t.n, 1);
    CHECK_EQ(t.mbr_signature, 0x4D414452u);
    CHECK_EQ(t.e[0].index, 1);
    CHECK_EQ(t.e[0].first, 2048);
    CHECK_EQ(t.e[0].sectors, 4096);
    CHECK_EQ(t.e[0].mbr_type, 0x83);
    CHECK(t.e[0].linux_fs);
    CHECK_EQ(t.e[0].name[0], 0);
    CHECK_EQ(pd_reads, 1);
    mbr_entry(0, 0x80, 0x83, 2048, PD_SECTORS - 2048);  /* bootable, to the last sector */
    readt();
    CHECK_EQ(t.kind, PART_KIND_MBR);
    CHECK_EQ(t.e[0].sectors, PD_SECTORS - 2048);
}

static void test_mbr_several(void)
{
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0x00, 0x07, 2048, 100);
    mbr_entry(2, 0x00, 0x83, 4096, 100);
    mbr_entry(3, 0x00, 0x05, 5000, 10);     /* extended: listed by its type only */
    readt();
    CHECK_EQ(t.kind, PART_KIND_MBR);
    CHECK_EQ(t.n, 3);
    CHECK_EQ(t.e[0].index, 1);
    CHECK(!t.e[0].linux_fs);
    CHECK_EQ(t.e[1].index, 3);
    CHECK(t.e[1].linux_fs);
    CHECK_EQ(t.e[2].index, 4);
    CHECK_EQ(t.e[2].mbr_type, 0x05);
    pd_clear();
    mbr_sig();                              /* a signature and four unused entries */
    readt();
    CHECK_EQ(t.kind, PART_KIND_MBR);
    CHECK_EQ(t.n, 0);
}

static void test_mbr_garbage(void)
{
    pd_clear();
    uint32_t x = 1405;
    for (int i = 0; i < 512; i++) {
        x = x * 1103515245u + 12345u;
        pd[0][i] = (uint8_t)(x >> 16);
    }
    pd[0][446 + 4] = 0x83;                  /* entry 1 used, its status byte garbage */
    pd[0][446] = 0x5A;
    pd[0][510] = 0x55;
    pd[0][511] = 0xAA;
    bad("status byte");
    pd_clear();
    mbr_sig();
    mbr_entry(1, 0x01, 0x83, 2048, 100);
    bad("status byte");
}

static void test_mbr_bounds(void)
{
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0, 0x83, 2048, 1000);
    mbr_entry(1, 0, 0x83, 2500, 100);
    bad("overlap");
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0, 0x83, 8000, 500);
    bad("beyond the end");
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0, 0x83, 8000, 192);       /* exactly to the end: fine */
    readt();
    CHECK_EQ(t.kind, PART_KIND_MBR);
    mbr_entry(1, 0, 0x83, 0, 10);
    bad("LBA 0");
    mbr_entry(1, 0, 0x83, 100, 0);
    bad("no sectors");
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0, 0x83, 0xFFFFFFF0u, 0xFFFFFFF0u);    /* 32-bit fields that would wrap */
    bad("beyond the end");
}

static void test_mbr_read_error(void)
{
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0, 0x83, 2048, 100);
    pd_fail = 0;
    bad("LBA 0 could not be read");
}

/* ---- GPT ---- */

static const uint8_t linux_guid[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4,
};
static const uint8_t other_guid[16] = {     /* EFI system partition C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B,
};

static uint8_t *gpt_array(void)
{
    return (uint8_t *)pd + 2u * 512u;        /* from LBA 2, across rows (pd is one object) */
}

static void gpt_entry(uint32_t no, uint32_t size, const uint8_t *type, uint64_t first, uint64_t last,
                      const char *name)
{
    uint8_t *e = gpt_array() + no * size;
    memcpy(e, type, 16);
    memset(e + 16, 0x11, 16);
    put64(e + 32, first);
    put64(e + 40, last);
    put64(e + 48, 0);
    for (int k = 0; name[k]; k++) {
        e[56 + 2 * k] = (uint8_t)name[k];
        e[57 + 2 * k] = 0;
    }
}

static void gpt_crc(uint8_t *h)
{
    put32(h + 16, 0);
    put32(h + 16, blk_crc32(0, h, get32(h + 12)));
}

static void gpt_header(uint64_t lba, uint64_t alt, uint64_t elba, uint32_t entries, uint32_t size,
                       uint32_t acrc)
{
    uint8_t *h = pd[lba];
    memset(h, 0, 512);
    memcpy(h, "EFI PART", 8);
    put32(h + 8, 0x00010000u);
    put32(h + 12, 92);
    put64(h + 24, lba);
    put64(h + 32, alt);
    put64(h + 40, 34);
    put64(h + 48, PD_LAST - 33);            /* 8158 */
    memset(h + 56, 0x22, 16);
    put64(h + 72, elba);
    put32(h + 80, entries);
    put32(h + 84, size);
    put32(h + 88, acrc);
    gpt_crc(h);
}

/* Both headers and the backup array for `entries` entries of `size` bytes. */
static void gpt_finish(uint32_t entries, uint32_t size)
{
    uint32_t bytes = entries * size, acrc = blk_crc32(0, gpt_array(), bytes);
    memcpy((uint8_t *)pd + (size_t)PD_BACKUP_ARRAY * 512u, gpt_array(), bytes);
    gpt_header(1, PD_LAST, 2, entries, size, acrc);
    gpt_header(PD_LAST, 1, PD_BACKUP_ARRAY, entries, size, acrc);
}

static void gpt_disk(void)
{
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0, 0xEE, 1, PD_LAST);
    gpt_entry(0, 128, linux_guid, 2048, 6143, "madire-test");
    gpt_finish(128, 128);
}

static void test_gpt_ok(void)
{
    gpt_disk();
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK_EQ(t.n, 1);
    CHECK(t.why == 0);
    CHECK_EQ(t.e[0].index, 1);
    CHECK_EQ(t.e[0].first, 2048);
    CHECK_EQ(t.e[0].sectors, 4096);
    CHECK_EQ(t.e[0].mbr_type, 0);
    CHECK(t.e[0].linux_fs);
    CHECK(strcmp(t.e[0].name, "madire-test") == 0);
    CHECK(memcmp(t.e[0].type_guid, linux_guid, 16) == 0);
    CHECK_EQ(t.gpt_header_crc, 0x92F117AEu);    /* Python's zlib.crc32 of the same header */
    CHECK_EQ(t.gpt_array_crc, 0x6706DB40u);     /* ... and of the same 16 KiB entry array */
    CHECK_EQ(t.gpt_entries, 128);
    CHECK_EQ(t.gpt_entry_size, 128);
    CHECK_EQ(t.gpt_entries_lba, 2);
    CHECK_EQ(t.gpt_first_usable, 34);
    CHECK_EQ(t.gpt_last_usable, 8158);
    CHECK_EQ(t.gpt_alternate, PD_LAST);
    CHECK(t.backup_ok);
    CHECK(t.backup_why == 0);
    CHECK_EQ(pd_reads, 1 + 1 + 32 + 1);     /* MBR, header, array, backup header */
    CHECK(part_guid_linux(linux_guid));
    CHECK(!part_guid_linux(other_guid));
    /* a second partition of another type, and a hole in the array */
    gpt_entry(5, 128, other_guid, 100, 199, "EFI system");
    gpt_finish(128, 128);
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK_EQ(t.n, 2);
    CHECK_EQ(t.e[1].index, 6);
    CHECK(!t.e[1].linux_fs);
    CHECK(strcmp(t.e[1].name, "EFI system") == 0);
}

static void test_gpt_crc_bad(void)
{
    gpt_disk();
    pd[1][60] ^= 1;                         /* the disk GUID, CRC32 not updated */
    bad("header's CRC32");
    gpt_disk();
    gpt_array()[100] ^= 1;                  /* entry 1's name, the array CRC32 not updated */
    bad("entry array's CRC32");
    gpt_disk();
    gpt_array()[128 * 127] = 1;             /* the last entry's type byte */
    bad("entry array's CRC32");
}

static void test_gpt_entries_bad(void)
{
    gpt_disk();
    gpt_entry(1, 128, linux_guid, 4000, 4100, "b");
    gpt_finish(128, 128);
    bad("overlap");
    gpt_disk();
    gpt_entry(0, 128, linux_guid, 2048, PD_LAST - 32, "x");    /* 8159 > the last usable 8158 */
    gpt_finish(128, 128);
    bad("outside the usable range");
    gpt_disk();
    gpt_entry(0, 128, linux_guid, 33, 100, "x");               /* before the first usable 34 */
    gpt_finish(128, 128);
    bad("outside the usable range");
    gpt_disk();
    gpt_entry(0, 128, linux_guid, 3000, 2999, "x");            /* first after last */
    gpt_finish(128, 128);
    bad("outside the usable range");
    gpt_disk();
    gpt_entry(0, 128, linux_guid, 34, 8158, "x");              /* the whole usable range: fine */
    gpt_finish(128, 128);
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK_EQ(t.n, 1);
}

static void test_gpt_counts(void)
{
    gpt_disk();
    put32(pd[1] + 80, 100000);              /* a huge entry count, CRC32 updated */
    gpt_crc(pd[1]);
    bad("entry count");
    gpt_disk();
    put32(pd[1] + 80, 0xFFFFFFFFu);
    gpt_crc(pd[1]);
    bad("entry count");
    gpt_disk();
    put32(pd[1] + 80, 0);
    gpt_crc(pd[1]);
    bad("entry count");
    gpt_disk();
    put32(pd[1] + 84, 127);
    gpt_crc(pd[1]);
    bad("entry size");
    gpt_disk();
    put32(pd[1] + 84, 132);
    gpt_crc(pd[1]);
    bad("entry size");
    gpt_disk();
    put32(pd[1] + 84, 0x80000000u);         /* a multiple of 8, but count * size is huge */
    gpt_crc(pd[1]);
    bad("entry count");
}

static void test_gpt_entry_136(void)
{
    /* 64 entries of 136 bytes (17 sectors): entries cross sector ends. */
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0, 0xEE, 1, PD_LAST);
    gpt_entry(3, 136, other_guid, 100, 199, "three");       /* bytes 408-543: across sectors 2 and 3 */
    gpt_entry(4, 136, linux_guid, 2048, 6143, "madire-test");
    gpt_finish(64, 136);
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK_EQ(t.n, 2);
    CHECK_EQ(t.e[0].index, 4);
    CHECK(strcmp(t.e[0].name, "three") == 0);
    CHECK_EQ(t.e[0].first, 100);
    CHECK_EQ(t.e[1].index, 5);
    CHECK(t.e[1].linux_fs);
    CHECK_EQ(t.e[1].sectors, 4096);
    CHECK_EQ(t.gpt_entry_size, 136);
    CHECK(t.backup_ok);
    CHECK_EQ(pd_reads, 1 + 1 + 17 + 1);
}

static void test_gpt_header_fields(void)
{
    gpt_disk();
    put32(pd[1] + 8, 0x00020000u);
    gpt_crc(pd[1]);
    bad("revision");
    gpt_disk();
    put32(pd[1] + 12, 91);
    gpt_crc(pd[1]);
    bad("size");
    gpt_disk();
    put32(pd[1] + 12, 513);
    put32(pd[1] + 16, 0);
    bad("size");
    gpt_disk();
    put64(pd[1] + 24, 2);
    gpt_crc(pd[1]);
    bad("own LBA");
    gpt_disk();
    put64(pd[1] + 72, 40);                  /* the array after the first usable LBA */
    gpt_crc(pd[1]);
    bad("entry array is not between");
    gpt_disk();
    put64(pd[1] + 72, 1);                   /* the array on the header */
    gpt_crc(pd[1]);
    bad("entry array is not between");
    gpt_disk();
    put64(pd[1] + 48, PD_SECTORS);          /* the last usable LBA beyond the disk */
    gpt_crc(pd[1]);
    bad("usable range");
    gpt_disk();
    put64(pd[1] + 40, 9000);                /* first usable after last usable */
    gpt_crc(pd[1]);
    bad("usable range");
    gpt_disk();
    put32(pd[1] + 12, 120);                 /* a longer header: its CRC32 over 120 bytes */
    gpt_crc(pd[1]);
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK_EQ(t.gpt_header_size, 120);
}

static void test_gpt_missing_and_errors(void)
{
    gpt_disk();
    memset(pd[1], 0, 512);
    bad("no GPT header at LBA 1");
    gpt_disk();
    pd_fail = 1;
    bad("LBA 1");
    gpt_disk();
    pd_fail = 7;
    bad("entry array could not be read");
    gpt_disk();
    pd_fail = PD_LAST;                      /* the backup cannot be read: reported only */
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK_EQ(t.n, 1);
    CHECK(!t.backup_ok);
    CHECK(t.backup_why && strstr(t.backup_why, "could not be read"));
}

static void test_gpt_too_many(void)
{
    pd_clear();
    mbr_sig();
    mbr_entry(0, 0, 0xEE, 1, PD_LAST);
    for (uint32_t i = 0; i < 16; i++)
        gpt_entry(i, 128, linux_guid, 100 + 10 * i, 109 + 10 * i, "p");
    gpt_finish(128, 128);
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK_EQ(t.n, 16);
    gpt_entry(16, 128, linux_guid, 300, 309, "p");
    gpt_finish(128, 128);
    bad("more than 16");
}

static void test_gpt_names(void)
{
    gpt_disk();
    uint8_t *e = gpt_array();
    e[56 + 2 * 3] = 0xE9;                   /* U+00E9 */
    e[56 + 2 * 4] = 0x20;
    e[57 + 2 * 4] = 0x26;                   /* U+2620 */
    for (int k = 0; k < 36; k++)            /* a name of all 36 units, no terminator */
        if (k > 10) {
            e[56 + 2 * k] = 'z';
            e[57 + 2 * k] = 0;
        }
    gpt_finish(128, 128);
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK_EQ(strlen(t.e[0].name), 36);
    CHECK(strncmp(t.e[0].name, "mad??", 5) == 0);
    CHECK_EQ(t.e[0].name[35], 'z');
}

static void test_gpt_backup(void)
{
    gpt_disk();
    pd[PD_LAST][60] ^= 1;                   /* the backup's disk GUID, its CRC32 not updated */
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK_EQ(t.n, 1);
    CHECK(!t.backup_ok);
    CHECK(t.backup_why && strstr(t.backup_why, "CRC32"));
    gpt_disk();
    pd[PD_LAST][60] ^= 1;
    gpt_crc(pd[PD_LAST]);
    readt();
    CHECK(!t.backup_ok);
    CHECK(t.backup_why && strstr(t.backup_why, "disk GUID"));
    gpt_disk();
    memset(pd[PD_LAST], 0, 512);
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK(!t.backup_ok);
    CHECK(t.backup_why && strstr(t.backup_why, "signature"));
    gpt_disk();
    put64(pd[1] + 32, 8000);                /* the primary's alternate LBA not the last */
    gpt_crc(pd[1]);
    readt();
    CHECK_EQ(t.kind, PART_KIND_GPT);
    CHECK(!t.backup_ok);
    CHECK(t.backup_why && strstr(t.backup_why, "alternate LBA"));
    gpt_disk();
    put32(pd[PD_LAST] + 88, 0x12345678u);   /* the backup's array CRC32 field differs */
    gpt_crc(pd[PD_LAST]);
    readt();
    CHECK(!t.backup_ok);
    CHECK(t.backup_why && strstr(t.backup_why, "entry-array CRC32"));
}

static void test_kind_str(void)
{
    CHECK(strcmp(part_kind_str(PART_KIND_NONE), "no partition table") == 0);
    CHECK(strcmp(part_kind_str(PART_KIND_MBR), "MBR") == 0);
    CHECK(strcmp(part_kind_str(PART_KIND_GPT), "GPT") == 0);
    CHECK(strcmp(part_kind_str(PART_KIND_BAD), "partition table not usable") == 0);
}

int main(void)
{
    printf("test_part_core\n");
    RUN(test_crc_vectors);
    RUN(test_none);
    RUN(test_mbr_one);
    RUN(test_mbr_several);
    RUN(test_mbr_garbage);
    RUN(test_mbr_bounds);
    RUN(test_mbr_read_error);
    RUN(test_gpt_ok);
    RUN(test_gpt_crc_bad);
    RUN(test_gpt_entries_bad);
    RUN(test_gpt_counts);
    RUN(test_gpt_entry_136);
    RUN(test_gpt_header_fields);
    RUN(test_gpt_missing_and_errors);
    RUN(test_gpt_too_many);
    RUN(test_gpt_names);
    RUN(test_gpt_backup);
    RUN(test_kind_str);
    return unit_report("test_part_core");
}
