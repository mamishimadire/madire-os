/* Unit tests for the 1.0 RAM filesystem (kernel/fs/ramfs_core.c) through
 * its ops table: node records and names at their exact size, the node
 * limit (ENOSPC), a failed allocation (ENOMEM) creating nothing, entry
 * order, node numbers, detach, move and release with every allocation
 * given back. */
#include <stdlib.h>
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/ramfs_core.h"
#include "tests/unit/unit.h"

static int live_allocs;
static size_t live_bytes;
static int fail_at = -1;                /* the nth allocation from now fails (0: the next) */

struct hdr { size_t size; long long pad; };

static void *t_alloc(size_t bytes)
{
    if (fail_at == 0) {
        fail_at = -1;
        return NULL;
    }
    if (fail_at > 0)
        fail_at--;
    struct hdr *h = malloc(sizeof(*h) + bytes);
    h->size = bytes;
    live_allocs++;
    live_bytes += bytes;
    return h + 1;
}

static void t_free(void *p)
{
    struct hdr *h = (struct hdr *)p - 1;
    live_allocs--;
    live_bytes -= h->size;
    free(h);
}

static const struct ramfs_mem mem = { t_alloc, t_free };
static struct ramfs fs;

static struct vnode *mk(struct vnode *dir, const char *name, enum vnode_type type)
{
    struct vnode *n = NULL;
    int r = ramfs_ops.create(&fs, dir, name, strlen(name), type, 0644, &n);
    return r == 0 ? n : NULL;
}

static void setup(uint32_t max)
{
    live_allocs = 0;
    live_bytes = 0;
    fail_at = -1;
    CHECK_EQ(ramfs_init(&fs, &mem, max), 0);
}

static void test_init(void)
{
    setup(RAMFS_MAX_NODES);
    struct vnode *root = &fs.root->vn;
    CHECK_EQ(fs.nodes, 1);
    CHECK_EQ(root->ino, 1);
    CHECK_EQ(root->type, VNODE_DIR);
    CHECK_EQ(root->mode, 0755);
    CHECK_EQ(root->nlink, 1);
    CHECK(root->parent == root);
    CHECK_EQ(fs.data_pages, 0);
    CHECK_EQ(live_allocs, 1);                                   /* the root's record only */
    CHECK_EQ(live_bytes, sizeof(struct ramfs_node));
    size_t len;
    ramfs_name(root, &len);
    CHECK_EQ(len, 0);
    CHECK(ramfs_first(root) == NULL);
    CHECK_EQ(RAMFS_MAX_NODES, 1024);
    CHECK_EQ(RAMFS_FILE_MAX, 2 * 1024 * 1024);
    ramfs_ops.release(&fs, root);
    CHECK_EQ(live_allocs, 0);
    fail_at = 0;
    CHECK_EQ(ramfs_init(&fs, &mem, 4), -FS_ENOMEM);             /* no memory for the root */
}

static void test_create_and_order(void)
{
    setup(RAMFS_MAX_NODES);
    struct vnode *root = &fs.root->vn;
    struct vnode *a = mk(root, "alpha", VNODE_FILE);
    struct vnode *b = mk(root, "b", VNODE_DIR);
    struct vnode *c = mk(root, "gamma", VNODE_FILE);
    CHECK(a && b && c);
    CHECK_EQ(a->ino, 2);
    CHECK_EQ(b->ino, 3);
    CHECK_EQ(c->ino, 4);
    CHECK(a->parent == root && b->parent == root);
    CHECK_EQ(a->nlink, 1);
    CHECK_EQ(a->size, 0);
    CHECK_EQ(a->mode, 0644);
    CHECK_EQ(fs.nodes, 4);
    /* Names at their exact size, no NUL. */
    CHECK_EQ(live_bytes, 4 * sizeof(struct ramfs_node) + 5 + 1 + 5);
    CHECK(ramfs_first(root) == a && ramfs_next(a) == b && ramfs_next(b) == c &&
          ramfs_next(c) == NULL);                               /* creation order */
    CHECK_EQ(ramfs_ops.entries(&fs, root), 3);
    CHECK(ramfs_ops.lookup(&fs, root, "b", 1) == b);
    CHECK(ramfs_ops.lookup(&fs, root, "gammaX", 5) == c);      /* by length */
    CHECK(ramfs_ops.lookup(&fs, root, "gam", 3) == NULL);
    CHECK(ramfs_ops.lookup(&fs, b, "alpha", 5) == NULL);
    size_t len;
    const char *s = ramfs_name(c, &len);
    CHECK(len == 5 && memcmp(s, "gamma", 5) == 0);
    struct vnode *d = mk(b, "inner", VNODE_FILE);
    CHECK(d && d->parent == b && ramfs_ops.entries(&fs, b) == 1);
}

static void test_node_limit(void)
{
    setup(4);
    struct vnode *root = &fs.root->vn, *n = NULL;
    CHECK(mk(root, "a", VNODE_FILE) && mk(root, "b", VNODE_FILE) && mk(root, "c", VNODE_DIR));
    CHECK_EQ(fs.nodes, 4);
    int allocs = live_allocs;
    CHECK_EQ(ramfs_ops.create(&fs, root, "d", 1, VNODE_FILE, 0644, &n), -FS_ENOSPC);
    CHECK_EQ(fs.nodes, 4);
    CHECK_EQ(live_allocs, allocs);                              /* nothing taken */
    CHECK_EQ(ramfs_ops.entries(&fs, root), 3);
    /* One freed: one more fits. */
    struct vnode *a = ramfs_ops.lookup(&fs, root, "a", 1);
    ramfs_ops.detach(&fs, a);
    ramfs_ops.release(&fs, a);
    CHECK(mk(root, "d", VNODE_FILE) != NULL);
}

static void test_no_memory(void)
{
    setup(RAMFS_MAX_NODES);
    struct vnode *root = &fs.root->vn, *n = NULL;
    uint64_t ino = fs.next_ino;
    int allocs = live_allocs;
    fail_at = 0;                                                /* the record */
    CHECK_EQ(ramfs_ops.create(&fs, root, "x", 1, VNODE_FILE, 0644, &n), -FS_ENOMEM);
    CHECK_EQ(live_allocs, allocs);
    CHECK_EQ(fs.nodes, 1);
    fail_at = 1;                                                /* the name */
    CHECK_EQ(ramfs_ops.create(&fs, root, "x", 1, VNODE_FILE, 0644, &n), -FS_ENOMEM);
    CHECK_EQ(live_allocs, allocs);
    CHECK_EQ(fs.nodes, 1);
    CHECK_EQ(fs.next_ino, ino);                                 /* no number used up */
    CHECK(ramfs_first(root) == NULL);
    CHECK(mk(root, "x", VNODE_FILE) != NULL);
    CHECK_EQ(ramfs_first(root)->ino, ino);
}

static void test_detach_and_release(void)
{
    setup(RAMFS_MAX_NODES);
    struct vnode *root = &fs.root->vn;
    struct vnode *a = mk(root, "a", VNODE_FILE);
    struct vnode *b = mk(root, "b", VNODE_FILE);
    struct vnode *c = mk(root, "c", VNODE_FILE);
    ramfs_ops.detach(&fs, b);                                   /* the middle one */
    CHECK_EQ(b->nlink, 0);
    CHECK(b->parent == NULL);
    CHECK(ramfs_first(root) == a && ramfs_next(a) == c && ramfs_next(c) == NULL);
    CHECK_EQ(fs.nodes, 4);                                      /* still allocated */
    ramfs_ops.release(&fs, b);
    CHECK_EQ(fs.nodes, 3);
    ramfs_ops.detach(&fs, c);                                   /* the last one */
    ramfs_ops.release(&fs, c);
    struct vnode *d = mk(root, "d", VNODE_FILE);
    CHECK(ramfs_next(a) == d && ramfs_next(d) == NULL);          /* appended after a */
    ramfs_ops.detach(&fs, a);                                   /* the first one */
    ramfs_ops.release(&fs, a);
    ramfs_ops.detach(&fs, d);
    ramfs_ops.release(&fs, d);
    CHECK(ramfs_first(root) == NULL);
    CHECK_EQ(ramfs_ops.entries(&fs, root), 0);
    CHECK_EQ(live_allocs, 1);
    CHECK_EQ(live_bytes, sizeof(struct ramfs_node));
}

static void test_move(void)
{
    setup(RAMFS_MAX_NODES);
    struct vnode *root = &fs.root->vn;
    struct vnode *dir = mk(root, "dir", VNODE_DIR);
    struct vnode *f = mk(root, "file", VNODE_FILE);
    struct vnode *g = mk(dir, "g", VNODE_FILE);
    size_t bytes = live_bytes;
    CHECK_EQ(ramfs_ops.move(&fs, f, dir, "renamed", 7, NULL), 0);
    CHECK(f->parent == dir);
    CHECK(ramfs_ops.lookup(&fs, root, "file", 4) == NULL);
    CHECK(ramfs_ops.lookup(&fs, dir, "renamed", 7) == f);
    CHECK_EQ(live_bytes, bytes - 4 + 7);                        /* the old name freed */
    CHECK_EQ(ramfs_ops.entries(&fs, dir), 2);
    CHECK_EQ(ramfs_ops.entries(&fs, root), 1);
    /* Over g: g detached, not freed (the VFS frees it when unused). */
    CHECK_EQ(ramfs_ops.move(&fs, f, dir, "g", 1, g), 0);
    CHECK_EQ(g->nlink, 0);
    CHECK(ramfs_ops.lookup(&fs, dir, "g", 1) == f);
    CHECK_EQ(ramfs_ops.entries(&fs, dir), 1);
    ramfs_ops.release(&fs, g);
    /* No memory for the new name: nothing changes. */
    int allocs = live_allocs;
    fail_at = 0;
    CHECK_EQ(ramfs_ops.move(&fs, f, root, "back", 4, NULL), -FS_ENOMEM);
    CHECK(f->parent == dir && ramfs_ops.lookup(&fs, dir, "g", 1) == f);
    CHECK_EQ(live_allocs, allocs);
}

int main(void)
{
    RUN(test_init);
    RUN(test_create_and_order);
    RUN(test_node_limit);
    RUN(test_no_memory);
    RUN(test_detach_and_release);
    RUN(test_move);
    return unit_report("ramfs");
}
