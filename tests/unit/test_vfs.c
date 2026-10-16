/* Unit tests for the 1.0 VFS layer (kernel/fs/vfs_core.c), over the RAM
 * filesystem: lookup from a start folder, create, unlink, rmdir and rename
 * with the errno Linux gives in each case, holds (a removed node lives
 * until its last drop), removed folders, the times, and no leak. */
#include <stdlib.h>
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/path_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs_core.h"
#include "tests/unit/unit.h"

static int live_allocs;
static int fail_next;

static void *t_alloc(size_t bytes)
{
    if (fail_next) {
        fail_next = 0;
        return NULL;
    }
    live_allocs++;
    return malloc(bytes);
}

static void t_free(void *p)
{
    live_allocs--;
    free(p);
}

static int64_t clock_now;
static int64_t t_now(void)
{
    return clock_now;
}

static const struct ramfs_mem mem = { t_alloc, t_free };
static struct ramfs fs;
static struct vfs v;
static struct vnode *R;                 /* the root */

static void setup(void)
{
    live_allocs = 0;
    fail_next = 0;
    clock_now = 100;
    CHECK_EQ(ramfs_init(&fs, &mem, RAMFS_MAX_NODES), 0);
    vfs_mount(&v, &ramfs_ops, &fs, &fs.root->vn, t_now);
    R = v.root;
}

static struct vnode *get(const char *path)
{
    struct vnode *n = NULL;
    return vfs_lookup(&v, R, path, &n) == 0 ? n : NULL;
}

static int mkd(const char *path)
{
    return vfs_create(&v, R, path, VNODE_DIR, 0755, NULL);
}

static int mkf(const char *path)
{
    return vfs_create(&v, R, path, VNODE_FILE, 0644, NULL);
}

static int look(struct vnode *start, const char *path)
{
    struct vnode *n = NULL;
    return vfs_lookup(&v, start, path, &n);
}

static void test_lookup(void)
{
    setup();
    CHECK_EQ(mkd("/a"), 0);
    CHECK_EQ(mkd("/a/b"), 0);
    CHECK_EQ(mkf("/a/b/f"), 0);
    struct vnode *a = get("/a"), *b = get("/a/b"), *f = get("/a/b/f"), *n = NULL;
    CHECK(a && b && f);
    CHECK(get("/") == R && get("//") == R);
    CHECK(get("/..") == R && get("/../..") == R);               /* ".." of "/" is "/" */
    CHECK(get("/a/./b/../b/f") == f);
    CHECK(get("/a/b/") == b && get("/a//b//") == b);
    CHECK_EQ(look(R, "/a/b/f/"), -FS_ENOTDIR);                  /* a trailing '/' on a file */
    CHECK_EQ(look(R, "/a/b/f/x"), -FS_ENOTDIR);
    CHECK_EQ(look(R, "/a/b/f/.."), -FS_ENOTDIR);
    CHECK_EQ(look(R, "/a/nosuch/f"), -FS_ENOENT);
    CHECK_EQ(look(R, "/a/nosuch"), -FS_ENOENT);
    CHECK_EQ(look(R, ""), -FS_ENOENT);
    /* Relative paths start at the start folder; absolute ones ignore it. */
    CHECK_EQ(vfs_lookup(&v, a, "b/f", &n), 0);
    CHECK(n == f);
    CHECK_EQ(vfs_lookup(&v, b, "..", &n), 0);
    CHECK(n == a);
    CHECK_EQ(vfs_lookup(&v, b, ".", &n), 0);
    CHECK(n == b);
    CHECK_EQ(vfs_lookup(&v, f, "/a", &n), 0);                   /* a file start, absolute path */
    CHECK(n == a);
    CHECK_EQ(look(f, "x"), -FS_ENOTDIR);                        /* a file start, relative path */
    CHECK_EQ(look(f, "."), -FS_ENOTDIR);
    /* Errors come in path order: the first bad name decides. */
    char p[600];
    memset(p, 0, sizeof(p));
    strcpy(p, "/nosuch/");
    memset(p + 8, 'n', 300);
    CHECK_EQ(look(R, p), -FS_ENOENT);
    strcpy(p, "/a/");
    memset(p + 3, 'n', 300);
    p[303] = '/';
    p[304] = 'x';
    p[305] = '\0';
    CHECK_EQ(look(R, p), -FS_ENAMETOOLONG);
}

static void test_create_cases(void)
{
    setup();
    struct vnode *n = NULL;
    CHECK_EQ(vfs_create(&v, R, "/d", VNODE_DIR, 0700, &n), 0);
    CHECK(n && n->type == VNODE_DIR && n->mode == 0700 && n->parent == R);
    CHECK_EQ(mkf("/d/f"), 0);
    CHECK_EQ(mkd("/d2/"), 0);                                   /* a folder may end in '/' */
    CHECK_EQ(mkf("/d/f"), -FS_EEXIST);
    CHECK_EQ(mkd("/d/f"), -FS_EEXIST);
    CHECK_EQ(mkd("/d"), -FS_EEXIST);
    CHECK_EQ(mkd("/"), -FS_EEXIST);
    CHECK_EQ(mkd("/d/."), -FS_EEXIST);
    CHECK_EQ(mkd("/d/.."), -FS_EEXIST);
    CHECK_EQ(mkf("/d/g/"), -FS_EISDIR);                         /* a file name ending in '/' */
    CHECK_EQ(mkf("/d/f/g"), -FS_ENOTDIR);
    CHECK_EQ(mkf("/nosuch/g"), -FS_ENOENT);
    CHECK_EQ(mkf(""), -FS_ENOENT);
    CHECK_EQ(vfs_create(&v, get("/d/f"), "g", VNODE_FILE, 0644, NULL), -FS_ENOTDIR);
    /* Relative to a start folder. */
    CHECK_EQ(vfs_create(&v, get("/d"), "rel", VNODE_FILE, 0644, NULL), 0);
    CHECK(get("/d/rel") != NULL);
    /* No memory: nothing created. */
    uint32_t nodes = fs.nodes;
    fail_next = 1;
    CHECK_EQ(mkf("/d/nomem"), -FS_ENOMEM);
    CHECK_EQ(fs.nodes, nodes);
    CHECK(get("/d/nomem") == NULL);
}

static void test_node_limit(void)
{
    setup();
    char name[16];
    unsigned made = 0;
    for (unsigned i = 0; i < RAMFS_MAX_NODES + 5; i++) {
        snprintf(name, sizeof(name), "/n%u", i);
        if (mkf(name) == 0)
            made++;
    }
    CHECK_EQ(made, RAMFS_MAX_NODES - 1);                        /* the root counts */
    CHECK_EQ(fs.nodes, RAMFS_MAX_NODES);
    CHECK_EQ(mkd("/more"), -FS_ENOSPC);
    CHECK(get("/more") == NULL);
    CHECK_EQ(vfs_unlink(&v, R, "/n0"), 0);
    CHECK_EQ(mkd("/more"), 0);
    CHECK_EQ(mkd("/more2"), -FS_ENOSPC);
}

static void test_unlink_cases(void)
{
    setup();
    CHECK_EQ(mkd("/d"), 0);
    CHECK_EQ(mkf("/d/f"), 0);
    CHECK_EQ(vfs_unlink(&v, R, "/d"), -FS_EISDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/"), -FS_EISDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/d/."), -FS_EISDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/d/.."), -FS_EISDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/d/f/"), -FS_ENOTDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/d/nosuch"), -FS_ENOENT);
    CHECK_EQ(vfs_unlink(&v, R, "/d/f/x"), -FS_ENOTDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/d/nosuch/x"), -FS_ENOENT);
    uint32_t nodes = fs.nodes;
    CHECK_EQ(vfs_unlink(&v, R, "/d/f"), 0);
    CHECK_EQ(fs.nodes, nodes - 1);
    CHECK(get("/d/f") == NULL);
    CHECK_EQ(vfs_unlink(&v, R, "/d/f"), -FS_ENOENT);
}

static void test_rmdir_cases(void)
{
    setup();
    CHECK_EQ(mkd("/d"), 0);
    CHECK_EQ(mkd("/d/e"), 0);
    CHECK_EQ(mkf("/f"), 0);
    CHECK_EQ(vfs_rmdir(&v, R, "/"), -FS_EBUSY);
    CHECK_EQ(vfs_rmdir(&v, R, "/d/."), -FS_EINVAL);
    CHECK_EQ(vfs_rmdir(&v, R, "/d/.."), -FS_ENOTEMPTY);
    CHECK_EQ(vfs_rmdir(&v, R, "/d"), -FS_ENOTEMPTY);
    CHECK_EQ(vfs_rmdir(&v, R, "/f"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rmdir(&v, R, "/f/"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rmdir(&v, R, "/nosuch"), -FS_ENOENT);
    CHECK_EQ(vfs_rmdir(&v, R, "/f/x"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rmdir(&v, R, "/d/e/"), 0);                     /* a trailing '/' is fine */
    CHECK_EQ(vfs_rmdir(&v, R, "/d"), 0);
    CHECK(get("/d") == NULL);
}

static void test_rename_cases(void)
{
    setup();
    CHECK_EQ(mkd("/a"), 0);
    CHECK_EQ(mkd("/a/b"), 0);
    CHECK_EQ(mkd("/empty"), 0);
    CHECK_EQ(mkd("/full"), 0);
    CHECK_EQ(mkf("/full/x"), 0);
    CHECK_EQ(mkf("/f"), 0);
    CHECK_EQ(mkf("/g"), 0);
    struct vnode *a = get("/a"), *f = get("/f"), *g = get("/g");
    CHECK_EQ(vfs_rename(&v, R, "/", R, "/x"), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/"), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, "/a/.", R, "/x"), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/a/.."), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, "/nosuch", R, "/x"), -FS_ENOENT);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/nosuch/x"), -FS_ENOENT);
    CHECK_EQ(vfs_rename(&v, R, "/f/x", R, "/x"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/g/x"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rename(&v, R, "/f/", R, "/x"), -FS_ENOTDIR);   /* a file with a trailing '/' */
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/x/"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rename(&v, R, "/a", R, "/a/b/c"), -FS_EINVAL); /* into its own subtree */
    CHECK_EQ(vfs_rename(&v, R, "/a", R, "/a/c"), -FS_EINVAL);
    CHECK_EQ(vfs_rename(&v, R, "/a/b", R, "/a"), -FS_ENOTEMPTY); /* onto its non-empty parent */
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/empty"), -FS_EISDIR);
    CHECK_EQ(vfs_rename(&v, R, "/a", R, "/f"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rename(&v, R, "/a", R, "/full"), -FS_ENOTEMPTY);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/./f"), 0);            /* the same node */
    CHECK(get("/f") == f);
    uint32_t nodes = fs.nodes;
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/g"), 0);              /* a file over a file */
    CHECK(get("/g") == f && get("/f") == NULL);
    CHECK_EQ(fs.nodes, nodes - 1);
    (void)g;
    CHECK_EQ(vfs_rename(&v, R, "/a", R, "/empty"), 0);          /* a folder over an empty one */
    CHECK(get("/empty") == a && get("/empty/b") != NULL && get("/a") == NULL);
    CHECK_EQ(fs.nodes, nodes - 2);
    CHECK_EQ(vfs_rename(&v, R, "/empty/", R, "/a/"), 0);        /* folders may end in '/' */
    CHECK(get("/a/b")->parent == a);
    /* Each side has its own start folder (renameat). */
    CHECK_EQ(vfs_rename(&v, get("/a"), "b", get("/full"), "b2"), 0);
    CHECK(get("/full/b2") != NULL && get("/a/b") == NULL);
    /* No memory for the new name: nothing changes. */
    fail_next = 1;
    CHECK_EQ(vfs_rename(&v, R, "/g", R, "/h"), -FS_ENOMEM);
    CHECK(get("/g") == f && get("/h") == NULL);
}

static void test_holds(void)
{
    setup();
    CHECK_EQ(mkf("/f"), 0);
    CHECK_EQ(mkd("/d"), 0);
    struct vnode *f = get("/f"), *d = get("/d");
    uint32_t nodes = fs.nodes;
    vfs_hold(f);
    vfs_hold(f);
    CHECK_EQ(vfs_unlink(&v, R, "/f"), 0);
    CHECK_EQ(fs.nodes, nodes);                                  /* held: still there */
    CHECK(get("/f") == NULL && f->nlink == 0);
    vfs_drop(&v, f);
    CHECK_EQ(fs.nodes, nodes);
    vfs_drop(&v, f);
    CHECK_EQ(fs.nodes, nodes - 1);                              /* freed at the last drop */
    /* A held folder removed (a current folder): nothing new inside it. */
    vfs_hold(d);
    CHECK_EQ(vfs_rmdir(&v, R, "/d"), 0);
    CHECK(d->nlink == 0 && d->parent == NULL);
    CHECK_EQ(vfs_create(&v, d, "x", VNODE_FILE, 0644, NULL), -FS_ENOENT);
    CHECK_EQ(look(d, "x"), -FS_ENOENT);
    CHECK_EQ(look(d, ".."), -FS_ENOENT);
    CHECK_EQ(look(d, "."), 0);
    CHECK_EQ(mkd("/d"), 0);                                     /* the name is free again */
    CHECK(get("/d") != d);
    vfs_drop(&v, d);
    CHECK_EQ(fs.nodes, nodes - 1);                              /* the root and the new /d */
    /* A drop without a hold of a linked node frees nothing. */
    struct vnode *nd = get("/d");
    vfs_drop(&v, nd);
    CHECK(get("/d") == nd);
}

static void test_times(void)
{
    setup();
    clock_now = 500;
    CHECK_EQ(mkd("/d"), 0);
    struct vnode *d = get("/d");
    CHECK(d->atime == 500 && d->mtime == 500 && d->ctime == 500);
    CHECK(R->mtime == 500);
    clock_now = 600;
    CHECK_EQ(mkf("/d/f"), 0);
    CHECK(d->mtime == 600 && d->atime == 500);
    clock_now = 700;
    CHECK_EQ(vfs_rename(&v, R, "/d/f", R, "/g"), 0);
    CHECK(d->mtime == 700 && R->mtime == 700 && get("/g")->ctime == 700 &&
          get("/g")->mtime == 600);
    clock_now = 800;
    CHECK_EQ(mkf("/x"), 0);
    CHECK(R->mtime == 800);
    clock_now = 900;
    CHECK_EQ(mkf("/x"), -FS_EEXIST);
    CHECK(R->mtime == 800);                                     /* a failed call changes nothing */
}

static void test_no_leak(void)
{
    setup();
    CHECK_EQ(mkd("/t"), 0);
    for (int i = 0; i < 50; i++) {
        char p[32];
        snprintf(p, sizeof(p), "/t/f%d", i);
        CHECK(mkf(p) == 0);
    }
    CHECK_EQ(mkd("/t/sub"), 0);
    CHECK_EQ(vfs_rename(&v, R, "/t/f0", R, "/t/sub/z"), 0);
    for (int i = 1; i < 50; i++) {
        char p[32];
        snprintf(p, sizeof(p), "/t/f%d", i);
        CHECK(vfs_unlink(&v, R, p) == 0);
    }
    CHECK_EQ(vfs_unlink(&v, R, "/t/sub/z"), 0);
    CHECK_EQ(vfs_rmdir(&v, R, "/t/sub"), 0);
    CHECK_EQ(vfs_rmdir(&v, R, "/t"), 0);
    CHECK_EQ(fs.nodes, 1);
    CHECK_EQ(live_allocs, 1);                                   /* the root's record only */
}

int main(void)
{
    RUN(test_lookup);
    RUN(test_create_cases);
    RUN(test_node_limit);
    RUN(test_unlink_cases);
    RUN(test_rmdir_cases);
    RUN(test_rename_cases);
    RUN(test_holds);
    RUN(test_times);
    RUN(test_no_leak);
    return unit_report("vfs");
}
