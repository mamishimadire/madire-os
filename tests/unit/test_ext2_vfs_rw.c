/* Unit tests for kernel/fs/ext2_vfs.c read-write and kernel/fs/ext2_vfs_rw.c
 * (1.4 stage 10; docs/plans/1.4.md, C5 with R4, C8, D9, risk 43): a small
 * ext2 image built here in memory (1 KiB blocks, one group, revision 1,
 * filetype), mounted read-write through the write core over the block
 * layer's write-back part, and mounted through the mount table on the RAM
 * filesystem's /home, as the kernel mounts it. Through the VFS calls: files
 * and folders made (their nodes found again by lookup), written across the
 * indirect block, read back, renamed (in a folder and between folders, over
 * a file), removed (their nodes freed once nothing holds them); after a
 * full save the raw image read again by a fresh read core gives the same
 * tree, and its bitmaps agree with the counts. -FS_EAGAIN while a save is
 * in progress (nothing changed) and a write that stops short when the room
 * for changes is used up (ext2_vfs_again), the rest after a save. An open
 * file whose name is removed stays readable and writable, and its inode is
 * deleted only after its last close. Eviction: more entries than the node
 * table holds are all listed (nodes freed on the way, never one in use),
 * and with every node held the listing answers ENOMEM. No writer, or a
 * read-only volume: EROFS. The write core's codes as errnos. The modules
 * are included directly. */
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/fs/ext2_core.c"
#include "kernel/storage/blk_core_save.c"
#include "kernel/fs/ext2_write.c"
#include "kernel/fs/ext2_vfs.c"
#include "kernel/fs/ext2_vfs_rw.c"
#include "kernel/fs/mount_core.h"
#include "kernel/fs/ramfs_core.h"

#define BS          1024u
#define NB          1025u               /* block 0 and one group of 1024 */
#define IPG         128u
#define ISZ         128u
#define IMG         (NB * BS)
#define CAP         2048u               /* chunks of changes */

static uint8_t img[IMG];

static void p16(uint64_t o, uint32_t v)
{
    img[o] = (uint8_t)v;
    img[o + 1] = (uint8_t)(v >> 8);
}

static void p32(uint64_t o, uint32_t v)
{
    p16(o, v & 0xFFFFu);
    p16(o + 2, v >> 16);
}

static uint32_t g16(uint64_t o)
{
    return (uint32_t)img[o] | (uint32_t)img[o + 1] << 8;
}

/* Blocks 1 superblock, 2 descriptors, 3 and 4 bitmaps, 5-20 the inode
 * table, 21 the root folder; inodes 1-10 reserved. */
static void mkimg(void)
{
    memset(img, 0, sizeof img);
    for (uint32_t b = 1; b <= 21; b++)
        img[3 * BS + (b - 1) / 8] |= (uint8_t)(1u << ((b - 1) % 8));
    for (uint32_t i = 1024; i < 8 * BS; i++)
        img[3 * BS + i / 8] |= (uint8_t)(1u << (i % 8));
    for (uint32_t i = 0; i < 10; i++)
        img[4 * BS + i / 8] |= (uint8_t)(1u << (i % 8));
    for (uint32_t i = IPG; i < 8 * BS; i++)
        img[4 * BS + i / 8] |= (uint8_t)(1u << (i % 8));
    uint64_t r = 5u * BS + 1u * ISZ;    /* inode 2 */
    p16(r, 0x41ED);
    p32(r + 4, BS);
    p16(r + 26, 2);
    p32(r + 28, 2);
    p32(r + 40, 21);
    p32(21u * BS, 2);
    p16(21u * BS + 4, 12);
    img[21u * BS + 6] = 1;
    img[21u * BS + 7] = 2;
    img[21u * BS + 8] = '.';
    p32(21u * BS + 12, 2);
    p16(21u * BS + 16, BS - 12);
    img[21u * BS + 18] = 2;
    img[21u * BS + 19] = 2;
    img[21u * BS + 20] = '.';
    img[21u * BS + 21] = '.';
    p32(2u * BS, 3);
    p32(2u * BS + 4, 4);
    p32(2u * BS + 8, 5);
    p16(2u * BS + 12, 1024 - 21);
    p16(2u * BS + 14, IPG - 10);
    p16(2u * BS + 16, 1);
    uint64_t s = 1024;
    p32(s + 0, IPG);
    p32(s + 4, NB);
    p32(s + 12, 1024 - 21);
    p32(s + 16, IPG - 10);
    p32(s + 20, 1);
    p32(s + 32, 1024);
    p32(s + 36, 1024);
    p32(s + 40, IPG);
    p16(s + 56, 0xEF53);
    p16(s + 58, 1);
    p16(s + 60, 1);
    p32(s + 76, 1);
    p32(s + 84, 11);
    p16(s + 88, ISZ);
    p32(s + 96, EXT2_INCOMPAT_FILETYPE);
    p32(s + 100, EXT2_RO_COMPAT_SPARSE_SUPER | EXT2_RO_COMPAT_LARGE_FILE);
    memcpy(img + s + 120, "madire-test", 11);
}

/* ---- the disk, the write core, the VFS ---- */

static int d_read(void *ctx, uint64_t off, uint32_t len, uint8_t *buf)
{
    (void)ctx;
    if (off > IMG || len > IMG - off)
        return -1;
    memcpy(buf, img + off, len);
    return 0;
}

static int d_write(void *ctx, uint64_t off, uint32_t len, const uint8_t *buf, uint32_t cls)
{
    (void)ctx;
    (void)cls;
    if (off > IMG || len > IMG - off)
        return -1;
    memcpy(img + off, buf, len);
    return 0;
}

static int d_flush(void *ctx)
{
    (void)ctx;
    return 0;
}

static const struct blk_sv_ops d_ops = { d_read, d_write, d_flush, 0 };
static struct blk_sv sv;
static struct blk_sv_ent ents[CAP];
static uint8_t sdata[CAP * BLK_SV_CHUNK];
static uint8_t srun[BLK_SV_RUN_MAX * BLK_SV_CHUNK];
static struct ext2_wfs w;
static struct ext2_bufs w_rbufs;        /* 1.5 stage 5: the cores' buffers and tables (ext2_write_init) */
static struct ext2_wbufs w_wbufs;
static struct ext2_vfs_writer wr;

static uint32_t t_secs(void *ctx)
{
    (void)ctx;
    return 1800000000u;
}

static const struct ext2_w_ops wops = { t_secs, 0, 0 };

static int live;

static void *t_alloc(size_t bytes)
{
    live++;
    return malloc(bytes);
}

static void t_free(void *p)
{
    live--;
    free(p);
}

static unsigned full_calls, reading_on, reading_off;

static void t_full(uint32_t max_nodes)
{
    (void)max_nodes;
    full_calls++;
}

static void t_reading(bool on)
{
    if (on)
        reading_on++;
    else
        reading_off++;
}

static const struct ramfs_mem r_mem = { t_alloc, t_free };
static const struct ext2_vfs_mem e_mem = { t_alloc, t_free, t_full, t_reading };
static struct ramfs rfs;
static struct ext2_vfs ev;
static struct vfs v;
static struct vnode *R, *H;

static int64_t t_now(void)
{
    return 77;
}

/* The image mounted read-write on the RAM /home; `cap` chunks of room. */
static void setup(uint32_t max_nodes, uint32_t cap)
{
    mkimg();
    full_calls = 0;
    blk_sv_init(&sv, &d_ops, ents, sdata, cap, srun, IMG);
    ext2_write_init(&w, &w_rbufs, &w_wbufs);
    CHECK_EQ(ext2_write_mount(&w, &sv, IMG, &wops, true), EXT2_OK);
    CHECK(w.rw);
    CHECK_EQ(ramfs_init(&rfs, &r_mem, 64), 0);
    vfs_mount(&v, &ramfs_ops, &rfs, &rfs.root->vn, t_now);
    R = v.root;
    CHECK_EQ(vfs_create(&v, R, "/home", VNODE_DIR, 0755, &H), 0);
    CHECK_EQ(ext2_vfs_init(&ev, &w.fs, &e_mem, max_nodes), 0);
    ext2_vfs_rw_init(&wr, &w);
    ext2_vfs_set_writer(&ev, &wr);
    CHECK_EQ(mount_add(&v.mounts, &ext2_vfs_ops, &ev, &ev.root->vn, H), 1);
}

static struct vnode *get(const char *path)
{
    struct vnode *n = NULL;
    return vfs_lookup(&v, R, path, &n) == 0 ? n : NULL;
}

/* A VFS call tried again after a save while it answers EAGAIN (as the
 * kernel's callers do, at most 3 saves). */
#define AGAIN(call) ({ int r_ = (call); for (int t_ = 0; r_ == -FS_EAGAIN && t_ < 3; t_++) { \
                       CHECK_EQ(ext2_write_save(&w), EXT2_OK); r_ = (call); } r_; })

static uint8_t pat[64 * 1024], out[64 * 1024];

static void mkpat(uint32_t seed, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        pat[i] = (uint8_t)(seed * 17u + i * 3u + (i >> 10));
}

/* All of buf written at off through the VFS, saving when asked. */
static bool write_all(struct vnode *n, uint64_t off, const uint8_t *buf, uint32_t len)
{
    uint32_t done = 0;
    int tries = 0;
    while (done < len && tries < 8) {
        int64_t put = vfs_write(&v, n, off + done, buf + done, len - done);
        if (put > 0)
            done += (uint32_t)put;
        if (put == -FS_EAGAIN || (put >= 0 && done < len && ext2_vfs_again(&ev))) {
            CHECK_EQ(ext2_write_save(&w), EXT2_OK);
            tries++;
            continue;
        }
        if (put <= 0)
            return false;
    }
    return done == len;
}

/* The raw image, read by a fresh read core: the inode of path's last
 * component under the root (one level, or two through `dir`). */
static struct ext2_fs rf;
static struct ext2_bufs rf_bufs;        /* 1.5 stage 5 */
static const struct ext2_ops raw_ops = { d_read, 0 };

static uint32_t raw_lookup(const char *dir, const char *name)
{
    struct ext2_inode d;
    uint32_t ino = EXT2_ROOT_INO;
    memset(&d, 0, sizeof d);
    ext2_init(&rf, &rf_bufs);
    if (ext2_mount(&rf, &raw_ops, IMG) != EXT2_OK || ext2_inode_get(&rf, ino, &d) != EXT2_OK)
        return 0;
    if (dir && (ext2_lookup(&rf, &d, dir, (uint32_t)strlen(dir), &ino) || ext2_inode_get(&rf, ino, &d)))
        return 0;
    if (ext2_lookup(&rf, &d, name, (uint32_t)strlen(name), &ino))
        return 0;
    return ino;
}

static struct ext2_inode raw_inode(uint32_t ino)
{
    struct ext2_inode in;
    memset(&in, 0, sizeof in);
    ext2_init(&rf, &rf_bufs);
    if (ext2_mount(&rf, &raw_ops, IMG) == EXT2_OK)
        ext2_inode_get(&rf, ino, &in);
    return in;
}

static bool raw_reads(uint32_t ino, const uint8_t *want, uint32_t len)
{
    struct ext2_inode in = raw_inode(ino);
    return in.size == len && ext2_read(&rf, &in, 0, out, len) == (int64_t)len && memcmp(out, want, len) == 0;
}

/* The bitmaps' free bits against the descriptor's and the superblock's
 * counts. */
static bool counts_agree(void)
{
    uint32_t fb = 0, fi = 0;
    for (uint32_t i = 0; i < 1024; i++)
        fb += !((img[3 * BS + i / 8] >> (i % 8)) & 1u);
    for (uint32_t i = 0; i < IPG; i++)
        fi += !((img[4 * BS + i / 8] >> (i % 8)) & 1u);
    return fb == g16(2u * BS + 12) && fi == g16(2u * BS + 14) && fb == g16(1024 + 12) && fi == g16(1024 + 16);
}

/* ---- the cases ---- */

static void test_create_write_rename_remove(void)
{
    setup(EXT2_VFS_NODES, CAP);
    struct vnode *f = NULL, *d = NULL, *g = NULL;
    CHECK_EQ(AGAIN(vfs_create(&v, R, "/home/notes.txt", VNODE_FILE, 0644, &f)), 0);
    CHECK(f && f->fs_id == 1 && f->type == VNODE_FILE && f->size == 0 && f->nlink == 1);
    CHECK(get("/home/notes.txt") == f);                 /* one node per entry */
    CHECK_EQ(vfs_create(&v, R, "/home/notes.txt", VNODE_FILE, 0644, &g), -FS_EEXIST);
    CHECK_EQ(AGAIN(vfs_create(&v, R, "/home/docs", VNODE_DIR, 0755, &d)), 0);
    CHECK(d && d->type == VNODE_DIR && vfs_subdirs(&v, &ev.root->vn) == 1);
    mkpat(1, 20000);                                    /* through the single indirect block */
    CHECK(write_all(f, 0, pat, 20000));
    CHECK_EQ(f->size, 20000);
    CHECK_EQ(vfs_read(&v, f, 0, out, 20000), 20000);
    CHECK(memcmp(out, pat, 20000) == 0);
    CHECK(reading_on > 0 && reading_on == reading_off);
    CHECK(vfs_pages(&v, f) >= 5);
    /* Renamed in its folder, then into docs, over a file there. */
    CHECK_EQ(AGAIN(vfs_rename(&v, R, "/home/notes.txt", R, "/home/n2.txt")), 0);
    CHECK(get("/home/n2.txt") == f && !get("/home/notes.txt"));
    CHECK_EQ(AGAIN(vfs_create(&v, R, "/home/docs/old.txt", VNODE_FILE, 0644, &g)), 0);
    int before = live;
    CHECK_EQ(AGAIN(vfs_rename(&v, R, "/home/n2.txt", R, "/home/docs/old.txt")), 0);
    CHECK(get("/home/docs/old.txt") == f && f->parent == d);
    CHECK(live < before);                               /* the replaced node freed */
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    uint32_t ino = raw_lookup("docs", "old.txt");
    CHECK(ino == (uint32_t)f->ino && raw_reads(ino, pat, 20000));
    CHECK_EQ(raw_lookup(0, "n2.txt"), 0);
    CHECK(counts_agree());
    /* Removed: unlink, then rmdir (ENOTEMPTY first). */
    CHECK_EQ(vfs_rmdir(&v, R, "/home/docs"), -FS_ENOTEMPTY);
    uint32_t count = ev.count;
    CHECK_EQ(AGAIN(vfs_unlink(&v, R, "/home/docs/old.txt")), 0);
    CHECK_EQ(ev.count, count - 1);
    CHECK_EQ(AGAIN(vfs_rmdir(&v, R, "/home/docs")), 0);
    CHECK(!get("/home/docs"));
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(raw_lookup(0, "docs"), 0);
    CHECK_EQ(raw_inode(ino).links, 0);
    CHECK(counts_agree());
    CHECK_EQ(g16(1024 + 12), 1024 - 21);                /* every block back */
    CHECK_EQ(g16(1024 + 16), IPG - 10);
}

static void test_again(void)
{
    /* While a save is in progress: EAGAIN, nothing changed. */
    setup(EXT2_VFS_NODES, CAP);
    struct vnode *f = NULL;
    CHECK_EQ(AGAIN(vfs_create(&v, R, "/home/a", VNODE_FILE, 0644, &f)), 0);
    mkpat(2, 4096);
    CHECK(write_all(f, 0, pat, 4096));
    bool done = false;
    CHECK_EQ(ext2_write_save_step(&w, 1, &done), EXT2_OK);
    CHECK(!done && ext2_write_saving(&w));
    struct vnode *g = NULL;
    uint32_t count = ev.count;
    CHECK_EQ(vfs_create(&v, R, "/home/b", VNODE_FILE, 0644, &g), -FS_EAGAIN);
    CHECK_EQ(vfs_write(&v, f, 0, "x", 1), -FS_EAGAIN);
    CHECK_EQ(vfs_unlink(&v, R, "/home/a"), -FS_EAGAIN);
    CHECK_EQ(vfs_rename(&v, R, "/home/a", R, "/home/c"), -FS_EAGAIN);
    CHECK_EQ(vfs_truncate_step(&v, f, 0, 1), -FS_EAGAIN);
    CHECK(get("/home/a") == f && !get("/home/b") && f->size == 4096 && ev.count == count);
    while (!done)
        CHECK_EQ(ext2_write_save_step(&w, 1, &done), EXT2_OK);
    CHECK_EQ(vfs_create(&v, R, "/home/b", VNODE_FILE, 0644, &g), 0);

    /* Little room: a 64 KiB write stops short (again), the rest after saves. */
    setup(EXT2_VFS_NODES, 400);
    CHECK_EQ(AGAIN(vfs_create(&v, R, "/home/big", VNODE_FILE, 0644, &f)), 0);
    mkpat(3, 64 * 1024);
    int64_t put = vfs_write(&v, f, 0, pat, 64 * 1024);
    CHECK(put > 0 && put < 64 * 1024);
    CHECK(ext2_vfs_again(&ev));
    CHECK(!ext2_vfs_again(&ev));                        /* read once */
    CHECK(write_all(f, (uint64_t)put, pat + put, 64 * 1024 - (uint32_t)put));
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK(raw_reads(raw_lookup(0, "big"), pat, 64 * 1024));
    CHECK(counts_agree());
}

static void test_orphan(void)
{
    setup(EXT2_VFS_NODES, CAP);
    struct vnode *f = NULL;
    CHECK_EQ(AGAIN(vfs_create(&v, R, "/home/open.txt", VNODE_FILE, 0644, &f)), 0);
    mkpat(4, 3000);
    CHECK(write_all(f, 0, pat, 3000));
    uint32_t ino = (uint32_t)f->ino;
    vfs_hold(f);                                        /* open */
    CHECK_EQ(AGAIN(vfs_unlink(&v, R, "/home/open.txt")), 0);
    CHECK(f->nlink == 0 && f->parent == NULL && ev.nodes != NULL);
    CHECK(ext2_vn_node(f)->orphan);
    CHECK_EQ(vfs_pages(&v, f), 0);                      /* no truncate in steps by the VFS */
    CHECK(!get("/home/open.txt"));
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(raw_lookup(0, "open.txt"), 0);
    CHECK_EQ(raw_inode(ino).links, 1);                  /* kept while open */
    CHECK_EQ(vfs_read(&v, f, 0, out, 3000), 3000);
    CHECK(memcmp(out, pat, 3000) == 0);
    CHECK(write_all(f, 3000, pat, 100));                /* still writable */
    uint32_t count = ev.count;
    vfs_drop(&v, f);                                    /* the last close */
    CHECK_EQ(ev.count, count - 1);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(raw_inode(ino).links, 0);
    CHECK(counts_agree());
    CHECK_EQ(g16(1024 + 12), 1024 - 21);

    /* A rename over an open file: the same. */
    struct vnode *a = NULL, *b = NULL;
    CHECK_EQ(AGAIN(vfs_create(&v, R, "/home/a", VNODE_FILE, 0644, &a)), 0);
    CHECK_EQ(AGAIN(vfs_create(&v, R, "/home/b", VNODE_FILE, 0644, &b)), 0);
    CHECK(write_all(b, 0, pat, 2000));
    uint32_t bino = (uint32_t)b->ino;
    vfs_hold(b);
    CHECK_EQ(AGAIN(vfs_rename(&v, R, "/home/a", R, "/home/b")), 0);
    CHECK(get("/home/b") == a && b->nlink == 0);
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(raw_inode(bino).links, 1);
    CHECK_EQ(ext2_vfs_forget_orphans(&ev), 1);          /* as before Restart */
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    CHECK_EQ(raw_inode(bino).links, 0);
    vfs_drop(&v, b);
    CHECK(counts_agree());
}

static void test_eviction(void)
{
    /* 60 files, a table of 32 nodes: all listed, nodes evicted. */
    setup(EXT2_VFS_NODES, CAP);
    char name[32];
    for (int i = 0; i < 60; i++) {
        snprintf(name, sizeof name, "/home/f%02d", i);
        CHECK_EQ(AGAIN(vfs_create(&v, R, name, VNODE_FILE, 0644, NULL)), 0);
    }
    CHECK_EQ(ext2_write_sync(&w), EXT2_OK);
    /* A fresh view of the saved volume with a small table (in place of the
     * mounted one: its root as mount_add sets a root). */
    CHECK_EQ(ext2_vfs_init(&ev, &w.fs, &e_mem, 32), 0);
    ext2_vfs_set_writer(&ev, &wr);
    v.mounts.m[1].root = &ev.root->vn;
    ev.root->vn.fs_id = 1;
    ev.root->vn.parent = &ev.root->vn;
    struct vnode *root = &ev.root->vn;
    uint32_t seen = 0;
    for (struct vnode *e = vfs_entry_from(&v, root, 0); e; e = vfs_entry_next(&v, root, e))
        seen++;
    CHECK_EQ(seen, 60);
    CHECK_EQ(vfs_lookup_error(&v, root), 0);
    CHECK(ev.count <= 32);
    CHECK(ev.evicted >= 60 - 31);
    CHECK_EQ(full_calls, 0);
    /* Every node held: the listing stops with ENOMEM. */
    struct vnode *held[64];
    uint32_t nh = 0;
    for (struct vnode *e = vfs_entry_from(&v, root, 0); e && nh < 64; e = vfs_entry_next(&v, root, e)) {
        vfs_hold(e);
        held[nh++] = e;
    }
    CHECK(nh < 60);
    CHECK_EQ(vfs_lookup_error(&v, root), -FS_ENOMEM);
    CHECK_EQ(full_calls, 1);
    for (uint32_t i = 0; i < nh; i++)
        CHECK(held[i]->nlink == 1 && held[i]->opens == 1);  /* none of the held evicted */
    for (uint32_t i = 0; i < nh; i++)
        vfs_drop(&v, held[i]);
    seen = 0;
    for (struct vnode *e = vfs_entry_from(&v, root, 0); e; e = vfs_entry_next(&v, root, e))
        seen++;
    CHECK_EQ(seen, 60);
}

static void test_readonly_and_codes(void)
{
    setup(EXT2_VFS_NODES, CAP);
    struct vnode *f = NULL;
    CHECK_EQ(AGAIN(vfs_create(&v, R, "/home/x", VNODE_FILE, 0644, &f)), 0);
    ext2_vfs_set_writer(&ev, NULL);                     /* stage 8's read-only */
    CHECK_EQ(vfs_writable(&v, f), -FS_EROFS);
    CHECK_EQ(vfs_create(&v, R, "/home/y", VNODE_FILE, 0644, NULL), -FS_EROFS);
    CHECK_EQ(vfs_unlink(&v, R, "/home/x"), -FS_EROFS);
    ext2_vfs_set_writer(&ev, &wr);
    w.rw = false;                                       /* the volume read-only (a failed save) */
    CHECK_EQ(vfs_writable(&v, f), -FS_EROFS);
    CHECK_EQ(vfs_write(&v, f, 0, "a", 1), -FS_EROFS);
    w.rw = true;
    ev.closed = true;                                   /* the save before Restart */
    CHECK_EQ(vfs_create(&v, R, "/home/y", VNODE_FILE, 0644, NULL), -FS_EROFS);
    ev.closed = false;
    CHECK_EQ(vfs_create(&v, R, "/home/y", VNODE_FILE, 0644, NULL), 0);
    CHECK_EQ(ext2_vfs_rw_errno(EXT2_OK), 0);
    CHECK_EQ(ext2_vfs_rw_errno(EXT2_EAGAIN), -FS_EAGAIN);
    CHECK_EQ(ext2_vfs_rw_errno(EXT2_ENOSPC), -FS_ENOSPC);
    CHECK_EQ(ext2_vfs_rw_errno(EXT2_EMLINK), -FS_EMLINK);
    CHECK_EQ(ext2_vfs_rw_errno(EXT2_ENOTEMPTY), -FS_ENOTEMPTY);
    CHECK_EQ(ext2_vfs_rw_errno(EXT2_ECORRUPT), -FS_EIO);
    CHECK_EQ(ext2_vfs_rw_errno(EXT2_EIO), -FS_EIO);
    CHECK_EQ(ext2_vfs_rw_errno(EXT2_ENOENT), -FS_ENOENT);
}

int main(void)
{
    RUN(test_create_write_rename_remove);
    RUN(test_again);
    RUN(test_orphan);
    RUN(test_eviction);
    RUN(test_readonly_and_codes);
    return unit_report("ext2_vfs_rw");
}
