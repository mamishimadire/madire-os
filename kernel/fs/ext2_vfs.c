/* ext2 behind the VFS (1.4 stage 8): see ext2_vfs.h. Pure; read-write
 * through a writer since stage 10. Every static is ext2_vn_-prefixed (the
 * unit suite includes this file and ext2_core.c in one translation unit),
 * and every byte loop is written out and no structure is copied whole (no
 * libc call in the kernel). */
#include "kernel/fs/ext2_vfs.h"
#include "kernel/fs/fs_errno.h"

#define EXT2_VN_SIZE_CAP 0xFFFFFFFFull  /* C7: files up to 4 GiB - 1 */
#define EXT2_VN_READ_MAX 0x40000000u    /* bytes per read call at most (the core takes 32 bits) */
#define EXT2_VN_GONE     0xFFFFFFFFFFFFFFFFull  /* stage 10: the byte of a removed node's entry */

static struct ext2_vfs_node *ext2_vn_node(const struct vnode *n)
{
    return (struct ext2_vfs_node *)n;   /* the vnode is the node's first member */
}

static struct ext2_vfs *ext2_vn_of(void *fsp)
{
    return fsp;
}

static void ext2_vn_zero(void *p, size_t n)
{
    uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        b[i] = 0;
}

/* A failed read: counted, the core's reason kept; -FS_EIO. */
static int ext2_vn_fail(struct ext2_vfs *x)
{
    x->errors++;
    x->why = x->fs->why ? x->fs->why : "a read failed";
    x->miss = -FS_EIO;
    return -FS_EIO;
}

static bool ext2_vn_is_dir(const struct ext2_inode *in)
{
    return (in->mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
}

/* "." or "..", which are not entries for the VFS. */
static bool ext2_vn_dots(const struct ext2_dirent *de)
{
    return de->name[0] == '.' && (de->name_len == 1 || (de->name_len == 2 && de->name[1] == '.'));
}

/*
 * Walks folder d from its first entry at byte `from` or later (from the
 * start of the block holding `from`: entries never cross a block), "." and
 * ".." skipped; with a name (len > 0), only the entry of that name. 1 with
 * x->de set, 0 at the end (or no such name), or -FS_EIO.
 */
static int ext2_vn_scan(struct ext2_vfs *x, const struct ext2_vfs_node *d, uint64_t from,
                        const char *name, size_t len)
{
    struct ext2_dir_iter it;
    it.off = from - from % x->fs->block_size;
    for (;;) {
        int r = ext2_dir_next(x->fs, &d->in, &it, &x->de);
        if (r < 0)
            return ext2_vn_fail(x);
        if (r == 0)
            return 0;
        if (x->de.off < from || ext2_vn_dots(&x->de))
            continue;
        if (len == 0)
            return 1;
        if ((size_t)x->de.name_len != len)
            continue;
        size_t i = 0;
        while (i < len && x->de.name[i] == name[i])
            i++;
        if (i == len)
            return 1;
    }
}

/* Sets node n's vnode from its inode; parent and name are the caller's. */
static void ext2_vn_fill(struct ext2_vfs_node *n)
{
    const struct ext2_inode *in = &n->in;
    bool dir = ext2_vn_is_dir(in);
    n->vn.type = dir ? VNODE_DIR : VNODE_FILE;
    n->vn.wr_next = n->vn.wr_serving = 0;
    n->vn.wr_holder = 0;
    n->vn.fs_id = 0;                    /* set by the VFS (vfs_core.c) */
    n->vn.ino = in->ino;
    n->vn.size = in->size;
    n->vn.mode = in->mode & 07777u;
    /* The VFS reads nlink as "has a name" (folders: 1); a file shows its
     * link count to stat (fsys.c). */
    n->vn.nlink = dir || in->links == 0 ? 1u : in->links;
    n->vn.opens = 0;
    n->vn.atime = in->atime;
    n->vn.mtime = in->mtime;
    n->vn.ctime = in->ctime;
}

/* Stage 10: node n out of the table and freed (its parent's count of
 * children lowered). */
static void ext2_vn_free(struct ext2_vfs *x, struct ext2_vfs_node *n)
{
    struct ext2_vfs_node **p = &x->nodes;
    while (*p && *p != n)
        p = &(*p)->next;
    if (*p)
        *p = n->next;
    if (n->vn.parent && n->vn.parent != &n->vn) {
        struct ext2_vfs_node *up = ext2_vn_node(n->vn.parent);
        if (up->children)
            up->children--;
    }
    if (n->name)
        x->mem.free(n->name);
    x->mem.free(n);
    x->count--;
}

/* Stage 10 (risk 43): frees the least recently used node nothing needs
 * (ext2_vfs.h); `keep` is the folder a new node goes into. True if one was
 * freed. */
static bool ext2_vn_evict(struct ext2_vfs *x, const struct ext2_vfs_node *keep)
{
    struct ext2_vfs_node *best = 0;
    for (struct ext2_vfs_node *n = x->nodes; n; n = n->next) {
        if (n == x->root || n == keep || n->vn.opens || n->children || n->vn.nlink == 0 ||
            n->stamp + EXT2_VFS_RECENT > x->clock)
            continue;
        if (!best || n->stamp < best->stamp)
            best = n;
    }
    if (!best)
        return false;
    ext2_vn_free(x, best);
    x->evicted++;
    return true;
}

/* The node of the entry at byte pos of folder d (inode ino, the name in
 * x->de): the one made before, or a new one; NULL if none can be made. */
static struct ext2_vfs_node *ext2_vn_get(struct ext2_vfs *x, struct ext2_vfs_node *d, uint32_t ino,
                                         uint64_t pos)
{
    for (struct ext2_vfs_node *n = x->nodes; n; n = n->next) {
        if (n->vn.parent == &d->vn && n->pos == pos && n != x->root) {
            n->stamp = ++x->clock;
            return n;
        }
    }
    if (x->count >= x->max && !ext2_vn_evict(x, d)) {
        x->full++;
        x->miss = -FS_ENOMEM;
        if (!x->full_said && x->mem.full) {
            x->full_said = true;
            x->mem.full(x->max);        /* once: the kernel logs it */
        }
        return 0;
    }
    struct ext2_vfs_node *n = x->mem.alloc(sizeof(*n));
    if (!n) {
        x->miss = -FS_ENOMEM;
        return 0;
    }
    ext2_vn_zero(n, sizeof(*n));
    uint32_t len = x->de.name_len;
    n->name = x->mem.alloc(len);
    if (!n->name) {
        x->mem.free(n);
        x->miss = -FS_ENOMEM;
        return 0;
    }
    for (uint32_t i = 0; i < len; i++)
        n->name[i] = x->de.name[i];
    n->name_len = len;
    if (ext2_inode_get(x->fs, ino, &n->in) != EXT2_OK) {
        ext2_vn_fail(x);
        x->mem.free(n->name);
        x->mem.free(n);
        return 0;
    }
    ext2_vn_fill(n);
    n->vn.parent = &d->vn;
    n->pos = pos;
    n->stamp = ++x->clock;
    d->children++;
    d->stamp = x->clock;
    n->next = x->nodes;
    x->nodes = n;
    x->count++;
    return n;
}

int ext2_vfs_init(struct ext2_vfs *x, struct ext2_fs *fs, const struct ext2_vfs_mem *mem,
                  uint32_t max_nodes)
{
    ext2_vn_zero(x, sizeof(*x));
    x->fs = fs;
    x->mem.alloc = mem->alloc;
    x->mem.free = mem->free;
    x->mem.full = mem->full;
    x->mem.reading = mem->reading;      /* stage 10 (NULL: none) */
    x->max = max_nodes;
    x->readonly = true;                 /* stage 8: no write path exists */
    uint64_t cap = ext2_max_size(fs);
    x->file_max = cap < EXT2_VN_SIZE_CAP ? cap : EXT2_VN_SIZE_CAP;
    struct ext2_vfs_node *r = mem->alloc(sizeof(*r));
    if (!r)
        return -FS_ENOMEM;
    ext2_vn_zero(r, sizeof(*r));
    if (ext2_inode_get(fs, EXT2_ROOT_INO, &r->in) != EXT2_OK || !ext2_vn_is_dir(&r->in)) {
        mem->free(r);
        return ext2_vn_fail(x);
    }
    ext2_vn_fill(r);
    r->vn.parent = &r->vn;              /* mount_add sets it again */
    r->name = 0;
    r->name_len = 0;
    r->pos = 0;
    x->root = x->nodes = r;
    x->count = 1;
    return 0;
}

/* ---- the ops table ---- */

static struct vnode *ext2_vn_lookup(void *fsp, struct vnode *dir, const char *name, size_t len)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    struct ext2_vfs_node *d = ext2_vn_node(dir);
    x->miss = 0;
    if (len == 0 || len > EXT2_NAME_MAX || ext2_vn_scan(x, d, 0, name, len) != 1)
        return 0;
    struct ext2_vfs_node *n = ext2_vn_get(x, d, x->de.ino, x->de.off);
    return n ? &n->vn : 0;
}

/* ---- stage 10: changes through the writer ---- */

static int ext2_vn_writable(void *fsp);

/* Node n's inode read again, and the vnode's facts from it (its parent,
 * position, write marks and filesystem number are kept). 0 or -FS_EIO. */
static int ext2_vn_reload(struct ext2_vfs *x, struct ext2_vfs_node *n)
{
    if (ext2_inode_get(x->fs, (uint32_t)n->in.ino, &n->in) != EXT2_OK)
        return ext2_vn_fail(x);
    bool dir = ext2_vn_is_dir(&n->in);
    n->vn.size = n->in.size;
    n->vn.mode = n->in.mode & 07777u;
    if (n->vn.nlink != 0)
        n->vn.nlink = dir || n->in.links == 0 ? 1u : n->in.links;
    n->vn.atime = n->in.atime;
    n->vn.mtime = n->in.mtime;
    n->vn.ctime = n->in.ctime;
    return 0;
}

/* Node n out of its folder: no name, no parent, no position. */
static void ext2_vn_unhook(struct ext2_vfs_node *n)
{
    if (n->vn.parent && n->vn.parent != &n->vn) {
        struct ext2_vfs_node *up = ext2_vn_node(n->vn.parent);
        if (up->children)
            up->children--;
    }
    n->vn.parent = 0;
    n->vn.nlink = 0;
    n->pos = EXT2_VN_GONE;
}

/* The inode an open file's removal must keep (0: none needed). */
static uint32_t ext2_vn_keep(const struct ext2_vfs_node *n)
{
    return n && n->vn.opens && n->vn.type == VNODE_FILE ? (uint32_t)n->in.ino : 0u;
}

static int ext2_vn_create(void *fsp, struct vnode *dir, const char *name, size_t len,
                          enum vnode_type type, uint32_t mode, struct vnode **out)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    struct ext2_vfs_node *d = ext2_vn_node(dir);
    int r = ext2_vn_writable(fsp);
    if (r < 0)
        return r;
    if (len > EXT2_NAME_MAX)
        return -FS_ENAMETOOLONG;
    uint32_t ino = 0;
    r = x->wr->create(x->wr->ctx, (uint32_t)d->in.ino, name, (uint32_t)len, type == VNODE_DIR, mode, &ino);
    if (r < 0)
        return r;
    x->changes++;
    if (ext2_vn_reload(x, d) < 0)
        return -FS_EIO;
    x->miss = 0;
    r = ext2_vn_scan(x, d, 0, name, len);
    if (r != 1)
        return r < 0 ? r : -FS_EIO;     /* the new entry must be there */
    struct ext2_vfs_node *n = ext2_vn_get(x, d, x->de.ino, x->de.off);
    if (!n)
        return x->miss ? x->miss : -FS_ENOMEM;
    *out = &n->vn;
    return 0;
}

/* Stage 8's detach, kept for the table; the VFS calls remove (below). */
static void ext2_vn_detach(void *fsp, struct vnode *node)
{
    (void)fsp;
    (void)node;
}

static int ext2_vn_remove(void *fsp, struct vnode *dir, struct vnode *node)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    struct ext2_vfs_node *d = ext2_vn_node(dir), *n = ext2_vn_node(node);
    int r = ext2_vn_writable(fsp);
    if (r < 0)
        return r;
    if (!n->name || n == x->root)
        return -FS_EBUSY;
    uint32_t keep = ext2_vn_keep(n);
    r = x->wr->remove(x->wr->ctx, (uint32_t)d->in.ino, n->name, n->name_len, node->type == VNODE_DIR, keep);
    if (r < 0)
        return r;
    x->changes++;
    n->orphan = keep != 0;
    ext2_vn_unhook(n);
    ext2_vn_reload(x, d);
    return 0;
}

static int ext2_vn_move(void *fsp, struct vnode *node, struct vnode *dir, const char *name, size_t len,
                        struct vnode *victim)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    struct ext2_vfs_node *n = ext2_vn_node(node), *nd = ext2_vn_node(dir);
    struct ext2_vfs_node *v = victim ? ext2_vn_node(victim) : 0;
    int r = ext2_vn_writable(fsp);
    if (r < 0)
        return r;
    if (!n->name || n == x->root || !n->vn.parent)
        return -FS_EBUSY;
    if (len == 0 || len > EXT2_NAME_MAX)
        return -FS_ENAMETOOLONG;
    struct ext2_vfs_node *od = ext2_vn_node(n->vn.parent);
    char *nm = x->mem.alloc(len);       /* first: after the rename nothing may fail */
    if (!nm)
        return -FS_ENOMEM;
    for (size_t i = 0; i < len; i++)
        nm[i] = name[i];
    uint32_t keep = ext2_vn_keep(v);
    r = x->wr->rename(x->wr->ctx, (uint32_t)od->in.ino, n->name, n->name_len, (uint32_t)nd->in.ino, name,
                      (uint32_t)len, keep);
    if (r < 0) {
        x->mem.free(nm);
        return r;
    }
    x->changes++;
    if (v) {
        v->orphan = keep != 0;
        ext2_vn_unhook(v);
    }
    if (od->children)
        od->children--;
    nd->children++;
    n->vn.parent = &nd->vn;
    x->mem.free(n->name);
    n->name = nm;
    n->name_len = (uint32_t)len;
    n->pos = EXT2_VN_GONE;
    x->miss = 0;
    if (ext2_vn_scan(x, nd, 0, name, len) == 1)
        n->pos = x->de.off;
    ext2_vn_reload(x, od);
    if (nd != od)
        ext2_vn_reload(x, nd);
    ext2_vn_reload(x, n);
    return 0;
}

static uint32_t ext2_vn_entries(void *fsp, struct vnode *dir)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    uint32_t count = 0;
    uint64_t from = 0;
    while (ext2_vn_scan(x, ext2_vn_node(dir), from, 0, 0) == 1) {
        count++;
        from = x->de.off + 1;
    }
    return count;
}

/* Stage 10: a node with no name and no hold is freed (the VFS calls this
 * only then); an orphan's inode is forgotten first (its R3 deletion then
 * runs). Stage 8 never reached it: no node lost its name. */
static void ext2_vn_release(void *fsp, struct vnode *node)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    struct ext2_vfs_node *n = ext2_vn_node(node);
    if (n == x->root || node->nlink != 0 || node->opens != 0)
        return;
    if (n->orphan && x->wr)
        x->wr->forget(x->wr->ctx, (uint32_t)n->in.ino);
    ext2_vn_free(x, n);
}

static int64_t ext2_vn_read(void *fsp, struct vnode *file, uint64_t off, void *buf, uint64_t len)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    struct ext2_vfs_node *n = ext2_vn_node(file);
    if (file->type == VNODE_DIR)
        return -FS_EISDIR;
    if (x->wr && ext2_vn_reload(x, n) < 0)     /* stage 10: the inode may have changed */
        return -FS_EIO;
    uint32_t want = len < EXT2_VN_READ_MAX ? (uint32_t)len : EXT2_VN_READ_MAX;
    if (x->mem.reading)
        x->mem.reading(true);
    int64_t r = ext2_read(x->fs, &n->in, off, buf, want);
    if (x->mem.reading)
        x->mem.reading(false);
    return r < 0 ? ext2_vn_fail(x) : r;
}

static int64_t ext2_vn_write(void *fsp, struct vnode *file, uint64_t off, const void *buf, uint64_t len)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    struct ext2_vfs_node *n = ext2_vn_node(file);
    if (file->type == VNODE_DIR)
        return -FS_EISDIR;
    int r = ext2_vn_writable(fsp);
    if (r < 0)
        return r;
    uint32_t want = len < EXT2_VN_READ_MAX ? (uint32_t)len : EXT2_VN_READ_MAX;
    int64_t got = x->wr->write(x->wr->ctx, (uint32_t)n->in.ino, off, buf, want);
    if (got < 0)
        return got;
    x->changes++;
    if (ext2_vn_reload(x, n) < 0)
        return -FS_EIO;
    if ((uint64_t)got < want)
        x->again = true;                /* stopped short: a save first, then the rest */
    return got;
}

/* The whole truncate in one step (the write core's own steps are bounded
 * by its reserve): 0 when done. */
static int ext2_vn_truncate_step(void *fsp, struct vnode *file, uint64_t length, uint32_t max_pages)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    struct ext2_vfs_node *n = ext2_vn_node(file);
    (void)max_pages;
    if (file->type == VNODE_DIR)
        return -FS_EISDIR;
    int r = ext2_vn_writable(fsp);
    if (r < 0)
        return r;
    r = x->wr->truncate(x->wr->ctx, (uint32_t)n->in.ino, length);
    if (r < 0)
        return r;
    x->changes++;
    return ext2_vn_reload(x, n) < 0 ? -FS_EIO : 0;
}

/* The 4 KiB pages of the blocks the file really uses (i_blocks counts
 * 512-byte units, its indirect and attribute blocks included); 0 for a
 * folder, as the RAM filesystem. Stage 10: 0 for a file with no name left
 * (its blocks go with R3's deletion after its last close, not by the
 * VFS's truncate in steps). */
static uint32_t ext2_vn_pages(void *fsp, const struct vnode *n)
{
    (void)fsp;
    if (n->type == VNODE_DIR || n->nlink == 0)
        return 0;
    return (ext2_vn_node(n)->in.blocks512 + 7u) / 8u;
}

static struct vnode *ext2_vn_entry_from(void *fsp, const struct vnode *dir, uint64_t pos)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    struct ext2_vfs_node *d = ext2_vn_node(dir);
    x->miss = 0;
    if (ext2_vn_scan(x, d, pos, 0, 0) != 1)
        return 0;
    struct ext2_vfs_node *n = ext2_vn_get(x, d, x->de.ino, x->de.off);
    return n ? &n->vn : 0;
}

static struct vnode *ext2_vn_entry_next(void *fsp, const struct vnode *dir, const struct vnode *e)
{
    return ext2_vn_entry_from(fsp, dir, ext2_vn_node(e)->pos + 1);
}

static const char *ext2_vn_name(void *fsp, const struct vnode *n, size_t *len)
{
    (void)fsp;
    const struct ext2_vfs_node *e = ext2_vn_node(n);
    *len = e->name ? e->name_len : 0;
    return e->name ? e->name : "";
}

static uint64_t ext2_vn_pos(void *fsp, const struct vnode *e)
{
    (void)fsp;
    return ext2_vn_node(e)->pos;
}

static int ext2_vn_error(void *fsp)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    int r = x->miss;
    x->miss = 0;
    return r;
}

static int ext2_vn_writable(void *fsp)
{
    struct ext2_vfs *x = ext2_vn_of(fsp);
    if (x->readonly || x->closed || !x->wr)
        return -FS_EROFS;
    return x->wr->writable(x->wr->ctx);
}

void ext2_vfs_set_writer(struct ext2_vfs *x, const struct ext2_vfs_writer *wr)
{
    x->wr = wr;
    x->readonly = wr == 0;
}

bool ext2_vfs_again(struct ext2_vfs *x)
{
    bool a = x->again;
    x->again = false;
    return a;
}

uint32_t ext2_vfs_forget_orphans(struct ext2_vfs *x)
{
    uint32_t n = 0;
    for (struct ext2_vfs_node *e = x->nodes; e; e = e->next) {
        if (e->orphan && x->wr) {
            x->wr->forget(x->wr->ctx, (uint32_t)e->in.ino);
            e->orphan = false;
            n++;
        }
    }
    return n;
}

static uint64_t ext2_vn_file_max(void *fsp)
{
    return ext2_vn_of(fsp)->file_max;
}

/* From the link count: a folder's ".." entries of its subfolders and its
 * own "." and name (links - 2). */
static uint32_t ext2_vn_subdirs(void *fsp, const struct vnode *dir)
{
    (void)fsp;
    uint32_t links = ext2_vn_node(dir)->in.links;
    return links >= 2 ? links - 2u : 0u;
}

const struct vfs_ops ext2_vfs_ops = {
    .lookup = ext2_vn_lookup,
    .create = ext2_vn_create,
    .detach = ext2_vn_detach,
    .move = ext2_vn_move,
    .entries = ext2_vn_entries,
    .release = ext2_vn_release,
    .read = ext2_vn_read,
    .write = ext2_vn_write,
    .truncate_step = ext2_vn_truncate_step,
    .pages = ext2_vn_pages,
    .entry_from = ext2_vn_entry_from,
    .entry_next = ext2_vn_entry_next,
    .name = ext2_vn_name,
    .pos = ext2_vn_pos,
    .writable = ext2_vn_writable,
    .file_max = ext2_vn_file_max,
    .subdirs = ext2_vn_subdirs,
    .error = ext2_vn_error,
    .remove = ext2_vn_remove,
};
