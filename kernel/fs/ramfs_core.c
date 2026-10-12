/*
 * The RAM filesystem (1.0), pure: see ramfs_core.h. The host unit tests
 * build this file too (tests/unit/test_ramfs.c).
 */
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/path_core.h"

#define RN(v) ((struct ramfs_node *)(v))

static struct ramfs_node *new_node(struct ramfs *fs, enum vnode_type type, uint32_t mode)
{
    if (fs->nodes >= fs->max_nodes)
        return 0;
    struct ramfs_node *n = fs->mem.alloc(sizeof(*n));
    if (!n)
        return 0;
    /* Field by field: no struct assignment, which could become a memset
     * call in the kernel. */
    n->vn.type = type;
    n->vn.ino = fs->next_ino++;
    n->vn.size = 0;
    n->vn.mode = mode;
    n->vn.nlink = 1;
    n->vn.opens = 0;
    n->vn.parent = 0;
    n->vn.atime = n->vn.mtime = n->vn.ctime = 0;
    n->name = 0;
    n->name_len = 0;
    n->first = n->last = n->next = 0;
    n->entries = 0;
    fs->nodes++;
    return n;
}

static void free_node(struct ramfs *fs, struct ramfs_node *n)
{
    if (n->name)
        fs->mem.free(n->name);
    fs->mem.free(n);
    fs->nodes--;
}

static char *copy_name(struct ramfs *fs, const char *name, size_t len)
{
    char *p = fs->mem.alloc(len);
    if (!p)
        return 0;
    for (size_t i = 0; i < len; i++)
        p[i] = name[i];
    return p;
}

int ramfs_init(struct ramfs *fs, const struct ramfs_mem *mem, uint32_t max_nodes)
{
    fs->mem.alloc = mem->alloc;
    fs->mem.free = mem->free;
    fs->max_nodes = max_nodes;
    fs->nodes = 0;
    fs->data_pages = 0;
    fs->next_ino = 1;
    fs->root = new_node(fs, VNODE_DIR, 0755);
    if (!fs->root)
        return -FS_ENOMEM;
    fs->root->vn.parent = &fs->root->vn;
    return 0;
}

static void append(struct ramfs_node *dir, struct ramfs_node *n)
{
    n->next = 0;
    if (dir->last)
        dir->last->next = n;
    else
        dir->first = n;
    dir->last = n;
    dir->entries++;
    n->vn.parent = &dir->vn;
}

static void unhook(struct ramfs_node *dir, struct ramfs_node *n)
{
    struct ramfs_node *prev = 0;
    for (struct ramfs_node *e = dir->first; e; prev = e, e = e->next) {
        if (e != n)
            continue;
        if (prev)
            prev->next = e->next;
        else
            dir->first = e->next;
        if (dir->last == e)
            dir->last = prev;
        dir->entries--;
        e->next = 0;
        return;
    }
}

static struct vnode *r_lookup(void *fsp, struct vnode *dir, const char *name, size_t len)
{
    (void)fsp;
    for (struct ramfs_node *e = RN(dir)->first; e; e = e->next)
        if (path_name_eq(e->name, e->name_len, name, len))
            return &e->vn;
    return 0;
}

static int r_create(void *fsp, struct vnode *dir, const char *name, size_t len,
                    enum vnode_type type, uint32_t mode, struct vnode **out)
{
    struct ramfs *fs = fsp;
    if (fs->nodes >= fs->max_nodes)
        return -FS_ENOSPC;
    struct ramfs_node *n = new_node(fs, type, mode);
    if (!n)
        return -FS_ENOMEM;
    n->name = copy_name(fs, name, len);
    if (!n->name) {
        free_node(fs, n);
        fs->next_ino--;                 /* the number was never seen */
        return -FS_ENOMEM;
    }
    n->name_len = len;
    append(RN(dir), n);
    *out = &n->vn;
    return 0;
}

static void r_detach(void *fsp, struct vnode *node)
{
    (void)fsp;
    struct ramfs_node *n = RN(node);
    if (node->parent)
        unhook(RN(node->parent), n);
    node->parent = 0;
    node->nlink = 0;
}

static int r_move(void *fsp, struct vnode *node, struct vnode *dir, const char *name,
                  size_t len, struct vnode *victim)
{
    struct ramfs *fs = fsp;
    struct ramfs_node *n = RN(node);
    char *p = copy_name(fs, name, len);
    if (!p)
        return -FS_ENOMEM;
    if (victim)
        r_detach(fs, victim);
    unhook(RN(node->parent), n);
    fs->mem.free(n->name);
    n->name = p;
    n->name_len = len;
    append(RN(dir), n);
    return 0;
}

static uint32_t r_entries(void *fsp, struct vnode *dir)
{
    (void)fsp;
    return RN(dir)->entries;
}

static void r_release(void *fsp, struct vnode *node)
{
    free_node(fsp, RN(node));
}

const struct vfs_ops ramfs_ops = {
    .lookup = r_lookup,
    .create = r_create,
    .detach = r_detach,
    .move = r_move,
    .entries = r_entries,
    .release = r_release,
};

const char *ramfs_name(const struct vnode *n, size_t *len)
{
    *len = RN(n)->name_len;
    return RN(n)->name ? RN(n)->name : "";
}

struct vnode *ramfs_first(const struct vnode *dir)
{
    struct ramfs_node *e = RN(dir)->first;
    return e ? &e->vn : 0;
}

struct vnode *ramfs_next(const struct vnode *n)
{
    struct ramfs_node *e = RN(n)->next;
    return e ? &e->vn : 0;
}
