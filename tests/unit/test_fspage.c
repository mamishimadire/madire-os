/* Unit tests for the 1.0 RAM filesystem's file data (kernel/fs/ramfs_core.c,
 * stage 2) over a host page source: the budget formula, writes and reads
 * across page boundaries, fresh pages never showing old contents, holes,
 * the 2 MiB file limit (short count, EFBIG), the budget (short count,
 * ENOSPC), the page source refusing a page, index pages given back,
 * truncate (shrinking gives pages back and zeroes the tail; growing takes
 * no page and is never ENOSPC), pages freed with the node, folders, and
 * one mapping at a time. */
#include <stdlib.h>
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/ramfs_core.h"
#include "tests/unit/unit.h"

#define POOL 1200u                      /* pages: more than one full file (513) */

static unsigned char pool[POOL][RAMFS_PAGE] __attribute__((aligned(4096)));
static unsigned char used[POOL];
static unsigned live_pages;
static int refuse_after = -1;           /* pages handed out before the source refuses (-1: never) */
static int mapped;                      /* mappings open now */
static int map_errors;                  /* a second mapping, or an unmap with none */
static int free_errors;                 /* a frame freed that was not handed out */

static uint64_t p_alloc(void)
{
    if (refuse_after == 0)
        return 0;
    for (unsigned i = 0; i < POOL; i++) {
        if (used[i])
            continue;
        used[i] = 1;
        live_pages++;
        memset(pool[i], 0xEE, RAMFS_PAGE);  /* what an earlier owner left */
        if (refuse_after > 0)
            refuse_after--;
        return (uint64_t)(i + 1) * RAMFS_PAGE;
    }
    return 0;
}

static unsigned frame_index(uint64_t frame)
{
    return (unsigned)(frame / RAMFS_PAGE) - 1;
}

static void p_free(uint64_t frame)
{
    unsigned i = frame_index(frame);
    if (frame % RAMFS_PAGE || i >= POOL || !used[i]) {
        free_errors++;
        return;
    }
    used[i] = 0;
    live_pages--;
}

static volatile void *p_map(uint64_t frame)
{
    if (mapped)
        map_errors++;
    mapped++;
    return pool[frame_index(frame)];
}

static void p_unmap(void)
{
    if (!mapped)
        map_errors++;
    mapped--;
}

static const struct ramfs_pages source = { p_alloc, p_free, p_map, p_unmap };

static void *h_alloc(size_t bytes) { return malloc(bytes); }
static void h_free(void *p) { free(p); }
static const struct ramfs_mem mem = { h_alloc, h_free };

static struct ramfs fs;
static unsigned char buf[3 * RAMFS_PAGE];
static unsigned char out[3 * RAMFS_PAGE];

static unsigned char pat(uint64_t pos)
{
    return (unsigned char)(pos * 7 + (pos >> 12) + 3);
}

static void pat_fill(uint64_t pos, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = pat(pos + i);
}

static int pat_ok(const unsigned char *p, uint64_t pos, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if (p[i] != pat(pos + i))
            return 0;
    return 1;
}

static int all_zero(const unsigned char *p, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if (p[i])
            return 0;
    return 1;
}

static void setup(uint32_t budget)
{
    memset(used, 0, sizeof(used));
    live_pages = 0;
    refuse_after = -1;
    mapped = map_errors = free_errors = 0;
    CHECK_EQ(ramfs_init(&fs, &mem, RAMFS_MAX_NODES), 0);
    ramfs_set_pages(&fs, &source, budget);
}

static struct vnode *mkfile(const char *name)
{
    struct vnode *n = NULL;
    CHECK_EQ(ramfs_ops.create(&fs, &fs.root->vn, name, strlen(name), VNODE_FILE, 0644, &n), 0);
    return n;
}

/* Detach and release, as an unlink with no holder does. */
static void rmfile(struct vnode *n)
{
    ramfs_ops.detach(&fs, n);
    ramfs_ops.release(&fs, n);
}

static void teardown(void)
{
    CHECK_EQ(fs.data_pages, 0);
    CHECK_EQ(live_pages, 0);
    CHECK_EQ(mapped, 0);
    CHECK_EQ(map_errors, 0);
    CHECK_EQ(free_errors, 0);
    ramfs_ops.release(&fs, &fs.root->vn);
}

static void test_budget_formula(void)
{
    CHECK_EQ(ramfs_budget_pages(0), 0);
    CHECK_EQ(ramfs_budget_pages(1), 0);
    CHECK_EQ(ramfs_budget_pages(4), 3);
    CHECK_EQ(ramfs_budget_pages(7), 5);
    CHECK_EQ(ramfs_budget_pages(7000), 5250);
    CHECK_EQ(ramfs_budget_pages(10922), 8191);
    CHECK_EQ(ramfs_budget_pages(10923), 8192);
    CHECK_EQ(ramfs_budget_pages(15000), 8192);
    CHECK_EQ(ramfs_budget_pages(917003), 8192);
    CHECK_EQ(ramfs_budget_pages(0xFFFFFFFFu), 8192);
}

static void test_no_source(void)
{
    memset(used, 0, sizeof(used));
    live_pages = 0;
    CHECK_EQ(ramfs_init(&fs, &mem, 8), 0);
    CHECK(fs.pages == NULL);
    CHECK_EQ(fs.budget_pages, 0);
    struct vnode *f = mkfile("a");
    CHECK_EQ(ramfs_write(&fs, f, 0, buf, 1), -FS_ENOSPC);
    CHECK_EQ(ramfs_read(&fs, f, 0, out, 1), 0);
    CHECK_EQ(ramfs_file_pages(f), 0);
    rmfile(f);
    CHECK_EQ(live_pages, 0);
    ramfs_ops.release(&fs, &fs.root->vn);
}

static void test_write_read(void)
{
    setup(100);
    struct vnode *f = mkfile("f");
    pat_fill(100, 10000);
    CHECK_EQ(ramfs_write(&fs, f, 100, buf, 10000), 10000);
    CHECK_EQ(f->size, 10100);
    CHECK_EQ(ramfs_file_pages(f), 4);           /* index + pages 0, 1, 2 */
    CHECK_EQ(fs.data_pages, 4);
    CHECK_EQ(live_pages, 4);
    memset(out, 0xFF, sizeof(out));
    CHECK_EQ(ramfs_read(&fs, f, 100, out, 10000), 10000);
    CHECK(pat_ok(out, 100, 10000));
    CHECK_EQ(ramfs_read(&fs, f, 0, out, 100), 100);
    CHECK(all_zero(out, 100));                  /* fresh page: no old contents */
    CHECK_EQ(ramfs_read(&fs, f, 10080, out, 50), 20);
    CHECK(pat_ok(out, 10080, 20));
    CHECK_EQ(ramfs_read(&fs, f, 10100, out, 50), 0);
    CHECK_EQ(ramfs_read(&fs, f, 99999, out, 50), 0);
    /* the rest of page 2 past the end is zero too: grow and look */
    CHECK_EQ(ramfs_truncate(&fs, f, 3 * RAMFS_PAGE), 0);
    CHECK_EQ(ramfs_read(&fs, f, 10100, out, 3 * RAMFS_PAGE - 10100), 3 * RAMFS_PAGE - 10100);
    CHECK(all_zero(out, 3 * RAMFS_PAGE - 10100));
    /* overwrite inside existing pages takes nothing */
    pat_fill(5000, 10);
    CHECK_EQ(ramfs_write(&fs, f, 5000, buf, 10), 10);
    CHECK_EQ(fs.data_pages, 4);
    CHECK_EQ(ramfs_write(&fs, f, 5000, buf, 0), 0);
    rmfile(f);
    teardown();
}

static void test_hole(void)
{
    setup(100);
    struct vnode *f = mkfile("h");
    buf[0] = 0x5a;
    CHECK_EQ(ramfs_write(&fs, f, 1024 * 1024, buf, 1), 1);
    CHECK_EQ(ramfs_file_pages(f), 2);
    CHECK_EQ(f->size, 1024 * 1024 + 1);
    memset(out, 0xFF, sizeof(out));
    CHECK_EQ(ramfs_read(&fs, f, 512 * 1024, out, 2 * RAMFS_PAGE), 2 * RAMFS_PAGE);
    CHECK(all_zero(out, 2 * RAMFS_PAGE));
    CHECK_EQ(ramfs_read(&fs, f, 1024 * 1024 - 2, out, 10), 3);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 0);
    CHECK_EQ(out[2], 0x5a);
    rmfile(f);
    teardown();
}

static void test_file_limit(void)
{
    setup(1000);
    struct vnode *f = mkfile("big");
    for (uint32_t k = 0; k < RAMFS_SLOTS; k++) {
        pat_fill((uint64_t)k * RAMFS_PAGE, RAMFS_PAGE);
        CHECK_EQ(ramfs_write(&fs, f, (uint64_t)k * RAMFS_PAGE, buf, RAMFS_PAGE), RAMFS_PAGE);
    }
    CHECK_EQ(f->size, RAMFS_FILE_MAX);
    CHECK_EQ(ramfs_file_pages(f), 513);
    CHECK_EQ(ramfs_write(&fs, f, RAMFS_FILE_MAX, buf, 1), -FS_EFBIG);
    CHECK_EQ(ramfs_write(&fs, f, RAMFS_FILE_MAX + 5, buf, 1), -FS_EFBIG);
    CHECK_EQ(ramfs_write(&fs, f, RAMFS_FILE_MAX, buf, 0), 0);
    pat_fill(RAMFS_FILE_MAX - 100, RAMFS_PAGE);
    CHECK_EQ(ramfs_write(&fs, f, RAMFS_FILE_MAX - 100, buf, RAMFS_PAGE), 100);
    CHECK_EQ(ramfs_read(&fs, f, RAMFS_FILE_MAX - 200, out, 1000), 200);
    CHECK(pat_ok(out, RAMFS_FILE_MAX - 200, 200));
    CHECK_EQ(ramfs_file_pages(f), 513);
    CHECK_EQ(ramfs_truncate(&fs, f, RAMFS_FILE_MAX + 1), -FS_EFBIG);
    CHECK_EQ(f->size, RAMFS_FILE_MAX);
    CHECK_EQ(ramfs_file_pages(f), 513);
    rmfile(f);
    teardown();

    /* a fresh file: one write of 2 MiB + 1 from offset 0 */
    setup(1000);
    f = mkfile("one");
    unsigned char *big = malloc(RAMFS_FILE_MAX + 1);
    memset(big, 0x11, RAMFS_FILE_MAX + 1);
    CHECK_EQ(ramfs_write(&fs, f, 0, big, RAMFS_FILE_MAX + 1), RAMFS_FILE_MAX);
    CHECK_EQ(ramfs_file_pages(f), 513);
    free(big);
    rmfile(f);
    teardown();
}

static void test_budget(void)
{
    setup(5);
    struct vnode *f = mkfile("a"), *g = mkfile("b");
    pat_fill(0, 3 * RAMFS_PAGE);
    /* index + 4 data pages fit: 5 pages; the write asked for 5 data pages */
    CHECK_EQ(ramfs_write(&fs, f, 0, buf, 3 * RAMFS_PAGE), 3 * RAMFS_PAGE);
    CHECK_EQ(ramfs_write(&fs, f, 3 * RAMFS_PAGE, buf, 2 * RAMFS_PAGE), RAMFS_PAGE);
    CHECK_EQ(fs.data_pages, 5);
    CHECK_EQ(ramfs_write(&fs, f, 4 * RAMFS_PAGE, buf, 1), -FS_ENOSPC);
    CHECK_EQ(ramfs_write(&fs, g, 0, buf, 1), -FS_ENOSPC);
    CHECK_EQ(ramfs_file_pages(g), 0);
    CHECK_EQ(live_pages, 5);
    /* a write running out part-way through a page returns the bytes in it */
    CHECK_EQ(ramfs_truncate(&fs, f, 2 * RAMFS_PAGE + 2048), 0);      /* frees page 3 */
    CHECK_EQ(fs.data_pages, 4);
    CHECK_EQ(ramfs_write(&fs, f, 2 * RAMFS_PAGE + 2048, buf, 3 * RAMFS_PAGE), 2048 + RAMFS_PAGE);
    CHECK_EQ(fs.data_pages, 5);
    /* growing by truncate at the budget: no page, never ENOSPC */
    CHECK_EQ(ramfs_truncate(&fs, g, RAMFS_FILE_MAX), 0);
    CHECK_EQ(g->size, RAMFS_FILE_MAX);
    CHECK_EQ(fs.data_pages, 5);
    CHECK_EQ(ramfs_write(&fs, g, 4096, buf, 1), -FS_ENOSPC);
    CHECK_EQ(ramfs_read(&fs, g, 4096, out, 10), 10);
    CHECK(all_zero(out, 10));
    /* index fits, data does not: the index page is given back */
    rmfile(g);
    CHECK_EQ(ramfs_truncate(&fs, f, 3 * RAMFS_PAGE), 0);     /* frees page 3: 4 in use */
    g = mkfile("c");
    CHECK_EQ(ramfs_write(&fs, g, 0, buf, 1), -FS_ENOSPC);
    CHECK_EQ(ramfs_file_pages(g), 0);
    CHECK_EQ(fs.data_pages, 4);
    CHECK_EQ(live_pages, 4);
    rmfile(g);
    rmfile(f);
    teardown();
}

static void test_source_refuses(void)
{
    setup(100);
    struct vnode *f = mkfile("a"), *g = mkfile("b");
    buf[0] = 7;
    CHECK_EQ(ramfs_write(&fs, f, 0, buf, 1), 1);
    refuse_after = 0;
    CHECK_EQ(ramfs_write(&fs, g, 0, buf, 1), -FS_ENOSPC);
    CHECK_EQ(ramfs_file_pages(g), 0);
    CHECK_EQ(ramfs_write(&fs, f, RAMFS_PAGE, buf, 1), -FS_ENOSPC);
    CHECK_EQ(ramfs_file_pages(f), 2);
    CHECK_EQ(ramfs_write(&fs, f, 100, buf, 1), 1);           /* an existing page */
    CHECK_EQ(fs.data_pages, 2);
    /* the index comes, the data page does not: no index page stays */
    refuse_after = 1;
    CHECK_EQ(ramfs_write(&fs, g, 0, buf, 1), -FS_ENOSPC);
    CHECK_EQ(ramfs_file_pages(g), 0);
    CHECK_EQ(live_pages, 2);
    /* part-way: 2 pages come (index + 1), the write asked for 2 */
    refuse_after = 2;
    pat_fill(0, 2 * RAMFS_PAGE);
    CHECK_EQ(ramfs_write(&fs, g, 0, buf, 2 * RAMFS_PAGE), RAMFS_PAGE);
    CHECK_EQ(g->size, RAMFS_PAGE);
    CHECK_EQ(ramfs_file_pages(g), 2);
    refuse_after = -1;
    rmfile(f);
    rmfile(g);
    teardown();
}

static void test_truncate(void)
{
    setup(100);
    struct vnode *f = mkfile("t");
    pat_fill(0, 3 * RAMFS_PAGE);
    CHECK_EQ(ramfs_write(&fs, f, 0, buf, 3 * RAMFS_PAGE), 3 * RAMFS_PAGE);
    CHECK_EQ(ramfs_file_pages(f), 4);
    CHECK_EQ(ramfs_truncate(&fs, f, 5000), 0);
    CHECK_EQ(f->size, 5000);
    CHECK_EQ(ramfs_file_pages(f), 3);           /* page 2 given back */
    CHECK_EQ(live_pages, 3);
    CHECK_EQ(ramfs_truncate(&fs, f, 3 * RAMFS_PAGE), 0);
    CHECK_EQ(ramfs_file_pages(f), 3);           /* growing takes no page */
    CHECK_EQ(ramfs_read(&fs, f, 0, out, 3 * RAMFS_PAGE), 3 * RAMFS_PAGE);
    CHECK(pat_ok(out, 0, 5000));
    CHECK(all_zero(out + 5000, 3 * RAMFS_PAGE - 5000));
    CHECK_EQ(ramfs_truncate(&fs, f, RAMFS_PAGE), 0);
    CHECK_EQ(ramfs_file_pages(f), 2);
    CHECK_EQ(ramfs_truncate(&fs, f, 0), 0);
    CHECK_EQ(ramfs_file_pages(f), 0);           /* the index page with the last one */
    CHECK_EQ(live_pages, 0);
    CHECK_EQ(f->size, 0);
    CHECK_EQ(ramfs_truncate(&fs, f, 0), 0);
    /* a file with only a far page: shrinking below it frees index and page */
    buf[0] = 9;
    CHECK_EQ(ramfs_write(&fs, f, 1024 * 1024, buf, 1), 1);
    CHECK_EQ(ramfs_truncate(&fs, f, 10), 0);
    CHECK_EQ(ramfs_file_pages(f), 0);
    CHECK_EQ(f->size, 10);
    CHECK_EQ(ramfs_read(&fs, f, 0, out, 20), 10);
    CHECK(all_zero(out, 10));
    rmfile(f);
    teardown();
}

static void test_release_frees(void)
{
    setup(1000);
    struct vnode *d = NULL;
    CHECK_EQ(ramfs_ops.create(&fs, &fs.root->vn, "d", 1, VNODE_DIR, 0755, &d), 0);
    CHECK_EQ(ramfs_write(&fs, d, 0, buf, 1), -FS_EISDIR);
    CHECK_EQ(ramfs_read(&fs, d, 0, out, 1), -FS_EISDIR);
    CHECK_EQ(ramfs_truncate(&fs, d, 0), -FS_EISDIR);
    CHECK_EQ(ramfs_file_pages(d), 0);
    struct vnode *a = mkfile("a"), *b = mkfile("b");
    pat_fill(0, 3 * RAMFS_PAGE);
    CHECK_EQ(ramfs_write(&fs, a, 0, buf, 3 * RAMFS_PAGE), 3 * RAMFS_PAGE);
    CHECK_EQ(ramfs_write(&fs, b, 300 * RAMFS_PAGE, buf, 10), 10);
    CHECK_EQ(fs.data_pages, 6);
    /* a moved-over file is freed with its pages */
    CHECK_EQ(ramfs_ops.move(&fs, b, &fs.root->vn, "a", 1, a), 0);
    ramfs_ops.release(&fs, a);
    CHECK_EQ(fs.data_pages, 2);
    CHECK_EQ(live_pages, 2);
    rmfile(b);
    ramfs_ops.detach(&fs, d);
    ramfs_ops.release(&fs, d);
    teardown();
    CHECK_EQ(sizeof(struct ramfs_node) <= 128, 1);  /* the heap block stays 144 bytes */
}

int main(void)
{
    printf("test_fspage\n");
    RUN(test_budget_formula);
    RUN(test_no_source);
    RUN(test_write_read);
    RUN(test_hole);
    RUN(test_file_limit);
    RUN(test_budget);
    RUN(test_source_refuses);
    RUN(test_truncate);
    RUN(test_release_frees);
    return unit_report("test_fspage");
}
