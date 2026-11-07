/* Unit tests for kernel/fs/ext2_write.c (the ext2 write core, 1.4 stage 9;
 * docs/plans/1.4.md, C5 with R1-R4, C7, D9, D11, D12) over
 * kernel/storage/blk_core_save.c, on small consistent images built here in
 * memory (1, 2 and 4 KiB blocks, 128- and 256-byte inodes, revision 0 and 1,
 * one or more groups). After the operations a full save is made and an
 * independent checker (this file's own, reading the raw image through the
 * read core alone) walks the tree: no block or inode in use but marked
 * free, none claimed twice, no leak left, every link count, every group's
 * and the superblock's counts. Cases: allocation and the counts; the kept
 * blocks of the read core updated by every write; C5's pending frees and
 * R3's three saves (the owner's case: B never gets A's blocks; A's inode
 * written as deleted in save 3, its bits cleared in save 4); hard links
 * (the count lowered one save later); R1's classes (new blocks in class 1);
 * R2's copies for writes and truncate (an indirect block the disk uses is
 * never changed in place); truncate's zeroed tail; ENOSPC only after the
 * pending frees are saved (EAGAIN before, R4); R4's folder growth and the
 * fresh folder block; mkdir, rmdir and rename with link counts and ".."
 * (and their refusals); dir_index cleared; attribute blocks (freed, or a
 * shared one's count lowered one save later); the triple indirect block and
 * sizes above 2 GiB; revision 0; the policy (unknown COMPAT or RO_COMPAT,
 * the error bit: read-only, nothing written); D12's recount; D9's failed
 * save (read-only, the error bit written if writing still works); inode 7
 * never written. The modules are included directly. */
#include <stdbool.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/fs/ext2_core.c"
#include "kernel/storage/blk_core_save.c"
#include "kernel/fs/ext2_write.c"

#define IMG_MAX     (4u * 1024u * 1024u)
#define SV_CAP      4096u
#define WLOG        4096u

static uint8_t img[IMG_MAX];
static uint64_t img_len;
static uint32_t BS, ISZ, NB, BPG, IPG, FDB, G, ITB, REV;

/* ---- the fake disk ---- */

struct wop {
    uint64_t off;
    uint32_t len, cls;
};

static struct wop wlog[WLOG];
static uint32_t nw, fail_from, flushes;     /* fail_from: the first write that fails (0: none) */

static int d_read(void *ctx, uint64_t off, uint32_t len, uint8_t *buf)
{
    (void)ctx;
    if (off > img_len || len > img_len - off)
        return -1;
    memcpy(buf, img + off, len);
    return 0;
}

static int d_write(void *ctx, uint64_t off, uint32_t len, const uint8_t *buf, uint32_t cls)
{
    (void)ctx;
    if (fail_from && nw + 1u >= fail_from)
        return -1;
    if (off > img_len || len > img_len - off)
        return -1;
    memcpy(img + off, buf, len);
    if (nw < WLOG) {
        wlog[nw].off = off;
        wlog[nw].len = len;
        wlog[nw].cls = cls;
    }
    nw++;
    return 0;
}

static int d_flush(void *ctx)
{
    (void)ctx;
    flushes++;
    return 0;
}

static const struct blk_sv_ops d_ops = { d_read, d_write, d_flush, 0 };
static struct blk_sv sv;
static struct blk_sv_ent sv_ents[SV_CAP];
static uint8_t sv_data[SV_CAP * BLK_SV_CHUNK];
static uint8_t sv_run[BLK_SV_RUN_MAX * BLK_SV_CHUNK];
static struct ext2_wfs w;
static uint32_t touches;

static uint32_t t_now(void *ctx)
{
    (void)ctx;
    return 1800000000u;
}

static void t_touch(void *ctx, uint32_t save, uint32_t ino)
{
    (void)ctx;
    (void)save;
    (void)ino;
    touches++;
}

static const struct ext2_w_ops t_wops = { t_now, t_touch, 0 };

/* ---- the image builder ---- */

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

static uint32_t g16(uint64_t off)
{
    return (uint32_t)img[off] | (uint32_t)img[off + 1] << 8;
}

static uint32_t g32(uint64_t off)
{
    return g16(off) | g16(off + 2) << 16;
}

static uint64_t boff(uint32_t b)
{
    return (uint64_t)b * BS;
}

static uint64_t gdoff(uint32_t g)
{
    return boff(FDB + 1) + (uint64_t)g * 32u;
}

static uint32_t gstart(uint32_t g)
{
    return FDB + g * BPG;
}

static uint32_t g_bb(uint32_t g) { return g32(gdoff(g)); }
static uint32_t g_ib(uint32_t g) { return g32(gdoff(g) + 4); }
static uint32_t g_it(uint32_t g) { return g32(gdoff(g) + 8); }

static uint64_t ioff(uint32_t ino)
{
    uint32_t g = (ino - 1) / IPG, i = (ino - 1) % IPG;
    return boff(g_it(g)) + (uint64_t)i * ISZ;
}

static bool bit(uint32_t blk, uint32_t i)
{
    return (img[boff(blk) + i / 8] >> (i % 8)) & 1u;
}

static void setbit(uint32_t blk, uint32_t i)
{
    img[boff(blk) + i / 8] |= (uint8_t)(1u << (i % 8));
}

static bool block_used(uint32_t b)
{
    uint32_t g = (b - FDB) / BPG;
    return bit(g_bb(g), (b - FDB) % BPG);
}

static bool inode_used(uint32_t ino)
{
    return bit(g_ib((ino - 1) / IPG), (ino - 1) % IPG);
}

static void mark_block(uint32_t b)
{
    setbit(g_bb((b - FDB) / BPG), (b - FDB) % BPG);
}

static uint32_t next_free;

/* A consistent empty volume: `groups` groups of `bpg` blocks (the last one
 * shorter by `short_by`), `ipg` inodes each, the root folder (2), inodes 1
 * to 10 reserved, lost+found absent (not needed). */
static void mkimg(uint32_t bs, uint32_t isz, uint32_t rev, uint32_t groups, uint32_t bpg, uint32_t ipg,
                  uint32_t short_by)
{
    memset(img, 0, sizeof img);
    BS = bs;
    ISZ = isz;
    REV = rev;
    BPG = bpg;
    IPG = ipg;
    FDB = bs == 1024 ? 1 : 0;
    G = groups;
    NB = FDB + groups * bpg - short_by;
    img_len = (uint64_t)NB * bs;
    ITB = (ipg * isz + bs - 1) / bs;
    uint32_t used_end0 = 0;
    for (uint32_t g = 0; g < G; g++) {
        uint32_t s = gstart(g);
        uint32_t first = g == 0 ? FDB + 2 : s;      /* group 0: the superblock, then the descriptors */
        p32(gdoff(g), first);
        p32(gdoff(g) + 4, first + 1);
        p32(gdoff(g) + 8, first + 2);
        uint32_t end = first + 2 + ITB;
        for (uint32_t b = (g == 0 ? 0 : s); b < end; b++)
            if (b >= FDB)
                mark_block(b);
        if (g == 0)
            used_end0 = end;
        /* padding: bits past the group's last block, and past ipg */
        uint32_t nbg = (g + 1 == G) ? NB - s : BPG;
        for (uint32_t i = nbg; i < 8 * bs; i++)
            setbit(g_bb(g), i);
        for (uint32_t i = ipg; i < 8 * bs; i++)
            setbit(g_ib(g), i);
    }
    for (uint32_t ino = 1; ino <= 10; ino++)
        setbit(g_ib(0), ino - 1);
    /* the root folder */
    uint32_t rb = used_end0;
    mark_block(rb);
    next_free = rb + 1;
    uint64_t o = ioff(2);
    p16(o, 0x41ED);
    p32(o + 4, bs);
    p16(o + 26, 2);
    p32(o + 28, bs / 512);
    p32(o + 40, rb);
    p32(boff(rb), 2);
    p16(boff(rb) + 4, 12);
    img[boff(rb) + 6] = 1;
    img[boff(rb) + 7] = rev ? 2 : 0;
    img[boff(rb) + 8] = '.';
    p32(boff(rb) + 12, 2);
    p16(boff(rb) + 16, bs - 12);
    img[boff(rb) + 18] = 2;
    img[boff(rb) + 19] = rev ? 2 : 0;
    img[boff(rb) + 20] = '.';
    img[boff(rb) + 21] = '.';
    if (isz >= 160)
        p16(o + 128, 32);
    /* the counts */
    uint32_t tfb = 0, tfi = 0;
    for (uint32_t g = 0; g < G; g++) {
        uint32_t nbg = (g + 1 == G) ? NB - gstart(g) : BPG, fb = 0, fi = 0;
        for (uint32_t i = 0; i < nbg; i++)
            fb += !bit(g_bb(g), i);
        for (uint32_t i = 0; i < ipg; i++)
            fi += !bit(g_ib(g), i);
        p16(gdoff(g) + 12, fb);
        p16(gdoff(g) + 14, fi);
        p16(gdoff(g) + 16, g == 0 ? 1 : 0);
        tfb += fb;
        tfi += fi;
    }
    uint64_t s = 1024;
    p32(s + 0, G * ipg);
    p32(s + 4, NB);
    p32(s + 12, tfb);
    p32(s + 16, tfi);
    p32(s + 20, FDB);
    p32(s + 24, bs == 1024 ? 0 : bs == 2048 ? 1 : 2);
    p32(s + 28, bs == 1024 ? 0 : bs == 2048 ? 1 : 2);
    p32(s + 32, bpg);
    p32(s + 36, bpg);
    p32(s + 40, ipg);
    p16(s + 56, 0xEF53);
    p16(s + 58, 1);
    p16(s + 60, 1);
    p32(s + 76, rev);
    if (rev) {
        p32(s + 84, 11);
        p16(s + 88, isz);
        p32(s + 96, EXT2_INCOMPAT_FILETYPE);
        p32(s + 100, EXT2_RO_COMPAT_SPARSE_SUPER | EXT2_RO_COMPAT_LARGE_FILE);
        memcpy(img + s + 120, "madire-test", 11);
    }
}

static int mount_rw(void)
{
    nw = 0;
    fail_from = 0;
    blk_sv_init(&sv, &d_ops, sv_ents, sv_data, SV_CAP, sv_run, img_len);
    return ext2_write_mount(&w, &sv, img_len, &t_wops, true);
}

/* ---- the independent checker ---- */

struct chk {
    uint32_t plus, dup, leaks_b, leaks_i, counts, links, bad;
};

static struct ext2_fs cf;
static const struct ext2_ops raw_ops = { d_read, 0 };
static uint8_t claimed[IMG_MAX / 1024 / 8 + 1];
static uint8_t iused[8192];
static uint16_t refs[65536];
static uint16_t subdirs[65536];

/* Block 0 exists only on a volume whose first data block is 0 (2 and 4 KiB
 * blocks: it holds the superblock); it is claimed only by check()'s
 * metadata loop, since a pointer of 0 is a hole and never reaches here. */
static void claim(struct chk *c, uint32_t b)
{
    if ((b == 0 && FDB != 0) || b >= NB) {
        c->bad++;
        return;
    }
    if (claimed[b / 8] & (1u << (b % 8)))
        c->dup++;
    claimed[b / 8] |= (uint8_t)(1u << (b % 8));
}

static void claim_tree(struct chk *c, uint32_t b, uint32_t h)
{
    if (!b)
        return;
    claim(c, b);
    if (h == 0 || b >= NB)
        return;
    for (uint32_t i = 0; i < BS / 4; i++)
        claim_tree(c, g32(boff(b) + 4 * i), h - 1);
}

static void check_inode_blocks(struct chk *c, const struct ext2_inode *in)
{
    uint32_t t = in->mode & 0xF000;
    if (t == 0x8000 || t == 0x4000 || (t == 0xA000 && !ext2_fast_link(&cf, in))) {
        for (uint32_t i = 0; i < 12; i++)
            if (in->block[i])
                claim(c, in->block[i]);
        claim_tree(c, in->block[12], 1);
        claim_tree(c, in->block[13], 2);
        claim_tree(c, in->block[14], 3);
    }
    if (in->file_acl && !(claimed[in->file_acl / 8] & (1u << (in->file_acl % 8))))
        claim(c, in->file_acl);
}

static void walk(struct chk *c, uint32_t dino, int depth)
{
    struct ext2_inode d;
    struct ext2_dir_iter it;
    struct ext2_dirent de;
    memset(&d, 0, sizeof d);
    memset(&de, 0, sizeof de);
    it.off = 0;
    if (depth > 20 || ext2_inode_get(&cf, dino, &d)) {
        c->bad++;
        return;
    }
    for (;;) {
        int r = ext2_dir_next(&cf, &d, &it, &de);
        if (r < 0) {
            c->bad++;
            return;
        }
        if (r == 0)
            return;
        if (strcmp(de.name, ".") == 0)
            continue;
        if (strcmp(de.name, "..") == 0)
            continue;
        refs[de.ino]++;
        if (iused[de.ino / 8] & (1u << (de.ino % 8)))
            continue;                               /* a hard link seen before */
        iused[de.ino / 8] |= (uint8_t)(1u << (de.ino % 8));
        struct ext2_inode in;
        memset(&in, 0, sizeof in);
        if (ext2_inode_get(&cf, de.ino, &in)) {
            c->bad++;
            continue;
        }
        check_inode_blocks(c, &in);
        if ((in.mode & 0xF000) == 0x4000) {
            subdirs[dino]++;
            walk(c, de.ino, depth + 1);
        }
    }
}

/* Mounts the raw image (the read core alone) and checks it. */
static struct chk check(void)
{
    struct chk c;
    memset(&c, 0, sizeof c);
    memset(claimed, 0, sizeof claimed);
    memset(iused, 0, sizeof iused);
    memset(refs, 0, sizeof refs);
    memset(subdirs, 0, sizeof subdirs);
    if (ext2_mount(&cf, &raw_ops, img_len)) {
        c.bad = 1000;
        return c;
    }
    /* metadata */
    for (uint32_t g = 0; g < G; g++) {
        uint32_t s = g == 0 ? FDB : gstart(g);
        for (uint32_t b = s; b < g_it(g) + ITB; b++)
            claim(&c, b);
    }
    for (uint32_t ino = 1; ino < cf.first_ino; ino++)
        iused[ino / 8] |= (uint8_t)(1u << (ino % 8));
    struct ext2_inode root;
    memset(&root, 0, sizeof root);
    ext2_inode_get(&cf, 2, &root);
    check_inode_blocks(&c, &root);
    walk(&c, 2, 0);
    /* link counts */
    for (uint32_t ino = cf.first_ino; ino <= cf.inodes_count; ino++) {
        if (!(iused[ino / 8] & (1u << (ino % 8))))
            continue;
        struct ext2_inode in;
        memset(&in, 0, sizeof in);
        ext2_inode_get(&cf, ino, &in);
        uint32_t want = (in.mode & 0xF000) == 0x4000 ? 2u + subdirs[ino] : refs[ino];
        if (in.links != want)
            c.links++;
    }
    if (root.links != 2u + subdirs[2])
        c.links++;
    /* bitmaps against what is used */
    for (uint32_t b = FDB; b < NB; b++) {
        bool u = claimed[b / 8] & (1u << (b % 8));
        if (u && !block_used(b))
            c.plus++;
        if (!u && block_used(b))
            c.leaks_b++;
    }
    for (uint32_t ino = 1; ino <= cf.inodes_count; ino++) {
        bool u = iused[ino / 8] & (1u << (ino % 8));
        if (u && !inode_used(ino))
            c.plus++;
        if (!u && inode_used(ino))
            c.leaks_i++;
    }
    /* counts */
    uint32_t tfb = 0, tfi = 0;
    for (uint32_t g = 0; g < G; g++) {
        uint32_t nbg = (g + 1 == G) ? NB - gstart(g) : BPG, fb = 0, fi = 0, nd = 0;
        for (uint32_t i = 0; i < nbg; i++)
            fb += !bit(g_bb(g), i);
        for (uint32_t i = 0; i < IPG; i++) {
            uint32_t ino = g * IPG + i + 1;
            fi += !bit(g_ib(g), i);
            if ((iused[ino / 8] & (1u << (ino % 8))) && (g16(ioff(ino)) & 0xF000) == 0x4000 && g16(ioff(ino) + 26))
                nd++;
        }
        c.counts += g16(gdoff(g) + 12) != fb;
        c.counts += g16(gdoff(g) + 14) != fi;
        c.counts += g16(gdoff(g) + 16) != nd;
        tfb += fb;
        tfi += fi;
    }
    c.counts += g32(1024 + 12) != tfb;
    c.counts += g32(1024 + 16) != tfi;
    return c;
}

static bool clean(void)
{
    struct chk c = check();
    if (c.plus || c.dup || c.leaks_b || c.leaks_i || c.counts || c.links || c.bad)
        printf("    check: plus %u dup %u leaks %u/%u counts %u links %u bad %u\n", c.plus, c.dup, c.leaks_b,
               c.leaks_i, c.counts, c.links, c.bad);
    return !(c.plus || c.dup || c.leaks_b || c.leaks_i || c.counts || c.links || c.bad);
}

/* ---- helpers over the core ---- */

static int save(void)
{
    return ext2_write_save(&w);
}

static int retry_create(uint32_t dir, const char *name, uint32_t *ino)
{
    int r = 0;
    for (int t = 0; t <= 3; t++) {
        r = ext2_write_create(&w, dir, name, (uint32_t)strlen(name), 0644, ino);
        if (r != EXT2_EAGAIN)
            return r;
        save();
    }
    return EXT2_ENOSPC;
}

static uint8_t pat[256 * 1024];

static void mkpat(uint32_t seed, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        pat[i] = (uint8_t)(seed * 31u + i * 7u + (i >> 9));
}

static int64_t write_all(uint32_t ino, uint64_t off, uint32_t len, uint32_t seed)
{
    mkpat(seed, len);
    uint32_t done = 0;
    int tries = 0;
    while (done < len) {
        int64_t n = ext2_write_pwrite(&w, ino, off + done, pat + done, len - done);
        if (n > 0) {
            done += (uint32_t)n;
            continue;
        }
        if (n != EXT2_EAGAIN || tries++ > 3)
            return n;
        save();
    }
    return done;
}

static bool reads_back(uint32_t ino, uint64_t off, uint32_t len, uint32_t seed)
{
    static uint8_t out[256 * 1024];
    struct ext2_inode in;
    memset(&in, 0, sizeof in);
    if (ext2_inode_get(&w.fs, ino, &in))
        return false;
    mkpat(seed, len);
    if (ext2_read(&w.fs, &in, off, out, len) != (int64_t)len)
        return false;
    return memcmp(out, pat, len) == 0;
}

static uint32_t lookup(uint32_t dir, const char *name)
{
    struct ext2_inode d;
    uint32_t ino = 0;
    memset(&d, 0, sizeof d);
    if (ext2_inode_get(&w.fs, dir, &d) || ext2_lookup(&w.fs, &d, name, (uint32_t)strlen(name), &ino))
        return 0;
    return ino;
}

static struct ext2_inode inode_raw(uint32_t ino)
{
    struct ext2_fs f;
    struct ext2_inode in;
    memset(&in, 0, sizeof in);
    if (ext2_mount(&f, &raw_ops, img_len) == 0)
        ext2_inode_get(&f, ino, &in);
    return in;
}

static void data_blocks(uint32_t ino, uint32_t *out, uint32_t n)
{
    struct ext2_inode in;
    memset(&in, 0, sizeof in);
    ext2_inode_get(&w.fs, ino, &in);
    for (uint32_t i = 0; i < n; i++) {
        out[i] = 0;
        ext2_bmap(&w.fs, &in, i, &out[i]);
    }
}

/* ---- the cases ---- */

static void test_mount_states(void)
{
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    CHECK(w.rw);
    CHECK(!w.recounted);
    CHECK_EQ(w.saves_done, 1);                      /* save 0 */
    CHECK_EQ(g16(1024 + 58) & 1u, 0);               /* "not clean" on the disk */
    CHECK(nw >= 1);
    CHECK_EQ(wlog[0].off, 1024);
    CHECK_EQ(wlog[0].cls, BLK_SV_SUPER);
    CHECK_EQ(ext2_write_unmount(&w), EXT2_OK);
    CHECK_EQ(g16(1024 + 58) & 1u, 1);               /* clean again */
    CHECK(clean());
}

static void test_create_write(void)
{
    mkimg(1024, 256, 1, 2, 1024, 64, 100);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t ino = 0;
    struct ext2_inode root;
    memset(&root, 0, sizeof root);
    CHECK_EQ(ext2_inode_get(&w.fs, 2, &root), EXT2_OK);    /* the read core keeps the inode block */
    CHECK_EQ(lookup(2, "nothing"), 0);                      /* ... and the folder block */
    CHECK_EQ(ext2_write_create(&w, 2, "hello.txt", 9, 0644, &ino), EXT2_OK);
    CHECK(ino >= 11);
    /* The kept blocks took the writes: no ext2_cache_drop needed. */
    CHECK_EQ(lookup(2, "hello.txt"), ino);
    struct ext2_inode in;
    memset(&in, 0, sizeof in);
    CHECK_EQ(ext2_inode_get(&w.fs, ino, &in), EXT2_OK);
    CHECK_EQ(in.mode, 0x81A4);
    CHECK_EQ(in.links, 1);
    CHECK_EQ(write_all(ino, 0, 5000, 1), 5000);
    CHECK(reads_back(ino, 0, 5000, 1));
    uint32_t file = ino, out = 0;                   /* the refused creates below set ino to 0 */
    CHECK_EQ(ext2_write_create(&w, 2, "hello.txt", 9, 0644, &ino), EXT2_EEXIST);
    CHECK_EQ(ext2_write_create(&w, 2, ".", 1, 0644, &ino), EXT2_EINVAL);
    CHECK_EQ(ext2_write_create(&w, 2, "a/b", 3, 0644, &ino), EXT2_EINVAL);
    char longname[300];
    memset(longname, 'n', sizeof longname);
    CHECK_EQ(ext2_write_create(&w, 2, longname, 256, 0644, &ino), EXT2_ENAMETOOLONG);
    CHECK_EQ(ext2_write_create(&w, file, "x", 1, 0644, &out), EXT2_ENOTDIR);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(!ext2_write_busy(&w));
    CHECK(clean());
    /* The same bytes through the raw image. */
    uint32_t h = lookup(2, "hello.txt");
    CHECK(reads_back(h, 0, 5000, 1));
    CHECK_EQ(inode_raw(h).size, 5000);
    CHECK_EQ(inode_raw(h).blocks512, 10);
}

/* The classes of one create-and-write (R1): its data and its new
 * indirect block in class 1, the bitmaps 2, the inode table 3, the folder
 * 5, the descriptors and the superblock 6, in that order (stage 11: class
 * 4, a cross-block rename's new name, does not occur here). */
static void test_classes(void)
{
    mkimg(1024, 256, 1, 1, 2048, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t ino = 0, mark = nw;
    CHECK_EQ(ext2_write_create(&w, 2, "f", 1, 0644, &ino), EXT2_OK);
    CHECK_EQ(write_all(ino, 0, 14 * 1024, 2), 14 * 1024);      /* 12 direct + the single indirect */
    CHECK_EQ(save(), EXT2_OK);
    bool order = true, seen[7] = { false, false, false, false, false, false, false };
    uint32_t last = 0;
    struct ext2_inode in;
    memset(&in, 0, sizeof in);
    ext2_inode_get(&w.fs, ino, &in);
    uint32_t ind = in.block[12], ind_cls = 0, it_cls = 0, root_cls = 0;
    for (uint32_t i = mark; i < nw; i++) {
        order = order && wlog[i].cls >= last;
        last = wlog[i].cls;
        seen[wlog[i].cls] = true;
        uint64_t a = wlog[i].off, z = a + wlog[i].len;
        if (boff(ind) >= a && boff(ind) < z)
            ind_cls = wlog[i].cls;
        if (ioff(ino) >= a && ioff(ino) < z)
            it_cls = wlog[i].cls;
        if (boff(inode_raw(2).block[0]) >= a && boff(inode_raw(2).block[0]) < z)
            root_cls = wlog[i].cls;
    }
    CHECK(order);
    CHECK(seen[1] && seen[2] && seen[3] && !seen[4] && seen[5] && seen[6]);
    CHECK_EQ(ind_cls, BLK_SV_DATA);
    CHECK_EQ(it_cls, BLK_SV_INODE);
    CHECK_EQ(root_cls, BLK_SV_FOLDER);
    CHECK(clean());
}

/* The owner's case (C5 and R3). */
static void test_owner_case(void)
{
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t a = 0, b = 0, ab[3], bb[3];
    CHECK_EQ(ext2_write_create(&w, 2, "A", 1, 0644, &a), EXT2_OK);
    CHECK_EQ(write_all(a, 0, 3000, 3), 3000);
    CHECK_EQ(save(), EXT2_OK);                      /* save 1 */
    data_blocks(a, ab, 3);
    CHECK_EQ(ext2_write_unlink(&w, 2, "A", 1), EXT2_OK);
    CHECK_EQ(ext2_write_unlink(&w, 2, "A", 1), EXT2_ENOENT);
    CHECK_EQ(ext2_write_create(&w, 2, "B", 1, 0644, &b), EXT2_OK);
    CHECK(b != a);                                  /* A's inode is not reused */
    CHECK_EQ(write_all(b, 0, 3000, 4), 3000);
    data_blocks(b, bb, 3);
    bool disjoint = true;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            disjoint = disjoint && ab[i] != bb[j];
    CHECK(disjoint);                                /* B never gets A's blocks */
    CHECK_EQ(save(), EXT2_OK);                      /* save 2: A's entry gone */
    CHECK_EQ(lookup(2, "A"), 0);
    CHECK_EQ(inode_raw(a).links, 1);                /* A's inode untouched on the disk */
    CHECK_EQ(inode_raw(a).block[0], ab[0]);
    CHECK(block_used(ab[0]) && inode_used(a));
    CHECK_EQ(save(), EXT2_OK);                      /* save 3: A's inode deleted */
    CHECK_EQ(inode_raw(a).links, 0);
    CHECK(inode_raw(a).dtime != 0);
    CHECK_EQ(inode_raw(a).block[0], 0);
    CHECK(block_used(ab[0]) && block_used(ab[2]) && inode_used(a));     /* bits still set */
    CHECK_EQ(save(), EXT2_OK);                      /* save 4: the bits cleared */
    CHECK(!block_used(ab[0]) && !block_used(ab[1]) && !block_used(ab[2]) && !inode_used(a));
    CHECK(!ext2_write_busy(&w));
    CHECK(clean());
    CHECK(reads_back(b, 0, 3000, 4));
    CHECK(w.pend_n == 0);
}

static void add_raw_entry(uint32_t dblk, const char *name, uint32_t ino, uint32_t ft)
{
    /* splits the last entry of the folder block */
    uint32_t pos = 0, last = 0;
    while (pos < BS) {
        last = pos;
        pos += g16(boff(dblk) + pos + 4);
    }
    uint32_t nl = (uint32_t)strlen(name), act = (8 + img[boff(dblk) + last + 6] + 3) & ~3u;
    uint32_t rec = g16(boff(dblk) + last + 4);
    p16(boff(dblk) + last + 4, act);
    uint64_t e = boff(dblk) + last + act;
    p32(e, ino);
    p16(e + 4, rec - act);
    img[e + 6] = (uint8_t)nl;
    img[e + 7] = (uint8_t)ft;
    memcpy(img + e + 8, name, nl);
}

static void test_hard_link(void)
{
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t h = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "h1", 2, 0644, &h), EXT2_OK);
    CHECK_EQ(write_all(h, 0, 2000, 5), 2000);
    CHECK_EQ(ext2_write_unmount(&w), EXT2_OK);
    add_raw_entry(inode_raw(2).block[0], "h2", h, 1);
    p16(ioff(h) + 26, 2);
    CHECK(clean());
    CHECK_EQ(mount_rw(), EXT2_OK);
    CHECK_EQ(ext2_write_unlink(&w, 2, "h1", 2), EXT2_OK);
    CHECK_EQ(save(), EXT2_OK);                      /* save E: the entry */
    CHECK_EQ(inode_raw(h).links, 2);                /* the count one save later (R3) */
    CHECK_EQ(save(), EXT2_OK);
    CHECK_EQ(inode_raw(h).links, 1);
    CHECK(clean());
    CHECK(reads_back(lookup(2, "h2"), 0, 2000, 5));
    CHECK_EQ(ext2_write_unlink(&w, 2, "h2", 2), EXT2_OK);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(!inode_used(h));
    CHECK(clean());
}

/* R2 for writes and truncate: an indirect block the disk already uses is
 * copied; a fresh one is changed in place; truncate's tail zeroed. */
static void test_copies_truncate(void)
{
    mkimg(1024, 256, 1, 1, 2048, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t f = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "big", 3, 0644, &f), EXT2_OK);
    CHECK_EQ(write_all(f, 0, 20 * 1024, 6), 20 * 1024);
    CHECK_EQ(save(), EXT2_OK);
    uint32_t ind0 = inode_raw(f).block[12];
    uint8_t before[1024];
    memcpy(before, img + boff(ind0), 1024);
    CHECK_EQ(ext2_write_truncate(&w, f, 14 * 1024 + 300), EXT2_OK);
    struct ext2_inode in;
    memset(&in, 0, sizeof in);
    ext2_inode_get(&w.fs, f, &in);
    uint32_t ind1 = in.block[12];
    CHECK(ind1 != 0 && ind1 != ind0);               /* copied */
    CHECK_EQ(write_all(f, 30 * 1024, 1024, 7), 1024);
    ext2_inode_get(&w.fs, f, &in);
    CHECK_EQ(in.block[12], ind1);                   /* fresh: changed in place */
    CHECK_EQ(save(), EXT2_OK);
    CHECK(memcmp(before, img + boff(ind0), 1024) == 0);    /* the old copy never written */
    CHECK(block_used(ind0));                        /* pending until the next save */
    CHECK(reads_back(f, 0, 14 * 1024 + 300, 6));
    static uint8_t out[2048];
    ext2_inode_get(&w.fs, f, &in);
    CHECK_EQ(ext2_read(&w.fs, &in, 14 * 1024 + 300, out, 1024 - 300), 1024 - 300);
    bool zero = true;
    for (uint32_t i = 0; i < 1024 - 300; i++)
        zero = zero && out[i] == 0;
    CHECK(zero);                                    /* the tail reads as zeros */
    CHECK(reads_back(f, 30 * 1024, 1024, 7));
    CHECK_EQ(save(), EXT2_OK);
    CHECK(!block_used(ind0));
    CHECK(clean());
    /* The double indirect tree, then everything past 5 blocks freed. */
    CHECK_EQ(write_all(f, (12 + 256 + 3) * 1024, 1024, 8), 1024);
    CHECK_EQ(save(), EXT2_OK);
    CHECK(clean());
    CHECK_EQ(ext2_write_truncate(&w, f, 5 * 1024), EXT2_OK);
    ext2_inode_get(&w.fs, f, &in);
    CHECK(in.block[12] == 0 && in.block[13] == 0 && in.block[5] == 0 && in.block[4] != 0);
    CHECK_EQ(in.blocks512, 10);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
    CHECK(reads_back(f, 0, 5 * 1024, 6));
    /* A truncate up is a hole. */
    CHECK_EQ(ext2_write_truncate(&w, f, 100 * 1024), EXT2_OK);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(inode_raw(f).size, 100 * 1024);
    CHECK_EQ(inode_raw(f).blocks512, 10);
    CHECK(clean());
}

/* ENOSPC only after the pending frees are saved (C5, R4). */
static void test_full_disk(void)
{
    mkimg(1024, 256, 1, 1, 160, 32, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t f = 0, g = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "fill", 4, 0644, &f), EXT2_OK);
    uint64_t off = 0;
    int64_t n = 0;
    for (;;) {
        mkpat(9, 1024);
        n = ext2_write_pwrite(&w, f, off, pat, 1024);
        if (n <= 0)
            break;
        off += (uint64_t)n;
    }
    CHECK_EQ(n, EXT2_ENOSPC);                       /* nothing pending: ENOSPC at once */
    CHECK(off > 50 * 1024);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
    CHECK_EQ(ext2_write_unlink(&w, 2, "fill", 4), EXT2_OK);
    CHECK_EQ(ext2_write_create(&w, 2, "next", 4, 0644, &g), EXT2_OK);
    mkpat(10, 1024);
    CHECK_EQ(ext2_write_pwrite(&w, g, 0, pat, 1024), EXT2_EAGAIN);     /* pending frees: save first */
    int saves = 0;
    while ((n = ext2_write_pwrite(&w, g, 0, pat, 1024)) == EXT2_EAGAIN && saves < 3) {
        CHECK_EQ(save(), EXT2_OK);
        saves++;
    }
    CHECK_EQ(n, 1024);
    CHECK_EQ(saves, 2);                             /* E: the entry, E+1: the inode; then the bits */
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
}

/* R4: a full folder grows by an empty block, then EAGAIN; a fresh folder
 * block takes no entry. */
static void test_folder_growth(void)
{
    mkimg(1024, 256, 1, 1, 1024, 128, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t ino = 0, d = 0;
    char name[64];
    int eagain = 0;
    for (int i = 0; i < 60; i++) {
        snprintf(name, sizeof name, "entry-with-a-name-of-forty-bytes-xx-%03d", i);
        int r = ext2_write_create(&w, 2, name, (uint32_t)strlen(name), 0644, &ino);
        if (r == EXT2_EAGAIN) {
            eagain++;
            struct ext2_inode root;
            memset(&root, 0, sizeof root);
            ext2_inode_get(&w.fs, 2, &root);
            CHECK_EQ(lookup(2, name), 0);           /* not added */
            CHECK(root.size >= 2048);               /* grown */
            CHECK_EQ(save(), EXT2_OK);
            r = ext2_write_create(&w, 2, name, (uint32_t)strlen(name), 0644, &ino);
        }
        CHECK_EQ(r, EXT2_OK);
    }
    CHECK(eagain >= 2);
    int r = ext2_write_mkdir(&w, 2, "d", 1, 0755, &d);
    if (r == EXT2_EAGAIN) {                         /* the root may need a block first */
        CHECK_EQ(save(), EXT2_OK);
        r = ext2_write_mkdir(&w, 2, "d", 1, 0755, &d);
    }
    CHECK_EQ(r, EXT2_OK);
    CHECK_EQ(ext2_write_create(&w, d, "inside", 6, 0644, &ino), EXT2_EAGAIN);  /* d's block is fresh */
    struct ext2_inode din;
    memset(&din, 0, sizeof din);
    ext2_inode_get(&w.fs, d, &din);
    CHECK_EQ(din.size, 1024);                       /* not grown for that */
    CHECK_EQ(save(), EXT2_OK);
    CHECK_EQ(ext2_write_create(&w, d, "inside", 6, 0644, &ino), EXT2_OK);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
    CHECK_EQ(lookup(2, "entry-with-a-name-of-forty-bytes-xx-059") != 0, 1);
}

static void test_dirs_rename(void)
{
    mkimg(2048, 256, 1, 1, 1024, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t d1 = 0, d2 = 0, f = 0, r1 = 0, r2 = 0, x = 0;
    CHECK_EQ(ext2_write_mkdir(&w, 2, "d1", 2, 0755, &d1), EXT2_OK);
    CHECK_EQ(ext2_write_mkdir(&w, 2, "d2", 2, 0755, &d2), EXT2_OK);
    CHECK_EQ(save(), EXT2_OK);
    CHECK_EQ(inode_raw(2).links, 4);
    CHECK_EQ(retry_create(d1, "f", &f), EXT2_OK);
    CHECK_EQ(write_all(f, 0, 3000, 11), 3000);
    CHECK_EQ(ext2_write_create(&w, 2, "r1", 2, 0644, &r1), EXT2_OK);
    CHECK_EQ(write_all(r1, 0, 100, 12), 100);
    CHECK_EQ(ext2_write_create(&w, 2, "r2", 2, 0644, &r2), EXT2_OK);
    CHECK_EQ(save(), EXT2_OK);
    CHECK_EQ(ext2_write_rmdir(&w, 2, "d1", 2), EXT2_ENOTEMPTY);
    CHECK_EQ(ext2_write_rmdir(&w, 2, "r1", 2), EXT2_ENOTDIR);
    CHECK_EQ(ext2_write_unlink(&w, 2, "d1", 2), EXT2_EISDIR);
    CHECK_EQ(ext2_write_rename(&w, 2, "r1", 2, 2, "r2", 2), EXT2_OK);      /* replaces r2 */
    CHECK_EQ(lookup(2, "r2"), r1);
    CHECK_EQ(lookup(2, "r1"), 0);
    CHECK_EQ(ext2_write_rename(&w, d1, "f", 1, d2, "g", 1), EXT2_OK);      /* across folders */
    CHECK_EQ(ext2_write_rename(&w, 2, "d1", 2, d2, "d1", 2), EXT2_OK);     /* a folder across */
    CHECK_EQ(ext2_write_rename(&w, d2, "d1", 2, d1, "x", 1), EXT2_EINVAL); /* into itself */
    CHECK_EQ(ext2_write_rename(&w, 2, "d2", 2, d1, "x", 1), EXT2_EINVAL);  /* into its own child */
    CHECK_EQ(ext2_write_rename(&w, 2, "r2", 2, 2, "d2", 2), EXT2_EISDIR);
    CHECK_EQ(ext2_write_rename(&w, 2, "d2", 2, 2, "r2", 2), EXT2_ENOTDIR);
    CHECK_EQ(ext2_write_rename(&w, 2, "r2", 2, 2, "r2", 2), EXT2_OK);      /* the same name: nothing */
    (void)x;
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
    struct ext2_inode in;
    memset(&in, 0, sizeof in);
    ext2_inode_get(&w.fs, d1, &in);
    struct ext2_w_loc up = { 0, 0, 0, 0, 0, -1 };
    CHECK_EQ(ext2_w_find(&w, &in, "..", 2, &up), EXT2_OK);
    CHECK_EQ(up.ino, d2);                           /* ".." follows the move */
    CHECK_EQ(inode_raw(2).links, 3);
    CHECK_EQ(inode_raw(d2).links, 3);
    CHECK(!inode_used(r2));                         /* the replaced file deleted (R3) */
    CHECK(reads_back(lookup(d2, "g"), 0, 3000, 11));
    CHECK_EQ(ext2_write_rmdir(&w, d2, "d1", 2), EXT2_OK);
    CHECK_EQ(inode_raw(d1).links, 2);               /* its inode one save later */
    CHECK_EQ(save(), EXT2_OK);
    CHECK_EQ(inode_raw(d1).links, 2);
    CHECK_EQ(inode_raw(d2).links, 2);
    CHECK_EQ(save(), EXT2_OK);
    CHECK_EQ(inode_raw(d1).links, 0);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
}

/* R5 (stage 11): a rename across folder blocks puts the new name's block
 * in class 4 and the old name's in class 5 (written in that order); a
 * rename inside one block is class 5; a plain change keeps a class-4 block
 * in class 4; a rename whose old name is in a class-4 block, or whose new
 * name would go into a block already changed in class 5, answers
 * EXT2_EAGAIN before changing anything, and succeeds after a save; a
 * folder move's ".." goes with the old name (class 5). */
static void test_rename_r5(void)
{
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t d1 = 0, d2 = 0, d3 = 0, f = 0, g = 0, h = 0, x = 0, n = 0;
    CHECK_EQ(ext2_write_mkdir(&w, 2, "d1", 2, 0755, &d1), EXT2_OK);
    CHECK_EQ(ext2_write_mkdir(&w, 2, "d2", 2, 0755, &d2), EXT2_OK);
    CHECK_EQ(ext2_write_mkdir(&w, 2, "d3", 2, 0755, &d3), EXT2_OK);
    CHECK_EQ(save(), EXT2_OK);
    CHECK_EQ(retry_create(d1, "f", &f), EXT2_OK);
    CHECK_EQ(write_all(f, 0, 2500, 21), 2500);
    CHECK_EQ(retry_create(d1, "g", &g), EXT2_OK);
    CHECK_EQ(retry_create(d2, "h", &h), EXT2_OK);
    CHECK_EQ(retry_create(d3, "x", &x), EXT2_OK);
    CHECK_EQ(save(), EXT2_OK);
    uint32_t b1 = inode_raw(d1).block[0], b2 = inode_raw(d2).block[0], b3 = inode_raw(d3).block[0];
    uint32_t br = inode_raw(2).block[0], mark = nw;
    CHECK(b1 && b2 && b3 && br && b1 != b2 && b2 != b3);
    CHECK_EQ(ext2_write_rename(&w, d1, "f", 1, d2, "f2", 2), EXT2_OK);     /* across folders */
    CHECK_EQ(blk_sv_class(&sv, boff(b2)), BLK_SV_NAME);
    CHECK_EQ(blk_sv_class(&sv, boff(b1)), BLK_SV_FOLDER);
    CHECK_EQ(ext2_write_rename(&w, d2, "h", 1, d3, "h", 1), EXT2_EAGAIN);  /* old name in a class-4 block */
    CHECK_EQ(lookup(d2, "h"), h);
    CHECK_EQ(lookup(d3, "h"), 0);
    CHECK_EQ(ext2_write_rename(&w, d3, "x", 1, d1, "x", 1), EXT2_EAGAIN);  /* new name into a class-5 block */
    CHECK_EQ(lookup(d3, "x"), x);
    CHECK_EQ(lookup(d1, "x"), 0);
    CHECK_EQ(blk_sv_class(&sv, boff(b3)), 0);                               /* nothing changed */
    CHECK_EQ(ext2_write_rename(&w, d1, "g", 1, d1, "g2", 2), EXT2_OK);     /* inside one block */
    CHECK_EQ(blk_sv_class(&sv, boff(b1)), BLK_SV_FOLDER);
    CHECK_EQ(ext2_write_create(&w, d2, "new", 3, 0644, &n), EXT2_OK);      /* a plain add: stays class 4 */
    CHECK_EQ(blk_sv_class(&sv, boff(b2)), BLK_SV_NAME);
    CHECK_EQ(save(), EXT2_OK);
    int64_t i1 = -1, i2 = -1;
    for (uint32_t i = mark; i < nw && i < WLOG; i++) {
        if (wlog[i].off == boff(b1))
            i1 = i;
        if (wlog[i].off == boff(b2))
            i2 = i;
    }
    CHECK(i2 >= 0 && i1 > i2);                      /* the new name's block first */
    CHECK(i2 >= 0 && wlog[i2].cls == BLK_SV_NAME);
    CHECK(i1 >= 0 && wlog[i1].cls == BLK_SV_FOLDER);
    CHECK_EQ(ext2_write_rename(&w, d2, "h", 1, d3, "h", 1), EXT2_OK);      /* after the save */
    CHECK_EQ(save(), EXT2_OK);
    /* A folder moved: the new name in class 4, the old name and its ".." in 5. */
    CHECK_EQ(ext2_write_rename(&w, 2, "d3", 2, d2, "d3", 2), EXT2_OK);
    CHECK_EQ(blk_sv_class(&sv, boff(b2)), BLK_SV_NAME);
    CHECK_EQ(blk_sv_class(&sv, boff(br)), BLK_SV_FOLDER);
    CHECK_EQ(blk_sv_class(&sv, boff(b3)), BLK_SV_FOLDER);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
    CHECK(reads_back(lookup(d2, "f2"), 0, 2500, 21));
    CHECK_EQ(lookup(d3, "h"), h);
    CHECK_EQ(lookup(d2, "d3"), d3);
    CHECK_EQ(lookup(2, "d3"), 0);
    CHECK_EQ(lookup(d1, "g2"), g);
}

static void test_dir_index_flag(void)
{
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    p32(1024 + 92, EXT2_COMPAT_DIR_INDEX);
    p32(ioff(2) + 32, EXT2_INDEX_FL);
    CHECK_EQ(mount_rw(), EXT2_OK);
    CHECK(w.rw);
    uint32_t f = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "f", 1, 0644, &f), EXT2_OK);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(inode_raw(2).flags & EXT2_INDEX_FL, 0);
}

/* Attribute blocks: one used once freed with its inode; a shared one's
 * count lowered one save after the inode's deletion. */
static void test_attr_blocks(void)
{
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    p32(1024 + 92, EXT2_COMPAT_EXT_ATTR);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t a = 0, b = 0, c = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "a", 1, 0644, &a), EXT2_OK);
    CHECK_EQ(ext2_write_create(&w, 2, "b", 1, 0644, &b), EXT2_OK);
    CHECK_EQ(ext2_write_create(&w, 2, "c", 1, 0644, &c), EXT2_OK);
    CHECK_EQ(ext2_write_unmount(&w), EXT2_OK);
    /* two attribute blocks by hand: E1 used by a (count 1), E2 by b and c (count 2) */
    uint32_t e1 = next_free + 200, e2 = next_free + 201;
    mark_block(e1);
    mark_block(e2);
    p16(gdoff(0) + 12, g16(gdoff(0) + 12) - 2);
    p32(1024 + 12, g32(1024 + 12) - 2);
    p32(boff(e1), EXT2_W_EA_MAGIC);
    p32(boff(e1) + 4, 1);
    p32(boff(e2), EXT2_W_EA_MAGIC);
    p32(boff(e2) + 4, 2);
    p32(ioff(a) + 104, e1);
    p32(ioff(b) + 104, e2);
    p32(ioff(c) + 104, e2);
    p32(ioff(a) + 28, 2);
    p32(ioff(b) + 28, 2);
    p32(ioff(c) + 28, 2);
    CHECK(clean());
    CHECK_EQ(mount_rw(), EXT2_OK);
    CHECK_EQ(ext2_write_unlink(&w, 2, "a", 1), EXT2_OK);
    CHECK_EQ(ext2_write_unlink(&w, 2, "b", 1), EXT2_OK);
    CHECK_EQ(save(), EXT2_OK);                      /* E */
    CHECK_EQ(save(), EXT2_OK);                      /* E+1: the inodes deleted */
    CHECK_EQ(g32(boff(e2) + 4), 2);                 /* not yet */
    CHECK(block_used(e1));
    CHECK_EQ(save(), EXT2_OK);                      /* E+2: the count lowered, e1's bit cleared */
    CHECK_EQ(g32(boff(e2) + 4), 1);
    CHECK(!block_used(e1));
    CHECK(block_used(e2));
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
}

/* The triple indirect block and a size above 2 GiB (large_file). */
static void test_tind_large(void)
{
    mkimg(1024, 256, 1, 1, 2048, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t f = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "large", 5, 0644, &f), EXT2_OK);
    uint64_t far = (1ull << 31) + 5000u;
    CHECK_EQ(write_all(f, far, 3000, 13), 3000);
    CHECK_EQ(write_all(f, 100, 50, 14), 50);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
    struct ext2_inode in = inode_raw(f);
    CHECK_EQ(in.size, far + 3000u);
    CHECK(in.block[14] != 0);
    CHECK(reads_back(f, far, 3000, 13));
    CHECK(reads_back(f, 100, 50, 14));
    CHECK_EQ(g32(ioff(f) + 108), 0);                /* (2^31 + ...) >> 32 = 0 */
    CHECK_EQ(write_all(f, (1ull << 32) - 10u, 9, 15), 9);  /* up to 4 GiB - 1 */
    CHECK_EQ(write_all(f, (1ull << 32) - 10u, 10, 15), EXT2_EFBIG);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(g32(ioff(f) + 108), 0);
    CHECK_EQ(inode_raw(f).size, (1ull << 32) - 1u);
    CHECK(clean());
    CHECK_EQ(ext2_write_truncate(&w, f, 0), EXT2_OK);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(inode_raw(f).blocks512, 0);
    CHECK(clean());
}

static void test_rev0_and_sizes(void)
{
    static const uint32_t bss[3] = { 1024, 2048, 4096 };
    for (int k = 0; k < 3; k++) {
        mkimg(bss[k], 128, 0, 2, 512, 32, 7);
        CHECK_EQ(mount_rw(), EXT2_OK);
        uint32_t f = 0, d = 0;
        CHECK_EQ(ext2_write_mkdir(&w, 2, "dir", 3, 0755, &d), EXT2_OK);
        CHECK_EQ(ext2_write_create(&w, 2, "file", 4, 0644, &f), EXT2_OK);
        CHECK_EQ(write_all(f, 0, 70000, 16), 70000);
        CHECK_EQ(retry_create(d, "inner", &f), EXT2_OK);
        CHECK_EQ(ext2_write_rename(&w, d, "inner", 5, 2, "moved", 5), EXT2_OK);
        CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
        CHECK(clean());
        CHECK_EQ(lookup(2, "moved"), f);
        CHECK(reads_back(lookup(2, "file"), 0, 70000, 16));
        /* revision 0: the core's entry "dir" (split from "..", at byte 24)
         * has a 16-bit name length and no file type byte */
        uint32_t rb = inode_raw(2).block[0];
        CHECK_EQ(g16(boff(rb) + 24 + 6), 3);
        CHECK(memcmp(img + boff(rb) + 24 + 8, "dir", 3) == 0);
    }
}

static void test_policy(void)
{
    static const uint32_t offs[3] = { 1024 + 92, 1024 + 100, 1024 + 58 };
    static const uint32_t vals[3] = { 0x1000, 0x8, 0x3 };
    for (int k = 0; k < 3; k++) {
        mkimg(1024, 256, 1, 1, 1024, 64, 0);
        if (k == 2)
            p16(offs[k], vals[k]);
        else
            p32(offs[k], g32(offs[k]) | vals[k]);
        CHECK_EQ(mount_rw(), EXT2_OK);
        CHECK(!w.rw);
        CHECK(w.ro_why != 0);
        uint32_t f = 0;
        CHECK_EQ(ext2_write_create(&w, 2, "no", 2, 0644, &f), EXT2_EROFS);
        CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
        CHECK_EQ(nw, 0);                            /* nothing written */
    }
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    blk_sv_init(&sv, &d_ops, sv_ents, sv_data, SV_CAP, sv_run, img_len);
    nw = 0;
    CHECK_EQ(ext2_write_mount(&w, &sv, img_len, &t_wops, false), EXT2_OK);
    CHECK(!w.rw);
    CHECK_EQ(nw, 0);
    /* inode 7 (resize_inode) is never written */
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    p32(1024 + 92, EXT2_COMPAT_RESIZE_INODE);
    p16(ioff(7), 0x8180);
    p16(ioff(7) + 26, 1);
    uint8_t seven[256];
    memcpy(seven, img + ioff(7), 256);
    CHECK_EQ(mount_rw(), EXT2_OK);
    CHECK(w.rw);
    uint32_t f = 0;
    for (int i = 0; i < 20; i++) {
        char n[16];
        snprintf(n, sizeof n, "f%d", i);
        CHECK_EQ(retry_create(2, n, &f), EXT2_OK);
        CHECK_EQ(write_all(f, 0, 1500, 17), 1500);
    }
    CHECK_EQ(ext2_write_unmount(&w), EXT2_OK);
    CHECK(memcmp(seven, img + ioff(7), 256) == 0);
}

static void test_recount(void)
{
    mkimg(1024, 256, 1, 2, 1024, 64, 0);
    p16(1024 + 58, 0);                              /* not clean, no error bit */
    p16(gdoff(0) + 12, 3);                          /* wrong counts */
    p16(gdoff(1) + 14, 1);
    p16(gdoff(0) + 16, 9);
    p32(1024 + 12, 5);
    CHECK_EQ(mount_rw(), EXT2_OK);
    CHECK(w.rw);
    CHECK(w.recounted);
    CHECK(clean());                                 /* save 0 wrote the recounted figures */
    CHECK_EQ(ext2_write_unmount(&w), EXT2_OK);
    CHECK(clean());
}

static void test_failed_save(void)
{
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t f = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "f", 1, 0644, &f), EXT2_OK);
    fail_from = nw + 2;                             /* the second write of the next save fails */
    CHECK_EQ(save(), EXT2_EIO);
    CHECK(!w.rw);
    CHECK(w.failed);
    CHECK_EQ(ext2_write_create(&w, 2, "g", 1, 0644, &f), EXT2_EROFS);
    CHECK_EQ(save(), EXT2_EIO);
    CHECK_EQ(g16(1024 + 58) & EXT2_STATE_ERROR, 0); /* writing failed: the error bit could not be written */
    /* A disk that failed a command but still takes writes: the save fails
     * and the error bit is written once (D9). */
    mkimg(1024, 256, 1, 1, 1024, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    CHECK_EQ(ext2_write_create(&w, 2, "f", 1, 0644, &f), EXT2_OK);
    CHECK(blk_sv_dirty(&sv) > 0);
    sv.failed = true;                               /* as after a failed command */
    CHECK_EQ(save(), EXT2_EIO);
    CHECK(w.failed);
    CHECK(!w.rw);
    CHECK_EQ(g16(1024 + 58) & EXT2_STATE_ERROR, EXT2_STATE_ERROR);
}

/* The checker itself, on a 2 KiB image (block 0 the superblock, block 1
 * the group descriptors): a direct or an indirect pointer that names a
 * metadata block is still reported (claimed twice). */
static void test_checker_negative(void)
{
    mkimg(2048, 256, 1, 1, 1024, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t f = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "f", 1, 0644, &f), EXT2_OK);
    CHECK_EQ(write_all(f, 0, 14 * 2048, 22), 14 * 2048);  /* 12 direct + the single indirect */
    CHECK_EQ(ext2_write_unmount(&w), EXT2_OK);
    CHECK(clean());                                 /* block 0 claimed once, by the metadata loop */
    uint32_t d1 = g32(ioff(f) + 40 + 4), ind = g32(ioff(f) + 40 + 48), e0 = g32(boff(ind));
    p32(ioff(f) + 40 + 4, 1);                       /* direct pointer -> the descriptor block */
    struct chk c = check();
    CHECK(c.dup >= 1);
    p32(ioff(f) + 40 + 4, d1);
    p32(boff(ind), 1);                              /* indirect pointer -> the descriptor block */
    c = check();
    CHECK(c.dup >= 1);
    p32(boff(ind), e0);
    p32(ioff(f) + 40 + 4, NB);                      /* beyond the last block */
    c = check();
    CHECK(c.bad >= 1);
    p32(ioff(f) + 40 + 4, d1);
    CHECK(clean());                                 /* restored: clean again */
}

static void test_pending_merge(void)
{
    mkimg(1024, 256, 1, 1, 2048, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t f = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "f", 1, 0644, &f), EXT2_OK);
    CHECK_EQ(write_all(f, 0, 11 * 1024, 18), 11 * 1024);
    CHECK_EQ(save(), EXT2_OK);
    CHECK_EQ(ext2_write_truncate(&w, f, 0), EXT2_OK);
    CHECK(w.pend_n <= 2);                           /* neighbouring frees merged into ranges */
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
    CHECK(touches > 0);
    CHECK(strcmp(ext2_write_err_str(EXT2_EAGAIN), "EAGAIN") == 0);
    CHECK(strcmp(ext2_write_err_str(EXT2_ENOENT), "ENOENT") == 0);
}

/* 1.4 stage 10: a save in pieces gives the same disk as a whole save;
 * while it is in progress every operation answers EAGAIN and changes
 * nothing; keep_ino keeps an open file's inode after its name is removed
 * until ext2_write_forget; ext2_write_mark_clean writes the clean state
 * (EAGAIN while a save is in progress). */
static void test_save_steps(void)
{
    mkimg(1024, 256, 1, 1, 2048, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t ino = 0, other = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "piece", 5, 0644, &ino), EXT2_OK);
    CHECK_EQ(write_all(ino, 0, 20 * 1024, 3), 20 * 1024);
    uint32_t saves = w.saves_done, calls = 0;
    bool done = false;
    while (!done) {
        uint32_t before = nw;
        CHECK_EQ(ext2_write_save_step(&w, 2, &done), EXT2_OK);
        CHECK(nw - before <= 2);
        calls++;
        if (!done) {
            CHECK(ext2_write_saving(&w));
            uint32_t n0 = nw;
            CHECK_EQ(ext2_write_create(&w, 2, "late", 4, 0644, &other), EXT2_EAGAIN);
            CHECK_EQ(ext2_write_pwrite(&w, ino, 0, (const uint8_t *)"x", 1), EXT2_EAGAIN);
            CHECK_EQ(ext2_write_unlink(&w, 2, "piece", 5), EXT2_EAGAIN);
            CHECK_EQ(ext2_write_mark_clean(&w), EXT2_EAGAIN);
            CHECK_EQ(nw, n0);
            CHECK_EQ(lookup(2, "late"), 0);
        }
        CHECK(calls < 200);
    }
    CHECK(calls > 3);
    CHECK(!ext2_write_saving(&w));
    CHECK_EQ(w.saves_done, saves + 1);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(clean());
    CHECK(reads_back(lookup(2, "piece"), 0, 20 * 1024, 3));

    /* keep_ino: the name goes, the inode stays until forget. */
    w.keep_ino = ino;
    w.kept = false;
    CHECK_EQ(ext2_write_unlink(&w, 2, "piece", 5), EXT2_OK);
    w.keep_ino = 0;
    CHECK(w.kept);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(lookup(2, "piece"), 0);
    CHECK_EQ(inode_raw(ino).links, 1);              /* still there: open */
    CHECK(reads_back(ino, 0, 20 * 1024, 3));
    ext2_write_forget(&w, ino);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(inode_raw(ino).links, 0);              /* deleted after its last close */
    CHECK(inode_raw(ino).dtime != 0);
    CHECK(clean());

    /* The clean state. */
    CHECK_EQ(g16(1024 + 58) & 1u, 0);
    CHECK_EQ(ext2_write_mark_clean(&w), EXT2_OK);
    CHECK_EQ(ext2_write_save(&w), EXT2_OK);
    CHECK_EQ(g16(1024 + 58) & 1u, 1);
}

/* 1.4 stage 10 (the owner's touch case): a truncate to the file's own size
 * (what the console's touch does on the disk) changes only the inode's
 * times: no block allocated or copied (no R2 copy), nothing pending, the
 * block map, size and i_blocks the same, and its save writes only the
 * inode table (class 3). */
static void test_truncate_same_size(void)
{
    mkimg(1024, 256, 1, 1, 2048, 64, 0);
    CHECK_EQ(mount_rw(), EXT2_OK);
    uint32_t ino = 0;
    CHECK_EQ(ext2_write_create(&w, 2, "t", 1, 0644, &ino), EXT2_OK);
    CHECK_EQ(write_all(ino, 0, 14 * 1024, 5), 14 * 1024);   /* 12 direct + the single indirect */
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    struct ext2_inode a = inode_raw(ino), b;
    uint32_t alloc0 = w.blocks_alloc, copies0 = w.copies, pend0 = w.pend_n, mark = nw;
    CHECK(a.block[EXT2_IND] != 0);
    CHECK_EQ(ext2_write_truncate(&w, ino, a.size), EXT2_OK);
    CHECK_EQ(w.blocks_alloc, alloc0);
    CHECK_EQ(w.copies, copies0);
    CHECK_EQ(w.pend_n, pend0);
    CHECK_EQ(save(), EXT2_OK);
    bool only3 = nw > mark;
    for (uint32_t i = mark; i < nw && i < WLOG; i++)
        only3 = only3 && wlog[i].cls == BLK_SV_INODE;
    CHECK(only3);
    b = inode_raw(ino);
    bool same = a.size == b.size && a.blocks512 == b.blocks512 && a.links == b.links && a.mode == b.mode;
    for (uint32_t i = 0; i < EXT2_NBLOCKS; i++)
        same = same && a.block[i] == b.block[i];
    CHECK(same);
    CHECK(reads_back(ino, 0, 14 * 1024, 5));
    CHECK(clean());
}

int main(void)
{
    printf("ext2_write (the write core over the save order)\n");
    RUN(test_mount_states);
    RUN(test_create_write);
    RUN(test_classes);
    RUN(test_owner_case);
    RUN(test_hard_link);
    RUN(test_copies_truncate);
    RUN(test_full_disk);
    RUN(test_folder_growth);
    RUN(test_dirs_rename);
    RUN(test_rename_r5);
    RUN(test_dir_index_flag);
    RUN(test_attr_blocks);
    RUN(test_tind_large);
    RUN(test_rev0_and_sizes);
    RUN(test_policy);
    RUN(test_recount);
    RUN(test_failed_save);
    RUN(test_checker_negative);
    RUN(test_pending_merge);
    RUN(test_save_steps);
    RUN(test_truncate_same_size);
    return unit_report("ext2_write");
}
