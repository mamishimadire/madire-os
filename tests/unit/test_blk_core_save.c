/* Unit tests for kernel/storage/blk_core_save.c (the block layer's
 * write-back part and its save order, 1.4 stage 9; docs/plans/1.4.md, C5,
 * D9) against a fake disk that logs every write command (its bytes, its
 * class) and every FLUSH: reads see the dirty chunks over the disk; a put is
 * all or nothing (bad range, bad class, no room); a chunk put again takes
 * the new bytes and class; the save writes class 1 to 6 in order, ascending
 * chunks within a class, neighbouring chunks of one class in one command of
 * at most 16, a FLUSH after each class that wrote and none after an empty
 * class; the superblock's chunk (bytes 1024-2047) is written alone, never
 * the partition's first sector; a failed write or flush ends the save with
 * EIO, leaves the rest dirty and the layer failed (no retry); room is half
 * the table. Stage 11 (R5): class 4 (a cross-block rename's new name) is
 * written and FLUSHed before class 5 (the other folder blocks), and
 * blk_sv_class reads a dirty chunk's class. The module is included
 * directly. */
#include <stdbool.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/storage/blk_core_save.c"

#define FK_BYTES    (256u * 1024u)
#define FK_LOG      256u
#define CAP         64u

struct fk_op {
    char kind;              /* 'W' or 'F' */
    uint64_t off;
    uint32_t len, cls;
};

static uint8_t disk[FK_BYTES];
static struct fk_op oplog[FK_LOG];
static uint32_t nops, reads, writes_ok, fail_write_at, fail_flush;

static int fk_read(void *ctx, uint64_t off, uint32_t len, uint8_t *buf)
{
    (void)ctx;
    reads++;
    if (off > FK_BYTES || len > FK_BYTES - off)
        return -1;
    memcpy(buf, disk + off, len);
    return 0;
}

static int fk_write(void *ctx, uint64_t off, uint32_t len, const uint8_t *buf, uint32_t cls)
{
    (void)ctx;
    if (fail_write_at && writes_ok + 1u == fail_write_at)
        return -1;
    if (off > FK_BYTES || len > FK_BYTES - off)
        return -1;
    memcpy(disk + off, buf, len);
    writes_ok++;
    if (nops < FK_LOG) {
        oplog[nops].kind = 'W';
        oplog[nops].off = off;
        oplog[nops].len = len;
        oplog[nops].cls = cls;
        nops++;
    }
    return 0;
}

static int fk_flush(void *ctx)
{
    (void)ctx;
    if (fail_flush)
        return -1;
    if (nops < FK_LOG)
        oplog[nops++].kind = 'F';
    return 0;
}

static const struct blk_sv_ops fk_ops = { fk_read, fk_write, fk_flush, 0 };
static struct blk_sv sv;
static struct blk_sv_ent ents[CAP];
static uint8_t data[CAP * BLK_SV_CHUNK];
static uint8_t run[BLK_SV_RUN_MAX * BLK_SV_CHUNK];
static uint8_t buf[32 * BLK_SV_CHUNK];

static uint8_t disk_byte(uint64_t i)
{
    return (uint8_t)(i * 7u + (i >> 10) + 1u);
}

static void reset(void)
{
    for (uint64_t i = 0; i < FK_BYTES; i++)
        disk[i] = disk_byte(i);
    nops = reads = writes_ok = fail_write_at = fail_flush = 0;
    blk_sv_init(&sv, &fk_ops, ents, data, CAP, run, FK_BYTES);
}

static void fill(uint8_t tag, uint32_t chunks)
{
    for (uint32_t i = 0; i < chunks * BLK_SV_CHUNK; i++)
        buf[i] = (uint8_t)(tag + i);
}

static void put(uint32_t chunk, uint32_t n, uint32_t cls, uint8_t tag)
{
    fill(tag, n);
    CHECK_EQ(blk_sv_put(&sv, (uint64_t)chunk * BLK_SV_CHUNK, n * BLK_SV_CHUNK, buf, cls), 0);
}

static bool disk_is(uint32_t chunk, uint8_t tag)
{
    for (uint32_t i = 0; i < BLK_SV_CHUNK; i++)
        if (disk[(uint64_t)chunk * BLK_SV_CHUNK + i] != (uint8_t)(tag + i))
            return false;
    return true;
}

static void test_read_merge(void)
{
    reset();
    put(3, 1, BLK_SV_DATA, 0x40);
    uint8_t out[4 * BLK_SV_CHUNK];
    reads = 0;
    CHECK_EQ(blk_sv_read(&sv, 2u * BLK_SV_CHUNK + 1000u, 3u * BLK_SV_CHUNK, out), 0);
    bool ok = true;
    for (uint32_t i = 0; i < 3u * BLK_SV_CHUNK; i++) {
        uint64_t at = 2u * BLK_SV_CHUNK + 1000u + i;
        uint8_t want = (at / BLK_SV_CHUNK == 3) ? (uint8_t)(0x40 + at % BLK_SV_CHUNK) : disk_byte(at);
        ok = ok && out[i] == want;
    }
    CHECK(ok);
    CHECK_EQ(reads, 2);                         /* the clean part before, and the clean run after */
    reads = 0;
    CHECK_EQ(blk_sv_read(&sv, 10u * BLK_SV_CHUNK, 4u * BLK_SV_CHUNK, out), 0);
    CHECK_EQ(reads, 1);                         /* four clean chunks: one disk read */
    CHECK_EQ(blk_sv_read(&sv, FK_BYTES - 10u, 11, out), -BLK_EIO);
    CHECK_EQ(blk_sv_read(&sv, FK_BYTES - 10u, 10, out), 0);
}

static void test_put_rules(void)
{
    reset();
    fill(1, 2);
    CHECK_EQ(blk_sv_put(&sv, 100, BLK_SV_CHUNK, buf, BLK_SV_DATA), -BLK_EIO);           /* misaligned */
    CHECK_EQ(blk_sv_put(&sv, 0, 100, buf, BLK_SV_DATA), -BLK_EIO);
    CHECK_EQ(blk_sv_put(&sv, 0, BLK_SV_CHUNK, buf, 0), -BLK_EIO);                       /* class 0 */
    CHECK_EQ(blk_sv_put(&sv, 0, BLK_SV_CHUNK, buf, BLK_SV_CLASSES + 1u), -BLK_EIO);     /* class 7 */
    CHECK_EQ(blk_sv_put(&sv, FK_BYTES, BLK_SV_CHUNK, buf, 1), -BLK_EIO);                /* beyond */
    CHECK_EQ(blk_sv_put(&sv, FK_BYTES - BLK_SV_CHUNK, 2 * BLK_SV_CHUNK, buf, 1), -BLK_EIO);
    CHECK_EQ(blk_sv_dirty(&sv), 0);
    /* No room: all or nothing. */
    for (uint32_t i = 0; i < CAP - 1u; i++)
        put(2u * i, 1, BLK_SV_DATA, (uint8_t)i);
    CHECK_EQ(blk_sv_dirty(&sv), CAP - 1u);
    fill(9, 2);
    CHECK_EQ(blk_sv_put(&sv, 201u * BLK_SV_CHUNK, 2 * BLK_SV_CHUNK, buf, 1), -BLK_SV_FULL);
    CHECK_EQ(blk_sv_dirty(&sv), CAP - 1u);
    CHECK_EQ(blk_sv_put(&sv, 2u * BLK_SV_CHUNK, 2 * BLK_SV_CHUNK, buf, 1), 0);          /* 1 new, 1 again */
    CHECK_EQ(blk_sv_dirty(&sv), CAP);
    CHECK(!blk_sv_room(&sv, 1));
    reset();
    CHECK(blk_sv_room(&sv, CAP / 2u));
    CHECK(!blk_sv_room(&sv, CAP / 2u + 1u));
    /* Put again: the new bytes and the new class. */
    put(5, 1, BLK_SV_FOLDER, 0x11);
    put(5, 1, BLK_SV_DATA, 0x22);
    CHECK_EQ(blk_sv_dirty(&sv), 1);
    CHECK_EQ(blk_sv_save(&sv), 0);
    CHECK_EQ(nops, 2);
    CHECK_EQ(oplog[0].cls, BLK_SV_DATA);
    CHECK(disk_is(5, 0x22));
}

static void test_save_order(void)
{
    reset();
    put(40, 1, BLK_SV_FOLDER, 0x01);
    put(7, 1, BLK_SV_DATA, 0x02);
    put(30, 2, BLK_SV_INODE, 0x03);
    put(3, 1, BLK_SV_BITMAP, 0x04);
    put(1, 1, BLK_SV_SUPER, 0x05);
    put(6, 1, BLK_SV_DATA, 0x06);
    put(9, 1, BLK_SV_DATA, 0x07);               /* not next to 7: its own command */
    put(8, 1, BLK_SV_INODE, 0x08);              /* between 7 and 9, another class */
    put(100, 20, BLK_SV_DATA, 0x09);            /* 20 neighbours: 16 + 4 */
    put(2, 1, BLK_SV_SUPER, 0x0A);
    CHECK_EQ(blk_sv_save(&sv), 0);
    /* Class 1: 6-7, 9, 100-115, 116-119, FLUSH; class 2: 3, FLUSH; class 3:
     * 8, 30-31, FLUSH; class 5 (folders): 40, FLUSH; class 6: 1-2, FLUSH
     * (stage 11: the folder and superblock classes are 5 and 6; class 4,
     * empty here, writes nothing and needs no FLUSH). */
    const char *kinds = "WWWWFWFWWFWFWF";
    static const uint32_t offs[] = { 6, 9, 100, 116, 0, 3, 0, 8, 30, 0, 40, 0, 1, 0 };
    static const uint32_t lens[] = { 2, 1, 16, 4, 0, 1, 0, 1, 2, 0, 1, 0, 2, 0 };
    static const uint32_t cls[] = { 1, 1, 1, 1, 0, 2, 0, 3, 3, 0, 5, 0, 6, 0 };
    CHECK_EQ(nops, 14);
    bool ok = nops == 14;
    for (uint32_t i = 0; ok && i < 14; i++) {
        ok = oplog[i].kind == kinds[i];
        if (ok && kinds[i] == 'W')
            ok = oplog[i].off == (uint64_t)offs[i] * BLK_SV_CHUNK && oplog[i].len == lens[i] * BLK_SV_CHUNK &&
                 oplog[i].cls == cls[i];
    }
    CHECK(ok);
    CHECK(disk_is(40, 0x01) && disk_is(7, 0x02) && disk_is(3, 0x04) && disk_is(1, 0x05) && disk_is(9, 0x07));
    CHECK(disk_is(2, 0x0A) && disk_is(8, 0x08));
    CHECK_EQ(blk_sv_dirty(&sv), 0);
    CHECK_EQ(sv.saves, 1);
    CHECK_EQ(sv.class_writes[1], 4);
    CHECK_EQ(sv.flushes, 5);
    /* An empty save: no command, no FLUSH. */
    nops = 0;
    CHECK_EQ(blk_sv_save(&sv), 0);
    CHECK_EQ(nops, 0);
    CHECK_EQ(sv.saves, 2);
    /* Only the bitmaps and a folder block dirty: two FLUSHes. */
    put(12, 1, BLK_SV_FOLDER, 0x30);
    put(13, 1, BLK_SV_BITMAP, 0x31);
    CHECK_EQ(blk_sv_save(&sv), 0);
    CHECK_EQ(nops, 4);
    CHECK(oplog[0].kind == 'W' && oplog[0].cls == BLK_SV_BITMAP && oplog[1].kind == 'F' &&
          oplog[2].cls == BLK_SV_FOLDER && oplog[3].kind == 'F');
}

/* Stage 11 (R5): a cross-block rename's new name (class 4) is written and
 * FLUSHed before the other folder blocks (class 5), whatever their order
 * on the disk; blk_sv_class reads a dirty chunk's class (0 when clean). */
static void test_name_class(void)
{
    reset();
    put(10, 1, BLK_SV_FOLDER, 0x41);            /* the old name's block, lower on the disk */
    put(20, 2, BLK_SV_NAME, 0x42);              /* the new name's block (2 KiB) */
    put(1, 1, BLK_SV_SUPER, 0x43);
    CHECK_EQ(BLK_SV_NAME, 4);
    CHECK_EQ(BLK_SV_FOLDER, 5);
    CHECK_EQ(BLK_SV_SUPER, 6);
    CHECK_EQ(blk_sv_class(&sv, 10u * BLK_SV_CHUNK), BLK_SV_FOLDER);
    CHECK_EQ(blk_sv_class(&sv, 21u * BLK_SV_CHUNK + 5u), BLK_SV_NAME);
    CHECK_EQ(blk_sv_class(&sv, 11u * BLK_SV_CHUNK), 0);
    CHECK_EQ(blk_sv_save(&sv), 0);
    /* W 20-21 (class 4), F, W 10 (class 5), F, W 1 (class 6), F. */
    CHECK_EQ(nops, 6);
    CHECK(oplog[0].kind == 'W' && oplog[0].off == 20u * BLK_SV_CHUNK && oplog[0].cls == BLK_SV_NAME);
    CHECK(oplog[1].kind == 'F');
    CHECK(oplog[2].kind == 'W' && oplog[2].off == 10u * BLK_SV_CHUNK && oplog[2].cls == BLK_SV_FOLDER);
    CHECK(oplog[3].kind == 'F');
    CHECK(oplog[4].kind == 'W' && oplog[4].cls == BLK_SV_SUPER && oplog[5].kind == 'F');
    CHECK_EQ(blk_sv_class(&sv, 20u * BLK_SV_CHUNK), 0);
    CHECK(disk_is(10, 0x41) && disk_is(20, 0x42) && disk_is(21, 0x42));
}

static void test_superblock_chunk(void)
{
    reset();
    /* A 4 KiB-block volume: block 0 holds the marker sector and the
     * superblock; only chunk 1 is put and written. */
    uint8_t first[BLK_SV_CHUNK];
    memcpy(first, disk, BLK_SV_CHUNK);
    put(1, 1, BLK_SV_SUPER, 0x55);
    CHECK_EQ(blk_sv_save(&sv), 0);
    CHECK_EQ(nops, 2);
    CHECK_EQ(oplog[0].off, 1024);
    CHECK_EQ(oplog[0].len, 1024);
    CHECK(memcmp(first, disk, BLK_SV_CHUNK) == 0);
    CHECK(disk_is(1, 0x55));
    CHECK(disk[2048] == disk_byte(2048));
}

static void test_failures(void)
{
    reset();
    put(4, 1, BLK_SV_DATA, 0x10);
    put(20, 1, BLK_SV_DATA, 0x11);
    put(5, 1, BLK_SV_BITMAP, 0x12);
    put(6, 1, BLK_SV_INODE, 0x13);
    fail_write_at = 2;                          /* the second write command fails */
    CHECK_EQ(blk_sv_save(&sv), -BLK_EIO);
    CHECK(sv.failed);
    CHECK_EQ(sv.failed_saves, 1);
    CHECK_EQ(writes_ok, 1);
    CHECK(disk_is(4, 0x10));
    CHECK(!disk_is(20, 0x11) && !disk_is(5, 0x12));
    CHECK_EQ(blk_sv_dirty(&sv), 3);             /* the written chunk gone, the rest dirty */
    uint8_t out[BLK_SV_CHUNK];
    CHECK_EQ(blk_sv_read(&sv, 20u * BLK_SV_CHUNK, BLK_SV_CHUNK, out), 0);
    CHECK_EQ(out[0], 0x11);                     /* still read from the cache */
    fail_write_at = 0;
    CHECK_EQ(blk_sv_save(&sv), -BLK_EIO);       /* failed stays failed: no retry */
    CHECK_EQ(writes_ok, 1);
    reset();
    put(4, 1, BLK_SV_DATA, 0x20);
    put(5, 1, BLK_SV_BITMAP, 0x21);
    fail_flush = 1;
    CHECK_EQ(blk_sv_save(&sv), -BLK_EIO);
    CHECK(sv.failed);
    CHECK_EQ(writes_ok, 1);                     /* class 2 never started */
    CHECK(!disk_is(5, 0x21));
}

/* 1.4 stage 10: the save in pieces. The same dirty chunks saved whole and
 * in pieces of 1, 2, 3 and 8 commands give exactly the same commands in
 * the same order; between pieces a put is refused (-BLK_SV_FULL, nothing
 * changed) and every chunk still reads its new bytes; the pieces are
 * counted. Then the slots: chunks put in descending order (each opening
 * the table's first entry) and again after a save (freed slots reused)
 * keep their own bytes. */
static void seed(void)
{
    put(40, 3, BLK_SV_DATA, 0x10);
    put(7, 1, BLK_SV_BITMAP, 0x20);
    put(1, 1, BLK_SV_SUPER, 0x30);
    put(20, 2, BLK_SV_INODE, 0x40);
    put(60, 20, BLK_SV_DATA, 0x50);     /* two commands: 16 + 4 */
    put(30, 1, BLK_SV_FOLDER, 0x60);
    put(9, 1, BLK_SV_BITMAP, 0x70);
}

static void test_steps(void)
{
    static struct fk_op whole[FK_LOG];
    reset();
    seed();
    CHECK_EQ(blk_sv_save(&sv), 0);
    uint32_t n_whole = nops;
    memcpy(whole, oplog, sizeof(whole));
    CHECK_EQ(n_whole, 13);              /* writes 3 (runs of 3, 16, 4) + 2 + 1 + 1 + 1, and 5 flushes */
    static const uint32_t limits[4] = { 1, 2, 3, 8 };
    for (uint32_t t = 0; t < 4; t++) {
        reset();
        seed();
        bool done = false;
        uint32_t calls = 0;
        while (!done) {
            uint32_t before = nops;
            CHECK_EQ(blk_sv_save_step(&sv, limits[t], &done), 0);
            CHECK(nops - before <= limits[t]);
            calls++;
            if (!done) {
                CHECK(sv.saving);
                fill(0x99, 1);
                CHECK_EQ(blk_sv_put(&sv, 50u * BLK_SV_CHUNK, BLK_SV_CHUNK, buf, BLK_SV_DATA), -BLK_SV_FULL);
                uint8_t out[BLK_SV_CHUNK];
                CHECK_EQ(blk_sv_read(&sv, 30u * BLK_SV_CHUNK, BLK_SV_CHUNK, out), 0);
                bool ok = true;
                for (uint32_t i = 0; i < BLK_SV_CHUNK; i++)
                    ok = ok && out[i] == (uint8_t)(0x60 + i);
                CHECK(ok);
            }
            CHECK(calls < 100);
        }
        CHECK(!sv.saving);
        CHECK_EQ(nops, n_whole);
        bool same = true;
        for (uint32_t i = 0; i < n_whole; i++)
            same = same && oplog[i].kind == whole[i].kind && oplog[i].off == whole[i].off &&
                   oplog[i].len == whole[i].len && oplog[i].cls == whole[i].cls;
        CHECK(same);
        CHECK_EQ(blk_sv_dirty(&sv), 0);
        CHECK_EQ(sv.saves, 1);
        CHECK(sv.pieces >= 1);
    }
    /* Descending puts, a save, and again: every chunk keeps its bytes. */
    reset();
    for (uint32_t k = 0; k < 20; k++)
        put(100 - k, 1, BLK_SV_DATA, (uint8_t)(k * 3));
    for (uint32_t k = 0; k < 20; k++) {
        uint8_t out[BLK_SV_CHUNK];
        CHECK_EQ(blk_sv_read(&sv, (uint64_t)(100 - k) * BLK_SV_CHUNK, BLK_SV_CHUNK, out), 0);
        CHECK_EQ(out[5], (uint8_t)(k * 3 + 5));
    }
    CHECK_EQ(blk_sv_save(&sv), 0);
    for (uint32_t k = 0; k < 20; k++)
        CHECK(disk_is(100 - k, (uint8_t)(k * 3)));
    for (uint32_t k = 0; k < 30; k++)
        put(150 - k, 1, BLK_SV_INODE, (uint8_t)(k * 5 + 1));
    CHECK_EQ(blk_sv_save(&sv), 0);
    for (uint32_t k = 0; k < 30; k++)
        CHECK(disk_is(150 - k, (uint8_t)(k * 5 + 1)));
    /* Every slot still held once by the table's entries. */
    uint32_t seen[CAP] = { 0 };
    for (uint32_t i = 0; i < CAP; i++)
        if (ents[i].slot < CAP)
            seen[ents[i].slot]++;
    bool once = true;
    for (uint32_t i = 0; i < CAP; i++)
        once = once && seen[i] == 1;
    CHECK(once);
}

int main(void)
{
    printf("blk_core_save (write-back, the save's classes and FLUSHes)\n");
    RUN(test_read_merge);
    RUN(test_put_rules);
    RUN(test_save_order);
    RUN(test_name_class);
    RUN(test_superblock_chunk);
    RUN(test_failures);
    RUN(test_steps);
    return unit_report("blk_core_save");
}
