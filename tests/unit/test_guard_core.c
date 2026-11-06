/* Unit tests for kernel/storage/guard_core.c (the write guard, 1.4 stage
 * 5; docs/plans/1.4.md, D5, C4) and the decision kept by the lowest layer
 * (ahci_guard_decide, ahci_guard_allows in kernel/storage/ahci_core.c):
 * all three conditions holding (QEMU HARDDISK, VBOX HARDDISK, the model
 * trimmed); each condition failing ON ITS OWN, the others holding: (a)
 * other models and near misses; (b) no marker, a wrong magic, version,
 * record length, a bad CRC32 (the computed value against Python's
 * zlib.crc32 of the same 40 bytes, 0xED71A5FC), the marker's start and
 * length not matching the partition, a byte after the record, a partition
 * too small; (c) no superblock, a wrong label, a missing label, a label
 * with more after it; the order (the first failed condition is the
 * result); the decision recorded once, only for a disk in use, refused if
 * its partition is not inside the disk, and reset by ahci_start. The
 * modules are included directly. */
#include <stdbool.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/storage/blk_core.c"
#include "kernel/storage/guard_core.c"
#include "kernel/storage/ahci_core.c"

#define FIRST   2048u
#define COUNT   4096u

static uint8_t mk[512], sb[512];
static struct guard_verdict v;

static void put32(uint8_t *p, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(x >> (8 * i));
}

static void put64(uint8_t *p, uint64_t x)
{
    put32(p, (uint32_t)x);
    put32(p + 4, (uint32_t)(x >> 32));
}

static void marker_crc(void)
{
    put32(mk + 40, blk_crc32(0, mk, 40));
}

/* The marker as tests/disk_image.py writes it. */
static void marker(uint64_t first, uint64_t count)
{
    memset(mk, 0, sizeof(mk));
    memcpy(mk, "MADIRE-TEST-DISK", 16);
    put32(mk + 16, 1);
    put32(mk + 20, 48);
    put64(mk + 24, first);
    put64(mk + 32, count);
    marker_crc();
}

static void superblock(const char *label)
{
    memset(sb, 0, sizeof(sb));
    sb[56] = 0x53;
    sb[57] = 0xEF;
    memcpy(sb + 120, label, strlen(label));
}

static void good(void)
{
    marker(FIRST, COUNT);
    superblock("madire-test");
}

static enum guard_cond check(const char *model)
{
    bool ok = guard_check(&v, model, FIRST, COUNT, mk, sb);
    CHECK_EQ(ok, v.cond == GUARD_OK);
    return v.cond;
}

static void test_all_three(void)
{
    good();
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_OK);
    CHECK(strcmp(v.model, "QEMU HARDDISK") == 0);
    CHECK(strcmp(v.label, "madire-test") == 0);
    CHECK_EQ(check("VBOX HARDDISK"), GUARD_OK);
    CHECK_EQ(check("  QEMU HARDDISK    "), GUARD_OK);     /* trimmed at both ends */
    CHECK(strcmp(v.model, "QEMU HARDDISK") == 0);
    CHECK_EQ(blk_crc32(0, mk, 40), 0xED71A5FCu);          /* zlib.crc32 of the same 40 bytes */
    char trimmed[GUARD_MODEL_MAX + 1];
    CHECK(guard_model_virtual("VBOX HARDDISK", trimmed));
    CHECK(!guard_model_virtual("", trimmed));
    CHECK_EQ(trimmed[0], 0);
}

static void test_a_alone(void)
{
    static const char *const models[] = {
        "QEMU HARDDISK2", "QEMU HARDDIS", "qemu harddisk", "QEMU  HARDDISK", "VBOX HARDDISK X", "QEMU DVD-ROM",
        "MADIRE-REAL-DISK-TEST", "WDC WD10EZEX-00BN5A0", "", "   ",
    };
    good();
    for (uint32_t i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
        CHECK_EQ(check(models[i]), GUARD_A_MODEL);
        CHECK(strcmp(guard_cond_letter(v.cond), "(a)") == 0);
    }
    /* longer than IDENTIFY's 40 characters: cut, and still not virtual */
    CHECK_EQ(check("QEMU HARDDISK                                   X"), GUARD_A_MODEL);
    CHECK(strlen(v.model) <= GUARD_MODEL_MAX);
}

static void test_b_alone(void)
{
    good();
    memset(mk, 0, sizeof(mk));              /* unmarked: ext2's first sector is zero */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_MAGIC);
    good();
    mk[15] = 'k';                           /* "MADIRE-TEST-DISk", CRC32 updated */
    marker_crc();
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_MAGIC);
    good();
    put32(mk + 16, 2);
    marker_crc();
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_VERSION);
    CHECK_EQ(v.got, 2);
    good();
    put32(mk + 20, 47);
    marker_crc();
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_LENGTH);
    CHECK_EQ(v.got, 47);
    good();
    put32(mk + 40, 0xED71A5FCu + 1u);       /* a bad CRC32 */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_CRC);
    CHECK_EQ(v.got, 0xED71A5FDu);
    CHECK_EQ(v.want, 0xED71A5FCu);
    good();
    mk[30] ^= 0x01;                         /* the record changed, CRC32 not updated */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_CRC);
    marker(FIRST + 1, COUNT);               /* the start does not match, CRC32 valid */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_START);
    CHECK_EQ(v.got, FIRST + 1);
    CHECK_EQ(v.want, FIRST);
    marker(FIRST, COUNT - 1);               /* the length does not match, CRC32 valid */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_SECTORS);
    CHECK_EQ(v.got, COUNT - 1);
    CHECK_EQ(v.want, COUNT);
    marker(FIRST + COUNT, COUNT);           /* another partition's marker */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_START);
    good();
    mk[511] = 1;                            /* a byte after the record */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_TAIL);
    CHECK_EQ(v.got, 511);
    good();
    mk[44] = 1;
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_TAIL);
    CHECK_EQ(v.got, 44);
    good();
    CHECK(!guard_check(&v, "QEMU HARDDISK", FIRST, 3, mk, sb));    /* too small */
    CHECK_EQ(v.cond, GUARD_B_SMALL);
    CHECK_EQ(v.got, 3);
    CHECK(!guard_check(&v, "QEMU HARDDISK", FIRST, COUNT, 0, 0));  /* nothing read */
    CHECK_EQ(v.cond, GUARD_B_SMALL);
    CHECK(strcmp(guard_cond_letter(GUARD_B_CRC), "(b)") == 0);
}

static void test_c_alone(void)
{
    good();
    sb[56] = 0;                             /* no ext2 magic */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_C_MAGIC);
    CHECK_EQ(v.got, 0xEF00);
    good();
    superblock("madire-tesT");
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_C_LABEL);
    CHECK(strcmp(v.label, "madire-tesT") == 0);
    superblock("");                         /* missing */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_C_LABEL);
    CHECK_EQ(v.label[0], 0);
    superblock("madire-test-2");
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_C_LABEL);
    superblock("madire-test");
    sb[120 + 12] = 'x';                     /* the name, a zero, then more */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_C_LABEL);
    superblock("madire-tes");
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_C_LABEL);
    superblock("madire-test\x01");
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_C_LABEL);
    CHECK(strcmp(v.label, "madire-test?") == 0);
    superblock("0123456789abcdef");         /* all 16 bytes, no terminator */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_C_LABEL);
    CHECK(strcmp(v.label, "0123456789abcdef") == 0);
    CHECK(strcmp(guard_cond_letter(GUARD_C_LABEL), "(c)") == 0);
    CHECK(strcmp(guard_cond_letter(GUARD_OK), "") == 0);
}

static void test_order(void)
{
    marker(FIRST + 1, COUNT);
    superblock("other");
    CHECK_EQ(check("MADIRE-REAL-DISK-TEST"), GUARD_A_MODEL);    /* all three fail: (a) */
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_B_START);            /* (b) and (c) fail: (b) */
    marker(FIRST, COUNT);
    CHECK_EQ(check("QEMU HARDDISK"), GUARD_C_LABEL);
    CHECK(v.label[0] == 'o');
}

/* ---- the decision in the lowest layer ---- */

static uint32_t fake_read(void *ctx, uint32_t off)
{
    (void)ctx;
    (void)off;
    return 0;                               /* no ports implemented */
}

static void fake_write(void *ctx, uint32_t off, uint32_t value)
{
    (void)ctx;
    (void)off;
    (void)value;
}

static uint64_t fake_ticks(void *ctx)
{
    (void)ctx;
    return 0;
}

static bool fake_page(void *ctx, volatile uint8_t **virt, uint32_t *phys)
{
    (void)ctx;
    (void)virt;
    (void)phys;
    return false;
}

static void fake_unpage(void *ctx, volatile uint8_t *virt, uint32_t phys)
{
    (void)ctx;
    (void)virt;
    (void)phys;
}

static const struct ahci_ops fake_ops = { fake_read, fake_write, fake_ticks, fake_page, fake_unpage, 0, 100 };
static struct ahci_hba h;

static void test_decide_once(void)
{
    memset(&h, 0, sizeof(h));
    h.port[0].state = AHCI_PORT_DISK;
    h.port[0].disk.sectors = 18432;
    h.port[1].state = AHCI_PORT_ATAPI;
    CHECK(!ahci_guard_allows(&h, 0));                       /* undecided: no */
    CHECK(ahci_guard_decide(&h, 0, true, 2048, 16384));
    CHECK(ahci_guard_allows(&h, 0));
    CHECK_EQ(h.port[0].guard_first, 2048);
    CHECK_EQ(h.port[0].guard_sectors, 16384);
    CHECK(!ahci_guard_decide(&h, 0, false, 0, 0));          /* once only */
    CHECK(ahci_guard_allows(&h, 0));
    CHECK(!ahci_guard_decide(&h, 1, true, 2048, 16384));    /* not a disk */
    CHECK(!ahci_guard_allows(&h, 1));
    CHECK(!ahci_guard_decide(&h, 2, true, 2048, 16384));    /* nothing there */
    CHECK(!ahci_guard_decide(&h, 32, true, 2048, 16384));   /* no such port */
    CHECK(!ahci_guard_allows(&h, 32));
    h.port[0].state = AHCI_PORT_FAILED;                     /* a disk marked failed: no */
    CHECK(!ahci_guard_allows(&h, 0));

    memset(&h, 0, sizeof(h));
    h.port[3].state = AHCI_PORT_DISK;
    h.port[3].disk.sectors = 18432;
    CHECK(ahci_guard_decide(&h, 3, true, 2048, 16385));     /* past the end: recorded as refused */
    CHECK(!ahci_guard_allows(&h, 3));
    CHECK(!ahci_guard_decide(&h, 3, true, 2048, 16384));
    CHECK(!ahci_guard_allows(&h, 3));
    memset(&h, 0, sizeof(h));
    h.port[3].state = AHCI_PORT_DISK;
    h.port[3].disk.sectors = 18432;
    CHECK(ahci_guard_decide(&h, 3, true, 18432, 1));        /* starts at the end */
    CHECK(!ahci_guard_allows(&h, 3));
    memset(&h, 0, sizeof(h));
    h.port[3].state = AHCI_PORT_DISK;
    h.port[3].disk.sectors = 18432;
    CHECK(ahci_guard_decide(&h, 3, true, 0, 0));            /* no sectors */
    CHECK(!ahci_guard_allows(&h, 3));
    memset(&h, 0, sizeof(h));
    h.port[3].state = AHCI_PORT_DISK;
    h.port[3].disk.sectors = 18432;
    CHECK(ahci_guard_decide(&h, 3, false, 2048, 16384));    /* refused */
    CHECK(!ahci_guard_allows(&h, 3));
    CHECK(h.port[3].guard_decided);

    /* ahci_start sets every port from scratch: no decision is left over */
    memset(&h, 0, sizeof(h));
    h.port[0].state = AHCI_PORT_DISK;
    h.port[0].disk.sectors = 18432;
    CHECK(ahci_guard_decide(&h, 0, true, 2048, 16384));
    ahci_start(&h, &fake_ops);
    CHECK(!h.port[0].guard_decided);
    CHECK(!h.port[0].guard_allows);
    CHECK_EQ(h.port[0].guard_first, 0);
    CHECK(!ahci_guard_allows(&h, 0));
}

int main(void)
{
    printf("test_guard_core\n");
    RUN(test_all_three);
    RUN(test_a_alone);
    RUN(test_b_alone);
    RUN(test_c_alone);
    RUN(test_order);
    RUN(test_decide_once);
    return unit_report("test_guard_core");
}
