/* Unit tests for the 1.0 folder calls, the pure part (kernel/fs/fsys_core.c,
 * stage 6): unlinkat's flags, mkdir's mode, struct linux_dirent64 records,
 * getdents64's offsets over the RAM filesystem's entry stamps (entries
 * removed, added and renamed between calls), the bytes kept before a bad
 * page, getcwd's path (ENOENT, ENAMETOOLONG), and the errno matrices of
 * mkdir, rmdir, unlink and rename with '/', '.', '..' and a trailing '/'
 * (vfs_core.c, the decisions the system calls return). */
#include <stdlib.h>
#include <string.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs_core.h"
#include "tests/unit/unit.h"

static void *t_alloc(size_t bytes)
{
    return malloc(bytes);
}

static void t_free(void *p)
{
    free(p);
}

static const struct ramfs_mem mem = { t_alloc, t_free };
static struct ramfs fs;
static struct vfs v;
static struct vnode *R;

static void setup(void)
{
    CHECK_EQ(ramfs_init(&fs, &mem, RAMFS_MAX_NODES), 0);
    vfs_mount(&v, &ramfs_ops, &fs, &fs.root->vn, NULL);
    R = v.root;
}

static struct vnode *get(const char *path)
{
    struct vnode *n = NULL;
    return vfs_lookup(&v, R, path, &n) == 0 ? n : NULL;
}

static int mkd(const char *path)
{
    return vfs_create(&v, R, path, VNODE_DIR, 0755, NULL);
}

static int mkf(const char *path)
{
    return vfs_create(&v, R, path, VNODE_FILE, 0644, NULL);
}

static uint64_t rd64(const uint8_t *p)
{
    uint64_t x = 0;
    for (int i = 0; i < 8; i++)
        x |= (uint64_t)p[i] << (8 * i);
    return x;
}

static uint32_t rl(const uint8_t *p)
{
    return (uint32_t)p[16] | ((uint32_t)p[17] << 8);
}

static void test_flags_and_mode(void)
{
    bool rd = true;
    CHECK_EQ(fsys_unlinkat_flags(0, &rd), 0);
    CHECK(!rd);
    CHECK_EQ(fsys_unlinkat_flags(FS_AT_REMOVEDIR, &rd), 0);
    CHECK(rd);
    CHECK_EQ(fsys_unlinkat_flags(0x100, &rd), -FS_EINVAL);
    CHECK_EQ(fsys_unlinkat_flags(0x400, &rd), -FS_EINVAL);
    CHECK_EQ(fsys_unlinkat_flags(0x1000, &rd), -FS_EINVAL);
    CHECK_EQ(fsys_unlinkat_flags(FS_AT_REMOVEDIR | 0x100, &rd), -FS_EINVAL);
    CHECK_EQ(fsys_unlinkat_flags((1ull << 32) | FS_AT_REMOVEDIR, &rd), 0);  /* int: low 32 bits */
    CHECK(rd);
    CHECK_EQ(fsys_mkdir_mode(0755), 0755);
    CHECK_EQ(fsys_mkdir_mode(07777), 01777);
    CHECK_EQ(fsys_mkdir_mode(0xFFFFF000ull | 0700), 0700);
}

static void test_records(void)
{
    CHECK_EQ(fsys_dirent_reclen(1), 24);
    CHECK_EQ(fsys_dirent_reclen(2), 24);
    CHECK_EQ(fsys_dirent_reclen(4), 24);
    CHECK_EQ(fsys_dirent_reclen(5), 32);
    CHECK_EQ(fsys_dirent_reclen(12), 32);
    CHECK_EQ(fsys_dirent_reclen(13), 40);
    CHECK_EQ(fsys_dirent_reclen(255), 280);
    uint8_t b[64];
    memset(b, 0xAA, sizeof(b));
    CHECK_EQ(fsys_dirent_put(b, 31, 7, 9, FS_DT_REG, "hello", 5), 0);   /* 32 needed */
    CHECK_EQ(b[0], 0xAA);                                               /* nothing written */
    CHECK_EQ(fsys_dirent_put(b, 32, 0x0102030405060708ull, 9, FS_DT_REG, "hello", 5), 32);
    CHECK_EQ(rd64(b), 0x0102030405060708ull);                           /* little-endian */
    CHECK_EQ(rd64(b + 8), 9);
    CHECK_EQ(rl(b), 32);
    CHECK_EQ(b[18], FS_DT_REG);
    CHECK(memcmp(b + 19, "hello", 5) == 0);
    bool zeros = true;
    for (int i = 24; i < 32; i++)
        zeros = zeros && b[i] == 0;
    CHECK(zeros);                                                       /* the NUL and padding */
    CHECK_EQ(b[32], 0xAA);                                              /* not past the record */
}

/* The names of the records in buf[0..n), joined by ' '. */
static void names(const uint8_t *b, uint32_t n, char *out)
{
    out[0] = '\0';
    for (uint32_t pos = 0; pos < n; pos += rl(b + pos)) {
        if (rl(b + pos) == 0)
            break;
        if (out[0])
            strcat(out, " ");
        strcat(out, (const char *)b + pos + 19);
    }
}

static void test_fill(void)
{
    setup();
    CHECK_EQ(mkd("/d"), 0);
    CHECK_EQ(mkd("/d/sub"), 0);
    CHECK_EQ(mkf("/d/fa"), 0);
    CHECK_EQ(mkf("/d/fb"), 0);
    struct vnode *d = get("/d");
    uint8_t b[4096];
    char s[256];
    uint64_t next = 99;
    bool more = true;
    uint32_t n = fsys_dir_fill(d, 0, b, sizeof(b), &next, &more);
    CHECK_EQ(n, 5 * 24);
    CHECK(!more);
    names(b, n, s);
    CHECK(strcmp(s, ". .. sub fa fb") == 0);
    CHECK_EQ(rd64(b), d->ino);                                  /* "." */
    CHECK_EQ(rd64(b + 24), R->ino);                             /* ".." */
    CHECK_EQ(b[18], FS_DT_DIR);
    CHECK_EQ(b[24 + 18], FS_DT_DIR);
    CHECK_EQ(b[48 + 18], FS_DT_DIR);                            /* sub */
    CHECK_EQ(b[72 + 18], FS_DT_REG);                            /* fa */
    CHECK_EQ(rd64(b + 8), 1);                                   /* d_off: 1, 2, stamp + 3 */
    CHECK_EQ(rd64(b + 24 + 8), 2);
    CHECK_EQ(rd64(b + 48 + 8), 3);
    CHECK_EQ(rd64(b + 96 + 8), 5);
    CHECK_EQ(next, 5);
    /* The end: nothing more. */
    CHECK_EQ(fsys_dir_fill(d, next, b, sizeof(b), &next, &more), 0);
    CHECK(!more);
    /* Small rooms: whole records only; more set; EINVAL is the caller's. */
    CHECK_EQ(fsys_dir_fill(d, 0, b, 23, &next, &more), 0);
    CHECK(more);
    CHECK_EQ(next, 0);
    CHECK_EQ(fsys_dir_fill(d, 0, b, 60, &next, &more), 48);
    CHECK(more);
    CHECK_EQ(next, 2);
    CHECK_EQ(fsys_dir_fill(d, next, b, 24, &next, &more), 24);  /* sub */
    names(b, 24, s);
    CHECK(strcmp(s, "sub") == 0);
    CHECK_EQ(next, 3);
    /* The root: "." and ".." are both node 1. */
    n = fsys_dir_fill(R, 0, b, sizeof(b), &next, &more);
    CHECK_EQ(rd64(b), 1);
    CHECK_EQ(rd64(b + 24), 1);
    names(b, n, s);
    CHECK(strcmp(s, ". .. d") == 0);
}

static void test_between_calls(void)
{
    setup();
    CHECK_EQ(mkd("/t"), 0);
    char p[32];
    for (int i = 0; i < 6; i++) {
        snprintf(p, sizeof(p), "/t/t%d", i);
        CHECK_EQ(mkf(p), 0);
    }
    struct vnode *t = get("/t");
    uint8_t b[4096];
    char s[256];
    uint64_t cur = 0;
    bool more = false;
    CHECK_EQ(fsys_dir_fill(t, 0, b, 72, &cur, &more), 72);     /* . .. t0 */
    CHECK_EQ(cur, 3);
    CHECK_EQ(vfs_unlink(&v, R, "/t/t0"), 0);                   /* read */
    CHECK_EQ(vfs_unlink(&v, R, "/t/t1"), 0);                   /* not yet read */
    CHECK_EQ(mkf("/t/t6"), 0);                                  /* added */
    uint64_t c2 = cur;
    uint32_t n = fsys_dir_fill(t, c2, b, sizeof(b), &cur, &more);
    names(b, n, s);
    CHECK(strcmp(s, "t2 t3 t4 t5 t6") == 0);                    /* no t1, no t0 again, t6 once */
    CHECK_EQ(cur, 9);
    /* Offsets stay valid: resume after t3 (d_off 6). */
    n = fsys_dir_fill(t, 6, b, sizeof(b), &cur, &more);
    names(b, n, s);
    CHECK(strcmp(s, "t4 t5 t6") == 0);
    /* A rename re-stamps the entry (it may be returned again: POSIX leaves
     * a rename during a listing unspecified). */
    CHECK_EQ(vfs_rename(&v, R, "/t/t2", R, "/t/t2b"), 0);
    n = fsys_dir_fill(t, 6, b, sizeof(b), &cur, &more);
    names(b, n, s);
    CHECK(strcmp(s, "t4 t5 t6 t2b") == 0);
    CHECK_EQ(ramfs_seq(get("/t/t2b")), 7);
    CHECK(ramfs_entry_from(t, 8) == NULL);
    CHECK(ramfs_entry_from(t, 0) == get("/t/t3"));
    /* A folder moved into another gets that folder's next stamp. */
    CHECK_EQ(mkd("/u"), 0);
    CHECK_EQ(mkf("/u/a"), 0);
    CHECK_EQ(vfs_rename(&v, R, "/t/t3", R, "/u/t3"), 0);
    CHECK_EQ(ramfs_seq(get("/u/a")), 0);
    CHECK_EQ(ramfs_seq(get("/u/t3")), 1);
    /* The node layout: still at most 128 bytes (stage 5's pin). */
    CHECK(sizeof(struct ramfs_node) <= 128);
    CHECK_EQ(sizeof(struct vnode), 72);
}

static void test_keep(void)
{
    setup();
    CHECK_EQ(mkd("/k"), 0);
    CHECK_EQ(mkf("/k/x"), 0);
    struct vnode *k = get("/k");
    uint8_t b[4096];
    uint64_t next = 0;
    bool more = false;
    uint32_t n = fsys_dir_fill(k, 0, b, sizeof(b), &next, &more);
    CHECK_EQ(n, 72);
    uint32_t keep = 99;
    CHECK_EQ(fsys_dir_keep(b, n, 72, 0, &keep), 3);             /* all valid */
    CHECK_EQ(keep, 72);
    CHECK_EQ(fsys_dir_keep(b, n, 24, 0, &keep), 1);             /* "." only */
    CHECK_EQ(keep, 24);
    CHECK_EQ(fsys_dir_keep(b, n, 47, 0, &keep), 1);
    CHECK_EQ(keep, 24);
    CHECK_EQ(fsys_dir_keep(b, n, 23, 0, &keep), 0);             /* none: the cursor */
    CHECK_EQ(keep, 0);
    CHECK_EQ(fsys_dir_keep(b + 24, 48, 48, 1, &keep), 3);
    CHECK_EQ(keep, 48);
}

static void test_cwd_path(void)
{
    setup();
    char buf[4096];
    CHECK_EQ(fsys_cwd_path(R, R, buf, sizeof(buf)), 2);
    CHECK(strcmp(buf, "/") == 0);
    CHECK_EQ(mkd("/tmp"), 0);
    CHECK_EQ(mkd("/tmp/ab"), 0);
    struct vnode *ab = get("/tmp/ab");
    CHECK_EQ(fsys_cwd_path(ab, R, buf, sizeof(buf)), 8);
    CHECK(strcmp(buf, "/tmp/ab") == 0);
    CHECK_EQ(fsys_cwd_path(ab, R, buf, 8), 8);                  /* exactly fits */
    CHECK(strcmp(buf, "/tmp/ab") == 0);
    CHECK_EQ(fsys_cwd_path(ab, R, buf, 7), -FS_ENAMETOOLONG);
    CHECK_EQ(fsys_cwd_path(R, R, buf, 1), -FS_ENAMETOOLONG);
    /* Removed: ENOENT, before any size check. */
    vfs_hold(ab);
    CHECK_EQ(vfs_rmdir(&v, R, "/tmp/ab"), 0);
    CHECK_EQ(fsys_cwd_path(ab, R, buf, 1), -FS_ENOENT);
    CHECK_EQ(fsys_cwd_path(ab, R, buf, sizeof(buf)), -FS_ENOENT);
    vfs_drop(&v, ab);
    /* Over 4096 bytes with the NUL: 16 levels of 255-byte names. */
    char name[256];
    memset(name, 'a', 255);
    name[255] = '\0';
    struct vnode *cur = get("/tmp");
    int depth = 0;
    for (; depth < 16; depth++) {
        struct vnode *n = NULL;
        if (vfs_create(&v, cur, name, VNODE_DIR, 0755, &n) != 0)
            break;
        cur = n;
        if (depth == 14)
            CHECK_EQ(fsys_cwd_path(cur, R, buf, sizeof(buf)), 4 + 15 * 256 + 1);
    }
    CHECK_EQ(depth, 16);
    CHECK_EQ(fsys_cwd_path(cur, R, buf, sizeof(buf)), -FS_ENAMETOOLONG);
}

/* The errno decisions the folder calls return (vfs_core.c). */
static void test_matrix(void)
{
    setup();
    CHECK_EQ(mkd("/d"), 0);
    CHECK_EQ(mkd("/d/e"), 0);
    CHECK_EQ(mkf("/f"), 0);
    /* mkdir */
    CHECK_EQ(mkd("/"), -FS_EEXIST);
    CHECK_EQ(mkd("."), -FS_EEXIST);
    CHECK_EQ(mkd(".."), -FS_EEXIST);
    CHECK_EQ(mkd("/d/."), -FS_EEXIST);
    CHECK_EQ(mkd("/f"), -FS_EEXIST);
    CHECK_EQ(mkd("/n/x"), -FS_ENOENT);
    CHECK_EQ(mkd(""), -FS_ENOENT);
    CHECK_EQ(mkd("/f/x"), -FS_ENOTDIR);
    CHECK_EQ(mkd("/g/"), 0);                                    /* a trailing '/' is fine */
    /* rmdir */
    CHECK_EQ(vfs_rmdir(&v, R, "/"), -FS_EBUSY);
    CHECK_EQ(vfs_rmdir(&v, R, "."), -FS_EINVAL);
    CHECK_EQ(vfs_rmdir(&v, R, "/d/."), -FS_EINVAL);
    CHECK_EQ(vfs_rmdir(&v, R, ".."), -FS_ENOTEMPTY);
    CHECK_EQ(vfs_rmdir(&v, R, "/d/.."), -FS_ENOTEMPTY);
    CHECK_EQ(vfs_rmdir(&v, R, "/d"), -FS_ENOTEMPTY);
    CHECK_EQ(vfs_rmdir(&v, R, "/f"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rmdir(&v, R, "/f/"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rmdir(&v, R, "/n"), -FS_ENOENT);
    CHECK_EQ(vfs_rmdir(&v, R, "/g/"), 0);
    /* unlink */
    CHECK_EQ(vfs_unlink(&v, R, "/"), -FS_EISDIR);
    CHECK_EQ(vfs_unlink(&v, R, "."), -FS_EISDIR);
    CHECK_EQ(vfs_unlink(&v, R, ".."), -FS_EISDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/d"), -FS_EISDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/d/"), -FS_EISDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/f/"), -FS_ENOTDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/f/x"), -FS_ENOTDIR);
    CHECK_EQ(vfs_unlink(&v, R, "/n"), -FS_ENOENT);
    CHECK_EQ(vfs_unlink(&v, R, "/n/"), -FS_ENOENT);
    /* rename */
    CHECK_EQ(vfs_rename(&v, R, "/", R, "/z"), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/"), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, ".", R, "/z"), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/d/.."), -FS_EBUSY);
    CHECK_EQ(vfs_rename(&v, R, "/d", R, "/d/e/x"), -FS_EINVAL);
    CHECK_EQ(vfs_rename(&v, R, "/d", R, "/d/x"), -FS_EINVAL);
    CHECK_EQ(vfs_rename(&v, R, "/d/e", R, "/d"), -FS_ENOTEMPTY);
    CHECK_EQ(vfs_rename(&v, R, "/d", R, "/f"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rename(&v, R, "/f/", R, "/z"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/z/"), -FS_ENOTDIR);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/d/e"), -FS_EISDIR);
    CHECK_EQ(vfs_rename(&v, R, "/n", R, "/z"), -FS_ENOENT);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/n/z"), -FS_ENOENT);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/f"), 0);
    CHECK_EQ(vfs_rename(&v, R, "/f", R, "/d/../f"), 0);
    CHECK(get("/f") != NULL && get("/z") == NULL);              /* nothing changed */
}

int main(void)
{
    RUN(test_flags_and_mode);
    RUN(test_records);
    RUN(test_fill);
    RUN(test_between_calls);
    RUN(test_keep);
    RUN(test_cwd_path);
    RUN(test_matrix);
    return unit_report("syscall_dir");
}
