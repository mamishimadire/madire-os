/* Unit tests for kernel/storage/blk_core.c (the block cache, reads only,
 * 1.4 stage 4) against a fake disk: every read's bytes against the disk's
 * pattern, the bounds (EIO before any command), runs of up to 16 pages in
 * one command, the partial last page, least-recently-used replacement,
 * hits, a slot whose page cannot be made, an injected read error (EIO, no
 * stale page left, no retry), and the CRC32 against known values (zlib's
 * crc32 of "123456789" and of 512 zero bytes). The module is included
 * directly. */
#include <stdbool.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/storage/blk_core.c"

#define FD_SLOTS 8

struct fd {
    uint8_t page[FD_SLOTS][BLK_PAGE];
    bool made[FD_SLOTS];
    uint32_t ready_limit;           /* slots that can be made */
    uint64_t sectors;
    uint32_t commands, fail_next;
    uint64_t last_lba;
    uint32_t last_sectors, last_n;
    uint32_t largest_run;
};

static struct fd fd;
static struct blk_slot slots[FD_SLOTS];
static struct blk_cache c;

static uint8_t fd_byte(uint64_t lba, uint32_t i)
{
    return (uint8_t)(lba * 13u + i * 5u + (lba >> 8) + 3u);
}

static bool fd_ready(void *ctx, uint32_t slot)
{
    (void)ctx;
    if (slot >= fd.ready_limit)
        return false;
    fd.made[slot] = true;
    memset(fd.page[slot], 0xAA, BLK_PAGE);  /* garbage, as a fresh frame */
    return true;
}

static uint8_t *fd_data(void *ctx, uint32_t slot)
{
    (void)ctx;
    return fd.page[slot];
}

static int fd_read(void *ctx, uint64_t lba, uint32_t sectors, const uint32_t *s, uint32_t n)
{
    (void)ctx;
    fd.commands++;
    fd.last_lba = lba;
    fd.last_sectors = sectors;
    fd.last_n = n;
    if (n > fd.largest_run)
        fd.largest_run = n;
    CHECK(n >= 1 && n <= BLK_RUN_MAX);
    CHECK(sectors > (n - 1) * BLK_PER_PAGE && sectors <= n * BLK_PER_PAGE);
    CHECK(lba % BLK_PER_PAGE == 0);
    CHECK(lba + sectors <= fd.sectors);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t k = i + 1; k < n; k++)
            CHECK(s[i] != s[k]);
    if (fd.fail_next) {
        fd.fail_next--;
        for (uint32_t i = 0; i < n; i++)
            memset(fd.page[s[i]], 0x55, BLK_PAGE);  /* a half-done transfer */
        return -BLK_EIO;
    }
    for (uint32_t k = 0; k < sectors * BLK_SECTOR; k++)
        fd.page[s[k / BLK_PAGE]][k % BLK_PAGE] = fd_byte(lba + k / BLK_SECTOR, k % BLK_SECTOR);
    return 0;
}

static const struct blk_ops ops = { fd_ready, fd_data, fd_read, 0 };

static void setup(uint64_t sectors, uint32_t cap, uint32_t ready_limit)
{
    memset(&fd, 0, sizeof(fd));
    fd.sectors = sectors;
    fd.ready_limit = ready_limit;
    blk_cache_init(&c, &ops, slots, cap, sectors);
}

static bool bytes_ok(const uint8_t *b, uint64_t lba, uint32_t count)
{
    for (uint32_t k = 0; k < count * BLK_SECTOR; k++)
        if (b[k] != fd_byte(lba + k / BLK_SECTOR, k % BLK_SECTOR))
            return false;
    return true;
}

static uint8_t buf[64 * BLK_SECTOR];

static void test_sizes(void)
{
    CHECK_EQ(blk_cache_pages(0), 0);
    CHECK_EQ(blk_cache_pages(15), 0);
    CHECK_EQ(blk_cache_pages(1600), 100);
    CHECK_EQ(blk_cache_pages(8192), 512);
    CHECK_EQ(blk_cache_pages(8208), 512);           /* 2 MiB at most */
    CHECK_EQ(blk_cache_pages(1000000), 512);
}

static void test_crc(void)
{
    CHECK_EQ(blk_crc32(0, (const uint8_t *)"123456789", 9), 0xCBF43926u);
    static const uint8_t zero[512];
    CHECK_EQ(blk_crc32(0, zero, 512), 0xB2AA7578u);
    CHECK_EQ(blk_crc32(blk_crc32(0, (const uint8_t *)"1234", 4), (const uint8_t *)"56789", 5), 0xCBF43926u);
    CHECK_EQ(blk_crc32(0, zero, 0), 0);
}

static void test_reads(void)
{
    setup(1000, FD_SLOTS, FD_SLOTS);
    CHECK_EQ(blk_cache_read(&c, 0, 1, buf), 0);
    CHECK(bytes_ok(buf, 0, 1));
    CHECK_EQ(fd.commands, 1);
    CHECK_EQ(fd.last_sectors, 8);                   /* the whole page */
    CHECK_EQ(blk_cache_read(&c, 3, 2, buf), 0);     /* a hit */
    CHECK(bytes_ok(buf, 3, 2));
    CHECK_EQ(fd.commands, 1);
    CHECK_EQ(c.hits, 1);
    CHECK_EQ(blk_cache_read(&c, 5, 20, buf), 0);    /* pages 0 (hit), 1-3: one command */
    CHECK(bytes_ok(buf, 5, 20));
    CHECK_EQ(fd.commands, 2);
    CHECK_EQ(fd.last_lba, 8);
    CHECK_EQ(fd.last_n, 3);
    CHECK_EQ(blk_cache_read(&c, 0, 0, buf), 0);     /* nothing */
    CHECK_EQ(fd.commands, 2);
    /* Bounds: refused before any command. */
    CHECK_EQ(blk_cache_read(&c, 1000, 1, buf), -BLK_EIO);
    CHECK_EQ(blk_cache_read(&c, 998, 3, buf), -BLK_EIO);
    CHECK_EQ(blk_cache_read(&c, 0xFFFFFFFFFFFFFFFFull, 2, buf), -BLK_EIO);
    CHECK_EQ(blk_cache_read(&c, 0, 0xFFFFFFFFu, buf), -BLK_EIO);
    CHECK_EQ(c.refused, 4);
    CHECK_EQ(fd.commands, 2);
}

/* The disk's last page holds 1000 - 992 = 8 sectors; with 1003 sectors
 * the last page holds 3: read in part, the rest zero. */
static void test_last_page(void)
{
    setup(1003, FD_SLOTS, FD_SLOTS);
    CHECK_EQ(blk_cache_read(&c, 1002, 1, buf), 0);
    CHECK(bytes_ok(buf, 1002, 1));
    CHECK_EQ(fd.last_lba, 1000);
    CHECK_EQ(fd.last_sectors, 3);
    int32_t s = blk_lookup(&c, 125);
    CHECK(s >= 0);
    uint32_t zero = 0;
    for (uint32_t k = 3 * BLK_SECTOR; s >= 0 && k < BLK_PAGE; k++)
        zero += fd.page[s][k] == 0;
    CHECK_EQ(zero, BLK_PAGE - 3 * BLK_SECTOR);
    CHECK_EQ(blk_cache_read(&c, 990, 13, buf), 0);  /* pages 123 (new), 124 (new), 125 (hit) */
    CHECK(bytes_ok(buf, 990, 13));
    CHECK_EQ(fd.last_lba, 984);
    CHECK_EQ(fd.last_sectors, 16);
    CHECK_EQ(blk_cache_read(&c, 1003, 1, buf), -BLK_EIO);
}

/* Least recently used first; a run never replaces a page of itself. */
static void test_replacement(void)
{
    setup(100000, 4, FD_SLOTS);
    for (uint64_t p = 0; p < 4; p++)
        CHECK_EQ(blk_cache_read(&c, p * 8, 1, buf), 0);
    CHECK_EQ(c.made, 4);
    CHECK_EQ(blk_cache_read(&c, 0, 1, buf), 0);     /* page 0 used again: page 1 is the oldest */
    CHECK_EQ(fd.commands, 4);
    CHECK_EQ(blk_cache_read(&c, 4 * 8, 1, buf), 0);
    CHECK(blk_lookup(&c, 1) < 0);
    CHECK(blk_lookup(&c, 0) >= 0 && blk_lookup(&c, 2) >= 0 && blk_lookup(&c, 3) >= 0);
    CHECK_EQ(c.replaced, 1);
    /* 64 sectors = 8 pages through a cache of 4: runs of at most 4. */
    CHECK_EQ(blk_cache_read(&c, 800, 64, buf), 0);
    CHECK(bytes_ok(buf, 800, 64));
    CHECK(fd.largest_run <= 4);
    CHECK_EQ(c.made, 4);
    /* 16 whole pages through a big cache: one command. */
    setup(100000, FD_SLOTS, FD_SLOTS);
    static uint8_t big[16 * BLK_PAGE];
    CHECK_EQ(blk_cache_read(&c, 8, 64, big), 0);
    CHECK_EQ(fd.last_n, 8);
    CHECK(bytes_ok(big, 8, 64));
}

static void test_short_of_pages(void)
{
    setup(100000, FD_SLOTS, 2);                     /* only two slots can be made */
    CHECK_EQ(blk_cache_read(&c, 0, 1, buf), 0);
    CHECK_EQ(blk_cache_read(&c, 8, 1, buf), 0);
    CHECK_EQ(blk_cache_read(&c, 16, 1, buf), 0);    /* the third replaces the oldest */
    CHECK(bytes_ok(buf, 16, 1));
    CHECK(c.short_of_pages);
    CHECK_EQ(c.made, 2);
    setup(100000, FD_SLOTS, 0);                     /* none at all */
    CHECK_EQ(blk_cache_read(&c, 0, 1, buf), -BLK_EIO);
    CHECK_EQ(fd.commands, 0);
    CHECK_EQ(c.errors, 1);
    setup(100000, 0, FD_SLOTS);                     /* a cache of 0 pages */
    CHECK_EQ(blk_cache_read(&c, 0, 1, buf), -BLK_EIO);
}

/* An injected error: EIO, none of the run's pages left in the cache, no
 * retry; the next read reads them again. */
static void test_error(void)
{
    setup(100000, FD_SLOTS, FD_SLOTS);
    CHECK_EQ(blk_cache_read(&c, 0, 8, buf), 0);     /* page 0 */
    fd.fail_next = 1;
    CHECK_EQ(blk_cache_read(&c, 0, 32, buf), -BLK_EIO);     /* page 0 a hit, 1-3 fail */
    CHECK_EQ(fd.commands, 2);
    CHECK(blk_lookup(&c, 1) < 0 && blk_lookup(&c, 2) < 0 && blk_lookup(&c, 3) < 0);
    CHECK(blk_lookup(&c, 0) >= 0);
    CHECK_EQ(c.errors, 1);
    CHECK_EQ(blk_cache_read(&c, 8, 24, buf), 0);
    CHECK_EQ(fd.commands, 3);
    CHECK(bytes_ok(buf, 8, 24));
}

int main(void)
{
    printf("test_blk_core\n");
    RUN(test_sizes);
    RUN(test_crc);
    RUN(test_reads);
    RUN(test_last_page);
    RUN(test_replacement);
    RUN(test_short_of_pages);
    RUN(test_error);
    return unit_report("blk_core");
}
