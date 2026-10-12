/*
 * The kernel's filesystem (1.0): see vfs.h. Node records and names come
 * from the kernel heap; times are seconds of uptime for now (the RTC date
 * is added with stat, stage 5).
 *
 * The boot self-test works in a scratch folder, /tmp/fstest, and leaves
 * nothing behind: the node count and the heap's bytes in use are compared
 * before and after, and (stage 2) the data pages and the page allocator's
 * free pages around its data step. Its long paths and its data are built
 * in static buffers, not on the boot stack.
 *
 * Stage 2: file data in pages from the page allocator (fspage.c), within
 * the budget fixed here at boot: min(32 MiB, 3/4 of the free pages now).
 * The budget is a ceiling, not an allocation: no page is taken at boot.
 *
 * Stage 3 (D10): one lock, vfs_lock(), for the whole filesystem
 * (kernel/proc/klock.h). The boot takes it like every later user: around
 * building the root folders and around each step of the self-test (thread
 * 0 is the only user then, so it never waits).
 */
#include <stddef.h>
#include <stdint.h>
#include "kernel/fs/vfs.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/fspage.h"
#include "kernel/fs/path_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/pmm.h"
#include "kernel/proc/klock.h"
#include "kernel/timer.h"

static struct ramfs ramfs;
static struct vfs vfs;
static uint32_t boot_free_pages;        /* the free pages the budget was computed from */
static struct klock lock;               /* stage 3: the VFS lock */

static void *heap_alloc_fn(size_t bytes)
{
    return bytes > 0xFFFFFFFFu ? 0 : kmalloc((uint32_t)bytes);
}

static int64_t uptime_now(void)
{
    return (int64_t)timer_uptime_seconds();
}

struct vfs *vfs_root(void)
{
    return &vfs;
}

struct ramfs *vfs_ramfs(void)
{
    return &ramfs;
}

uint32_t vfs_boot_free_pages(void)
{
    return boot_free_pages;
}

struct klock *vfs_lock(void)
{
    return &lock;
}

/* ---- the boot self-test ---- */

static unsigned mismatches;             /* in the current step */
static char longpath[PATH_MAX_BYTES + 8];

static void want(int got, int expected, const char *what)
{
    if (got == expected)
        return;
    mismatches++;
    klogf("Filesystem self-test: %s: got %d (%s), expected %d (%s)\n", what, got,
          got ? fs_errno_name(got) : "OK", expected, expected ? fs_errno_name(expected) : "OK");
}

static void want_true(int ok, const char *what)
{
    if (ok)
        return;
    mismatches++;
    klogf("Filesystem self-test: %s: wrong\n", what);
}

static struct vnode *find(const char *path)
{
    struct vnode *n = 0;
    return vfs_lookup(&vfs, vfs.root, path, &n) == 0 ? n : 0;
}

static bool step_end(const char *line)
{
    bool ok = mismatches == 0;
    if (ok)
        klogf("Filesystem self-test: %s: OK\n", line);
    else
        klogf("Filesystem self-test: %s: FAIL (%u mismatches)\n", line, mismatches);
    mismatches = 0;
    return ok;
}

static bool test_create(void)
{
    static const char *const dirs[] = { "/tmp/fstest", "/tmp/fstest/a", "/tmp/fstest/a/b" };
    static const char *const files[] = { "/tmp/fstest/f1", "/tmp/fstest/f2", "/tmp/fstest/f3",
                                         "/tmp/fstest/f4", "/tmp/fstest/a/b/x" };
    static const char *const rel[] = { ".", "a", "a/b", "f1", "f2", "f3", "f4", "a/b/x" };
    struct vnode *made[8];
    unsigned k = 0;
    for (unsigned i = 0; i < 3; i++)
        want(vfs_create(&vfs, vfs.root, dirs[i], VNODE_DIR, 0755, &made[k++]), 0, dirs[i]);
    for (unsigned i = 0; i < 5; i++)
        want(vfs_create(&vfs, vfs.root, files[i], VNODE_FILE, 0644, &made[k++]), 0, files[i]);
    if (mismatches)
        return step_end("create and look up");
    struct vnode *top = made[0];
    /* made[] is in the order of rel[]: the folders, then the files. */
    for (unsigned i = 0; i < 8; i++) {
        struct vnode *n = 0;
        const char *apath = i < 3 ? dirs[i] : files[i - 3];
        want_true(find(apath) == made[i], apath);
        want(vfs_lookup(&vfs, top, rel[i], &n), 0, rel[i]);
        want_true(n == made[i], rel[i]);
        want_true(made[i]->type == (i < 3 ? VNODE_DIR : VNODE_FILE), "node type");
        for (unsigned j = 0; j < i; j++)
            want_true(made[i]->ino != made[j]->ino, "node numbers distinct");
    }
    return step_end("create and look up: 3 folders and 5 files under /tmp/fstest, "
                    "each found by its absolute and its relative path");
}

static bool test_paths(void)
{
    struct vnode *fstest = find("/tmp/fstest"), *a = find("/tmp/fstest/a"), *n = 0;
    want_true(find("/tmp/fstest/./a/../a/b") == find("/tmp/fstest/a/b"), "'.' and '..'");
    want_true(find("/../../tmp") == find("/tmp"), "'..' of '/'");
    want_true(find("//tmp///fstest//a") == a, "repeated '/'");
    want_true(find("/tmp/fstest/a/") == a, "a trailing '/' on a folder");
    want(vfs_lookup(&vfs, vfs.root, "/tmp/fstest/f1/", &n), -FS_ENOTDIR, "a trailing '/' on a file");
    want(vfs_lookup(&vfs, vfs.root, "", &n), -FS_ENOENT, "the empty path");
    want(vfs_lookup(&vfs, fstest, ".", &n), 0, "'.' from a start folder");
    want_true(n == fstest, "'.' is the start folder");
    want_true(find("/") == vfs.root && find("///") == vfs.root, "'/' is the root");
    return step_end("paths: '.', '..', '..' of '/', repeated '/', a trailing '/' "
                    "(folders only), the empty path");
}

/* longpath = "/" + "./" x reps + tail. */
static void build_dots(unsigned reps, const char *tail)
{
    size_t p = 0;
    longpath[p++] = '/';
    for (unsigned i = 0; i < reps; i++) {
        longpath[p++] = '.';
        longpath[p++] = '/';
    }
    while (*tail)
        longpath[p++] = *tail++;
    longpath[p] = '\0';
}

static size_t str_len(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

static bool test_limits(void)
{
    uint32_t nodes = ramfs.nodes;
    static const char dir[] = "/tmp/fstest/";
    size_t d = sizeof(dir) - 1;
    for (size_t i = 0; i < d; i++)
        longpath[i] = dir[i];
    for (size_t i = 0; i < PATH_NAME_MAX; i++)
        longpath[d + i] = 'n';
    longpath[d + PATH_NAME_MAX] = '\0';
    want(vfs_create(&vfs, vfs.root, longpath, VNODE_FILE, 0644, 0), 0, "a 255-byte name");
    want_true(find(longpath) != 0, "the 255-byte name found");
    want(vfs_unlink(&vfs, vfs.root, longpath), 0, "the 255-byte name removed");
    longpath[d + PATH_NAME_MAX] = 'n';
    longpath[d + PATH_NAME_MAX + 1] = '\0';
    want(vfs_create(&vfs, vfs.root, longpath, VNODE_FILE, 0644, 0), -FS_ENAMETOOLONG,
         "a 256-byte name");
    build_dots(2045, "tmp/");                   /* 1 + 4090 + 4 = 4095 bytes */
    want_true(str_len(longpath) == PATH_MAX_BYTES - 1, "the 4095-byte path's length");
    want_true(find(longpath) == find("/tmp"), "a 4095-byte path");
    build_dots(2045, "tmp//");                  /* 4096 bytes: no room for the NUL */
    struct vnode *n = 0;
    want(vfs_lookup(&vfs, vfs.root, longpath, &n), -FS_ENAMETOOLONG, "a 4096-byte path");
    want(vfs_create(&vfs, vfs.root, longpath, VNODE_DIR, 0755, 0), -FS_ENAMETOOLONG,
         "a 4096-byte path (create)");
    want_true(ramfs.nodes == nodes, "nothing created");
    return step_end("limits: a 255-byte name created and found, 256 bytes ENAMETOOLONG; "
                    "a 4095-byte path found, 4096 bytes ENAMETOOLONG; nothing created");
}

static bool test_errors(void)
{
    struct vnode *n = 0, *r = vfs.root;
    uint32_t nodes = ramfs.nodes;
    unsigned cases = 0;
#define CASE(expr, err, what) do { want((expr), -(err), what); cases++; } while (0)
    CASE(vfs_create(&vfs, r, "/tmp/fstest/f1", VNODE_FILE, 0644, 0), FS_EEXIST, "create an existing file");
    CASE(vfs_create(&vfs, r, "/tmp/fstest/a", VNODE_DIR, 0755, 0), FS_EEXIST, "mkdir an existing folder");
    CASE(vfs_create(&vfs, r, "/", VNODE_DIR, 0755, 0), FS_EEXIST, "mkdir /");
    CASE(vfs_lookup(&vfs, r, "/tmp/fstest/nosuch", &n), FS_ENOENT, "look up a missing name");
    CASE(vfs_create(&vfs, r, "/tmp/fstest/nosuch/x", VNODE_FILE, 0644, 0), FS_ENOENT, "create in a missing folder");
    CASE(vfs_create(&vfs, r, "/tmp/fstest/f1/x", VNODE_FILE, 0644, 0), FS_ENOTDIR, "create under a file");
    CASE(vfs_lookup(&vfs, r, "/tmp/fstest/f1/x", &n), FS_ENOTDIR, "look up under a file");
    CASE(vfs_create(&vfs, r, "/tmp/fstest/newf/", VNODE_FILE, 0644, 0), FS_EISDIR, "create a file named with a trailing '/'");
    CASE(vfs_unlink(&vfs, r, "/tmp/fstest/a"), FS_EISDIR, "unlink a folder");
    CASE(vfs_unlink(&vfs, r, "/tmp/fstest/f1/"), FS_ENOTDIR, "unlink a file named with a trailing '/'");
    CASE(vfs_rmdir(&vfs, r, "/tmp/fstest/f1"), FS_ENOTDIR, "rmdir a file");
    CASE(vfs_rmdir(&vfs, r, "/tmp/fstest/a"), FS_ENOTEMPTY, "rmdir a folder with entries");
    CASE(vfs_rmdir(&vfs, r, "/tmp/fstest/a/."), FS_EINVAL, "rmdir a last '.'");
    CASE(vfs_rmdir(&vfs, r, "/tmp/fstest/a/.."), FS_ENOTEMPTY, "rmdir a last '..'");
    CASE(vfs_rmdir(&vfs, r, "/"), FS_EBUSY, "rmdir /");
    CASE(vfs_rename(&vfs, r, "/", r, "/tmp/x"), FS_EBUSY, "rename /");
#undef CASE
    want_true(ramfs.nodes == nodes, "nothing created or removed");
    char line[120];
    ksnprintf(line, sizeof(line), "errors: %u cases answered as Linux does (EEXIST, ENOENT, "
              "ENOTDIR, EISDIR, ENOTEMPTY, EINVAL, EBUSY)", cases);
    return step_end(line);
}

static bool test_rename(void)
{
    struct vnode *r = vfs.root;
    struct vnode *f1 = find("/tmp/fstest/f1"), *f2 = find("/tmp/fstest/f2");
    struct vnode *f3 = find("/tmp/fstest/f3"), *a = find("/tmp/fstest/a");
    uint32_t nodes = ramfs.nodes;
    want(vfs_rename(&vfs, r, "/tmp/fstest/f1", r, "/tmp/fstest/f1r"), 0, "rename in place");
    want_true(find("/tmp/fstest/f1r") == f1 && !find("/tmp/fstest/f1"), "the new name only");
    want(vfs_rename(&vfs, r, "/tmp/fstest/f2", r, "/tmp/fstest/a/b/f2"), 0, "into another folder");
    want_true(find("/tmp/fstest/a/b/f2") == f2 && !find("/tmp/fstest/f2"), "moved");
    want(vfs_rename(&vfs, r, "/tmp/fstest/f3", r, "/tmp/fstest/f4"), 0, "over a file");
    want_true(find("/tmp/fstest/f4") == f3 && !find("/tmp/fstest/f3"), "replaced");
    want_true(ramfs.nodes == nodes - 1, "the replaced file freed");
    want(vfs_rename(&vfs, r, "/tmp/fstest/a", r, "/tmp/fstest/c"), 0, "a folder");
    want_true(find("/tmp/fstest/c") == a && find("/tmp/fstest/c/b/x") != 0, "its contents follow");
    want(vfs_rename(&vfs, r, "/tmp/fstest/c", r, "/tmp/fstest/c/b/d"), -FS_EINVAL,
         "a folder into its own subtree");
    want(vfs_rename(&vfs, r, "/tmp/fstest/f4", r, "/tmp/fstest/./f4"), 0, "the same node");
    want_true(find("/tmp/fstest/f4") == f3, "the same node unchanged");
    want(vfs_create(&vfs, r, "/tmp/fstest/e", VNODE_DIR, 0755, 0), 0, "mkdir e");
    struct vnode *b = find("/tmp/fstest/c/b");
    want(vfs_rename(&vfs, r, "/tmp/fstest/c/b", r, "/tmp/fstest/e"), 0,
         "a folder over an empty folder");
    want_true(find("/tmp/fstest/e") == b && find("/tmp/fstest/e/x") != 0, "the folder replaced");
    want(vfs_rename(&vfs, r, "/tmp/fstest/f1r", r, "/tmp/fstest/e"), -FS_EISDIR,
         "a file onto a folder");
    want(vfs_rename(&vfs, r, "/tmp/fstest/e", r, "/tmp/fstest/f1r"), -FS_ENOTDIR,
         "a folder onto a file");
    return step_end("rename: in place, into another folder, over a file, a folder over an "
                    "empty folder; into its own subtree EINVAL, a file onto a folder EISDIR, "
                    "a folder onto a file ENOTDIR, the same node unchanged");
}

#define DEPTH 64u

static bool test_depth(void)
{
    static const char base[] = "/tmp/fstest/deep";
    uint32_t nodes = ramfs.nodes;
    size_t p = sizeof(base) - 1;
    for (size_t i = 0; i < p; i++)
        longpath[i] = base[i];
    longpath[p] = '\0';
    for (unsigned i = 0; i < DEPTH; i++) {
        if (i) {
            longpath[p++] = '/';
            longpath[p++] = 'd';
            longpath[p] = '\0';
        }
        want(vfs_create(&vfs, vfs.root, longpath, VNODE_DIR, 0755, 0), 0, "a folder of the chain");
    }
    struct vnode *bottom = find(longpath);
    want_true(bottom != 0 && bottom->type == VNODE_DIR, "the chain's last folder by one path");
    want_true(ramfs.nodes == nodes + DEPTH, "64 folders");
    for (unsigned i = 0; i < DEPTH; i++) {
        want(vfs_rmdir(&vfs, vfs.root, longpath), 0, "a folder of the chain removed");
        while (p > 0 && longpath[p - 1] != '/')
            p--;
        if (p > 0)
            p--;
        longpath[p] = '\0';
    }
    want_true(ramfs.nodes == nodes, "the chain removed");
    return step_end("depth: a chain of 64 folders created, looked up by one path, removed");
}

/* ---- stage 2: file data ---- */

static uint8_t databuf[RAMFS_PAGE];

/* The byte the data step writes at file position pos. */
static uint8_t pattern(uint64_t pos)
{
    return (uint8_t)(pos * 7 + (pos >> 12) + 3);
}

static void fill(uint64_t pos, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        databuf[i] = pattern(pos + i);
}

/* databuf[from, from + len) holds the bytes of file positions pos on. */
static bool holds(uint32_t from, uint64_t pos, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        if (databuf[from + i] != pattern(pos + i))
            return false;
    return true;
}

static bool zeros(uint32_t from, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        if (databuf[from + i])
            return false;
    return true;
}

static bool test_data(void)
{
    struct vnode *r = vfs.root, *f = 0;
    uint32_t data0 = ramfs.data_pages;
    want(vfs_create(&vfs, r, "/tmp/fstest/data", VNODE_FILE, 0644, &f), 0, "create data");
    if (!f)
        return step_end("data");
    uint32_t free0 = pmm_free_pages();
    fill(100, RAMFS_PAGE);
    want((int)ramfs_write(&ramfs, f, 100, databuf, RAMFS_PAGE), RAMFS_PAGE, "write 4096 bytes at 100");
    fill(100 + RAMFS_PAGE, RAMFS_PAGE);
    want((int)ramfs_write(&ramfs, f, 100 + RAMFS_PAGE, databuf, RAMFS_PAGE), RAMFS_PAGE,
         "write 4096 bytes at 4196");
    want_true(f->size == 100 + 2 * RAMFS_PAGE, "size 8292");
    want_true(ramfs_file_pages(f) == 4 && ramfs.data_pages == data0 + 4 &&
              pmm_free_pages() == free0 - 4, "3 data pages and the index page taken");
    want((int)ramfs_read(&ramfs, f, 100, databuf, RAMFS_PAGE), RAMFS_PAGE, "read 4096 at 100");
    want_true(holds(0, 100, RAMFS_PAGE), "bytes 100-4195 read back");
    want((int)ramfs_read(&ramfs, f, 100 + RAMFS_PAGE, databuf, RAMFS_PAGE), RAMFS_PAGE,
         "read 4096 at 4196");
    want_true(holds(0, 100 + RAMFS_PAGE, RAMFS_PAGE), "bytes 4196-8291 read back");
    want((int)ramfs_read(&ramfs, f, 0, databuf, 100), 100, "read 100 at 0");
    want_true(zeros(0, 100), "bytes before the first write read as zeros");
    want((int)ramfs_read(&ramfs, f, 100 + 2 * RAMFS_PAGE, databuf, 10), 0, "read at the end");

    databuf[0] = 0x5a;
    want((int)ramfs_write(&ramfs, f, 1024 * 1024, databuf, 1), 1, "write a byte at 1 MiB");
    want_true(ramfs_file_pages(f) == 5 && pmm_free_pages() == free0 - 5,
              "the byte at 1 MiB costs one page");
    want((int)ramfs_read(&ramfs, f, 512 * 1024, databuf, RAMFS_PAGE), RAMFS_PAGE, "read in the hole");
    want_true(zeros(0, RAMFS_PAGE), "the hole reads as zeros");
    want((int)ramfs_read(&ramfs, f, 1024 * 1024, databuf, RAMFS_PAGE), 1, "read the last byte");
    want_true(databuf[0] == 0x5a, "the byte at 1 MiB read back");

    want(ramfs_truncate(&ramfs, f, 5000), 0, "truncate to 5000");
    want_true(f->size == 5000 && ramfs_file_pages(f) == 3 && pmm_free_pages() == free0 - 3,
              "the truncate gives back 2 pages");
    want(ramfs_truncate(&ramfs, f, RAMFS_FILE_MAX), 0, "truncate to 2 MiB");
    want_true(f->size == RAMFS_FILE_MAX && ramfs_file_pages(f) == 3 &&
              pmm_free_pages() == free0 - 3, "growing takes no page");
    want((int)ramfs_read(&ramfs, f, RAMFS_PAGE, databuf, RAMFS_PAGE), RAMFS_PAGE,
         "read the page holding byte 5000");
    want_true(holds(0, RAMFS_PAGE, 5000 - RAMFS_PAGE) &&
              zeros(5000 - RAMFS_PAGE, 2 * RAMFS_PAGE - 5000), "bytes past 5000 read as zeros");
    want(ramfs_truncate(&ramfs, f, RAMFS_FILE_MAX + 1), -FS_EFBIG, "truncate past 2 MiB");
    want(vfs_unlink(&vfs, r, "/tmp/fstest/data"), 0, "unlink data");
    uint32_t free1 = pmm_free_pages();
    want_true(ramfs.data_pages == data0 && free1 == free0, "every page given back");
    char line[240];
    ksnprintf(line, sizeof(line), "data: 8192 bytes written across 3 pages and read back, a hole "
              "at 1 MiB reads as zeros and costs nothing, truncate frees pages and growth takes "
              "none; data pages %u -> %u, free pages %u -> %u", data0, ramfs.data_pages, free0, free1);
    return step_end(line);
}

static bool test_cleanup(uint32_t nodes0, uint32_t heap0)
{
    struct vnode *r = vfs.root;
    struct vnode *held = find("/tmp/fstest/f4");
    uint32_t nodes = ramfs.nodes;
    vfs_hold(held);
    want(vfs_unlink(&vfs, r, "/tmp/fstest/f4"), 0, "unlink a held file");
    want_true(ramfs.nodes == nodes && !find("/tmp/fstest/f4") && held->nlink == 0,
              "the held file outlives its name");
    vfs_drop(&vfs, held);
    want_true(ramfs.nodes == nodes - 1, "the held file freed at its drop");
    want(vfs_unlink(&vfs, r, "/tmp/fstest/e/x"), 0, "unlink e/x");
    want(vfs_unlink(&vfs, r, "/tmp/fstest/e/f2"), 0, "unlink e/f2");
    want(vfs_rmdir(&vfs, r, "/tmp/fstest/e"), 0, "rmdir e");
    want(vfs_rmdir(&vfs, r, "/tmp/fstest/c"), 0, "rmdir c");
    want(vfs_unlink(&vfs, r, "/tmp/fstest/f1r"), 0, "unlink f1r");
    want(vfs_rmdir(&vfs, r, "/tmp/fstest"), 0, "rmdir /tmp/fstest");
    uint32_t heap1 = kheap_stats()->used_bytes;
    want_true(ramfs.nodes == nodes0, "every node freed");
    want_true(heap1 == heap0, "every heap byte freed");
    char line[160];
    ksnprintf(line, sizeof(line), "cleanup: everything removed (a held file freed at its drop); "
              "nodes %u -> %u, heap in use %u -> %u bytes", nodes0, ramfs.nodes, heap0, heap1);
    return step_end(line);
}

static bool selftest(void)
{
    uint32_t nodes0 = ramfs.nodes;
    uint32_t heap0 = kheap_stats()->used_bytes;
    mismatches = 0;
    static bool (*const steps[])(void) = {
        test_create, test_paths, test_limits, test_errors, test_rename, test_depth, test_data,
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        klock_acquire(&lock);           /* stage 3: each step under the VFS lock */
        ok = steps[i]() && ok;
        klock_release(&lock);
    }
    klock_acquire(&lock);
    ok = test_cleanup(nodes0, heap0) && ok;
    klock_release(&lock);
    klogf("Filesystem self-test: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

/* The root and its folders (under the VFS lock). */
static bool build(void)
{
    static const struct ramfs_mem mem = { .alloc = heap_alloc_fn, .free = kfree };
    if (ramfs_init(&ramfs, &mem, RAMFS_MAX_NODES) < 0) {
        klogf("Filesystem: no heap memory for the root folder\n");
        return false;
    }
    boot_free_pages = pmm_free_pages();
    ramfs_set_pages(&ramfs, &fspage_source, ramfs_budget_pages(boot_free_pages));
    vfs_mount(&vfs, &ramfs_ops, &ramfs, &ramfs.root->vn, uptime_now);
    ramfs.root->vn.atime = ramfs.root->vn.mtime = ramfs.root->vn.ctime = uptime_now();
    int rh = vfs_create(&vfs, vfs.root, "/home", VNODE_DIR, 0755, 0);
    int rt = vfs_create(&vfs, vfs.root, "/tmp", VNODE_DIR, 01777, 0);
    if (rh < 0 || rt < 0) {
        klogf("Filesystem: cannot create /home (%d) or /tmp (%d)\n", rh, rt);
        return false;
    }
    klogf("Filesystem: live RAM filesystem, %u nodes, %u MiB per file, budget %u pages\n",
          RAMFS_MAX_NODES, RAMFS_FILE_MAX / (1024u * 1024u), ramfs.budget_pages);
    klogf("Filesystem: budget: min(%u pages, 3/4 of %u free pages) = %u pages (%u KiB), "
          "0 taken at boot\n", RAMFS_BUDGET_CAP, boot_free_pages, ramfs.budget_pages,
          ramfs.budget_pages * (RAMFS_PAGE / 1024u));
    char names[64];
    size_t p = 0;
    for (struct vnode *e = ramfs_first(vfs.root); e && p + 20 < sizeof(names); e = ramfs_next(e)) {
        size_t len;
        const char *s = ramfs_name(e, &len);
        if (p) {
            names[p++] = ',';
            names[p++] = ' ';
        }
        for (size_t i = 0; i < len && i < 16; i++)
            names[p++] = s[i];
    }
    names[p] = '\0';
    klogf("Filesystem: / holds %s; %u nodes, %u data pages\n", names, ramfs.nodes,
          ramfs.data_pages);
    return true;
}

bool vfs_boot(void)
{
    klock_init(&lock);
    klock_acquire(&lock);
    bool built = build();
    klock_release(&lock);
    return built && selftest();
}
