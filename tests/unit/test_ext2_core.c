/* Unit tests for kernel/fs/ext2_core.c (the ext2 read core, 1.4 stage 6;
 * docs/plans/1.4.md, D11, D12, C7, risk 31), on small images built here in
 * memory from the specification's layout (one group): block sizes 1, 2 and
 * 4 KiB, 128- and 256-byte inodes, revision 0 and 1; the root folder in its
 * on-disk order, lookups, a fast and a slow symbolic link, a sparse file
 * with data behind the direct, single, double and triple indirect blocks
 * and holes between (read as zeros); the D11 policy (an unknown INCOMPAT
 * flag refuses the mount, before the geometry; an unknown RO_COMPAT flag
 * mounts read only; ext_attr, resize_inode, dir_index known; another COMPAT
 * flag reported; revision 0's feature fields ignored); the clean and error
 * states reported; and the bounds: every superblock field, every group
 * descriptor field, inode numbers, sizes, block numbers at every level of
 * indirection, folder entries (rec_len, name_len, inode, '/' and NUL in a
 * name, holes, sizes, revision 0's 16-bit name_len), an indexed folder read
 * in its on-disk order, and failed reads: each an error code, never a read
 * outside the image. The module is included directly. */
#include <stdbool.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/fs/ext2_core.c"

#define IMG_MAX     (512u * 1024u)
#define IPG         32u

static uint8_t img[IMG_MAX];
static uint64_t img_len;
static int reads, fail_after = -1;
static int bad_reads;

static int t_read(void *ctx, uint64_t off, uint32_t len, uint8_t *buf)
{
    (void)ctx;
    reads++;
    if (off > img_len || len > img_len - off) {
        bad_reads++;
        return -1;
    }
    if (fail_after >= 0 && reads > fail_after)
        return -1;
    memcpy(buf, img + off, len);
    return 0;
}

static const struct ext2_ops t_ops = { t_read, 0 };
static struct ext2_fs fs;
static uint32_t BS, ISZ, NBLK, FDB, NEXT, REV;

static void p16(uint64_t off, uint32_t v)
{
    img[off] = (uint8_t)v;
    img[off + 1] = (uint8_t)(v >> 8);
}

static void p32(uint64_t off, uint32_t v)
{
    p16(off, v & 0xFFFF);
    p16(off + 2, v >> 16);
}

static uint32_t g32(uint64_t off)
{
    return (uint32_t)img[off] | (uint32_t)img[off + 1] << 8 | (uint32_t)img[off + 2] << 16 |
           (uint32_t)img[off + 3] << 24;
}

static uint64_t boff(uint32_t b)
{
    return (uint64_t)b * BS;
}

static uint64_t ioff(uint32_t ino)
{
    return boff(FDB + 4) + (uint64_t)(ino - 1) * ISZ;
}

static uint64_t gdoff(void)
{
    return boff(FDB + 1);
}

static void inode(uint32_t ino, uint32_t mode, uint64_t size, uint32_t links)
{
    uint64_t o = ioff(ino);
    p16(o, mode);
    p32(o + 4, (uint32_t)size);
    p16(o + 26, links);
    p32(o + 108, (uint32_t)(size >> 32));
}

static void iblock(uint32_t ino, uint32_t i, uint32_t b)
{
    p32(ioff(ino) + 40 + 4 * i, b);
}

/* Folder entries, written one after another into block `b`. */
static uint32_t d_blk, d_pos, d_last;

static void dir_begin(uint32_t b)
{
    d_blk = b;
    d_pos = 0;
    d_last = 0;
}

static void dent(uint32_t ino, const char *name, uint32_t ft)
{
    uint32_t nl = (uint32_t)strlen(name);
    uint32_t rec = (8 + nl + 3) & ~3u;
    uint64_t o = boff(d_blk) + d_pos;
    p32(o, ino);
    p16(o + 4, rec);
    if (REV) {
        img[o + 6] = (uint8_t)nl;
        img[o + 7] = (uint8_t)ft;
    } else {
        p16(o + 6, nl);
    }
    memcpy(img + o + 8, name, nl);
    d_last = d_pos;
    d_pos += rec;
}

static void dir_end(void)
{
    p16(boff(d_blk) + d_last + 4, BS - d_last);
}

static void fill(uint32_t b, uint8_t tag)
{
    for (uint32_t i = 0; i < BS; i++)
        img[boff(b) + i] = (uint8_t)(tag + i * 7u);
}

/* The standard image: the root (2) with hello.txt (12), sub (13, holding
 * inner, 17), big (14, sparse), link (15, fast: "hello.txt") and slow (16,
 * a 70-byte target in a block). big's data: block 0 (tag 0x10), 12 (the
 * single indirect's first, 0x20), 12 + p (the double's first, 0x30) and
 * 12 + p + p^2 (the triple's first, 0x40), holes between; its size ends
 * one block after the last. */
static uint32_t B_D0, B_D1, B_D2, B_D3, B_IND, B_DIND, B_DIND1, B_TIND, B_TIND1, B_TIND2, B_ROOT, B_SUB, B_SLOW;

static uint32_t t_alloc(void)
{
    return NEXT++;
}

static void std_image(uint32_t bs, uint32_t isz, uint32_t rev)
{
    memset(img, 0, sizeof img);
    BS = bs;
    ISZ = isz;
    REV = rev;
    NBLK = IMG_MAX / bs < 256 ? IMG_MAX / bs : 256;
    img_len = (uint64_t)NBLK * bs;
    FDB = bs == 1024 ? 1 : 0;
    uint64_t s = 1024;
    p32(s + 0, IPG);
    p32(s + 4, NBLK);
    p32(s + 20, FDB);
    p32(s + 24, bs == 1024 ? 0 : bs == 2048 ? 1 : 2);
    p32(s + 32, 8 * bs);
    p32(s + 40, IPG);
    p16(s + 56, 0xEF53);
    p16(s + 58, 1);
    p32(s + 76, rev);
    if (rev) {
        p32(s + 84, 11);
        p16(s + 88, isz);
        p32(s + 96, EXT2_INCOMPAT_FILETYPE);
        p32(s + 100, EXT2_RO_COMPAT_SPARSE_SUPER | EXT2_RO_COMPAT_LARGE_FILE);
        memcpy(img + s + 120, "madire-test", 11);
    }
    p32(gdoff(), FDB + 2);
    p32(gdoff() + 4, FDB + 3);
    p32(gdoff() + 8, FDB + 4);
    NEXT = FDB + 4 + (IPG * isz + bs - 1) / bs;
    uint32_t p = bs / 4;

    B_ROOT = t_alloc();
    inode(2, 0x41ED, bs, 4);
    iblock(2, 0, B_ROOT);
    dir_begin(B_ROOT);
    dent(2, ".", 2);
    dent(2, "..", 2);
    dent(12, "hello.txt", 1);
    dent(13, "sub", 2);
    dent(14, "big", 1);
    dent(15, "link", 7);
    dent(16, "slow", 7);
    dir_end();

    uint32_t h = t_alloc();
    inode(12, 0x81A4, 5, 1);
    iblock(12, 0, h);
    memcpy(img + boff(h), "hello", 5);

    B_SUB = t_alloc();
    inode(13, 0x41ED, bs, 2);
    iblock(13, 0, B_SUB);
    dir_begin(B_SUB);
    dent(13, ".", 2);
    dent(2, "..", 2);
    dent(17, "inner", 1);
    dir_end();
    inode(17, 0x81A4, 0, 1);

    B_D0 = t_alloc(); B_D1 = t_alloc(); B_D2 = t_alloc(); B_D3 = t_alloc();
    B_IND = t_alloc(); B_DIND = t_alloc(); B_DIND1 = t_alloc();
    B_TIND = t_alloc(); B_TIND1 = t_alloc(); B_TIND2 = t_alloc();
    fill(B_D0, 0x10);
    fill(B_D1, 0x20);
    fill(B_D2, 0x30);
    fill(B_D3, 0x40);
    inode(14, 0x81A4, ((uint64_t)12 + p + (uint64_t)p * p + 2) * bs, 1);
    iblock(14, 0, B_D0);
    iblock(14, 12, B_IND);
    p32(boff(B_IND), B_D1);
    iblock(14, 13, B_DIND);
    p32(boff(B_DIND), B_DIND1);
    p32(boff(B_DIND1), B_D2);
    iblock(14, 14, B_TIND);
    p32(boff(B_TIND), B_TIND1);
    p32(boff(B_TIND1), B_TIND2);
    p32(boff(B_TIND2), B_D3);

    inode(15, 0xA1FF, 9, 1);
    memcpy(img + ioff(15) + 40, "hello.txt", 9);

    B_SLOW = t_alloc();
    inode(16, 0xA1FF, 70, 1);
    p32(ioff(16) + 28, bs / 512);
    iblock(16, 0, B_SLOW);
    for (uint32_t i = 0; i < 70; i++)
        img[boff(B_SLOW) + i] = (uint8_t)('a' + i % 26);
}

static int t_mount(void)
{
    reads = 0;
    return ext2_mount(&fs, &t_ops, img_len);
}

static struct ext2_inode in;

static int get(uint32_t ino)
{
    memset(&in, 0, sizeof in);
    return ext2_inode_get(&fs, ino, &in);
}

/* The names of folder `ino`, joined by spaces, or "ERR <code>". */
static char names[512];

static int list(uint32_t ino)
{
    struct ext2_dir_iter it = { 0 };
    struct ext2_dirent de;
    memset(&de, 0, sizeof de);
    int r = 0;
    names[0] = 0;
    if (get(ino) != 0)
        return -100;
    while ((r = ext2_dir_next(&fs, &in, &it, &de)) == 1) {
        if (names[0])
            strcat(names, " ");
        strcat(names, de.name);
    }
    return r;
}

static void test_shapes(void)
{
    static const uint32_t shapes[][3] = {
        { 1024, 128, 1 }, { 2048, 256, 1 }, { 4096, 128, 1 }, { 4096, 256, 1 }, { 1024, 128, 0 }, { 2048, 128, 0 },
    };
    for (unsigned k = 0; k < sizeof shapes / sizeof shapes[0]; k++) {
        std_image(shapes[k][0], shapes[k][1], shapes[k][2]);
        bad_reads = 0;
        CHECK_EQ(t_mount(), EXT2_OK);
        CHECK(fs.why == 0);
        CHECK_EQ(fs.block_size, BS);
        CHECK_EQ(fs.inode_size, ISZ);
        CHECK_EQ(fs.rev, REV);
        CHECK_EQ(fs.groups, 1);
        CHECK_EQ(fs.first_ino, 11);
        CHECK_EQ(fs.filetype, REV == 1);
        CHECK(!fs.readonly);
        CHECK(fs.state_clean && !fs.state_error);
        uint64_t p = BS / 4;
        CHECK_EQ(fs.max_blocks, 12 + p + p * p + p * p * p);
        CHECK_EQ(ext2_max_size(&fs), fs.max_blocks * BS);
        if (REV)
            CHECK(strcmp(fs.label, "madire-test") == 0);
        CHECK_EQ(list(2), 0);
        CHECK(strcmp(names, ". .. hello.txt sub big link slow") == 0);
        CHECK_EQ(list(13), 0);
        CHECK(strcmp(names, ". .. inner") == 0);
        /* The file types: in revision 1 only. */
        struct ext2_dir_iter it = { 0 };
        struct ext2_dirent de;
        memset(&de, 0, sizeof de);
        CHECK_EQ(get(2), 0);
        for (int i = 0; i < 4; i++)
            CHECK_EQ(ext2_dir_next(&fs, &in, &it, &de), 1);
        CHECK(strcmp(de.name, "sub") == 0);
        CHECK_EQ(de.ino, 13);
        CHECK_EQ(de.file_type, REV ? 2 : 0);
        CHECK_EQ(de.name_len, 3);
        CHECK_EQ(de.off, 12 + 12 + 20);
        /* Lookups. */
        uint32_t ino = 99;
        CHECK_EQ(ext2_lookup(&fs, &in, "sub", 3, &ino), EXT2_OK);
        CHECK_EQ(ino, 13);
        CHECK_EQ(ext2_lookup(&fs, &in, "hello.txt", 9, &ino), EXT2_OK);
        CHECK_EQ(ino, 12);
        CHECK_EQ(ext2_lookup(&fs, &in, "hello.tx", 8, &ino), EXT2_ENOENT);
        CHECK_EQ(ino, 0);
        CHECK_EQ(ext2_lookup(&fs, &in, "", 0, &ino), EXT2_ENOENT);
        /* A file. */
        uint8_t buf[16];
        CHECK_EQ(get(12), 0);
        CHECK_EQ(in.size, 5);
        CHECK_EQ(in.links, 1);
        CHECK_EQ(ext2_read(&fs, &in, 0, buf, sizeof buf), 5);
        CHECK(memcmp(buf, "hello", 5) == 0);
        CHECK_EQ(ext2_read(&fs, &in, 3, buf, 1), 1);
        CHECK_EQ(buf[0], 'l');
        CHECK_EQ(ext2_read(&fs, &in, 5, buf, 4), 0);
        CHECK_EQ(ext2_read(&fs, &in, 1000, buf, 4), 0);
        CHECK_EQ(ext2_lookup(&fs, &in, "x", 1, &ino), EXT2_ENOTDIR);
        CHECK_EQ(bad_reads, 0);
    }
}

/* The sparse file through every level, at every block size. */
static void test_indirect(void)
{
    static const uint32_t sizes[] = { 1024, 2048, 4096 };
    static uint8_t buf[3 * 4096];
    for (unsigned k = 0; k < 3; k++) {
        std_image(sizes[k], 128, 1);
        bad_reads = 0;
        CHECK_EQ(t_mount(), EXT2_OK);
        CHECK_EQ(get(14), 0);
        uint64_t p = BS / 4;
        uint64_t at[4] = { 0, 12, 12 + p, 12 + p + p * p };
        uint32_t want[4] = { B_D0, B_D1, B_D2, B_D3 };
        uint8_t tag[4] = { 0x10, 0x20, 0x30, 0x40 };
        for (int i = 0; i < 4; i++) {
            uint32_t pb = 7;
            CHECK_EQ(ext2_bmap(&fs, &in, at[i], &pb), EXT2_OK);
            CHECK_EQ(pb, want[i]);
            /* The block before it is a hole (but for block 0). */
            if (at[i]) {
                CHECK_EQ(ext2_bmap(&fs, &in, at[i] - 1, &pb), EXT2_OK);
                CHECK_EQ(pb, 0);
            }
            CHECK_EQ(ext2_bmap(&fs, &in, at[i] + 1, &pb), EXT2_OK);
            CHECK_EQ(pb, 0);
            /* Read from half a block before to half a block after: zeros,
             * the data, zeros. */
            uint64_t off = at[i] * BS;
            uint64_t from = off ? off - BS / 2 : 0;
            int64_t n = ext2_read(&fs, &in, from, buf, 2 * BS);
            CHECK_EQ(n, 2 * BS);
            bool ok = true;
            for (uint64_t j = 0; j < 2 * (uint64_t)BS; j++) {
                uint64_t pos = from + j;
                uint8_t e = (pos >= off && pos < off + BS) ? (uint8_t)(tag[i] + (pos - off) * 7u) : 0;
                if (buf[j] != e)
                    ok = false;
            }
            CHECK(ok);
        }
        /* Holes inside each indirect block's range, and past the last data. */
        uint32_t pb = 9;
        CHECK_EQ(ext2_bmap(&fs, &in, 12 + p - 1, &pb), EXT2_OK);
        CHECK_EQ(pb, 0);
        CHECK_EQ(ext2_bmap(&fs, &in, 12 + p + p, &pb), EXT2_OK);    /* the DIND's second slot: 0 */
        CHECK_EQ(pb, 0);
        CHECK_EQ(ext2_bmap(&fs, &in, 12 + p + p * p + p * p, &pb), EXT2_OK);
        CHECK_EQ(pb, 0);
        CHECK_EQ(ext2_bmap(&fs, &in, fs.max_blocks - 1, &pb), EXT2_OK);
        CHECK_EQ(pb, 0);
        CHECK_EQ(ext2_bmap(&fs, &in, fs.max_blocks, &pb), EXT2_ERANGE);
        CHECK_EQ(ext2_bmap(&fs, &in, ~0ull, &pb), EXT2_ERANGE);
        /* The end of the file: one block of hole after the last data. */
        CHECK_EQ(ext2_read(&fs, &in, in.size - 10, buf, 100), 10);
        CHECK(buf[0] == 0 && buf[9] == 0);
        CHECK_EQ(bad_reads, 0);
    }
}

static void test_links(void)
{
    uint8_t buf[100];
    std_image(1024, 128, 1);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(get(15), 0);
    CHECK(ext2_fast_link(&fs, &in));
    CHECK_EQ(ext2_read(&fs, &in, 0, buf, sizeof buf), 9);
    CHECK(memcmp(buf, "hello.txt", 9) == 0);
    CHECK_EQ(ext2_read(&fs, &in, 6, buf, 2), 2);
    CHECK(memcmp(buf, "tx", 2) == 0);
    CHECK_EQ(get(16), 0);
    CHECK(!ext2_fast_link(&fs, &in));
    CHECK_EQ(ext2_read(&fs, &in, 0, buf, sizeof buf), 70);
    CHECK(buf[0] == 'a' && buf[25] == 'z' && buf[26] == 'a' && buf[69] == 'r');
    CHECK_EQ(get(12), 0);
    CHECK(!ext2_fast_link(&fs, &in));                       /* a regular file */
    /* A fast link with an attribute block counted in i_blocks. */
    p32(ioff(15) + 104, 200);
    p32(ioff(15) + 28, 2);
    ext2_cache_drop(&fs);
    CHECK_EQ(get(15), 0);
    CHECK(ext2_fast_link(&fs, &in));
    /* i_blocks says a data block: read through the map, where "hell" is a
     * block number far beyond the last. */
    p32(ioff(15) + 104, 0);
    ext2_cache_drop(&fs);
    CHECK_EQ(get(15), 0);
    CHECK(!ext2_fast_link(&fs, &in));
    CHECK_EQ(ext2_read(&fs, &in, 0, buf, sizeof buf), EXT2_ECORRUPT);
    /* A link of 60 bytes or more is never fast. */
    inode(15, 0xA1FF, 60, 1);
    p32(ioff(15) + 28, 0);
    ext2_cache_drop(&fs);
    CHECK_EQ(get(15), 0);
    CHECK(!ext2_fast_link(&fs, &in));
}

/* The superblock's checks: each field out of range alone. */
static int sb_case(uint32_t off, uint32_t size, uint32_t v)
{
    std_image(1024, 128, 1);
    if (size == 2)
        p16(1024 + off, v);
    else
        p32(1024 + off, v);
    return t_mount();
}

static void test_superblock(void)
{
    bad_reads = 0;
    CHECK_EQ(sb_case(56, 2, 0xEF54), EXT2_ESUPER);           /* magic */
    CHECK(fs.why != 0 && strstr(fs.why, "magic") != 0);
    CHECK_EQ(sb_case(76, 4, 2), EXT2_ESUPER);                /* revision 2 */
    CHECK_EQ(sb_case(24, 4, 3), EXT2_ESUPER);                /* 8 KiB blocks */
    CHECK(strstr(fs.why, "4096") != 0);
    CHECK_EQ(sb_case(24, 4, 0xFFFFFFFF), EXT2_ESUPER);
    CHECK_EQ(sb_case(20, 4, 0), EXT2_ESUPER);                /* first data block 0 at 1 KiB */
    CHECK_EQ(sb_case(20, 4, 7), EXT2_ESUPER);
    CHECK_EQ(sb_case(4, 4, 256 + 1), EXT2_ESUPER);           /* larger than the partition */
    CHECK(strstr(fs.why, "partition") != 0);
    CHECK_EQ(sb_case(4, 4, 0xFFFFFFFF), EXT2_ESUPER);
    CHECK_EQ(sb_case(4, 4, 1), EXT2_ESUPER);                 /* not above the first data block */
    CHECK_EQ(sb_case(32, 4, 0), EXT2_ESUPER);                /* blocks per group */
    CHECK_EQ(sb_case(32, 4, 8 * 1024 + 1), EXT2_ESUPER);
    CHECK_EQ(sb_case(40, 4, 0), EXT2_ESUPER);                /* inodes per group */
    CHECK_EQ(sb_case(40, 4, 8 * 1024 + 1), EXT2_ESUPER);
    CHECK_EQ(sb_case(88, 2, 200), EXT2_ESUPER);              /* inode size */
    CHECK_EQ(sb_case(88, 2, 512), EXT2_ESUPER);
    CHECK_EQ(sb_case(88, 2, 0), EXT2_ESUPER);
    CHECK_EQ(sb_case(0, 4, 0), EXT2_ESUPER);                 /* inode count */
    CHECK_EQ(sb_case(0, 4, IPG + 1), EXT2_ESUPER);
    CHECK_EQ(sb_case(0, 4, 0xFFFFFFFF), EXT2_ESUPER);
    CHECK_EQ(sb_case(84, 4, 10), EXT2_ESUPER);               /* first inode */
    CHECK_EQ(sb_case(84, 4, IPG + 1), EXT2_ESUPER);
    CHECK_EQ(sb_case(84, 4, IPG), EXT2_OK);
    /* The descriptor table beyond the last block: 2 blocks, 1 group. */
    std_image(1024, 128, 1);
    p32(1024 + 4, 2);
    p32(1024 + 32, 1);
    CHECK_EQ(t_mount(), EXT2_ESUPER);
    CHECK(strstr(fs.why, "descriptors") != 0);
    /* Many groups: their descriptors are zero, so group 1's bitmap is at
     * block 0, before the first data block. */
    std_image(1024, 128, 1);
    p32(1024 + 32, 8);
    CHECK_EQ(t_mount(), EXT2_ECORRUPT);
    CHECK_EQ(fs.groups, (256 - 1 + 7) / 8);
    CHECK_EQ(fs.bad_group, 1);
    /* A partition too small, and failed reads. */
    std_image(1024, 128, 1);
    CHECK_EQ(ext2_mount(&fs, &t_ops, 2047), EXT2_ESUPER);
    fail_after = 0;
    CHECK_EQ(t_mount(), EXT2_EIO);
    fail_after = 1;                                          /* the superblock, not the descriptors */
    CHECK_EQ(t_mount(), EXT2_EIO);
    fail_after = 2;                                          /* not the root's inode table block */
    CHECK_EQ(t_mount(), EXT2_EIO);
    fail_after = -1;
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(bad_reads, 0);
}

static void test_groupdesc(void)
{
    /* `why`: a word of the expected reason ("bitmap", "inode table", or
     * "root" when the table's 4 blocks end exactly at the last block, so
     * the descriptor is accepted and the root inode read there is zero). */
    static const struct { uint32_t off, v; int want; const char *why; } c[] = {
        { 0, 0xFFFFFFFF, EXT2_ECORRUPT, "bitmap" }, { 0, 256, EXT2_ECORRUPT, "bitmap" },
        { 0, 0, EXT2_ECORRUPT, "bitmap" }, { 4, 0xFFFFFFFF, EXT2_ECORRUPT, "bitmap" },
        { 4, 0, EXT2_ECORRUPT, "bitmap" }, { 8, 0xFFFFFFFF, EXT2_ECORRUPT, "inode table" },
        { 8, 0, EXT2_ECORRUPT, "inode table" }, { 8, 256 - 3, EXT2_ECORRUPT, "inode table" },
        { 8, 256 - 4, EXT2_ECORRUPT, "root" }, { 0, 255, EXT2_OK, 0 },
    };
    bad_reads = 0;
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        std_image(1024, 128, 1);
        p32(gdoff() + c[i].off, c[i].v);
        CHECK_EQ(t_mount(), c[i].want);
        if (c[i].why)
            CHECK(fs.why != 0 && strstr(fs.why, c[i].why) != 0);
    }
    /* The root not a folder. */
    std_image(1024, 128, 1);
    p16(ioff(2), 0x81A4);
    CHECK_EQ(t_mount(), EXT2_ECORRUPT);
    CHECK(strstr(fs.why, "root") != 0);
    std_image(1024, 128, 1);
    CHECK_EQ(t_mount(), EXT2_OK);
    struct ext2_gd gd = { 0, 0, 0, 0, 0, 0 };
    CHECK_EQ(ext2_group(&fs, 0, &gd), EXT2_OK);
    CHECK_EQ(gd.inode_table, FDB + 4);
    CHECK_EQ(ext2_group(&fs, 1, &gd), EXT2_ECORRUPT);
    CHECK_EQ(bad_reads, 0);
}

/* The feature policy (D11) and the states (D12, reported). */
static void test_policy(void)
{
    std_image(1024, 128, 1);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK(!fs.readonly);
    CHECK_EQ(fs.incompat_unknown | fs.ro_compat_unknown | fs.compat_unknown, 0);
    /* INCOMPAT: anything but filetype refuses the mount. */
    static const uint32_t bad_incompat[] = { 0x1, 0x4, 0x10, 0x40, 0x80, 0x200, 0x80000000 };
    for (unsigned i = 0; i < sizeof bad_incompat / sizeof bad_incompat[0]; i++) {
        std_image(1024, 128, 1);
        p32(1024 + 96, EXT2_INCOMPAT_FILETYPE | bad_incompat[i]);
        CHECK_EQ(t_mount(), EXT2_EINCOMPAT);
        CHECK_EQ(fs.incompat_unknown, bad_incompat[i]);
        CHECK(strstr(fs.why, "INCOMPAT") != 0);
    }
    /* ... before the geometry is looked at. */
    std_image(1024, 128, 1);
    p32(1024 + 96, 0x40);
    p32(1024 + 24, 7);
    CHECK_EQ(t_mount(), EXT2_EINCOMPAT);
    /* No INCOMPAT flag at all: mounted, folder entries without file types. */
    std_image(1024, 128, 1);
    p32(1024 + 96, 0);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK(!fs.filetype);
    /* RO_COMPAT: sparse_super and large_file known; any other: read only. */
    static const uint32_t ro[] = { 0x4, 0x8, 0x10, 0x20, 0x40, 0x80000000 };
    for (unsigned i = 0; i < sizeof ro / sizeof ro[0]; i++) {
        std_image(1024, 128, 1);
        p32(1024 + 100, EXT2_RO_COMPAT_KNOWN | ro[i]);
        CHECK_EQ(t_mount(), EXT2_OK);
        CHECK(fs.readonly);
        CHECK_EQ(fs.ro_compat_unknown, ro[i]);
        CHECK_EQ(list(2), 0);                               /* still read */
    }
    /* COMPAT: ext_attr, resize_inode, dir_index known; another reported. */
    std_image(1024, 128, 1);
    p32(1024 + 92, EXT2_COMPAT_KNOWN);
    p16(1024 + 206, 63);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK(!fs.readonly);
    CHECK_EQ(fs.compat_unknown, 0);
    CHECK_EQ(fs.reserved_gdt, 63);
    std_image(1024, 128, 1);
    p32(1024 + 92, EXT2_COMPAT_HAS_JOURNAL | EXT2_COMPAT_DIR_INDEX);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK(!fs.readonly);
    CHECK_EQ(fs.compat_unknown, EXT2_COMPAT_HAS_JOURNAL);
    /* Revision 0 has no feature fields: whatever is there is ignored. */
    std_image(1024, 128, 0);
    p32(1024 + 92, 0xFFFFFFFF);
    p32(1024 + 96, 0xFFFFFFFF);
    p32(1024 + 100, 0xFFFFFFFF);
    p32(1024 + 84, 3);
    p16(1024 + 88, 999);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK(!fs.readonly && !fs.filetype);
    CHECK_EQ(fs.first_ino, 11);
    CHECK_EQ(fs.inode_size, 128);
    CHECK_EQ(fs.compat | fs.incompat | fs.ro_compat, 0);
    /* The states: reported; the mount decides nothing in stage 6. */
    static const struct { uint32_t state; bool clean, error; } st[] = {
        { 0, false, false }, { 1, true, false }, { 2, false, true }, { 3, true, true },
    };
    for (unsigned i = 0; i < 4; i++) {
        std_image(1024, 128, 1);
        p16(1024 + 58, st[i].state);
        CHECK_EQ(t_mount(), EXT2_OK);
        CHECK_EQ(fs.state_clean, st[i].clean);
        CHECK_EQ(fs.state_error, st[i].error);
        CHECK(!fs.readonly);
    }
}

static void test_inode_bounds(void)
{
    uint8_t buf[64];
    uint32_t pb = 0;
    std_image(1024, 128, 1);
    bad_reads = 0;
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(get(0), EXT2_EINODE);
    CHECK_EQ(get(IPG + 1), EXT2_EINODE);
    CHECK_EQ(get(0xFFFFFFFF), EXT2_EINODE);
    CHECK_EQ(get(IPG), EXT2_OK);
    /* The size's high 32 bits: a regular file in revision 1 only. */
    p32(ioff(12) + 108, 1);
    ext2_cache_drop(&fs);
    CHECK_EQ(get(12), 0);
    CHECK_EQ(in.size, (1ull << 32) + 5);
    p32(ioff(13) + 108, 1);
    ext2_cache_drop(&fs);
    CHECK_EQ(get(13), 0);
    CHECK_EQ(in.size, 1024);
    /* A size beyond the map's reach. */
    p32(ioff(12) + 108, 0x7FFFFFFF);
    ext2_cache_drop(&fs);
    CHECK_EQ(get(12), 0);
    CHECK_EQ(ext2_read(&fs, &in, 0, buf, 4), EXT2_ECORRUPT);
    p32(ioff(12) + 108, 0);
    std_image(1024, 128, 0);
    p32(ioff(12) + 108, 1);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(get(12), 0);
    CHECK_EQ(in.size, 5);
    /* Block numbers beyond the last, at every level. */
    std_image(1024, 128, 1);
    CHECK_EQ(t_mount(), EXT2_OK);
    uint64_t p = 256;
    iblock(12, 0, NBLK);
    ext2_cache_drop(&fs);
    CHECK_EQ(get(12), 0);
    CHECK_EQ(ext2_bmap(&fs, &in, 0, &pb), EXT2_ECORRUPT);
    CHECK_EQ(ext2_read(&fs, &in, 0, buf, 4), EXT2_ECORRUPT);
    iblock(12, 0, 0xFFFFFFFF);
    ext2_cache_drop(&fs);
    CHECK_EQ(get(12), 0);
    CHECK_EQ(ext2_read(&fs, &in, 0, buf, 4), EXT2_ECORRUPT);
    iblock(14, 12, NBLK);                                   /* the single indirect block itself */
    ext2_cache_drop(&fs);
    CHECK_EQ(get(14), 0);
    CHECK_EQ(ext2_bmap(&fs, &in, 12, &pb), EXT2_ECORRUPT);
    CHECK_EQ(ext2_bmap(&fs, &in, 0, &pb), EXT2_OK);          /* the direct block unaffected */
    CHECK_EQ(pb, B_D0);
    iblock(14, 12, B_IND);
    ext2_cache_drop(&fs);
    p32(boff(B_IND), NBLK);                                 /* an entry of it */
    fs.ind_valid[0] = false;
    CHECK_EQ(get(14), 0);
    CHECK_EQ(ext2_bmap(&fs, &in, 12, &pb), EXT2_ECORRUPT);
    p32(boff(B_IND), B_D1);
    p32(boff(B_DIND1), 0xFFFFFFFF);                         /* the double's second level */
    fs.ind_valid[1] = false;
    CHECK_EQ(ext2_bmap(&fs, &in, 12 + p, &pb), EXT2_ECORRUPT);
    p32(boff(B_DIND1), B_D2);
    p32(boff(B_DIND), NBLK + 5);                            /* the double's first level */
    fs.ind_valid[0] = false;
    CHECK_EQ(ext2_bmap(&fs, &in, 12 + p, &pb), EXT2_ECORRUPT);
    p32(boff(B_DIND), B_DIND1);
    p32(boff(B_TIND2), NBLK);                               /* the triple's third level */
    fs.ind_valid[0] = fs.ind_valid[1] = fs.ind_valid[2] = false;
    CHECK_EQ(ext2_bmap(&fs, &in, 12 + p + p * p, &pb), EXT2_ECORRUPT);
    CHECK(strstr(fs.why, "data block") != 0);
    p32(boff(B_TIND2), B_D3);
    p32(boff(B_TIND1), 0xFFFFFFF0);                         /* the triple's second level */
    fs.ind_valid[0] = fs.ind_valid[1] = fs.ind_valid[2] = false;
    CHECK_EQ(ext2_bmap(&fs, &in, 12 + p + p * p, &pb), EXT2_ECORRUPT);
    CHECK(strstr(fs.why, "indirect") != 0);
    p32(boff(B_TIND1), B_TIND2);
    fs.ind_valid[0] = fs.ind_valid[1] = fs.ind_valid[2] = false;
    CHECK_EQ(ext2_bmap(&fs, &in, 12 + p + p * p, &pb), EXT2_OK);
    CHECK_EQ(pb, B_D3);
    /* An indirect block pointing at itself: the depth is fixed, no loop. */
    p32(boff(B_TIND), B_TIND);
    p32(boff(B_TIND) + 4, 0);
    fs.ind_valid[0] = fs.ind_valid[1] = fs.ind_valid[2] = false;
    CHECK_EQ(ext2_bmap(&fs, &in, 12 + p + p * p, &pb), EXT2_OK);
    CHECK_EQ(pb, B_TIND);                                   /* the block read as data: harmless */
    /* A failed read in the middle of a file. */
    std_image(1024, 128, 1);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(get(14), 0);
    static uint8_t big[4096];
    fail_after = reads;
    CHECK_EQ(ext2_read(&fs, &in, 0, big, 1024), EXT2_EIO);
    fail_after = -1;
    CHECK_EQ(ext2_read(&fs, &in, 0, big, 1024), 1024);
    CHECK_EQ(bad_reads, 0);
}

static void test_dirs(void)
{
    struct ext2_dir_iter it = { 0 };
    struct ext2_dirent de;
    memset(&de, 0, sizeof de);
    uint64_t r0 = 0;
    std_image(1024, 128, 1);
    bad_reads = 0;
    r0 = boff(B_ROOT);
    /* Entries: . at 0 (12), .. at 12 (12), hello.txt at 24 (20), sub at
     * 44 (12), big at 56 (12), link at 68 (12), slow at 80 (to the end). */
    static const struct { uint32_t off, size, v; const char *after; } c[] = {
        { 4, 2, 0, "" },                    /* rec_len 0 */
        { 4, 2, 13, "" },                   /* not a multiple of 4 */
        { 4, 2, 8, "" },                    /* below 12 */
        { 24 + 4, 2, 1024, ". .." },        /* past the block's end */
        { 24 + 4, 2, 1024 - 24 + 4, ". .." },
        { 24 + 6, 1, 0, ". .." },           /* name_len 0 for a used entry */
        { 24 + 6, 1, 13, ". .." },          /* name_len above rec_len - 8 */
        { 24, 4, IPG + 1, ". .." },         /* an inode number out of range */
        { 24, 4, 0xFFFFFFFF, ". .." },
        { 24 + 8, 1, '/', ". .." },         /* '/' in a name */
        { 24 + 9, 1, 0, ". .." },           /* NUL in a name */
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        std_image(1024, 128, 1);
        if (c[i].size == 1)
            img[r0 + c[i].off] = (uint8_t)c[i].v;
        else if (c[i].size == 2)
            p16(r0 + c[i].off, c[i].v);
        else
            p32(r0 + c[i].off, c[i].v);
        CHECK_EQ(t_mount(), EXT2_OK);
        CHECK_EQ(list(2), EXT2_ECORRUPT);
        CHECK(strcmp(names, c[i].after) == 0);
    }
    /* After an error the walk stays where it was (no loop, no skip). */
    std_image(1024, 128, 1);
    p16(r0 + 24 + 4, 3);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(get(2), 0);
    it.off = 0;
    CHECK_EQ(ext2_dir_next(&fs, &in, &it, &de), 1);
    CHECK_EQ(ext2_dir_next(&fs, &in, &it, &de), 1);
    CHECK_EQ(ext2_dir_next(&fs, &in, &it, &de), EXT2_ECORRUPT);
    CHECK_EQ(it.off, 24);
    CHECK_EQ(ext2_dir_next(&fs, &in, &it, &de), EXT2_ECORRUPT);
    CHECK_EQ(it.off, 24);
    /* An unused slot (inode 0) is skipped; its name is not looked at. */
    std_image(1024, 128, 1);
    p32(r0 + 24, 0);
    img[r0 + 24 + 6] = 200;
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(list(2), 0);
    CHECK(strcmp(names, ". .. sub big link slow") == 0);
    uint32_t ino = 0;
    CHECK_EQ(ext2_lookup(&fs, &in, "hello.txt", 9, &ino), EXT2_ENOENT);
    /* Folder sizes: not whole blocks, beyond the filesystem, a hole. */
    std_image(1024, 128, 1);
    p32(ioff(2) + 4, 1000);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(list(2), EXT2_ECORRUPT);
    std_image(1024, 128, 1);
    p32(ioff(2) + 4, 257 * 1024);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(list(2), EXT2_ECORRUPT);
    std_image(1024, 128, 1);
    p32(ioff(2) + 4, 2 * 1024);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(list(2), EXT2_ECORRUPT);                       /* block 1 is a hole */
    CHECK(strcmp(names, ". .. hello.txt sub big link slow") == 0);
    CHECK(strstr(fs.why, "hole") != 0);
    std_image(1024, 128, 1);
    iblock(2, 0, NBLK + 1);
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(list(2), EXT2_ECORRUPT);
    /* Not a folder. */
    CHECK_EQ(list(12), EXT2_ENOTDIR);
    /* Revision 0: name_len is 16 bits, so a byte at 7 is part of it. */
    std_image(1024, 128, 0);
    r0 = boff(B_ROOT);
    img[r0 + 24 + 7] = 1;
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(list(2), EXT2_ECORRUPT);
    std_image(1024, 128, 1);
    r0 = boff(B_ROOT);
    img[r0 + 24 + 7] = 1;                                   /* revision 1: the file type */
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(list(2), 0);
    CHECK_EQ(bad_reads, 0);
}

/* An indexed folder (dir_index): its index's blocks look like unused
 * entries, so it reads in its on-disk order. */
static void test_dir_index(void)
{
    std_image(1024, 128, 1);
    p32(1024 + 92, EXT2_COMPAT_DIR_INDEX);
    uint32_t b0 = t_alloc(), b1 = t_alloc(), b2 = t_alloc();
    inode(18, 0x41ED, 3 * 1024, 2);
    p32(ioff(18) + 32, EXT2_INDEX_FL);
    iblock(18, 0, b0);
    iblock(18, 1, b1);
    iblock(18, 2, b2);
    /* The root block: "." (12), ".." to the end with the index after its
     * name; the interior block: inode 0, rec_len 1024, index data. */
    dir_begin(b0);
    dent(18, ".", 2);
    dent(2, "..", 2);
    dir_end();
    for (uint32_t i = 32; i < 1024; i++)
        img[boff(b0) + i] = (uint8_t)(i * 13u + 1u);
    p32(boff(b1), 0);
    p16(boff(b1) + 4, 1024);
    for (uint32_t i = 8; i < 1024; i++)
        img[boff(b1) + i] = (uint8_t)(i * 5u + 3u);
    dir_begin(b2);
    dent(12, "a", 1);
    dent(17, "b", 1);
    dir_end();
    bad_reads = 0;
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(list(18), 0);
    CHECK(strcmp(names, ". .. a b") == 0);
    CHECK_EQ(in.flags, EXT2_INDEX_FL);
    uint32_t ino = 0;
    CHECK_EQ(ext2_lookup(&fs, &in, "b", 1, &ino), EXT2_OK);
    CHECK_EQ(ino, 17);
    CHECK_EQ(bad_reads, 0);
}

/* The kept blocks (1.4 stage 6 fix): a repeated inode, folder or indirect
 * lookup reads nothing from the device; after ext2_cache_drop it reads
 * exactly the blocks it needs again. `reads` counts every device read. */
static void test_cache(void)
{
    struct ext2_dir_iter it = { 0 };
    struct ext2_dirent de;
    memset(&de, 0, sizeof de);
    uint32_t pb = 0;
    int r0 = 0;
    std_image(1024, 128, 1);
    bad_reads = 0;
    CHECK_EQ(t_mount(), EXT2_OK);
    CHECK_EQ(reads, 3);                     /* the superblock, the descriptors, the root's inode block */
    /* Inodes: 12 and 13 share an inode-table block (inodes 9-16). */
    CHECK_EQ(get(12), 0);
    CHECK_EQ(reads, 4);                     /* that block, once */
    r0 = reads;
    CHECK_EQ(get(12), 0);
    CHECK_EQ(get(13), 0);
    CHECK_EQ(reads, r0);                    /* no new device read */
    ext2_cache_drop(&fs);
    CHECK_EQ(get(12), 0);
    CHECK_EQ(reads, r0 + 2);                /* the descriptors' block and the inode block */
    CHECK_EQ(in.size, 5);
    /* A folder: its block is read once for a whole walk, and not again. */
    CHECK_EQ(get(2), 0);
    r0 = reads;
    it.off = 0;
    while (ext2_dir_next(&fs, &in, &it, &de) == 1)
        ;
    CHECK_EQ(reads, r0 + 1);
    r0 = reads;
    it.off = 0;
    while (ext2_dir_next(&fs, &in, &it, &de) == 1)
        ;
    uint32_t ino = 0;
    CHECK_EQ(ext2_lookup(&fs, &in, "slow", 4, &ino), EXT2_OK);
    CHECK_EQ(ino, 16);
    CHECK_EQ(reads, r0);
    ext2_cache_drop(&fs);
    it.off = 0;
    CHECK_EQ(ext2_dir_next(&fs, &in, &it, &de), 1);
    CHECK_EQ(reads, r0 + 1);                /* the folder's block again */
    /* Indirect blocks: the triple path keeps its three levels. */
    CHECK_EQ(get(14), 0);
    uint64_t lb = 12 + 256 + 256 * 256;
    r0 = reads;
    CHECK_EQ(ext2_bmap(&fs, &in, lb, &pb), EXT2_OK);
    CHECK_EQ(pb, B_D3);
    CHECK_EQ(reads, r0 + 3);                /* TIND, its DIND, its IND */
    r0 = reads;
    CHECK_EQ(ext2_bmap(&fs, &in, lb, &pb), EXT2_OK);
    CHECK_EQ(ext2_bmap(&fs, &in, lb + 1, &pb), EXT2_OK);
    CHECK_EQ(reads, r0);
    ext2_cache_drop(&fs);
    CHECK_EQ(ext2_bmap(&fs, &in, lb, &pb), EXT2_OK);
    CHECK_EQ(pb, B_D3);
    CHECK_EQ(reads, r0 + 3);
    /* Without a drop, a change on the device is not seen (the kept copy is
     * used); with one, it is. */
    p32(boff(B_TIND2), B_D2);
    CHECK_EQ(ext2_bmap(&fs, &in, lb, &pb), EXT2_OK);
    CHECK_EQ(pb, B_D3);
    ext2_cache_drop(&fs);
    CHECK_EQ(ext2_bmap(&fs, &in, lb, &pb), EXT2_OK);
    CHECK_EQ(pb, B_D2);
    CHECK_EQ(bad_reads, 0);
}

static void test_misc(void)
{
    CHECK(strcmp(ext2_err_str(EXT2_EINCOMPAT), "EINCOMPAT") == 0);
    CHECK(strcmp(ext2_err_str(EXT2_ECORRUPT), "ECORRUPT") == 0);
    CHECK(strcmp(ext2_err_str(-99), "unknown") == 0);
    CHECK_EQ(g32(1024 + 56) & 0xFFFF, 0xEF53);
}

int main(void)
{
    RUN(test_shapes);
    RUN(test_indirect);
    RUN(test_links);
    RUN(test_superblock);
    RUN(test_groupdesc);
    RUN(test_policy);
    RUN(test_inode_bounds);
    RUN(test_dirs);
    RUN(test_dir_index);
    RUN(test_cache);
    RUN(test_misc);
    return unit_report("ext2_core");
}
