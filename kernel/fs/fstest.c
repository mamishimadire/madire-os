/*
 * The filesystem's storage diagnostics (1.0, stage 2): see fstest.h.
 *
 * Both work in a scratch folder under /tmp. File contents follow a pattern
 * of the file's number and the byte's position, so every byte read back
 * can be checked without keeping a copy. Every page or heap block the
 * tests hold themselves is chained through its own first word, so they
 * need no memory to remember them.
 */
#include <stddef.h>
#include <stdint.h>
#include "kernel/fs/fstest.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/vm_layout.h"

#define MAX_LOGGED 8u                   /* mismatch lines per test */

static struct ramfs *fs;
static struct vfs *v;
static const char *tag;                 /* "limits" or "fill" */
static unsigned mismatches, logged;
static uint8_t buf[2 * RAMFS_PAGE];
static char name[48];

static void want(int64_t got, int64_t expected, const char *what)
{
    if (got == expected)
        return;
    mismatches++;
    if (logged++ < MAX_LOGGED)
        klogf("Filesystem %s test: %s: got %lld, expected %lld\n", tag, what, (long long)got,
              (long long)expected);
}

static void want_true(bool ok, const char *what)
{
    want(ok, 1, what);
}

/* Logs the step's line with OK, or FAIL and the mismatches since the last. */
static bool step_end(const char *line, unsigned *since)
{
    bool ok = mismatches == *since;
    klogf("Filesystem %s test: %s: %s\n", tag, line, ok ? "OK" : "FAIL");
    *since = mismatches;
    return ok;
}

static uint8_t pattern(uint32_t file, uint64_t pos)
{
    return (uint8_t)(pos * 7 + (pos >> 12) + file * 131u + 3);
}

static void fill(uint32_t file, uint64_t pos, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        buf[i] = pattern(file, pos + i);
}

static const char *path(const char *dir, const char *prefix, uint32_t n)
{
    ksnprintf(name, sizeof(name), "%s/%s%u", dir, prefix, n);
    return name;
}

static struct vnode *create(const char *p, enum vnode_type type)
{
    struct vnode *n = 0;
    want(vfs_create(v, v->root, p, type, type == VNODE_DIR ? 0755 : 0644, &n), 0, "create");
    return n;
}

/* The figures a test must give back: nodes, data pages, free pages, and
 * what the heap took from the page allocator (it never gives that back). */
struct acct {
    uint32_t nodes, data, free, arena, tables;
};

static void acct_now(struct acct *a)
{
    a->nodes = fs->nodes;
    a->data = fs->data_pages;
    a->free = pmm_free_pages();
    a->arena = kheap_stats()->arena_bytes;
    a->tables = paging_stats()->page_tables;
}

/* Logs the closing line; true when everything came back exactly. */
static bool acct_end(const struct acct *a0, bool steps_ok)
{
    struct acct a1;
    acct_now(&a1);
    uint32_t heap_pages = (a1.arena - a0->arena) / RAMFS_PAGE, tables = a1.tables - a0->tables;
    bool back = a1.nodes == a0->nodes && a1.data == 0 && a0->data == 0 &&
                a1.free == a0->free - heap_pages - tables;
    bool ok = steps_ok && back && mismatches == 0;
    klogf("Filesystem %s test: %s (nodes %u -> %u, data pages %u -> %u, free pages %u -> %u = "
          "%u heap pages + %u page tables)\n", tag, ok ? "PASS" : "FAIL", a0->nodes, a1.nodes,
          a0->data, a1.data, a0->free, a1.free, heap_pages, tables);
    return ok;
}

/* ---- filling to the budget (both tests) ---- */

struct filled {
    uint32_t files;                     /* fill files dir/f0 .. f<files-1> */
    struct vnode *probe;                /* dir/probe: the short write's file */
    int64_t short_count;                /* what the write running out returned */
};

/*
 * Files of 4096-byte writes, 2 MiB each, until a write answers ENOSPC; the
 * pages in use must then be exactly the budget. One case needs help: a new
 * file whose index page fits but whose first data page does not (one page
 * left) gives its index page back, so the file before it gives back its
 * last page first. Then two pages are made free (the last two of a file
 * with three or more) and a new file's 8192-byte write at 2048 takes the
 * index and one data page and returns 2048: the short count; the next
 * write is ENOSPC. Finally the probe grows to 2 MiB by truncate (no page,
 * never ENOSPC) and a write into that hole is ENOSPC.
 */
static void fill_budget(const char *dir, struct filled *out)
{
    out->files = 0;
    out->probe = 0;
    out->short_count = 0;
    struct vnode *prev = 0, *f = 0;
    bool helped = false;
    for (;;) {
        f = create(path(dir, "f", out->files), VNODE_FILE);
        if (!f)
            return;
        out->files++;
        uint32_t k = 0;
        int64_t r = 0;
        while (k < RAMFS_SLOTS) {
            fill(out->files - 1, (uint64_t)k * RAMFS_PAGE, RAMFS_PAGE);
            r = ramfs_write(fs, f, (uint64_t)k * RAMFS_PAGE, buf, RAMFS_PAGE);
            if (r == RAMFS_PAGE) {
                k++;
                continue;
            }
            if (r == -FS_ENOSPC && k == 0 && !helped && prev &&
                fs->data_pages == fs->budget_pages - 1) {
                helped = true;
                want(ramfs_truncate(fs, prev, RAMFS_FILE_MAX - RAMFS_PAGE), 0, "make room");
                continue;
            }
            break;
        }
        if (k < RAMFS_SLOTS) {
            want(r, -FS_ENOSPC, "the write past the budget");
            break;
        }
        prev = f;
    }
    want(fs->data_pages, fs->budget_pages, "pages in use = the budget");
    if (ramfs_file_pages(f) == 0)       /* the file whose first write found no room */
        f = prev;
    struct vnode *x = f && ramfs_file_pages(f) >= 4 ? f : prev;
    if (!x || ramfs_file_pages(x) < 4) {
        want(0, 1, "a file with 3 data pages to shorten");
        return;
    }
    uint32_t last = (uint32_t)((x->size - 1) / RAMFS_PAGE);
    want(ramfs_truncate(fs, x, (uint64_t)(last - 1) * RAMFS_PAGE), 0, "give back 2 pages");
    want(fs->data_pages, fs->budget_pages - 2, "2 pages free");
    out->probe = create(path(dir, "probe", 0), VNODE_FILE);
    if (!out->probe)
        return;
    fill(out->files, 2048, 2 * RAMFS_PAGE);
    out->short_count = ramfs_write(fs, out->probe, 2048, buf, 2 * RAMFS_PAGE);
    want(out->short_count, 2048, "the write running out");
    want(ramfs_write(fs, out->probe, RAMFS_PAGE, buf, RAMFS_PAGE), -FS_ENOSPC, "the next write");
    want(fs->data_pages, fs->budget_pages, "pages in use = the budget again");
    want(ramfs_truncate(fs, out->probe, RAMFS_FILE_MAX), 0, "growing by truncate at the budget");
    want(fs->data_pages, fs->budget_pages, "growing took no page");
    want(ramfs_write(fs, out->probe, 1024 * 1024, buf, 1), -FS_ENOSPC, "a write into the grown hole");
}

/* Reads every fill file and the probe's data back; the bytes checked. */
static uint64_t verify(const char *dir, const struct filled *fd)
{
    uint64_t bytes = 0, bad = 0;
    for (uint32_t i = 0; i < fd->files; i++) {
        struct vnode *f = 0;
        if (vfs_lookup(v, v->root, path(dir, "f", i), &f) < 0) {
            want(0, 1, "look up a fill file");
            continue;
        }
        for (uint64_t pos = 0; pos < f->size; pos += RAMFS_PAGE) {
            int64_t r = ramfs_read(fs, f, pos, buf, RAMFS_PAGE);
            if (r <= 0) {
                bad++;
                break;
            }
            for (int64_t j = 0; j < r; j++)
                bad += buf[j] != pattern(i, pos + (uint64_t)j);
            bytes += (uint64_t)r;
        }
    }
    if (fd->probe) {
        want(ramfs_read(fs, fd->probe, 0, buf, RAMFS_PAGE), RAMFS_PAGE, "read the probe");
        for (uint32_t j = 0; j < RAMFS_PAGE; j++)
            bad += buf[j] != (j < 2048 ? 0 : pattern(fd->files, j));
        bytes += RAMFS_PAGE;
    }
    want((int64_t)bad, 0, "bytes read back wrong");
    return bytes;
}

static void remove_fill(const char *dir, const struct filled *fd)
{
    for (uint32_t i = 0; i < fd->files; i++)
        want(vfs_unlink(v, v->root, path(dir, "f", i)), 0, "unlink a fill file");
    if (fd->probe)
        want(vfs_unlink(v, v->root, path(dir, "probe", 0)), 0, "unlink the probe");
    want(vfs_rmdir(v, v->root, dir), 0, "rmdir");
}

static bool begin(const char *t)
{
    fs = vfs_ramfs();
    v = vfs_root();
    tag = t;
    mismatches = logged = 0;
    if (!fs->root || !fs->pages) {
        klogf("Filesystem %s test: FAIL (no filesystem)\n", tag);
        return false;
    }
    return true;
}

/* ---- test=fslimits ---- */

#define LDIR "/tmp/limits"

static bool limit_nodes(unsigned *since)
{
    uint32_t before = fs->nodes, made = 0;
    int r = 0;
    while (made <= RAMFS_MAX_NODES) {
        r = vfs_create(v, v->root, path(LDIR, "n", made), VNODE_FILE, 0644, 0);
        if (r < 0)
            break;
        made++;
    }
    want(r, -FS_ENOSPC, "the file past 1024 nodes");
    want(fs->nodes, RAMFS_MAX_NODES, "nodes at the limit");
    struct vnode *n = 0;
    want(vfs_create(v, v->root, LDIR "/dir", VNODE_DIR, 0755, 0), -FS_ENOSPC,
         "the folder past 1024 nodes");
    want(fs->nodes, RAMFS_MAX_NODES, "nothing created");
    want(vfs_lookup(v, v->root, LDIR "/dir", &n), -FS_ENOENT, "the folder not there");
    want(vfs_lookup(v, v->root, path(LDIR, "n", made), &n), -FS_ENOENT, "the file not there");
    for (uint32_t i = 0; i < made; i++)
        want(vfs_unlink(v, v->root, path(LDIR, "n", i)), 0, "unlink");
    want(fs->nodes, before, "all removed");
    char line[200];
    ksnprintf(line, sizeof(line), "nodes: %u files created up to %u nodes; the next file ENOSPC, "
              "the next folder ENOSPC, nothing created; all removed", made, RAMFS_MAX_NODES);
    return step_end(line, since);
}

static bool limit_size(unsigned *since)
{
    struct vnode *f = create(LDIR "/big", VNODE_FILE);
    if (!f)
        return step_end("file size", since);
    uint32_t full = 0;
    for (uint32_t k = 0; k < RAMFS_SLOTS; k++) {
        fill(1, (uint64_t)k * RAMFS_PAGE, RAMFS_PAGE);
        full += ramfs_write(fs, f, (uint64_t)k * RAMFS_PAGE, buf, RAMFS_PAGE) == RAMFS_PAGE;
    }
    want(full, RAMFS_SLOTS, "512 writes of 4096 bytes");
    want(ramfs_file_pages(f), RAMFS_SLOTS + 1, "513 pages with the index");
    want(ramfs_write(fs, f, RAMFS_FILE_MAX, buf, 1), -FS_EFBIG, "one more byte");
    want(ramfs_truncate(fs, f, RAMFS_FILE_MAX - 100), 0, "truncate to 2 MiB - 100");
    fill(1, RAMFS_FILE_MAX - 100, RAMFS_PAGE);
    int64_t crossing = ramfs_write(fs, f, RAMFS_FILE_MAX - 100, buf, RAMFS_PAGE);
    want(crossing, 100, "a write crossing 2 MiB");
    want(ramfs_write(fs, f, 3u * 1024 * 1024, buf, 1), -FS_EFBIG, "a write at 3 MiB");
    want(ramfs_truncate(fs, f, RAMFS_FILE_MAX + 1), -FS_EFBIG, "truncate to 2 MiB + 1");
    want((int64_t)f->size, RAMFS_FILE_MAX, "the size unchanged");
    want(ramfs_truncate(fs, f, RAMFS_FILE_MAX), 0, "truncate to 2 MiB");
    uint64_t bad = 0;
    for (uint32_t k = 0; k < RAMFS_SLOTS; k++) {
        want(ramfs_read(fs, f, (uint64_t)k * RAMFS_PAGE, buf, RAMFS_PAGE), RAMFS_PAGE, "read");
        for (uint32_t j = 0; j < RAMFS_PAGE; j++)
            bad += buf[j] != pattern(1, (uint64_t)k * RAMFS_PAGE + j);
    }
    want((int64_t)bad, 0, "bytes read back wrong");
    uint32_t data = fs->data_pages;
    want(vfs_unlink(v, v->root, LDIR "/big"), 0, "unlink");
    want(fs->data_pages, data - (RAMFS_SLOTS + 1), "513 pages given back");
    char line[300];
    ksnprintf(line, sizeof(line), "file size: 2 MiB written in 512 writes of 4096 bytes and read "
              "back (513 pages with the index); one more byte EFBIG; a write crossing 2 MiB "
              "returned %lld of 4096 bytes; a write at 3 MiB EFBIG; truncate to 2 MiB + 1 EFBIG, "
              "to 2 MiB OK", (long long)crossing);
    return step_end(line, since);
}

static bool limit_hole(unsigned *since)
{
    struct vnode *f = create(LDIR "/hole", VNODE_FILE);
    if (!f)
        return step_end("hole", since);
    uint32_t data = fs->data_pages;
    buf[0] = 0xa5;
    want(ramfs_write(fs, f, 1024 * 1024, buf, 1), 1, "a byte at 1 MiB");
    want(ramfs_file_pages(f), 2, "index + data");
    want(fs->data_pages, data + 2, "2 pages in use");
    uint64_t bad = 0;
    for (uint32_t k = 0; k < 256; k++) {
        want(ramfs_read(fs, f, (uint64_t)k * RAMFS_PAGE, buf, RAMFS_PAGE), RAMFS_PAGE, "read");
        for (uint32_t j = 0; j < RAMFS_PAGE; j++)
            bad += buf[j] != 0;
    }
    want((int64_t)bad, 0, "hole bytes not zero");
    want(ramfs_read(fs, f, 1024 * 1024, buf, RAMFS_PAGE), 1, "read the byte");
    want(buf[0], 0xa5, "the byte");
    want(vfs_unlink(v, v->root, LDIR "/hole"), 0, "unlink");
    want(fs->data_pages, data, "given back");
    return step_end("hole: one byte written at 1 MiB costs 2 pages (index + data); the 1 MiB "
                    "before it reads as zeros", since);
}

static bool limit_allocator(unsigned *since)
{
    struct vnode *f1 = create(LDIR "/p1", VNODE_FILE), *f2 = create(LDIR "/p2", VNODE_FILE);
    if (!f1 || !f2)
        return step_end("page allocator", since);
    buf[0] = 1;
    want(ramfs_write(fs, f1, 0, buf, 1), 1, "a byte in p1");
    uint32_t taken = 0, page;
    uint64_t head = 0;
    while ((page = memory_alloc_page()) != 0) {
        volatile uint64_t *w = vmm_window_map(page);
        w[0] = head;
        vmm_window_unmap();
        head = page;
        taken++;
    }
    uint32_t data = fs->data_pages;
    want_true(data < fs->budget_pages, "under the budget");
    want(ramfs_write(fs, f2, 0, buf, 1), -FS_ENOSPC, "a new file's first write");
    want(ramfs_file_pages(f2), 0, "no index page left behind");
    want(ramfs_write(fs, f1, 2 * RAMFS_PAGE, buf, 1), -FS_ENOSPC, "a new page of a file");
    want(ramfs_file_pages(f1), 2, "p1 unchanged");
    want(fs->data_pages, data, "no page taken");
    want(ramfs_write(fs, f1, 100, buf, 1), 1, "an existing page");
    uint32_t back = 0;
    while (head) {
        const volatile uint64_t *w = vmm_window_map(head);
        uint64_t next = w[0];
        vmm_window_unmap();
        memory_free_page((uint32_t)head);
        head = next;
        back++;
    }
    want(back, taken, "every page given back");
    want(ramfs_write(fs, f2, 0, buf, 1), 1, "p2 once pages are back");
    want(vfs_unlink(v, v->root, LDIR "/p1"), 0, "unlink p1");
    want(vfs_unlink(v, v->root, LDIR "/p2"), 0, "unlink p2");
    char line[300];
    ksnprintf(line, sizeof(line), "page allocator: all %u free pages taken, under the budget: a "
              "new file's first write ENOSPC with no index page left behind, a new page of a "
              "file ENOSPC, an existing page still written; all pages given back", taken);
    return step_end(line, since);
}

static bool limit_budget(unsigned *since)
{
    struct filled fd = { 0, 0, 0 };
    if (create(LDIR "/b", VNODE_DIR))
        fill_budget(LDIR "/b", &fd);
    want(vfs_rmdir(v, v->root, LDIR "/b"), -FS_ENOTEMPTY, "the folder holds the files");
    remove_fill(LDIR "/b", &fd);
    want(fs->data_pages, 0, "every page given back");
    char line[360];
    ksnprintf(line, sizeof(line), "budget: %u files filled to %u pages = the budget, the next "
              "write ENOSPC; a write running out returned %lld of 8192 bytes, the next ENOSPC; "
              "growing a file by truncate takes no page and is never ENOSPC, a write into the "
              "grown hole ENOSPC", fd.files, fs->budget_pages, (long long)fd.short_count);
    return step_end(line, since);
}

static bool limit_heap(unsigned *since)
{
    uint32_t nodes = fs->nodes;
    /* The block a node record will fit in exactly, taken before the heap
     * fills up, wherever it lies. */
    void *slot = kmalloc(sizeof(struct ramfs_node));
    static const uint32_t sizes[] = { 65536 - 16, 4096 - 16, 16 };
    void *head = 0;
    uint32_t blocks = 0;
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        void *p;
        while ((p = kmalloc(sizes[k])) != 0) {
            *(void **)p = head;
            head = p;
            blocks++;
        }
    }
    want_true(slot != 0, "the record's block");
    want(kheap_stats()->arena_bytes, VM_HEAP_MAX, "the heap at its 16 MiB cap");
    struct vnode *n = 0;
    want(vfs_create(v, v->root, LDIR "/m", VNODE_FILE, 0644, 0), -FS_ENOMEM,
         "a file with no room for its record");
    want(fs->nodes, nodes, "nothing created");
    if (slot)
        kfree(slot);
    uint32_t used = kheap_stats()->used_bytes;
    want(vfs_create(v, v->root, LDIR "/m", VNODE_DIR, 0755, 0), -FS_ENOMEM,
         "a folder whose record fits but not its name");
    want(fs->nodes, nodes, "nothing created");
    want(kheap_stats()->used_bytes, used, "its record given back");
    want(vfs_lookup(v, v->root, LDIR "/m", &n), -FS_ENOENT, "the name not there");
    while (head) {
        void *next = *(void **)head;
        kfree(head);
        head = next;
    }
    want(vfs_create(v, v->root, LDIR "/m", VNODE_FILE, 0644, 0), 0, "a file once the heap is back");
    want(vfs_unlink(v, v->root, LDIR "/m"), 0, "unlink");
    char line[300];
    ksnprintf(line, sizeof(line), "heap: filled to its 16 MiB cap (%u blocks): a file ENOMEM (no "
              "room for its record), a folder ENOMEM (its record fit, its name did not); nothing "
              "half-created; the heap given back", blocks);
    return step_end(line, since);
}

bool fstest_limits(void)
{
    if (!begin("limits"))
        return false;
    struct acct a0;
    acct_now(&a0);
    klogf("Filesystem limits test: budget %u pages, %u nodes in use, %u free pages\n",
          fs->budget_pages, a0.nodes, a0.free);
    unsigned since = 0;
    bool ok = create(LDIR, VNODE_DIR) != 0;
    ok = limit_nodes(&since) && ok;
    ok = limit_size(&since) && ok;
    ok = limit_hole(&since) && ok;
    ok = limit_allocator(&since) && ok;
    ok = limit_budget(&since) && ok;
    ok = limit_heap(&since) && ok;
    want(vfs_rmdir(v, v->root, LDIR), 0, "rmdir " LDIR);
    return acct_end(&a0, ok);
}

/* ---- test=fsfill ---- */

#define FDIR "/tmp/fill"

bool fstest_fill(void)
{
    if (!begin("fill"))
        return false;
    struct acct a0;
    acct_now(&a0);
    klogf("Filesystem fill test: budget %u pages = min(%u, 3/4 of %u free pages at boot); "
          "%u free pages now\n", fs->budget_pages, RAMFS_BUDGET_CAP, vfs_boot_free_pages(), a0.free);
    unsigned since = 0;
    bool ok = create(FDIR, VNODE_DIR) != 0;
    struct filled fd = { 0, 0, 0 };
    if (ok)
        fill_budget(FDIR, &fd);
    uint64_t bytes = verify(FDIR, &fd);
    char line[360];
    ksnprintf(line, sizeof(line), "%u files filled to %u pages (data + index) = the budget, the "
              "next write ENOSPC; a write running out returned %lld of 8192 bytes, the next "
              "ENOSPC; growing a file by truncate took no page; every byte read back (%llu bytes)",
              fd.files, fs->budget_pages, (long long)fd.short_count, (unsigned long long)bytes);
    ok = step_end(line, &since) && ok;
    remove_fill(FDIR, &fd);
    return acct_end(&a0, ok);
}
