#ifndef MADIRE_FS_RAMFS_CORE_H
#define MADIRE_FS_RAMFS_CORE_H

/*
 * The RAM filesystem (1.0), pure: tests/unit/test_ramfs.c. The filesystem
 * of Live mode: every file and folder lives in memory only.
 *
 * Node records and names are taken through the allocator given at init
 * (the kernel heap in the kernel, malloc in the unit tests), each at its
 * exact size; at most max_nodes nodes, the root included (RAMFS_MAX_NODES
 * in the kernel): creating one more is ENOSPC, a failed allocation ENOMEM,
 * and nothing is half-created either way. A folder keeps its entries in
 * creation order. Node numbers start at 1 (the root) and are never reused.
 *
 * File data (stage 2) lives in whole pages from the page source given with
 * ramfs_set_pages (the page allocator through the physical-access window
 * in the kernel, kernel/fs/fspage.c; a pool in tests/unit/test_fspage.c),
 * never in the allocator above. Each file has at most RAMFS_FILE_MAX bytes:
 * one index page of 512 frame numbers, taken at the file's first data page
 * and given back with its last, then a page per 4 KiB written. Pages are
 * taken as files grow and given back at once when a file shrinks or its
 * node is freed. Every page, index pages included, counts against the
 * budget. Never-written pages (holes) read as zeros and cost nothing, and
 * the bytes of a page past the file's end are always zero.
 */

#include <stddef.h>
#include <stdint.h>
#include "kernel/fs/vfs_core.h"

#define RAMFS_MAX_NODES   1024u
#define RAMFS_FILE_MAX    (2u * 1024 * 1024)    /* one index page of 512 frames */
#define RAMFS_PAGE        4096u
#define RAMFS_SLOTS       (RAMFS_FILE_MAX / RAMFS_PAGE)
#define RAMFS_BUDGET_CAP  8192u                 /* pages: 32 MiB */

struct ramfs_mem {
    void *(*alloc)(size_t bytes);       /* NULL when out of memory */
    void (*free)(void *p);
};

/* Where file pages come from. A frame number is the page's physical
 * address (0 is never a frame). One frame is mapped at a time, from map
 * to unmap. */
struct ramfs_pages {
    uint64_t (*alloc)(void);            /* a page with any contents, or 0 */
    void (*free)(uint64_t frame);
    volatile void *(*map)(uint64_t frame);
    void (*unmap)(void);
};

struct ramfs_node {
    struct vnode vn;                    /* first: a vnode pointer is a ramfs_node pointer */
    char *name;                         /* not NUL-terminated; NULL for the root */
    size_t name_len;
    struct ramfs_node *first, *last;    /* a folder's entries */
    struct ramfs_node *next;            /* the next entry of its folder */
    uint32_t entries;
    uint32_t pages;                     /* a file's pages, its index page included */
    uint64_t index;                     /* a file's index page, 0 while it has no data page */
};

struct ramfs {
    struct ramfs_mem mem;
    uint32_t max_nodes;
    uint32_t nodes;                     /* records allocated now, the root included */
    uint32_t data_pages;                /* file pages in use now, index pages included */
    uint64_t next_ino;
    struct ramfs_node *root;
    const struct ramfs_pages *pages;    /* NULL: no page source, every write ENOSPC */
    uint32_t budget_pages;
};

/* An empty filesystem: the root folder (mode 0755) only. 0 or -FS_ENOMEM.
 * No page source and a budget of 0 until ramfs_set_pages. */
int ramfs_init(struct ramfs *fs, const struct ramfs_mem *mem, uint32_t max_nodes);

/* The budget for a machine with free_pages free pages at boot:
 * min(RAMFS_BUDGET_CAP, 3/4 of them), rounded down (docs/plans/1.0.md, B5). */
uint32_t ramfs_budget_pages(uint32_t free_pages);

/* The page source and the budget, in pages (set once, before any write). */
void ramfs_set_pages(struct ramfs *fs, const struct ramfs_pages *pages, uint32_t budget_pages);

/*
 * File data. Each returns -FS_EISDIR for a folder.
 *
 * ramfs_write: len bytes at off; the count written, or an error when
 * nothing was. A write reaching past RAMFS_FILE_MAX writes up to it (a
 * short count); at or past it: -FS_EFBIG. Running out of the budget, or
 * the page source refusing a page, ends the write early: the short count,
 * or -FS_ENOSPC with nothing written (and no page left taken). len 0: 0.
 *
 * ramfs_read: up to len bytes at off, never past the file's end (0 at or
 * past it); holes read as zeros.
 *
 * ramfs_truncate: the size becomes length. Shrinking gives back every page
 * wholly past the end (and the index page with the last one) and zeroes
 * the rest of the last page; growing adds a hole and takes no page, so it
 * is never ENOSPC (running out shows at write time). Past RAMFS_FILE_MAX:
 * -FS_EFBIG, nothing changed.
 */
int64_t ramfs_write(struct ramfs *fs, struct vnode *file, uint64_t off, const void *buf,
                    uint64_t len);
int64_t ramfs_read(struct ramfs *fs, struct vnode *file, uint64_t off, void *buf, uint64_t len);
int ramfs_truncate(struct ramfs *fs, struct vnode *file, uint64_t length);

/* A file's pages now, its index page included (0 for a folder). */
uint32_t ramfs_file_pages(const struct vnode *file);

/* The filesystem's half of the VFS (fs is a struct ramfs). */
extern const struct vfs_ops ramfs_ops;

/* The name of an entry (len 0 for the root). */
const char *ramfs_name(const struct vnode *n, size_t *len);

/* The first entry of a folder, and the entry after n, in creation order
 * (NULL at the end). */
struct vnode *ramfs_first(const struct vnode *dir);
struct vnode *ramfs_next(const struct vnode *n);

#endif
