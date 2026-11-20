/* A fake filesystem in memory for the Files core's tests (1.5 stage 11):
 * tests/unit/test_files_core.c and tests/filesdump.c include it, and
 * tests/files_ref.py models it exactly. Nodes are kept in the order they
 * were made (a freed node's slot is never used again); node 0 is the root
 * folder. A folder lists its entries in that order. A path is looked up
 * from the root, one component at a time; a link (or a file) in the middle
 * of a path answers -ENOTDIR, as Madire's VFS does (rule SL: no link is
 * ever followed). A file holds `size` bytes made from its seed; a copy
 * keeps the seed. ffs_fail_at makes the n-th changing call (mkdir, unlink,
 * rmdir, copy_chunk) fail with -ENOMEM; ffs_calls counts them. */
#ifndef MADIRE_TESTS_FILES_FAKEFS_H
#define MADIRE_TESTS_FILES_FAKEFS_H

#include <stdint.h>
#include <string.h>
#include "kernel/files/files_core.h"

#define FFS_NODES  6000
#define FFS_CHUNK  4096u
#define FFS_ENOENT  (-2)
#define FFS_ENOMEM  (-12)
#define FFS_EEXIST  (-17)
#define FFS_ENOTDIR (-20)
#define FFS_EISDIR  (-21)
#define FFS_ENOTEMPTY (-39)

struct ffs_node {
    int used, parent, kind;
    char name[FILES_NAME_MAX + 1];
    uint64_t size;
    uint32_t seed;
};

static struct ffs_node ffs_node[FFS_NODES];
static int ffs_count;
static int ffs_fail_at, ffs_calls;

static void ffs_reset(void)
{
    memset(ffs_node, 0, sizeof(ffs_node));
    ffs_node[0].used = 1;
    ffs_node[0].parent = -1;
    ffs_node[0].kind = FILES_K_DIR;
    ffs_count = 1;
    ffs_fail_at = 0;
    ffs_calls = 0;
}

static int ffs_child(int dir, const char *name, size_t len)
{
    for (int i = 1; i < ffs_count; i++)
        if (ffs_node[i].used && ffs_node[i].parent == dir && strlen(ffs_node[i].name) == len &&
            memcmp(ffs_node[i].name, name, len) == 0)
            return i;
    return -1;
}

static int ffs_add(int parent, int kind, const char *name, uint64_t size, uint32_t seed)
{
    if (ffs_count >= FFS_NODES || strlen(name) > FILES_NAME_MAX)
        return -1;
    struct ffs_node *n = &ffs_node[ffs_count];
    n->used = 1;
    n->parent = parent;
    n->kind = kind;
    strcpy(n->name, name);
    n->size = size;
    n->seed = seed;
    return ffs_count++;
}

/* The node of path, or a negative error: -ENOENT, -ENOTDIR (a component
 * that is not a folder). *parent: the folder that holds (or would hold) the
 * last component, or -1. */
static int ffs_lookup(const char *path, int *parent)
{
    int cur = 0;
    *parent = -1;
    if (path[0] != '/')
        return FFS_ENOENT;
    const char *p = path + 1;
    if (!*p)
        return 0;
    for (;;) {
        const char *e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (ffs_node[cur].kind != FILES_K_DIR) {
            *parent = -1;
            return FFS_ENOTDIR;
        }
        *parent = cur;
        int c = ffs_child(cur, p, len);
        if (!e)
            return c < 0 ? FFS_ENOENT : c;
        if (c < 0) {
            *parent = -1;
            return FFS_ENOENT;
        }
        cur = c;
        p = e + 1;
    }
}

static const char *ffs_base(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static int ffs_change(void)
{
    ffs_calls++;
    return ffs_fail_at && ffs_calls == ffs_fail_at;
}

static enum files_kind ffs_lstat(void *ctx, const char *path)
{
    int parent, n = ffs_lookup(path, &parent);
    (void)ctx;
    if (n == FFS_ENOENT)
        return FILES_K_NONE;
    if (n < 0)
        return FILES_K_ERROR;
    return (enum files_kind)ffs_node[n].kind;
}

static int ffs_read_dir(void *ctx, const char *path, uint64_t *cookie, char *name)
{
    int parent, n = ffs_lookup(path, &parent);
    (void)ctx;
    if (n < 0)
        return n;
    if (ffs_node[n].kind != FILES_K_DIR)
        return FFS_ENOTDIR;
    for (int i = (int)*cookie + 1; i < ffs_count; i++)
        if (ffs_node[i].used && ffs_node[i].parent == n) {
            strcpy(name, ffs_node[i].name);
            *cookie = (uint64_t)i;
            return 1;
        }
    return 0;
}

static int ffs_mkdir(void *ctx, const char *path)
{
    int parent, n = ffs_lookup(path, &parent);
    (void)ctx;
    if (ffs_change())
        return FFS_ENOMEM;
    if (n >= 0)
        return FFS_EEXIST;
    if (parent < 0)
        return n;
    return ffs_add(parent, FILES_K_DIR, ffs_base(path), 0, 0) < 0 ? FFS_ENOMEM : 0;
}

static int ffs_unlink(void *ctx, const char *path)
{
    int parent, n = ffs_lookup(path, &parent);
    (void)ctx;
    if (ffs_change())
        return FFS_ENOMEM;
    if (n < 0)
        return n;
    if (ffs_node[n].kind == FILES_K_DIR)
        return FFS_EISDIR;
    ffs_node[n].used = 0;
    return 0;
}

static int ffs_rmdir(void *ctx, const char *path)
{
    int parent, n = ffs_lookup(path, &parent);
    (void)ctx;
    if (ffs_change())
        return FFS_ENOMEM;
    if (n < 0)
        return n;
    if (ffs_node[n].kind != FILES_K_DIR)
        return FFS_ENOTDIR;
    if (n == 0)
        return FFS_ENOTEMPTY;
    for (int i = 1; i < ffs_count; i++)
        if (ffs_node[i].used && ffs_node[i].parent == n)
            return FFS_ENOTEMPTY;
    ffs_node[n].used = 0;
    return 0;
}

static int64_t ffs_copy_chunk(void *ctx, const char *src, const char *dst, uint64_t offset)
{
    int sp, dp, s = ffs_lookup(src, &sp), d = ffs_lookup(dst, &dp);
    (void)ctx;
    if (ffs_change())
        return FFS_ENOMEM;
    if (s < 0)
        return s;
    if (ffs_node[s].kind != FILES_K_FILE)
        return FFS_EISDIR;
    if (offset == 0) {
        if (d >= 0)
            return FFS_EEXIST;
        if (dp < 0)
            return d;
        d = ffs_add(dp, FILES_K_FILE, ffs_base(dst), 0, ffs_node[s].seed);
        if (d < 0)
            return FFS_ENOMEM;
    } else if (d < 0) {
        return d;
    }
    if (offset >= ffs_node[s].size)
        return 0;
    uint64_t n = ffs_node[s].size - offset;
    if (n > FFS_CHUNK)
        n = FFS_CHUNK;
    ffs_node[d].size += n;
    return (int64_t)n;
}

static const struct files_fs ffs_fs = {
    0, ffs_lstat, ffs_read_dir, ffs_mkdir, ffs_unlink, ffs_rmdir, ffs_copy_chunk,
};

/* The node's full path into out. */
__attribute__((unused)) static void ffs_path(int n, char *out)
{
    if (n == 0) {
        strcpy(out, "/");
        return;
    }
    char tmp[FILES_PATH_MAX * 2 + 2];
    tmp[0] = 0;
    for (int c = n; c > 0; c = ffs_node[c].parent) {
        char part[FILES_NAME_MAX + 2];
        part[0] = '/';
        strcpy(part + 1, ffs_node[c].name);
        size_t pl = strlen(part), tl = strlen(tmp);
        memmove(tmp + pl, tmp, tl + 1);
        memcpy(tmp, part, pl);
    }
    strcpy(out, tmp);
}

/* Makes the folders of path (absolute) and a node of kind at its end. */
__attribute__((unused)) static int ffs_make(const char *path, int kind, uint64_t size, uint32_t seed)
{
    int cur = 0;
    const char *p = path + 1;
    for (;;) {
        const char *e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char name[FILES_NAME_MAX + 1];
        memcpy(name, p, len);
        name[len] = 0;
        int c = ffs_child(cur, name, len);
        if (!e)
            return c >= 0 ? c : ffs_add(cur, kind, name, size, seed);
        if (c < 0)
            c = ffs_add(cur, FILES_K_DIR, name, 0, 0);
        cur = c;
        p = e + 1;
    }
}

#endif
