/* Unit tests for kernel/fs/ext2_vfs.c (ext2 behind the VFS, 1.4 stage 8;
 * docs/plans/1.4.md, C8, C9, D16): a small ext2 image built here in memory
 * from the specification's layout (1 KiB blocks, one group, revision 1,
 * filetype), mounted read-only through the mount table on the RAM
 * filesystem's /home, as the kernel mounts it. Lookups across the mount
 * ("/home" is the volume's root, ".." of it is "/"), one node per entry
 * (two lookups give the same node; a hard link is its own entry), the
 * folder listing in on-disk order without "." and "..", positions, data
 * through direct and indirect blocks and holes, link counts and pages,
 * getdents64's records and getcwd across the mount; EROFS for every change
 * at Linux's place (EEXIST first for a taken name; EXDEV first between the
 * filesystems), nothing changed in the image or the RAM tree; the
 * per-filesystem file-size limit (the RAM filesystem's 2 MiB, ext2's from
 * its block map, at most 4 GiB - 1); the node table full; a failed disk
 * read (EIO); no memory. The modules are included directly. */
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/fs/ext2_core.c"
#include "kernel/fs/ext2_vfs.c"
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/mount_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/shell/shell.h"

#define BS          1024u
#define NBLOCKS     256u
#define IMG_BYTES   (NBLOCKS * BS)

static uint8_t img[IMG_BYTES], img0[IMG_BYTES];
static int fail_reads;                  /* > 0: reads fail */
static unsigned reads;

static int t_read(void *ctx, uint64_t off, uint32_t len, uint8_t *buf)
{
    (void)ctx;
    reads++;
    if (fail_reads || off > IMG_BYTES || len > IMG_BYTES - off)
        return -1;
    memcpy(buf, img + off, len);
    return 0;
}

static void p16(uint32_t off, uint32_t v)
{
    img[off] = (uint8_t)v;
    img[off + 1] = (uint8_t)(v >> 8);
}

static void p32(uint32_t off, uint32_t v)
{
    p16(off, v & 0xFFFFu);
    p16(off + 2, v >> 16);
}

/* Inode ino (128 bytes in the table at block 5). */
static uint32_t ioff(uint32_t ino)
{
    return 5u * BS + (ino - 1u) * 128u;
}

static void inode(uint32_t ino, uint32_t mode, uint32_t size, uint32_t links, uint32_t blocks512)
{
    uint32_t o = ioff(ino);
    p16(o, mode);
    p32(o + 4, size);
    p32(o + 8, 1000 + ino);             /* atime */
    p32(o + 12, 2000 + ino);            /* ctime */
    p32(o + 16, 3000 + ino);            /* mtime */
    p16(o + 26, links);
    p32(o + 28, blocks512);
}

static void iblock(uint32_t ino, uint32_t i, uint32_t b)
{
    p32(ioff(ino) + 40 + 4 * i, b);
}

/* A folder block: entries added in order, the last one's rec_len to the
 * block's end. */
static uint32_t d_blk, d_pos, d_last;

static void dir_begin(uint32_t b)
{
    d_blk = b;
    d_pos = 0;
    d_last = 0;
}

static void dent(uint32_t ino, const char *name, uint32_t ft)
{
    uint32_t nl = (uint32_t)strlen(name), rec = (8 + nl + 3) & ~3u;
    uint32_t o = d_blk * BS + d_pos;
    p32(o, ino);
    p16(o + 4, rec);
    img[o + 6] = (uint8_t)nl;
    img[o + 7] = (uint8_t)ft;
    memcpy(img + o + 8, name, nl);
    d_last = d_pos;
    d_pos += rec;
}

static void dir_end(void)
{
    p16(d_blk * BS + d_last + 4, BS - d_last);
}

/* The byte the image holds at file position i of the file tagged t. */
static uint8_t pat(uint32_t t, uint64_t i)
{
    return (uint8_t)(t * 31u + i * 7u + (i >> 10));
}

static void data(uint32_t b, uint32_t t, uint64_t from)
{
    for (uint32_t i = 0; i < BS; i++)
        img[b * BS + i] = pat(t, from + i);
}

/* The image: "/" (2) holds a.txt (12, 1500 bytes, 2 links), sub/ (13)
 * with b.txt (16, "hello"), hole.bin (14: 3000 bytes, only its third block
 * written), big.bin (15: 14 KiB through the single indirect block) and
 * hard (12 again). On-disk order: a.txt, sub, hole.bin, big.bin, hard. */
static void build(void)
{
    memset(img, 0, sizeof(img));
    uint32_t s = 1024;
    p32(s + 0, 32);                     /* inodes */
    p32(s + 4, NBLOCKS);
    p32(s + 12, 100);                   /* free blocks (report only) */
    p32(s + 16, 16);                    /* free inodes */
    p32(s + 20, 1);                     /* first data block */
    p32(s + 24, 0);                     /* 1 KiB blocks */
    p32(s + 32, 8192);                  /* blocks per group */
    p32(s + 40, 32);                    /* inodes per group */
    p16(s + 56, EXT2_MAGIC);
    p16(s + 58, EXT2_STATE_VALID);
    p32(s + 76, 1);                     /* revision 1 */
    p32(s + 84, 11);
    p16(s + 88, 128);
    p32(s + 96, EXT2_INCOMPAT_FILETYPE);
    memcpy(img + s + 120, "madire-test", 11);
    uint32_t g = 2 * BS;                /* the group descriptor */
    p32(g, 3);
    p32(g + 4, 4);
    p32(g + 8, 5);                      /* the inode table: blocks 5-8 */

    inode(2, 040755, BS, 3, 2);
    iblock(2, 0, 20);
    dir_begin(20);
    dent(2, ".", 2);
    dent(2, "..", 2);
    dent(12, "a.txt", 1);
    dent(13, "sub", 2);
    dent(14, "hole.bin", 1);
    dent(15, "big.bin", 1);
    dent(12, "hard", 1);
    dir_end();

    inode(12, 0100644, 1500, 2, 4);
    iblock(12, 0, 30);
    iblock(12, 1, 31);
    data(30, 1, 0);
    data(31, 1, BS);

    inode(13, 040750, BS, 2, 2);
    iblock(13, 0, 21);
    dir_begin(21);
    dent(13, ".", 2);
    dent(2, "..", 2);
    dent(16, "b.txt", 1);
    dir_end();
    inode(16, 0100600, 5, 1, 2);
    iblock(16, 0, 32);
    memcpy(img + 32 * BS, "hello", 5);

    inode(14, 0100644, 3000, 1, 2);
    iblock(14, 2, 33);
    data(33, 3, 2 * BS);

    inode(15, 0100644, 14 * BS, 1, 30);
    for (uint32_t i = 0; i < 12; i++) {
        iblock(15, i, 40 + i);
        data(40 + i, 4, (uint64_t)i * BS);
    }
    iblock(15, EXT2_IND, 52);
    p32(52 * BS, 53);
    p32(52 * BS + 4, 54);
    data(53, 4, 12u * BS);
    data(54, 4, 13u * BS);
}

static int live;                        /* heap blocks in use */
static int alloc_left = -1;             /* >= 0: allocations before one fails */

static void *t_alloc(size_t bytes)
{
    if (alloc_left == 0)
        return NULL;
    if (alloc_left > 0)
        alloc_left--;
    live++;
    return malloc(bytes);
}

static void t_free(void *p)
{
    live--;
    free(p);
}

static unsigned full_calls, full_max;   /* the node table's "full" notices */

static void t_full(uint32_t max_nodes)
{
    full_calls++;
    full_max = max_nodes;
}

static const struct ramfs_mem r_mem = { t_alloc, t_free };
static const struct ext2_vfs_mem e_mem = { .alloc = t_alloc, .free = t_free, .full = t_full };
static const struct ext2_ops t_ops = { t_read, 0 };

static struct ramfs rfs;
static struct ext2_fs efs;
static struct ext2_bufs efs_bufs;       /* 1.5 stage 5: the read core's buffers (ext2_init) */
static struct ext2_vfs ev;
static struct vfs v;
static struct vnode *R, *H;             /* "/" and the RAM /home under the mount */

static int64_t t_now(void)
{
    return 77;
}

static struct vnode *get(const char *path)
{
    struct vnode *n = NULL;
    return vfs_lookup(&v, R, path, &n) == 0 ? n : NULL;
}

/* "/", "/home", "/tmp" in RAM, the image mounted on /home. */
static void setup(uint32_t max_nodes)
{
    build();
    memcpy(img0, img, sizeof(img));
    fail_reads = 0;
    alloc_left = -1;
    CHECK_EQ(ramfs_init(&rfs, &r_mem, 64), 0);
    vfs_mount(&v, &ramfs_ops, &rfs, &rfs.root->vn, t_now);
    R = v.root;
    CHECK_EQ(vfs_create(&v, R, "/home", VNODE_DIR, 0755, &H), 0);
    CHECK_EQ(vfs_create(&v, R, "/tmp", VNODE_DIR, 01777, NULL), 0);
    ext2_init(&efs, &efs_bufs);
    CHECK_EQ(ext2_mount(&efs, &t_ops, IMG_BYTES), EXT2_OK);
    CHECK_EQ(ext2_vfs_init(&ev, &efs, &e_mem, max_nodes), 0);
    CHECK_EQ(mount_add(&v.mounts, &ext2_vfs_ops, &ev, &ev.root->vn, H), 1);
}

static void test_walk(void)
{
    setup(EXT2_VFS_NODES);
    struct vnode *root = &ev.root->vn, *a = get("/home/a.txt"), *sub = get("/home/sub");
    CHECK(get("/home") == root && get("/home/") == root);
    CHECK(root->fs_id == 1 && root->type == VNODE_DIR && root->ino == EXT2_ROOT_INO);
    CHECK(a && a->fs_id == 1 && a->type == VNODE_FILE && a->ino == 12 && a->size == 1500);
    CHECK(a && a->mode == 0644 && a->nlink == 2 && a->atime == 1012 && a->ctime == 2012 && a->mtime == 3012);
    CHECK(get("/home/a.txt") == a && get("/home/./sub/../a.txt") == a);    /* one node per entry */
    struct vnode *hard = get("/home/hard");
    CHECK(hard && hard != a && hard->ino == 12 && hard->size == 1500);       /* its own entry */
    CHECK(sub && sub->type == VNODE_DIR && sub->mode == 0750 && sub->parent == root);
    CHECK(get("/home/sub/..") == root && get("/home/..") == R && get("/home/../tmp") == get("/tmp"));
    struct vnode *b = get("/home/sub/b.txt");
    CHECK(b && b->size == 5 && b->ino == 16 && b->parent == sub);
    struct vnode *n = NULL;
    CHECK_EQ(vfs_lookup(&v, R, "/home/nosuch", &n), -FS_ENOENT);
    CHECK_EQ(vfs_lookup(&v, R, "/home/a.txt/", &n), -FS_ENOTDIR);
    CHECK_EQ(vfs_lookup(&v, R, "/home/.", &n), 0);
    CHECK(n == root);
    CHECK_EQ(vfs_lookup(&v, sub, "b.txt", &n), 0);                          /* relative, from an ext2 folder */
    CHECK(n == b);
    CHECK(get("/home/sub/b.txt/x") == NULL && get("/home/a.txt/.") == NULL);
    CHECK_EQ(ev.count, 5);              /* root, a.txt, sub, hard, b.txt (a missing name makes none) */
}

static void test_data(void)
{
    setup(EXT2_VFS_NODES);
    static uint8_t buf[16 * BS];
    struct vnode *a = get("/home/a.txt"), *hole = get("/home/hole.bin"), *big = get("/home/big.bin");
    CHECK_EQ(vfs_read(&v, a, 0, buf, sizeof(buf)), 1500);
    bool ok = true;
    for (uint32_t i = 0; i < 1500; i++)
        ok = ok && buf[i] == pat(1, i);
    CHECK(ok);
    CHECK_EQ(vfs_read(&v, a, 1400, buf, 200), 100);
    CHECK(buf[0] == pat(1, 1400) && buf[99] == pat(1, 1499));
    CHECK_EQ(vfs_read(&v, a, 1500, buf, 10), 0);
    CHECK_EQ(vfs_read(&v, hole, 0, buf, sizeof(buf)), 3000);
    ok = true;
    for (uint32_t i = 0; i < 3000; i++)
        ok = ok && buf[i] == (i < 2 * BS ? 0 : pat(3, i));
    CHECK(ok);
    CHECK_EQ(vfs_read(&v, big, 0, buf, sizeof(buf)), 14 * BS);
    ok = true;
    for (uint32_t i = 0; i < 14 * BS; i++)
        ok = ok && buf[i] == pat(4, i);
    CHECK(ok);
    CHECK_EQ(vfs_read(&v, get("/home/sub"), 0, buf, 10), -FS_EISDIR);
    CHECK_EQ(vfs_read(&v, get("/home/sub/b.txt"), 0, buf, 10), 5);
    CHECK(memcmp(buf, "hello", 5) == 0);
    /* Pages: i_blocks / 8, rounded up; 0 for a folder (as the RAM filesystem). */
    CHECK_EQ(vfs_pages(&v, a), 1);
    CHECK_EQ(vfs_pages(&v, big), 4);
    CHECK_EQ(vfs_pages(&v, get("/home/sub")), 0);
    /* Subfolders from the link count. */
    CHECK_EQ(vfs_subdirs(&v, get("/home")), 1);
    CHECK_EQ(vfs_subdirs(&v, get("/home/sub")), 0);
    CHECK_EQ(vfs_subdirs(&v, R), 2);    /* the RAM root still counts its entries */
}

static void test_listing(void)
{
    setup(EXT2_VFS_NODES);
    static const char *const want[] = { "a.txt", "sub", "hole.bin", "big.bin", "hard" };
    struct vnode *root = get("/home");
    unsigned i = 0;
    uint64_t last = 0;
    bool ok = true;
    for (struct vnode *e = vfs_entry_from(&v, root, 0); e; e = vfs_entry_next(&v, root, e), i++) {
        size_t len = 0;
        const char *s = vfs_entry_name(&v, e, &len);
        uint64_t pos = vfs_entry_pos(&v, e);
        ok = ok && i < 5 && len == strlen(want[i]) && memcmp(s, want[i], len) == 0 && pos > last &&
             e->fs_id == 1 && e->parent == root;
        last = pos;
    }
    CHECK_EQ(i, 5);
    CHECK(ok);
    /* Positions: the entry's byte in its folder ("." 0, ".." 12, a.txt 24,
     * sub 40, hole.bin 52, big.bin 68, hard 84). */
    struct vnode *a = get("/home/a.txt"), *sub = get("/home/sub");
    CHECK(vfs_entry_from(&v, root, 0) == a);    /* the same node as the lookup's */
    CHECK_EQ(vfs_entry_pos(&v, a), 24);
    CHECK_EQ(vfs_entry_pos(&v, sub), 40);
    CHECK_EQ(vfs_entry_pos(&v, get("/home/hard")), 84);
    CHECK(vfs_entry_from(&v, root, vfs_entry_pos(&v, sub) + 1) == get("/home/hole.bin"));
    CHECK(vfs_entry_from(&v, root, 1) == a);
    CHECK(vfs_entry_from(&v, root, 0xFFFFFFFFu) == NULL);
    size_t len = 99;
    vfs_entry_name(&v, root, &len);
    CHECK_EQ(len, 0);
    /* getdents64 across the mount: ".", ".." (the RAM root's node), then
     * the five entries; d_off is position + 3. */
    static uint8_t buf[2048];
    uint64_t next = 0;
    bool more = true;
    uint32_t used = fsys_dir_list(&v, root, 0, buf, sizeof(buf), &next, &more);
    uint32_t recs = 0, pos = 0;
    uint64_t dotdot = 0, first_off = 0;
    while (pos + FSYS_DIRENT_HEAD <= used) {
        uint32_t rl = (uint32_t)buf[pos + 16] | ((uint32_t)buf[pos + 17] << 8);
        uint64_t ino, off;
        memcpy(&ino, buf + pos, 8);
        memcpy(&off, buf + pos + 8, 8);
        if (recs == 1)
            dotdot = ino;
        if (recs == 2)
            first_off = off;
        recs++;
        pos += rl;
    }
    CHECK_EQ(recs, 7);
    CHECK(!more && dotdot == R->ino && first_off == 24 + 3);
    /* getcwd across the mount. */
    char path[64];
    CHECK_EQ(fsys_cwd_path_v(&v, sub, path, sizeof(path)), 10);
    CHECK(strcmp(path, "/home/sub") == 0);
    CHECK_EQ(fsys_cwd_path_v(&v, root, path, sizeof(path)), 6);
    CHECK(strcmp(path, "/home") == 0);
}

static void test_readonly(void)
{
    setup(EXT2_VFS_NODES);
    struct vnode *a = get("/home/a.txt"), *root = get("/home"), *n = NULL;
    uint32_t rnodes = rfs.nodes, count = ev.count;
    CHECK_EQ(vfs_writable(&v, root), -FS_EROFS);
    CHECK_EQ(vfs_writable(&v, a), -FS_EROFS);
    CHECK_EQ(vfs_writable(&v, R), 0);
    CHECK_EQ(vfs_writable(&v, H), 0);
    CHECK_EQ(vfs_create(&v, R, "/home/new.txt", VNODE_FILE, 0644, &n), -FS_EROFS);
    CHECK_EQ(vfs_create(&v, R, "/home/newdir", VNODE_DIR, 0755, &n), -FS_EROFS);
    CHECK_EQ(vfs_create(&v, R, "/home/sub/x", VNODE_FILE, 0644, &n), -FS_EROFS);
    CHECK_EQ(vfs_create(&v, R, "/home/a.txt", VNODE_FILE, 0644, &n), -FS_EEXIST);   /* Linux: EEXIST first */
    CHECK_EQ(vfs_create(&v, R, "/home/sub", VNODE_DIR, 0755, &n), -FS_EEXIST);
    CHECK_EQ(vfs_create(&v, R, "/home", VNODE_DIR, 0755, &n), -FS_EEXIST);
    CHECK_EQ(vfs_create(&v, R, "/home/nosuch/x", VNODE_FILE, 0644, &n), -FS_ENOENT);
    CHECK_EQ(vfs_unlink(&v, R, "/home/a.txt"), -FS_EROFS);
    CHECK_EQ(vfs_unlink(&v, R, "/home/nosuch"), -FS_EROFS);    /* Linux: before the lookup */
    CHECK_EQ(vfs_unlink(&v, R, "/home/sub/b.txt"), -FS_EROFS);
    CHECK_EQ(vfs_unlink(&v, R, "/home/."), -FS_EISDIR);
    CHECK_EQ(vfs_rmdir(&v, R, "/home/sub"), -FS_EROFS);
    CHECK_EQ(vfs_rmdir(&v, R, "/home/sub/.."), -FS_ENOTEMPTY);
    CHECK_EQ(vfs_rmdir(&v, R, "/home"), -FS_EBUSY);           /* the mount point (RAM) */
    CHECK_EQ(vfs_rename(&v, R, "/home/a.txt", R, "/home/c.txt"), -FS_EROFS);
    CHECK_EQ(vfs_rename(&v, R, "/home/a.txt", R, "/home/sub/a.txt"), -FS_EROFS);
    CHECK_EQ(vfs_rename(&v, R, "/home/sub", R, "/home/sub2"), -FS_EROFS);
    CHECK_EQ(vfs_create(&v, R, "/tmp/x", VNODE_FILE, 0644, NULL), 0);
    CHECK_EQ(vfs_rename(&v, R, "/tmp/x", R, "/home/x"), -FS_EXDEV);
    CHECK_EQ(vfs_rename(&v, R, "/home/a.txt", R, "/tmp/a.txt"), -FS_EXDEV);
    CHECK_EQ(vfs_rename(&v, R, "/home", R, "/home2"), -FS_EBUSY);
    CHECK_EQ(vfs_write(&v, a, 0, "x", 1), -FS_EROFS);
    CHECK_EQ(vfs_write(&v, root, 0, "x", 1), -FS_EISDIR);
    CHECK_EQ(vfs_truncate_step(&v, a, 0, 1), -FS_EROFS);
    /* Nothing changed: the image, the nodes, the file, the RAM tree (but /tmp/x). */
    CHECK(memcmp(img, img0, sizeof(img)) == 0);
    CHECK(a->size == 1500 && a->nlink == 2 && get("/home/a.txt") == a && get("/home/sub/b.txt") != NULL);
    CHECK_EQ(ev.count, count + 2);      /* sub's and b.txt's nodes, made by lookups */
    CHECK_EQ(rfs.nodes, rnodes + 1);    /* /tmp/x */
    /* The RAM filesystem is unaffected. */
    CHECK_EQ(vfs_unlink(&v, R, "/tmp/x"), 0);
    CHECK_EQ(vfs_create(&v, R, "/tmp/d", VNODE_DIR, 0755, NULL), 0);
    CHECK_EQ(vfs_rmdir(&v, R, "/tmp/d"), 0);
}

/* 1.4 stage 8 (item 4): the file-size limit is each filesystem's, from
 * its ops table. */
static void test_file_max(void)
{
    setup(EXT2_VFS_NODES);
    CHECK_EQ(vfs_create(&v, R, "/tmp/f", VNODE_FILE, 0644, NULL), 0);
    CHECK_EQ(vfs_file_max(&v, get("/tmp/f")), RAMFS_FILE_MAX);
    CHECK_EQ(vfs_file_max(&v, R), 2u * 1024 * 1024);
    CHECK_EQ(vfs_file_max(&v, get("/home/a.txt")), 0xFFFFFFFFull);      /* 1 KiB blocks reach ~16 GiB: capped */
    CHECK_EQ(vfs_file_max(&v, get("/home")), 0xFFFFFFFFull);
    CHECK(ext2_max_size(&efs) > 0xFFFFFFFFull);
    /* A smaller reach is not capped (a volume whose block map reaches less). */
    efs.max_blocks = 1000;
    CHECK_EQ(ext2_vfs_init(&ev, &efs, &e_mem, 4), 0);
    CHECK_EQ(ev.file_max, 1000ull * BS);
    CHECK_EQ(ext2_vfs_ops.file_max(&ev), 1000ull * BS);
    /* An ops table without file_max gives 0 (no file may grow). */
    static const struct vfs_ops bare = { .lookup = 0 };
    struct vfs w;
    memset(&w, 0, sizeof(w));
    w.mounts.m[0].ops = &bare;
    w.mounts.count = 1;
    struct vnode node;
    memset(&node, 0, sizeof(node));
    CHECK_EQ(vfs_file_max(&w, &node), 0);
    CHECK_EQ(vfs_writable(&w, &node), 0);
}

static void test_limits(void)
{
    /* The node table full: max 3 (the root and two entries): ENOMEM (not
     * ENOENT), counted, the notice given once. */
    full_calls = 0;
    setup(3);
    struct vnode *root = get("/home");
    struct vnode *e1 = vfs_entry_from(&v, root, 0);
    struct vnode *e2 = e1 ? vfs_entry_next(&v, root, e1) : NULL;
    struct vnode *e3 = e2 ? vfs_entry_next(&v, root, e2) : NULL;
    CHECK(e1 && e2 && !e3);
    CHECK_EQ(vfs_lookup_error(&v, root), -FS_ENOMEM);  /* the listing stopped: not the end */
    CHECK_EQ(vfs_lookup_error(&v, root), 0);            /* read once */
    CHECK_EQ(ev.count, 3);
    CHECK_EQ(ev.full, 1);
    CHECK_EQ(full_calls, 1);
    CHECK_EQ(full_max, 3);
    CHECK(get("/home/a.txt") == e1);    /* made before: still found */
    CHECK_EQ(vfs_lookup_error(&v, root), 0);
    struct vnode *n = NULL;
    CHECK_EQ(vfs_lookup(&v, R, "/home/big.bin", &n), -FS_ENOMEM);
    CHECK_EQ(vfs_lookup(&v, R, "/home/big.bin/x", &n), -FS_ENOMEM);   /* in the walk too */
    CHECK_EQ(vfs_lookup(&v, R, "/home/nosuch", &n), -FS_ENOENT);      /* not there: still ENOENT */
    CHECK_EQ(vfs_create(&v, R, "/home/new", VNODE_FILE, 0644, &n), -FS_EROFS);
    CHECK_EQ(ev.full, 3);
    CHECK_EQ(full_calls, 1);            /* one notice only */
    /* The RAM filesystem has no error op: always 0. */
    CHECK_EQ(vfs_lookup(&v, R, "/nosuch", &n), -FS_ENOENT);
    CHECK_EQ(vfs_lookup_error(&v, R), 0);
    /* A failed disk read: EIO, counted, the core's reason kept. */
    setup(EXT2_VFS_NODES);
    struct vnode *big = get("/home/big.bin");
    static uint8_t buf[BS];
    fail_reads = 1;
    CHECK_EQ(vfs_read(&v, big, 13u * BS, buf, BS), -FS_EIO);
    CHECK_EQ(ev.errors, 1);
    CHECK(ev.why != NULL);
    CHECK_EQ(vfs_lookup(&v, R, "/home/sub/b.txt", &n), -FS_EIO);   /* its folder's block not readable: EIO */
    CHECK_EQ(ev.errors, 2);
    fail_reads = 0;
    CHECK_EQ(vfs_read(&v, big, 13u * BS, buf, BS), BS);
    CHECK(buf[0] == pat(4, 13u * BS));
    /* No memory: init and a new node fail cleanly. */
    setup(EXT2_VFS_NODES);
    alloc_left = 0;
    CHECK_EQ(ext2_vfs_init(&ev, &efs, &e_mem, 8), -FS_ENOMEM);
    alloc_left = -1;
    CHECK_EQ(ext2_vfs_init(&ev, &efs, &e_mem, 8), 0);
    alloc_left = 1;                     /* the node, not its name */
    CHECK(ext2_vfs_ops.lookup(&ev, &ev.root->vn, "a.txt", 5) == NULL);
    alloc_left = -1;
    CHECK(ext2_vfs_ops.lookup(&ev, &ev.root->vn, "a.txt", 5) != NULL);
    CHECK(ext2_vfs_ops.lookup(&ev, &ev.root->vn, "a.tx", 4) == NULL);
    CHECK(ext2_vfs_ops.lookup(&ev, &ev.root->vn, "", 0) == NULL);
    /* The adapter's other calls. */
    CHECK_EQ(ext2_vfs_ops.entries(&ev, &ev.root->vn), 5);
    CHECK_EQ(ext2_vfs_ops.create(&ev, &ev.root->vn, "z", 1, VNODE_FILE, 0644, &n), -FS_EROFS);
    CHECK_EQ(ext2_vfs_ops.move(&ev, &ev.root->vn, &ev.root->vn, "z", 1, NULL), -FS_EROFS);
    CHECK(ev.readonly);
}

/* Every write-type op of the adapter refuses with EROFS (EISDIR for data
 * calls on a folder) and changes nothing: the read-only proof in the
 * suite (part static checks madire.elf holds no other write-type ext2_
 * symbol). */
static void test_write_ops(void)
{
    setup(EXT2_VFS_NODES);
    struct vnode *root = &ev.root->vn, *a = get("/home/a.txt"), *sub = get("/home/sub"), *n = NULL;
    uint32_t count = ev.count;
    const struct vfs_ops *o = &ext2_vfs_ops;
    CHECK_EQ(o->create(&ev, root, "f", 1, VNODE_FILE, 0644, &n), -FS_EROFS);
    CHECK_EQ(o->create(&ev, root, "d", 1, VNODE_DIR, 0755, &n), -FS_EROFS);
    CHECK(n == NULL);
    CHECK_EQ(o->move(&ev, a, sub, "a.txt", 5, NULL), -FS_EROFS);
    CHECK_EQ(o->write(&ev, a, 0, "x", 1), -FS_EROFS);
    CHECK_EQ(o->write(&ev, a, 1500, "x", 1), -FS_EROFS);
    CHECK_EQ(o->write(&ev, sub, 0, "x", 1), -FS_EISDIR);
    CHECK_EQ(o->truncate_step(&ev, a, 0, 1), -FS_EROFS);
    CHECK_EQ(o->truncate_step(&ev, a, 4096, 1), -FS_EROFS);
    CHECK_EQ(o->truncate_step(&ev, sub, 0, 1), -FS_EISDIR);
    CHECK_EQ(o->writable(&ev), -FS_EROFS);
    o->detach(&ev, a);                  /* never reached in the kernel: changes nothing */
    o->release(&ev, a);
    CHECK(a->nlink == 2 && a->parent == root && a->size == 1500 && get("/home/a.txt") == a);
    CHECK_EQ(ev.count, count);
    CHECK(memcmp(img, img0, sizeof(img)) == 0);
}

/* 1.4 stage 8: a disk read error during cat ends with an error line. */
static char cat_out[256];
static int cat_kind;

static void cat_write(void *ctx, const char *text, enum shell_text kind)
{
    (void)ctx;
    snprintf(cat_out, sizeof(cat_out), "%s", text);
    cat_kind = (int)kind;
}

static void test_cat_error(void)
{
    const struct shell_output o = { cat_write, NULL };
    shell_cat_error("big.txt", -FS_EIO, &o);
    CHECK(strcmp(cat_out, "cat: 'big.txt': I/O error.\n") == 0);
    CHECK_EQ(cat_kind, SHELL_ERROR);
    shell_cat_error("/home/docs/a.txt", -FS_ENOMEM, &o);
    CHECK(strcmp(cat_out, "cat: '/home/docs/a.txt': out of kernel memory.\n") == 0);
    shell_cat_error("x", -99, &o);
    CHECK(strcmp(cat_out, "cat: 'x': error 99.\n") == 0);
}

int main(void)
{
    RUN(test_walk);
    RUN(test_data);
    RUN(test_listing);
    RUN(test_readonly);
    RUN(test_file_max);
    RUN(test_limits);
    RUN(test_write_ops);
    RUN(test_cat_error);
    return unit_report("ext2_vfs");
}
