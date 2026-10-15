/* Unit tests for the 1.0 file system calls, the pure part
 * (kernel/fs/fsys_core.c, stage 5): open's flags and decisions, trailing
 * slashes, the 144-byte stat structure, lseek's arithmetic, newfstatat's
 * flags, the clock's epoch seconds, the page step of a user buffer, the
 * node write mark's FIFO order; and the RAM filesystem's chunked truncate
 * (ramfs_truncate_step, K7 (b)): at most 64 pages per step, the index page
 * included, and the same result as ramfs_truncate. */
#include <stdlib.h>
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/proc/fdtable_core.h"
#include "tests/unit/unit.h"

static void test_open_flags(void)
{
    struct fsys_open o;
    CHECK_EQ(fsys_open_flags(FS_O_CREAT | FS_O_EXCL | FS_O_TRUNC, &o), 0);
    CHECK_EQ(o.access, OF_READ);
    CHECK(o.create && o.excl && o.trunc && !o.append && !o.directory && !o.cloexec);
    CHECK_EQ(fsys_open_flags(FS_O_WRONLY | FS_O_APPEND | FS_O_CLOEXEC, &o), 0);
    CHECK_EQ(o.access, OF_WRITE);
    CHECK(o.append && o.cloexec && !o.create);
    CHECK_EQ(fsys_open_access(&o), OF_WRITE | OF_APPEND);
    CHECK_EQ(fsys_open_flags(FS_O_RDWR | FS_O_DIRECTORY, &o), 0);
    CHECK_EQ(o.access, OF_READ | OF_WRITE);
    CHECK(o.directory);
    CHECK_EQ(fsys_open_access(&o), OF_READ | OF_WRITE);
    CHECK_EQ(fsys_open_flags(3, &o), -FS_EINVAL);                       /* access mode 3 */
    CHECK_EQ(fsys_open_flags(0x40000000 | FS_O_WRONLY, &o), 0);        /* unknown bits ignored */
    CHECK_EQ(o.access, OF_WRITE);
    CHECK_EQ(fsys_open_flags((1ull << 32) | FS_O_RDWR, &o), 0);        /* int flags: low 32 bits */
    CHECK_EQ(o.access, OF_READ | OF_WRITE);
    CHECK_EQ(fsys_open_flags((1ull << 32) | 3, &o), -FS_EINVAL);
}

static void test_open_existing(void)
{
    struct fsys_open o;
    fsys_open_flags(0, &o);
    CHECK_EQ(fsys_open_existing(&o, VNODE_FILE), 0);
    CHECK_EQ(fsys_open_existing(&o, VNODE_DIR), 0);                    /* a folder read-only */
    fsys_open_flags(FS_O_DIRECTORY, &o);
    CHECK_EQ(fsys_open_existing(&o, VNODE_FILE), -FS_ENOTDIR);
    CHECK_EQ(fsys_open_existing(&o, VNODE_DIR), 0);
    fsys_open_flags(FS_O_WRONLY, &o);
    CHECK_EQ(fsys_open_existing(&o, VNODE_DIR), -FS_EISDIR);
    CHECK_EQ(fsys_open_existing(&o, VNODE_FILE), 0);
    fsys_open_flags(FS_O_RDWR, &o);
    CHECK_EQ(fsys_open_existing(&o, VNODE_DIR), -FS_EISDIR);
    fsys_open_flags(FS_O_TRUNC, &o);                                    /* O_RDONLY | O_TRUNC */
    CHECK_EQ(fsys_open_existing(&o, VNODE_DIR), -FS_EISDIR);
    CHECK_EQ(fsys_open_existing(&o, VNODE_FILE), 0);
}

static void test_paths(void)
{
    CHECK(fsys_trailing_slash("a/"));
    CHECK(fsys_trailing_slash("/tmp/"));
    CHECK(fsys_trailing_slash("a//"));
    CHECK(!fsys_trailing_slash("/"));
    CHECK(!fsys_trailing_slash("///"));
    CHECK(!fsys_trailing_slash("a"));
    CHECK(!fsys_trailing_slash(""));
    CHECK(fsys_absolute("/tmp"));
    CHECK(!fsys_absolute("tmp"));
    CHECK(!fsys_absolute(""));
    CHECK_EQ(FS_AT_FDCWD, 0xFFFFFFFFFFFFFF9Cull);                       /* the sign-extended -100 */
}

static void test_stat_fill(void)
{
    struct fs_stat st;
    struct fsys_facts f;
    CHECK_EQ(sizeof(struct fs_stat), 144);
    CHECK_EQ(offsetof(struct fs_stat, st_mode), 24);
    CHECK_EQ(offsetof(struct fs_stat, st_rdev), 40);
    CHECK_EQ(offsetof(struct fs_stat, st_size), 48);
    CHECK_EQ(offsetof(struct fs_stat, st_blocks), 64);
    CHECK_EQ(offsetof(struct fs_stat, st_atime_sec), 72);
    CHECK_EQ(offsetof(struct fs_stat, st_mtime_sec), 88);
    CHECK_EQ(offsetof(struct fs_stat, st_ctime_sec), 104);
    memset(&st, 0xAA, sizeof(st));
    memset(&f, 0, sizeof(f));
    f.kind = FSYS_FILE;
    f.ino = 42;
    f.size = 5000;
    f.mode = 0100644;                   /* only the permission bits count */
    f.nlink = 1;
    f.pages = 3;
    f.atime = 10;
    f.mtime = 20;
    f.ctime = 30;
    fsys_stat_fill(&st, &f);
    CHECK_EQ(st.st_dev, FSYS_DEV);
    CHECK_EQ(st.st_ino, 42);
    CHECK_EQ(st.st_nlink, 1);
    CHECK_EQ(st.st_mode, FS_S_IFREG | 0644);
    CHECK_EQ(st.st_uid, 0);
    CHECK_EQ(st.st_gid, 0);
    CHECK_EQ(st.pad0, 0);
    CHECK_EQ(st.st_rdev, 0);
    CHECK_EQ(st.st_size, 5000);
    CHECK_EQ(st.st_blksize, 4096);
    CHECK_EQ(st.st_blocks, 24);
    CHECK_EQ(st.st_atime_sec, 10);
    CHECK_EQ(st.st_mtime_sec, 20);
    CHECK_EQ(st.st_ctime_sec, 30);
    CHECK_EQ(st.st_atime_nsec | st.st_mtime_nsec | st.st_ctime_nsec, 0);
    CHECK_EQ(st.reserved[0] | st.reserved[1] | st.reserved[2], 0);
    f.nlink = 0;                        /* removed, still open */
    fsys_stat_fill(&st, &f);
    CHECK_EQ(st.st_nlink, 0);
    f.kind = FSYS_DIR;
    f.mode = 01777;
    f.nlink = 1;
    f.subdirs = 3;
    f.size = 999;
    fsys_stat_fill(&st, &f);
    CHECK_EQ(st.st_mode, FS_S_IFDIR | 01777);
    CHECK_EQ(st.st_nlink, 5);
    CHECK_EQ(st.st_size, 0);
    CHECK_EQ(st.st_blocks, 0);
    f.nlink = 0;
    fsys_stat_fill(&st, &f);
    CHECK_EQ(st.st_nlink, 0);
    memset(&f, 0, sizeof(f));
    f.kind = FSYS_CONSOLE;
    f.ino = 7;
    fsys_stat_fill(&st, &f);
    CHECK_EQ(st.st_mode, FS_S_IFCHR | 0620);
    CHECK_EQ(st.st_rdev, 0x0501);
    CHECK_EQ(st.st_ino, 0);
    CHECK_EQ(st.st_nlink, 1);
    CHECK_EQ(st.st_size, 0);
}

static void test_lseek(void)
{
    CHECK_EQ(fsys_lseek(7, 100, 50, FS_SEEK_SET), 50);
    CHECK_EQ(fsys_lseek(7, 100, 10, FS_SEEK_CUR), 17);
    CHECK_EQ(fsys_lseek(7, 100, -10, FS_SEEK_END), 90);
    CHECK_EQ(fsys_lseek(7, 100, 1000, FS_SEEK_END), 1100);             /* past the end */
    CHECK_EQ(fsys_lseek(7, 100, -8, FS_SEEK_CUR), -FS_EINVAL);         /* negative */
    CHECK_EQ(fsys_lseek(7, 100, -1, FS_SEEK_SET), -FS_EINVAL);
    CHECK_EQ(fsys_lseek(7, 100, -7, FS_SEEK_CUR), 0);
    CHECK_EQ(fsys_lseek(7, 100, 0, 3), -FS_EINVAL);                    /* SEEK_DATA */
    CHECK_EQ(fsys_lseek(7, 100, 0, 4), -FS_EINVAL);                    /* SEEK_HOLE */
    CHECK_EQ(fsys_lseek(7, 100, 0, 5), -FS_EINVAL);
    CHECK_EQ(fsys_lseek(7, 100, 0, UINT64_MAX), -FS_EINVAL);
    CHECK_EQ(fsys_lseek(7, 100, 0, (1ull << 32) | FS_SEEK_CUR), 7);    /* low 32 bits */
    CHECK_EQ(fsys_lseek(0, 0, INT64_MAX, FS_SEEK_SET), INT64_MAX);
    CHECK_EQ(fsys_lseek(INT64_MAX, 0, 1, FS_SEEK_CUR), -FS_EINVAL);    /* overflow */
    CHECK_EQ(fsys_lseek(10, 0, INT64_MAX, FS_SEEK_CUR), -FS_EINVAL);
    CHECK_EQ(fsys_lseek((uint64_t)INT64_MAX + 1, 0, -1, FS_SEEK_CUR), -FS_EINVAL);
}

static void test_at_flags(void)
{
    bool e = true;
    CHECK_EQ(fsys_at_flags(0, &e), 0);
    CHECK(!e);
    CHECK_EQ(fsys_at_flags(FS_AT_SYMLINK_NOFOLLOW | FS_AT_NO_AUTOMOUNT, &e), 0);
    CHECK(!e);
    CHECK_EQ(fsys_at_flags(FS_AT_EMPTY_PATH, &e), 0);
    CHECK(e);
    CHECK_EQ(fsys_at_flags((1ull << 32) | FS_AT_EMPTY_PATH, &e), 0);   /* int flags */
    CHECK_EQ(fsys_at_flags(0x1, &e), -FS_EINVAL);
    CHECK_EQ(fsys_at_flags(0x200, &e), -FS_EINVAL);                     /* AT_REMOVEDIR */
    CHECK_EQ(fsys_at_flags(0x4000, &e), -FS_EINVAL);
}

static void test_epoch(void)
{
    struct rtc_time t = { 1970, 1, 1, 0, 0, 0 };
    CHECK_EQ(fsys_epoch(&t), 0);
    t = (struct rtc_time){ 2000, 1, 1, 0, 0, 0 };
    CHECK_EQ(fsys_epoch(&t), 946684800);
    t = (struct rtc_time){ 2000, 2, 29, 12, 0, 0 };                     /* a leap day */
    CHECK_EQ(fsys_epoch(&t), 951825600);
    t = (struct rtc_time){ 2026, 10, 12, 0, 0, 0 };                     /* the Makefile's ISO_EPOCH */
    CHECK_EQ(fsys_epoch(&t), 1791763200);
    t = (struct rtc_time){ 2026, 10, 15, 12, 34, 56 };
    CHECK_EQ(fsys_epoch(&t), 1792067696);
    t = (struct rtc_time){ 2099, 12, 31, 23, 59, 59 };
    CHECK_EQ(fsys_epoch(&t), 4102444799ll);
}

static void test_page_step(void)
{
    CHECK_EQ(fsys_page_step(0x1000, 10), 10);
    CHECK_EQ(fsys_page_step(0x1FFF, 10), 1);
    CHECK_EQ(fsys_page_step(0x1000, 5000), 4096);
    CHECK_EQ(fsys_page_step(0x1F00, 0x200), 0x100);
    CHECK_EQ(fsys_page_step(0x1F00, 0), 0);
}

static void test_wmark(void)
{
    struct vnode n;
    memset(&n, 0, sizeof(n));           /* a zeroed node: the mark is free */
    CHECK_EQ(fsys_wmark_holder(&n), -1);
    uint32_t a = fsys_wmark_ticket(&n), b = fsys_wmark_ticket(&n), c = fsys_wmark_ticket(&n);
    CHECK_EQ(n.wr_ahead_max, 2);
    CHECK(!fsys_wmark_try(&n, b, 11, 0));                               /* not its turn */
    CHECK(!fsys_wmark_try(&n, a, -1, 0));                               /* not a thread */
    CHECK(fsys_wmark_try(&n, a, 0, 0));                                 /* thread 0 may hold it */
    CHECK_EQ(fsys_wmark_holder(&n), 0);
    CHECK(!fsys_wmark_release(&n, 11));                                 /* not the holder */
    CHECK(fsys_wmark_release(&n, 0));
    CHECK_EQ(fsys_wmark_holder(&n), -1);
    CHECK(!fsys_wmark_try(&n, c, 12, 1));                               /* b is next, not c */
    CHECK(fsys_wmark_try(&n, b, 11, 3));
    CHECK(!fsys_wmark_try(&n, c, 12, 2));                               /* held */
    CHECK(fsys_wmark_release(&n, 11));
    CHECK(fsys_wmark_try(&n, c, 12, 4));
    CHECK(fsys_wmark_release(&n, 12));
    CHECK(!fsys_wmark_release(&n, 12));
    CHECK_EQ(n.wr_takes, 3);
    CHECK_EQ(n.wr_contended, 2);
    CHECK_EQ(n.wr_next, n.wr_serving);
}

/* ---- ramfs_truncate_step over a host page pool ---- */

#define POOL 600u

static unsigned char pool[POOL][RAMFS_PAGE] __attribute__((aligned(4096)));
static unsigned char used[POOL];
static int mapped, map_errors;

static uint64_t p_alloc(void)
{
    for (unsigned i = 0; i < POOL; i++)
        if (!used[i]) {
            used[i] = 1;
            memset(pool[i], 0xEE, RAMFS_PAGE);
            return (uint64_t)(i + 1) * RAMFS_PAGE;
        }
    return 0;
}

static void p_free(uint64_t frame)
{
    used[frame / RAMFS_PAGE - 1] = 0;
}

static volatile void *p_map(uint64_t frame)
{
    if (mapped++)
        map_errors++;
    return pool[frame / RAMFS_PAGE - 1];
}

static void p_unmap(void)
{
    if (--mapped)
        map_errors++;
}

static const struct ramfs_pages source = { p_alloc, p_free, p_map, p_unmap };
static void *h_alloc(size_t bytes) { return malloc(bytes); }
static void h_free(void *p) { free(p); }
static const struct ramfs_mem mem = { h_alloc, h_free };
static struct ramfs fs;
static unsigned char buf[RAMFS_PAGE];

static unsigned pool_used(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < POOL; i++)
        n += used[i];
    return n;
}

static struct vnode *new_file(const char *name)
{
    struct vnode *f = 0;
    ramfs_ops.create(&fs, &fs.root->vn, name, strlen(name), VNODE_FILE, 0644, &f);
    return f;
}

static void test_truncate_step(void)
{
    CHECK_EQ(ramfs_init(&fs, &mem, 16), 0);
    ramfs_set_pages(&fs, &source, POOL);
    struct vnode *f = new_file("big");
    CHECK(f != 0);
    for (unsigned i = 0; i < RAMFS_SLOTS; i++) {
        memset(buf, (int)(i + 1), sizeof(buf));
        CHECK_EQ(ramfs_write(&fs, f, (uint64_t)i * RAMFS_PAGE, buf, RAMFS_PAGE), RAMFS_PAGE);
    }
    CHECK_EQ(ramfs_file_pages(f), 513);
    CHECK_EQ(fs.data_pages, 513);
    /* 2 MiB to 0: 8 steps of 64 data pages, then the index page alone. */
    unsigned steps = 0, before = fs.data_pages;
    int r;
    do {
        r = ramfs_truncate_step(&fs, f, 0, FSYS_TRUNC_STEP);
        steps++;
        CHECK(before - fs.data_pages <= FSYS_TRUNC_STEP);               /* at most 64 per step */
        CHECK_EQ(f->size, 0);                                           /* the new size at once */
        before = fs.data_pages;
    } while (r == 1 && steps < 20);
    CHECK_EQ(r, 0);
    CHECK_EQ(steps, 9);
    CHECK_EQ(fs.data_pages, 0);
    CHECK_EQ(ramfs_file_pages(f), 0);
    CHECK_EQ(pool_used(), 0);

    /* A partial last page: the same as ramfs_truncate (zeros past the end). */
    memset(buf, 0x5A, sizeof(buf));
    for (unsigned i = 0; i < 3; i++)
        CHECK_EQ(ramfs_write(&fs, f, (uint64_t)i * RAMFS_PAGE, buf, RAMFS_PAGE), RAMFS_PAGE);
    CHECK_EQ(ramfs_truncate_step(&fs, f, 5000, 64), 0);
    CHECK_EQ(f->size, 5000);
    CHECK_EQ(ramfs_file_pages(f), 3);                                   /* index + slots 0, 1 */
    CHECK_EQ(ramfs_truncate_step(&fs, f, 2 * RAMFS_PAGE, 64), 0);       /* grow back: a hole */
    CHECK_EQ(ramfs_read(&fs, f, RAMFS_PAGE, buf, RAMFS_PAGE), RAMFS_PAGE);
    unsigned ok = 1;
    for (unsigned i = 0; i < RAMFS_PAGE; i++)
        if (buf[i] != (i < 5000 - RAMFS_PAGE ? 0x5A : 0))
            ok = 0;
    CHECK(ok);
    /* Growing takes no page and needs no step. */
    CHECK_EQ(ramfs_truncate_step(&fs, f, RAMFS_FILE_MAX, 64), 0);
    CHECK_EQ(f->size, RAMFS_FILE_MAX);
    CHECK_EQ(ramfs_file_pages(f), 3);
    /* max_pages 0 counts as 1: three steps for 2 data pages and the index. */
    steps = 0;
    while (ramfs_truncate_step(&fs, f, 0, 0) == 1 && steps < 10)
        steps++;
    CHECK_EQ(steps, 2);
    CHECK_EQ(ramfs_file_pages(f), 0);
    CHECK_EQ(ramfs_truncate_step(&fs, f, RAMFS_FILE_MAX + 1, 64), -FS_EFBIG);
    CHECK_EQ(f->size, 0);
    CHECK_EQ(ramfs_truncate_step(&fs, &fs.root->vn, 0, 64), -FS_EISDIR);
    CHECK_EQ(map_errors, 0);
    CHECK_EQ(mapped, 0);
}

int main(void)
{
    RUN(test_open_flags);
    RUN(test_open_existing);
    RUN(test_paths);
    RUN(test_stat_fill);
    RUN(test_lseek);
    RUN(test_at_flags);
    RUN(test_epoch);
    RUN(test_page_step);
    RUN(test_wmark);
    RUN(test_truncate_step);
    return unit_report("syscall_fs");
}
