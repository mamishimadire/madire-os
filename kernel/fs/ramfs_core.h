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
 * File data (whole pages, at most RAMFS_FILE_MAX bytes per file) comes in
 * stage 2; until then every file has size 0 and no page.
 */

#include <stddef.h>
#include <stdint.h>
#include "kernel/fs/vfs_core.h"

#define RAMFS_MAX_NODES   1024u
#define RAMFS_FILE_MAX    (2u * 1024 * 1024)    /* one index page of 512 frames */

struct ramfs_mem {
    void *(*alloc)(size_t bytes);       /* NULL when out of memory */
    void (*free)(void *p);
};

struct ramfs_node {
    struct vnode vn;                    /* first: a vnode pointer is a ramfs_node pointer */
    char *name;                         /* not NUL-terminated; NULL for the root */
    size_t name_len;
    struct ramfs_node *first, *last;    /* a folder's entries */
    struct ramfs_node *next;            /* the next entry of its folder */
    uint32_t entries;
};

struct ramfs {
    struct ramfs_mem mem;
    uint32_t max_nodes;
    uint32_t nodes;                     /* records allocated now, the root included */
    uint32_t data_pages;                /* stage 2 */
    uint64_t next_ino;
    struct ramfs_node *root;
};

/* An empty filesystem: the root folder (mode 0755) only. 0 or -FS_ENOMEM. */
int ramfs_init(struct ramfs *fs, const struct ramfs_mem *mem, uint32_t max_nodes);

/* The filesystem's half of the VFS (fs is a struct ramfs). */
extern const struct vfs_ops ramfs_ops;

/* The name of an entry (len 0 for the root). */
const char *ramfs_name(const struct vnode *n, size_t *len);

/* The first entry of a folder, and the entry after n, in creation order
 * (NULL at the end). */
struct vnode *ramfs_first(const struct vnode *dir);
struct vnode *ramfs_next(const struct vnode *n);

#endif
