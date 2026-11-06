/* Unit tests for the 1.4 stage 7 mount table (kernel/fs/mount_core.c) and
 * the VFS's mount crossing (kernel/fs/vfs_core.c): a walk enters a mount
 * point, ".." at a mounted root goes to the parent of the folder it is
 * mounted on, a rename between filesystems is EXDEV, a mount point cannot
 * be removed, renamed or renamed onto (EBUSY), nested mounts, mount_add's
 * refusals, and every call of the ops table reaching the node's own
 * filesystem. The filesystems are fakes: each is a RAM filesystem behind
 * its own ops table, which counts the calls it gets (the kernel mounts only
 * "/" in this stage). */
#include <stdlib.h>
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/mount_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs_core.h"
#include "tests/unit/unit.h"

/* ---- the fakes ---- */

struct mt_fake {
    struct ramfs rfs;
    unsigned calls;                     /* ops calls this filesystem got */
};

static int mt_live;                     /* heap blocks in use */

static void *mt_alloc(size_t bytes)
{
    mt_live++;
    return malloc(bytes);
}

static void mt_free(void *p)
{
    mt_live--;
    free(p);
}

static const struct ramfs_mem mt_mem = { mt_alloc, mt_free };

/* File pages: host pages whose address is the frame number. */
static int mt_pages_live;

static uint64_t mt_page_alloc(void)
{
    void *p = aligned_alloc(RAMFS_PAGE, RAMFS_PAGE);
    if (!p)
        return 0;
    mt_pages_live++;
    return (uint64_t)(uintptr_t)p;
}

static void mt_page_free(uint64_t frame)
{
    mt_pages_live--;
    free((void *)(uintptr_t)frame);
}

static volatile void *mt_page_map(uint64_t frame)
{
    return (volatile void *)(uintptr_t)frame;
}

static void mt_page_unmap(void)
{
}

static const struct ramfs_pages mt_pages = { mt_page_alloc, mt_page_free, mt_page_map,
                                             mt_page_unmap };

#define MT_FS(p) (&((struct mt_fake *)(p))->rfs)
#define MT_COUNT(p) (((struct mt_fake *)(p))->calls++)

static struct vnode *mt_lookup(void *fs, struct vnode *dir, const char *name, size_t len)
{
    MT_COUNT(fs);
    return ramfs_ops.lookup(MT_FS(fs), dir, name, len);
}

static int mt_create(void *fs, struct vnode *dir, const char *name, size_t len,
                     enum vnode_type type, uint32_t mode, struct vnode **out)
{
    MT_COUNT(fs);
    return ramfs_ops.create(MT_FS(fs), dir, name, len, type, mode, out);
}

static void mt_detach(void *fs, struct vnode *node)
{
    MT_COUNT(fs);
    ramfs_ops.detach(MT_FS(fs), node);
}

static int mt_move(void *fs, struct vnode *node, struct vnode *dir, const char *name, size_t len,
                   struct vnode *victim)
{
    MT_COUNT(fs);
    return ramfs_ops.move(MT_FS(fs), node, dir, name, len, victim);
}

static uint32_t mt_entries(void *fs, struct vnode *dir)
{
    MT_COUNT(fs);
    return ramfs_ops.entries(MT_FS(fs), dir);
}

static void mt_release(void *fs, struct vnode *node)
{
    MT_COUNT(fs);
    ramfs_ops.release(MT_FS(fs), node);
}

static int64_t mt_read(void *fs, struct vnode *f, uint64_t off, void *buf, uint64_t len)
{
    MT_COUNT(fs);
    return ramfs_ops.read(MT_FS(fs), f, off, buf, len);
}

static int64_t mt_write(void *fs, struct vnode *f, uint64_t off, const void *buf, uint64_t len)
{
    MT_COUNT(fs);
    return ramfs_ops.write(MT_FS(fs), f, off, buf, len);
}

static int mt_truncate_step(void *fs, struct vnode *f, uint64_t length, uint32_t max_pages)
{
    MT_COUNT(fs);
    return ramfs_ops.truncate_step(MT_FS(fs), f, length, max_pages);
}

static uint32_t mt_npages(void *fs, const struct vnode *n)
{
    MT_COUNT(fs);
    return ramfs_ops.pages(MT_FS(fs), n);
}

static struct vnode *mt_entry_from(void *fs, const struct vnode *dir, uint64_t pos)
{
    MT_COUNT(fs);
    return ramfs_ops.entry_from(MT_FS(fs), dir, pos);
}

static struct vnode *mt_entry_next(void *fs, const struct vnode *dir, const struct vnode *e)
{
    MT_COUNT(fs);
    return ramfs_ops.entry_next(MT_FS(fs), dir, e);
}

static const char *mt_name(void *fs, const struct vnode *n, size_t *len)
{
    MT_COUNT(fs);
    return ramfs_ops.name(MT_FS(fs), n, len);
}

static uint64_t mt_pos(void *fs, const struct vnode *e)
{
    MT_COUNT(fs);
    return ramfs_ops.pos(MT_FS(fs), e);
}

static const struct vfs_ops mt_ops = {
    .lookup = mt_lookup, .create = mt_create, .detach = mt_detach, .move = mt_move,
    .entries = mt_entries, .release = mt_release, .read = mt_read, .write = mt_write,
    .truncate_step = mt_truncate_step, .pages = mt_npages, .entry_from = mt_entry_from,
    .entry_next = mt_entry_next, .name = mt_name, .pos = mt_pos,
};

/* ---- the tree: A at "/", B on A's /a/mnt, C on B's bd (/a/mnt/bd) ---- */

static struct mt_fake A, B, C, D;
static struct vfs v;
static struct vnode *R;                 /* "/" */

static struct vnode *mt_get(const char *path)
{
    struct vnode *n = NULL;
    return vfs_lookup(&v, R, path, &n) == 0 ? n : NULL;
}

static void mt_init(struct mt_fake *f)
{
    CHECK_EQ(ramfs_init(&f->rfs, &mt_mem, 64), 0);
    ramfs_set_pages(&f->rfs, &mt_pages, 64);
    f->calls = 0;
}

static void mt_setup(void)
{
    mt_live = 0;
    mt_pages_live = 0;
    mt_init(&A);
    mt_init(&B);
    mt_init(&C);
    mt_init(&D);
    vfs_mount(&v, &mt_ops, &A, &A.rfs.root->vn, NULL);
    R = v.root;
    CHECK_EQ(vfs_create(&v, R, "/a", VNODE_DIR, 0755, NULL), 0);          /* ino 2 in A */
    CHECK_EQ(vfs_create(&v, R, "/a/mnt", VNODE_DIR, 0755, NULL), 0);      /* ino 3 */
    CHECK_EQ(vfs_create(&v, R, "/a/mnt/hidden", VNODE_FILE, 0644, NULL), 0);
    CHECK_EQ(vfs_create(&v, R, "/d", VNODE_DIR, 0755, NULL), 0);
    CHECK_EQ(vfs_create(&v, R, "/f", VNODE_FILE, 0644, NULL), 0);
}

/* B on /a/mnt (number 1), with a file bf and a folder bd; C on /a/mnt/bd
 * (number 2), with a file cf. */
static void mt_mount_b_c(void)
{
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &B, &B.rfs.root->vn, mt_get("/a/mnt")), 1);
    CHECK_EQ(vfs_create(&v, R, "/a/mnt/bf", VNODE_FILE, 0644, NULL), 0);
    CHECK_EQ(vfs_create(&v, R, "/a/mnt/bd", VNODE_DIR, 0755, NULL), 0);
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &C, &C.rfs.root->vn, mt_get("/a/mnt/bd")), 2);
    CHECK_EQ(vfs_create(&v, R, "/a/mnt/bd/cf", VNODE_FILE, 0644, NULL), 0);
}

/* ---- the cases ---- */

/* Only "/": the kernel's shape in this stage; nothing crosses. */
static void test_one_filesystem(void)
{
    mt_setup();
    struct vnode *a = mt_get("/a"), *mnt = mt_get("/a/mnt");
    CHECK(a && mnt);
    CHECK_EQ(v.mounts.count, 1);
    CHECK(v.mounts.m[0].root == R && v.mounts.m[0].covered == NULL);
    CHECK(R->parent == R && R->fs_id == 0 && a->fs_id == 0 && mnt->fs_id == 0);
    CHECK(mount_enter(&v.mounts, mnt) == mnt);
    CHECK(mount_parent(&v.mounts, mnt) == a && mount_parent(&v.mounts, R) == R);
    CHECK(mount_covered(&v.mounts, R) == R && mount_covered(&v.mounts, a) == a);
    CHECK(!mount_is_point(&v.mounts, mnt) && !mount_is_point(&v.mounts, R));
    CHECK(mount_of(&v.mounts, mnt) == &v.mounts.m[0]);
    CHECK(mt_get("/a/mnt/hidden") != NULL);
    CHECK(B.calls == 0 && A.calls > 0);
    CHECK_EQ(sizeof(struct vnode), 72);
}

/* A walk enters a mount point; what was in the folder is hidden; new nodes
 * land in the mounted filesystem and carry its number. */
static void test_walk_enters(void)
{
    mt_setup();
    struct vnode *mnt = mt_get("/a/mnt");
    uint32_t a_nodes = A.rfs.nodes;
    mt_mount_b_c();
    CHECK_EQ(A.rfs.nodes, a_nodes);                     /* nothing new in A */
    CHECK_EQ(B.rfs.nodes, 3);                           /* root, bf, bd */
    CHECK_EQ(C.rfs.nodes, 2);                           /* root, cf */
    struct vnode *broot = &B.rfs.root->vn, *croot = &C.rfs.root->vn;
    CHECK(mt_get("/a/mnt") == broot && mt_get("/a/mnt/") == broot);
    CHECK(broot->fs_id == 1 && broot->parent == broot);
    CHECK(mt_get("/a/mnt/hidden") == NULL);             /* under the mount */
    struct vnode *bf = mt_get("/a/mnt/bf"), *cf = mt_get("/a/mnt/bd/cf"), *n = NULL;
    CHECK(bf && bf->fs_id == 1 && bf->parent == broot);
    CHECK(mt_get("/a/mnt/bd") == croot && croot->fs_id == 2);
    CHECK(cf && cf->fs_id == 2 && cf->parent == croot);
    CHECK_EQ(vfs_lookup(&v, mt_get("/a"), "mnt/bd/cf", &n), 0);  /* relative, across both */
    CHECK(n == cf);
    CHECK_EQ(vfs_lookup(&v, broot, "bd/./cf", &n), 0);
    CHECK(n == cf);
    CHECK(mount_is_point(&v.mounts, mnt) && mnt->opens == 1);   /* the mount holds it */
    CHECK(mount_enter(&v.mounts, mnt) == broot);
    CHECK(mount_covered(&v.mounts, broot) == mnt && mount_covered(&v.mounts, bf) == bf);
    CHECK_EQ(vfs_create(&v, R, "/a/mnt/bd/cf", VNODE_FILE, 0644, NULL), -FS_EEXIST);
    CHECK_EQ(vfs_lookup(&v, R, "/a/mnt/bf/x", &n), -FS_ENOTDIR);
    CHECK_EQ(vfs_lookup(&v, R, "/a/mnt/nosuch", &n), -FS_ENOENT);
}

/* ".." at a mounted root goes to the parent of the folder it is on. */
static void test_dotdot(void)
{
    mt_setup();
    mt_mount_b_c();
    struct vnode *a = mt_get("/a"), *broot = &B.rfs.root->vn, *croot = &C.rfs.root->vn;
    struct vnode *n = NULL;
    CHECK(mt_get("/a/mnt/..") == a);
    CHECK(mt_get("/a/mnt/../..") == R);
    CHECK(mt_get("/a/mnt/../mnt/bf") == mt_get("/a/mnt/bf"));
    CHECK(mt_get("/a/mnt/bd/..") == broot);              /* nested: up into B */
    CHECK(mt_get("/a/mnt/bd/../..") == a);               /* and on into A */
    CHECK(mt_get("/a/mnt/bd/../../../../..") == R);      /* ".." of "/" is "/" */
    CHECK_EQ(vfs_lookup(&v, croot, "..", &n), 0);
    CHECK(n == broot);
    CHECK_EQ(vfs_lookup(&v, broot, "../mnt/bd/cf", &n), 0);
    CHECK(n == mt_get("/a/mnt/bd/cf"));
    CHECK(vfs_parent(&v, croot) == broot && vfs_parent(&v, broot) == a);
    CHECK(vfs_parent(&v, R) == R);
    /* A removed folder of a mounted filesystem: ENOENT for "..". */
    struct vnode *gone = NULL;
    CHECK_EQ(vfs_create(&v, R, "/a/mnt/gone", VNODE_DIR, 0755, &gone), 0);
    vfs_hold(gone);
    CHECK_EQ(vfs_rmdir(&v, R, "/a/mnt/gone"), 0);
    CHECK_EQ(vfs_lookup(&v, gone, "..", &n), -FS_ENOENT);
    vfs_drop(&v, gone);
}

/* A rename between two filesystems is EXDEV, first after the walks; one
 * within a mounted filesystem works there. */
static void test_exdev(void)
{
    mt_setup();
    mt_mount_b_c();
    uint32_t a_nodes = A.rfs.nodes, b_nodes = B.rfs.nodes, c_nodes = C.rfs.nodes;
    struct vnode *bf = mt_get("/a/mnt/bf");
    CHECK_EQ(vfs_rename(&v, R, "/a/mnt/bf", R, "/bf"), -FS_EXDEV);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/a/mnt/f"), -FS_EXDEV);
    CHECK_EQ(vfs_rename(&v, R, "/a/mnt/bf", R, "/a/mnt/bd/bf"), -FS_EXDEV);   /* B to C */
    CHECK_EQ(vfs_rename(&v, R, "/a/mnt/bd/cf", R, "/a/mnt/cf"), -FS_EXDEV);   /* C to B */
    CHECK_EQ(vfs_rename(&v, R, "/a/mnt/nosuch", R, "/x"), -FS_EXDEV);         /* before ENOENT */
    CHECK_EQ(vfs_rename(&v, R, "/d", R, "/a/mnt/bd/d"), -FS_EXDEV);           /* a folder */
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/a/mnt/bf"), -FS_EXDEV);             /* over a file */
    CHECK(A.rfs.nodes == a_nodes && B.rfs.nodes == b_nodes && C.rfs.nodes == c_nodes);
    CHECK(mt_get("/a/mnt/bf") == bf && mt_get("/f") != NULL);
    /* Within one mounted filesystem: as on "/". */
    CHECK_EQ(vfs_rename(&v, R, "/a/mnt/bf", R, "/a/mnt/bg"), 0);
    CHECK(mt_get("/a/mnt/bg") == bf && mt_get("/a/mnt/bf") == NULL);
    struct vnode *n = NULL;
    CHECK_EQ(vfs_lookup(&v, R, "/a/mnt/bg", &n), 0);
    CHECK(n->fs_id == 1);
    /* Relative paths from a mounted root: still EXDEV across. */
    CHECK_EQ(vfs_rename(&v, &B.rfs.root->vn, "bg", R, "/bg"), -FS_EXDEV);
    CHECK_EQ(vfs_rename(&v, &B.rfs.root->vn, "bg", &B.rfs.root->vn, "../g"), -FS_EXDEV);
}

/* A mount point cannot be removed, renamed or renamed onto. */
static void test_busy(void)
{
    mt_setup();
    mt_mount_b_c();
    struct vnode *mnt = v.mounts.m[1].covered, *bd = v.mounts.m[2].covered;
    CHECK_EQ(vfs_rmdir(&v, R, "/a/mnt"), -FS_EBUSY);
    CHECK_EQ(vfs_rmdir(&v, R, "/a/mnt/"), -FS_EBUSY);
    CHECK_EQ(vfs_rmdir(&v, R, "/a/mnt/bd"), -FS_EBUSY);           /* nested */
    CHECK_EQ(vfs_rename(&v, R, "/a/mnt", R, "/a/m2"), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, "/a/mnt/bd", R, "/a/mnt/b2"), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, "/d", R, "/a/mnt"), -FS_EBUSY);    /* onto a mount point */
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/a/mnt"), -FS_EISDIR);   /* a file onto a folder first */
    CHECK_EQ(vfs_unlink(&v, R, "/a/mnt"), -FS_EISDIR);
    CHECK_EQ(vfs_create(&v, R, "/a/mnt", VNODE_DIR, 0755, NULL), -FS_EEXIST);
    CHECK_EQ(vfs_rmdir(&v, R, "/a/mnt/."), -FS_EINVAL);            /* the mounted root itself */
    CHECK_EQ(vfs_rmdir(&v, &B.rfs.root->vn, "."), -FS_EINVAL);
    CHECK(mt_get("/a/mnt") == &B.rfs.root->vn);
    CHECK(mnt->nlink == 1 && mnt->opens == 1 && bd->nlink == 1 && bd->opens == 1);
    /* The folder holding the mount point moves with it (Linux allows it). */
    CHECK_EQ(vfs_rename(&v, R, "/a", R, "/a2"), 0);
    CHECK(mt_get("/a2/mnt/bd/cf") != NULL && mt_get("/a2/mnt") == &B.rfs.root->vn);
}

/* mount_add's refusals: nothing changes on any of them. */
static void test_mount_refusals(void)
{
    mt_setup();
    struct vnode *mnt = mt_get("/a/mnt"), *f = mt_get("/f"), *d = mt_get("/d");
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &B, &B.rfs.root->vn, R), -FS_EBUSY);   /* "/" */
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &B, &B.rfs.root->vn, f), -FS_ENOTDIR);
    struct vnode *gone = NULL;
    CHECK_EQ(vfs_create(&v, R, "/gone", VNODE_DIR, 0755, &gone), 0);
    vfs_hold(gone);
    CHECK_EQ(vfs_rmdir(&v, R, "/gone"), 0);
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &B, &B.rfs.root->vn, gone), -FS_ENOENT);
    CHECK_EQ(v.mounts.count, 1);
    CHECK(gone->opens == 1 && f->opens == 0 && R->opens == 0);
    vfs_drop(&v, gone);
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &B, &B.rfs.root->vn, mnt), 1);
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &C, &C.rfs.root->vn, mnt), -FS_EBUSY);  /* taken */
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &C, &C.rfs.root->vn, &B.rfs.root->vn),
             -FS_EBUSY);                                            /* a mounted root */
    CHECK_EQ(v.mounts.count, 2);
    CHECK(mnt->opens == 1 && B.rfs.root->vn.opens == 0);
    /* The table holds MOUNT_MAX filesystems, "/" included. */
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &C, &C.rfs.root->vn, d), 2);
    CHECK_EQ(vfs_create(&v, R, "/a/x", VNODE_DIR, 0755, NULL), 0);
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &D, &D.rfs.root->vn, mt_get("/a/x")), 3);
    CHECK_EQ(vfs_create(&v, R, "/a/y", VNODE_DIR, 0755, NULL), 0);
    struct mt_fake E;
    mt_init(&E);
    CHECK_EQ(mount_add(&v.mounts, &mt_ops, &E, &E.rfs.root->vn, mt_get("/a/y")), -FS_ENOSPC);
    CHECK_EQ(v.mounts.count, MOUNT_MAX);
    CHECK(mt_get("/a/y")->opens == 0);
    CHECK(mt_get("/d") == &C.rfs.root->vn && mt_get("/a/x") == &D.rfs.root->vn);
}

/* Every table call reaches the node's own filesystem (its fake counts the
 * call; the others do not), with the RAM filesystem's results. */
static void test_dispatch(void)
{
    mt_setup();
    mt_mount_b_c();
    struct vnode *cf = mt_get("/a/mnt/bd/cf"), *f = mt_get("/f");
    unsigned a0 = A.calls, b0 = B.calls, c0 = C.calls;
    uint8_t buf[100];
    for (unsigned i = 0; i < sizeof(buf); i++)
        buf[i] = (uint8_t)(i * 3 + 1);
    CHECK_EQ(vfs_write(&v, cf, 5000, buf, sizeof(buf)), sizeof(buf));
    CHECK(C.calls == c0 + 1 && A.calls == a0 && B.calls == b0);
    CHECK_EQ(C.rfs.data_pages, 2);                      /* the index page and one data page */
    CHECK(A.rfs.data_pages == 0 && B.rfs.data_pages == 0);
    CHECK_EQ(vfs_pages(&v, cf), 2);
    uint8_t back[100];
    memset(back, 0, sizeof(back));
    CHECK_EQ(vfs_read(&v, cf, 5000, back, sizeof(back)), sizeof(back));
    CHECK(memcmp(back, buf, sizeof(buf)) == 0 && cf->size == 5100);
    CHECK_EQ(vfs_truncate_step(&v, cf, 0, FSYS_TRUNC_STEP), 0);
    CHECK(cf->size == 0 && C.rfs.data_pages == 0);
    CHECK_EQ(C.calls, c0 + 4);
    CHECK(A.calls == a0 && B.calls == b0);
    CHECK_EQ(vfs_write(&v, f, 0, buf, 10), 10);         /* a file of A: A's table */
    CHECK(A.calls == a0 + 1 && A.rfs.data_pages == 2 && C.calls == c0 + 4);
    /* The listing calls: a folder of B. */
    unsigned bl = B.calls;
    struct vnode *broot = &B.rfs.root->vn;
    struct vnode *e = vfs_entry_from(&v, broot, 0);
    size_t len = 0;
    const char *name = e ? vfs_entry_name(&v, e, &len) : "";
    CHECK(e && len == 2 && memcmp(name, "bf", 2) == 0 && e->fs_id == 1);
    struct vnode *e2 = vfs_entry_next(&v, broot, e);
    name = e2 ? vfs_entry_name(&v, e2, &len) : "";
    CHECK(e2 && len == 2 && memcmp(name, "bd", 2) == 0 && e2->fs_id == 1);
    CHECK(vfs_entry_pos(&v, e2) > vfs_entry_pos(&v, e));
    CHECK(vfs_entry_next(&v, broot, e2) == NULL);
    CHECK(vfs_enter(&v, e2) == &C.rfs.root->vn);        /* bd is C's mount point */
    CHECK_EQ(vfs_subdirs(&v, broot), 1);
    CHECK_EQ(B.calls, bl + 10);
    CHECK_EQ(vfs_subdirs(&v, R), 2);                    /* /a and /d */
}

/* getdents64's records and getcwd's path across mounts (fsys_core.c). */
static uint64_t mt_get64(const uint8_t *p)
{
    uint64_t x = 0;
    for (int i = 7; i >= 0; i--)
        x = (x << 8) | p[i];
    return x;
}

static void test_dir_list_and_cwd(void)
{
    mt_setup();
    mt_mount_b_c();
    struct vnode *broot = &B.rfs.root->vn, *croot = &C.rfs.root->vn;
    uint8_t b[512];
    uint64_t next = 0;
    bool more = true;
    uint32_t used = fsys_dir_list(&v, broot, 0, b, sizeof(b), &next, &more);
    /* ".", "..", bf, bd: 24 bytes each (19 + a name of 1 or 2 + NUL, to 8). */
    CHECK_EQ(used, 96);
    CHECK(!more);
    CHECK_EQ(mt_get64(b), broot->ino);                  /* "." */
    CHECK_EQ(mt_get64(b + 24), mt_get("/a")->ino);      /* "..": /a, ino 2 in A */
    CHECK_EQ(mt_get("/a")->ino, 2);
    CHECK(memcmp(b + 48 + 19, "bf", 3) == 0 && memcmp(b + 72 + 19, "bd", 3) == 0);
    used = fsys_dir_list(&v, croot, 0, b, sizeof(b), &next, &more);
    CHECK_EQ(used, 72);                                 /* ".", "..", cf */
    CHECK_EQ(mt_get64(b + 24), broot->ino);             /* "..": B's root */
    /* With "/" alone, fsys_dir_list gives what fsys_dir_fill does. */
    uint8_t b2[512];
    uint64_t next2 = 0;
    bool more2 = true;
    memset(b, 0xAA, sizeof(b));
    memset(b2, 0xAA, sizeof(b2));
    used = fsys_dir_list(&v, R, 0, b, sizeof(b), &next, &more);
    CHECK_EQ(fsys_dir_fill(R, 0, b2, sizeof(b2), &next2, &more2), used);
    CHECK(memcmp(b, b2, sizeof(b)) == 0 && next == next2 && more == more2);
    char path[64];
    CHECK_EQ(fsys_cwd_path_v(&v, broot, path, sizeof(path)), 7);
    CHECK(strcmp(path, "/a/mnt") == 0);
    CHECK_EQ(fsys_cwd_path_v(&v, croot, path, sizeof(path)), 10);
    CHECK(strcmp(path, "/a/mnt/bd") == 0);
    struct vnode *cd = NULL;
    CHECK_EQ(vfs_create(&v, R, "/a/mnt/bd/cd", VNODE_DIR, 0755, &cd), 0);
    CHECK_EQ(fsys_cwd_path_v(&v, cd, path, sizeof(path)), 13);
    CHECK(strcmp(path, "/a/mnt/bd/cd") == 0);
    CHECK_EQ(fsys_cwd_path_v(&v, R, path, sizeof(path)), 2);
    CHECK(strcmp(path, "/") == 0);
    CHECK_EQ(fsys_cwd_path_v(&v, cd, path, 12), -FS_ENAMETOOLONG);
}

/* Everything removed again: no node, heap block or page left in any of the
 * filesystems but their roots (the mounts keep their covered folders). */
static void test_cleanup(void)
{
    mt_setup();
    mt_mount_b_c();
    uint8_t x = 7;
    CHECK_EQ(vfs_write(&v, mt_get("/a/mnt/bd/cf"), 0, &x, 1), 1);
    CHECK_EQ(vfs_unlink(&v, R, "/a/mnt/bd/cf"), 0);
    CHECK_EQ(vfs_unlink(&v, R, "/a/mnt/bf"), 0);
    CHECK_EQ(vfs_rmdir(&v, R, "/a/mnt/bd"), -FS_EBUSY);
    CHECK_EQ(vfs_unlink(&v, R, "/f"), 0);
    CHECK_EQ(vfs_rmdir(&v, R, "/d"), 0);
    CHECK_EQ(mt_pages_live, 0);
    CHECK(B.rfs.nodes == 2 && C.rfs.nodes == 1);        /* root + bd; root */
    CHECK(A.rfs.nodes == 4);                            /* root, a, mnt, hidden */
    /* The records and names left: A's root and 3 named nodes (7 blocks),
     * B's root and bd (3), C's and D's roots (1 each). */
    CHECK_EQ(mt_live, 12);
}

int main(void)
{
    RUN(test_one_filesystem);
    RUN(test_walk_enters);
    RUN(test_dotdot);
    RUN(test_exdev);
    RUN(test_busy);
    RUN(test_mount_refusals);
    RUN(test_dispatch);
    RUN(test_dir_list_and_cwd);
    RUN(test_cleanup);
    return unit_report("mount_core");
}
