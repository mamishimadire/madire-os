/*
 * The RAM filesystem (1.0), pure: see ramfs_core.h. The host unit tests
 * build this file too (tests/unit/test_ramfs.c, tests/unit/test_fspage.c).
 *
 * A file's data: an index page of RAMFS_SLOTS frame numbers (slot i holds
 * bytes i * 4096 to i * 4096 + 4095; 0 for a hole), and the data pages.
 * Every page is reached through the page source's one mapping at a time,
 * so the index is read or written one entry per mapping.
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
    n->vn.wr_next = n->vn.wr_serving = 0;   /* stage 5: the write mark free */
    n->vn.wr_holder = n->vn.wr_spare = 0;
    n->name = 0;
    n->name_len = 0;
    n->first = n->last = n->next = 0;
    n->entries = 0;
    n->pages = 0;
    n->index = 0;
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
    fs->pages = 0;
    fs->budget_pages = 0;
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

static void free_pages_from(struct ramfs *fs, struct ramfs_node *n, uint32_t first_slot);

static void r_release(void *fsp, struct vnode *node)
{
    if (node->type == VNODE_FILE)
        free_pages_from(fsp, RN(node), 0);
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

/* ---- file data (stage 2) ---- */

uint32_t ramfs_budget_pages(uint32_t free_pages)
{
    uint64_t b = (uint64_t)free_pages * 3 / 4;
    return b < RAMFS_BUDGET_CAP ? (uint32_t)b : RAMFS_BUDGET_CAP;
}

void ramfs_set_pages(struct ramfs *fs, const struct ramfs_pages *pages, uint32_t budget_pages)
{
    fs->pages = pages;
    fs->budget_pages = budget_pages;
}

uint32_t ramfs_file_pages(const struct vnode *file)
{
    return file->type == VNODE_FILE ? RN(file)->pages : 0;
}

/* A page under the budget, or 0 (the budget is used up, or the source has none). */
static uint64_t take_page(struct ramfs *fs)
{
    if (!fs->pages || fs->data_pages >= fs->budget_pages)
        return 0;
    uint64_t frame = fs->pages->alloc();
    if (frame)
        fs->data_pages++;
    return frame;
}

static void give_page(struct ramfs *fs, uint64_t frame)
{
    fs->pages->free(frame);
    fs->data_pages--;
}

static uint64_t slot_get(struct ramfs *fs, const struct ramfs_node *n, uint32_t slot)
{
    if (!n->index)
        return 0;
    const volatile uint64_t *ix = fs->pages->map(n->index);
    uint64_t frame = ix[slot];
    fs->pages->unmap();
    return frame;
}

static void slot_set(struct ramfs *fs, struct ramfs_node *n, uint32_t slot, uint64_t frame)
{
    volatile uint64_t *ix = fs->pages->map(n->index);
    ix[slot] = frame;
    fs->pages->unmap();
}

/* len bytes into the frame at `at`; a fresh frame gets zeros around them,
 * so it never shows what the page held before. */
static void page_put(struct ramfs *fs, uint64_t frame, uint32_t at, const uint8_t *src,
                     uint32_t len, bool fresh)
{
    volatile uint8_t *p = fs->pages->map(frame);
    if (fresh) {
        for (uint32_t i = 0; i < at; i++)
            p[i] = 0;
        for (uint32_t i = at + len; i < RAMFS_PAGE; i++)
            p[i] = 0;
    }
    for (uint32_t i = 0; i < len; i++)
        p[at + i] = src[i];
    fs->pages->unmap();
}

static void page_get(struct ramfs *fs, uint64_t frame, uint32_t at, uint8_t *dst, uint32_t len)
{
    const volatile uint8_t *p = fs->pages->map(frame);
    for (uint32_t i = 0; i < len; i++)
        dst[i] = p[at + i];
    fs->pages->unmap();
}

/* Gives back the data pages in slots first_slot and up, and the index page
 * once no data page is left. */
static void free_pages_from(struct ramfs *fs, struct ramfs_node *n, uint32_t first_slot)
{
    if (!n->index)
        return;
    for (uint32_t s = first_slot; s < RAMFS_SLOTS && n->pages > 1; s++) {
        volatile uint64_t *ix = fs->pages->map(n->index);
        uint64_t frame = ix[s];
        if (frame)
            ix[s] = 0;
        fs->pages->unmap();
        if (frame) {
            give_page(fs, frame);
            n->pages--;
        }
    }
    if (n->pages == 1) {
        give_page(fs, n->index);
        n->index = 0;
        n->pages = 0;
    }
}

int64_t ramfs_write(struct ramfs *fs, struct vnode *file, uint64_t off, const void *buf,
                    uint64_t len)
{
    if (file->type != VNODE_FILE)
        return -FS_EISDIR;
    if (len == 0)
        return 0;
    if (off >= RAMFS_FILE_MAX)
        return -FS_EFBIG;
    if (len > RAMFS_FILE_MAX - off)
        len = RAMFS_FILE_MAX - off;
    struct ramfs_node *n = RN(file);
    const uint8_t *src = buf;
    bool new_index = false;
    if (!n->index) {
        uint64_t ix = take_page(fs);
        if (!ix)
            return -FS_ENOSPC;
        volatile uint64_t *w = fs->pages->map(ix);
        for (uint32_t i = 0; i < RAMFS_SLOTS; i++)
            w[i] = 0;
        fs->pages->unmap();
        n->index = ix;
        n->pages = 1;
        new_index = true;
    }
    uint64_t done = 0;
    while (done < len) {
        uint64_t pos = off + done;
        uint32_t slot = (uint32_t)(pos / RAMFS_PAGE), at = (uint32_t)(pos % RAMFS_PAGE);
        uint32_t chunk = RAMFS_PAGE - at;
        if (chunk > len - done)
            chunk = (uint32_t)(len - done);
        uint64_t frame = slot_get(fs, n, slot);
        bool fresh = frame == 0;
        if (fresh) {
            frame = take_page(fs);
            if (!frame)
                break;                  /* out of budget or pages: a short count */
        }
        page_put(fs, frame, at, src + done, chunk, fresh);
        if (fresh) {
            slot_set(fs, n, slot, frame);
            n->pages++;
        }
        done += chunk;
    }
    if (done == 0) {
        if (new_index) {                /* no data page came: no index page stays */
            give_page(fs, n->index);
            n->index = 0;
            n->pages = 0;
        }
        return -FS_ENOSPC;
    }
    if (off + done > file->size)
        file->size = off + done;
    return (int64_t)done;
}

int64_t ramfs_read(struct ramfs *fs, struct vnode *file, uint64_t off, void *buf, uint64_t len)
{
    if (file->type != VNODE_FILE)
        return -FS_EISDIR;
    if (off >= file->size)
        return 0;
    if (len > file->size - off)
        len = file->size - off;
    struct ramfs_node *n = RN(file);
    uint8_t *dst = buf;
    uint64_t done = 0;
    while (done < len) {
        uint64_t pos = off + done;
        uint32_t slot = (uint32_t)(pos / RAMFS_PAGE), at = (uint32_t)(pos % RAMFS_PAGE);
        uint32_t chunk = RAMFS_PAGE - at;
        if (chunk > len - done)
            chunk = (uint32_t)(len - done);
        uint64_t frame = slot_get(fs, n, slot);
        if (frame) {
            page_get(fs, frame, at, dst + done, chunk);
        } else {
            for (uint32_t i = 0; i < chunk; i++)
                dst[done + i] = 0;      /* a hole */
        }
        done += chunk;
    }
    return (int64_t)done;
}

int ramfs_truncate(struct ramfs *fs, struct vnode *file, uint64_t length)
{
    if (file->type != VNODE_FILE)
        return -FS_EISDIR;
    if (length > RAMFS_FILE_MAX)
        return -FS_EFBIG;
    struct ramfs_node *n = RN(file);
    if (length < file->size && n->index) {
        uint32_t at = (uint32_t)(length % RAMFS_PAGE);
        if (at) {                       /* the rest of the last page reads as zeros */
            uint64_t frame = slot_get(fs, n, (uint32_t)(length / RAMFS_PAGE));
            if (frame) {
                volatile uint8_t *p = fs->pages->map(frame);
                for (uint32_t i = at; i < RAMFS_PAGE; i++)
                    p[i] = 0;
                fs->pages->unmap();
            }
        }
        free_pages_from(fs, n, (uint32_t)((length + RAMFS_PAGE - 1) / RAMFS_PAGE));
    }
    file->size = length;                /* growing: a hole, no page taken */
    return 0;
}

int ramfs_truncate_step(struct ramfs *fs, struct vnode *file, uint64_t length,
                        uint32_t max_pages)
{
    if (file->type != VNODE_FILE)
        return -FS_EISDIR;
    if (length > RAMFS_FILE_MAX)
        return -FS_EFBIG;
    struct ramfs_node *n = RN(file);
    if (length < file->size && n->index) {
        uint32_t at = (uint32_t)(length % RAMFS_PAGE);
        if (at) {                       /* the rest of the last page reads as zeros */
            uint64_t frame = slot_get(fs, n, (uint32_t)(length / RAMFS_PAGE));
            if (frame) {
                volatile uint8_t *p = fs->pages->map(frame);
                for (uint32_t i = at; i < RAMFS_PAGE; i++)
                    p[i] = 0;
                fs->pages->unmap();
            }
        }
    }
    file->size = length;
    if (!n->index)
        return 0;
    if (max_pages == 0)
        max_pages = 1;
    if (max_pages > RAMFS_TRUNC_STEP_MAX)
        max_pages = RAMFS_TRUNC_STEP_MAX;
    uint64_t frames[RAMFS_TRUNC_STEP_MAX];
    uint32_t got = 0;
    bool more = false;
    volatile uint64_t *ix = fs->pages->map(n->index);
    for (uint32_t s = (uint32_t)((length + RAMFS_PAGE - 1) / RAMFS_PAGE); s < RAMFS_SLOTS; s++) {
        if (!ix[s])
            continue;
        if (got == max_pages) {
            more = true;
            break;
        }
        frames[got++] = ix[s];
        ix[s] = 0;
    }
    fs->pages->unmap();
    /* The index page goes with the last data page, within max_pages: if
     * the step is full, it goes in the next one. */
    if (!more && n->pages - got == 1 && got == max_pages)
        more = true;
    for (uint32_t i = 0; i < got; i++) {
        give_page(fs, frames[i]);
        n->pages--;
    }
    if (!more && n->pages == 1) {       /* no data page left: the index page goes too */
        give_page(fs, n->index);
        n->index = 0;
        n->pages = 0;
    }
    return more ? 1 : 0;
}
