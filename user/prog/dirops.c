/* dirops (1.0 stage 6, test=dirops and test=fsunlink): the folder calls
 * getcwd, chdir, rename, mkdir, rmdir, unlink, getdents64, mkdirat and
 * unlinkat on the RAM filesystem, one case per arg; names under /tmp/do-*
 * and /tmp/un-*. Exits with the number of wrong results (each one logged).
 *   1  getcwd and chdir moving          2  getcwd errors
 *   3  chdir errors                     4  relative paths and AT_FDCWD
 *   5  mkdir   6  mkdirat   7  rmdir   8  unlink   9  unlinkat
 *   10 rename                           11 rename errors
 *   12 getdents64 listing               13 getdents64 small buffers
 *   14 getdents64 between calls         15 getdents64 errors
 *   16 name and path limits
 *   17 ENOSPC (the kernel fills the nodes first)
 *   18, 19, 20  chdir to /tmp/cw1, cw2, cw3, then exit, fault, or spin
 *      until killed
 *   21 getdents64 on /tmp/do-big (1000 entries made by the kernel)
 * test=fsunlink:
 *   30 fd 3 (a 10,000-byte file, read and write): unlink it, use it
 *   31 unlink /tmp/un-big (2 MiB, not open)
 *   32 fd 3 (an unlinked 2 MiB file): close it once /tmp/un-go exists
 *   33 hold fd 3 and spin until killed
 *   34 hold fd 3; once /tmp/un-go exists, fault
 *   35, 36  writers: 32 writes of 32 KiB to /tmp/un-w (un-v); after 8,
 *      wait until it is unlinked (renamed over), then the rest, read back
 *   37 unlink /tmp/un-w             38 rename a new file over /tmp/un-v
 *   39 rename onto an open file     40 rmdir of the current folder
 *   41 rmdir of a folder open as an fd
 *
 * The 40 KiB area is the program's only bss object (everything else is in
 * .data or on the stack), so the last mapped data page ends just after
 * it: case 15 uses its last 24 bytes as a buffer that turns unmapped. */
#include "user/ulib.h"

#define DATA_DATA  __attribute__((section(".data")))
#define UNMAPPED   0x10000000ull
#define BLOCK      32768u
#define POLL_MAX   300                  /* polls of one tick (3 seconds) */

extern char __bss_end[];                /* user/link.ld */

static uint64_t areaw[40960 / 8];
#define dbuf ((uint8_t *)areaw)                 /* listings, data: 32 KiB */
#define cbuf ((char *)areaw)                    /* getcwd results, when no listing is held */
#define pbuf ((char *)areaw + 32768)            /* built paths: 4600 bytes */
#define qbuf ((char *)areaw + 32768 + 2048)     /* a second built name */

static int bad DATA_DATA;
static struct stat st DATA_DATA;
static struct stat s2 DATA_DATA;
static const char ro[160] = { 1 };      /* read-only data: not writable */

/* One record of struct linux_dirent64: the name at byte 19. */
struct dent {
    uint64_t ino;
    int64_t off;
    uint16_t reclen;
    uint8_t type;
    char name[];
};

static void want(const char *what, long got, long expected)
{
    bad += u_expect(what, got, expected);
}

static long streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* getcwd gives exactly `path` (and returns its length with the NUL). */
static long cwd_is(const char *path)
{
    long n = sys_getcwd(cbuf, 4096);
    return n == (long)u_strlen(path) + 1 && streq(cbuf, path);
}

static uint8_t pat(uint64_t pos, uint32_t seed)
{
    return (uint8_t)(pos * 7 + (pos >> 15) + seed);
}

/* A new file of n bytes (at most 32 KiB, seed's pattern), closed again. */
static void make(const char *path, uint64_t n, uint32_t seed)
{
    long fd = sys_open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    want("dirops: make: open: ", fd >= 0, 1);
    for (uint64_t i = 0; i < n; i++)
        dbuf[i] = pat(i, seed);
    want("dirops: make: write: ", sys_write((uint64_t)fd, dbuf, n), (long)n);
    want("dirops: make: close: ", sys_close((uint64_t)fd), 0);
}

static long ino_of(const char *path)
{
    return sys_stat(path, &st) == 0 ? (long)st.st_ino : -1;
}

static long is_dir(const char *path)
{
    return sys_stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

/* head, then n bytes c, then a NUL, at `at`. */
static const char *build(char *at, const char *head, unsigned n, char c)
{
    unsigned p = 0;
    while (head[p]) {
        at[p] = head[p];
        p++;
    }
    for (unsigned i = 0; i < n; i++)
        at[p++] = c;
    at[p] = '\0';
    return at;
}

static const char *long_name(unsigned n)
{
    return build(pbuf, "/tmp/", n, 'n');
}

/* 4096 bytes with no NUL (a NUL only after them). */
static const char *no_nul(void)
{
    for (unsigned i = 0; i < 4096; i++)
        pbuf[i] = 'a';
    pbuf[4096] = '\0';
    return pbuf;
}

static void tick(void)
{
    struct timespec ts = { 0, 10000000 };
    sys_nanosleep(&ts, 0);
}

/* The end of the last mapped data page. */
static uint64_t data_end(void)
{
    return ((uint64_t)(uintptr_t)__bss_end + 4095) & ~4095ull;
}

static const struct dent *rec(const uint8_t *b, long pos)
{
    return (const struct dent *)(const void *)(b + pos);
}

/* A record is well formed: its length is 19 + name + NUL rounded up to 8,
 * the name NUL-terminated inside it. */
static long rec_ok(const struct dent *d)
{
    unsigned len = 0;
    unsigned rl = d->reclen;
    while (19u + len < rl && d->name[len])
        len++;
    return 19u + len < rl && rl == ((19u + len + 1 + 7) & ~7u) && rl % 8 == 0;
}

/* ---- 1-4: getcwd, chdir, relative paths ---- */

static void case_moves(void)
{
    want("dirops 1: getcwd at start: ", sys_getcwd(cbuf, 4096), 2);
    want("dirops 1: it is /: ", streq(cbuf, "/"), 1);
    want("dirops 1: chdir /tmp: ", sys_chdir("/tmp"), 0);
    want("dirops 1: getcwd /tmp returns 5: ", sys_getcwd(cbuf, 4096), 5);
    want("dirops 1: it is /tmp: ", streq(cbuf, "/tmp"), 1);
    want("dirops 1: chdir ..: ", sys_chdir(".."), 0);
    want("dirops 1: at /: ", cwd_is("/"), 1);
    want("dirops 1: chdir tmp: ", sys_chdir("tmp"), 0);
    want("dirops 1: at /tmp: ", cwd_is("/tmp"), 1);
    want("dirops 1: chdir /: ", sys_chdir("/"), 0);
    want("dirops 1: chdir ./tmp/: ", sys_chdir("./tmp/"), 0);
    want("dirops 1: at /tmp again: ", cwd_is("/tmp"), 1);
    want("dirops 1: chdir / at the end: ", sys_chdir("/"), 0);
    want("dirops 1: at / at the end: ", cwd_is("/"), 1);
}

static void case_getcwd_errors(void)
{
    want("dirops 2: chdir /tmp: ", sys_chdir("/tmp"), 0);
    want("dirops 2: one byte short: ", sys_getcwd(cbuf, 4), -ERANGE);
    want("dirops 2: the exact size: ", sys_getcwd(cbuf, 5), 5);
    want("dirops 2: size 0: ", sys_getcwd(cbuf, 0), -ERANGE);
    want("dirops 2: NULL: ", sys_getcwd(0, 4096), -EFAULT);
    want("dirops 2: unmapped: ", sys_getcwd((char *)UNMAPPED, 4096), -EFAULT);
    want("dirops 2: read-only: ", sys_getcwd((char *)(uintptr_t)ro, 160), -EFAULT);
    want("dirops 2: kernel: ", sys_getcwd((char *)K_TEXT, 4096), -EFAULT);
    for (unsigned i = 0; i < 100; i++)
        cbuf[i] = 0x55;
    want("dirops 2: into a marked buffer: ", sys_getcwd(cbuf, 100), 5);
    long untouched = 1;
    for (unsigned i = 5; i < 100; i++)
        if (cbuf[i] != 0x55)
            untouched = 0;
    want("dirops 2: no byte written past the NUL: ", untouched, 1);
    want("dirops 2: chdir /: ", sys_chdir("/"), 0);
}

static void chdir_fails(const char *what, const char *path, long err)
{
    want(what, sys_chdir(path), err);
    want("dirops 3: the current folder unchanged: ", cwd_is("/tmp"), 1);
}

static void case_chdir_errors(void)
{
    make("/tmp/do-f", 100, 1);
    want("dirops 3: chdir /tmp: ", sys_chdir("/tmp"), 0);
    chdir_fails("dirops 3: a missing folder: ", "/tmp/do-nosuch", -ENOENT);
    chdir_fails("dirops 3: the empty path: ", "", -ENOENT);
    chdir_fails("dirops 3: a file: ", "/tmp/do-f", -ENOTDIR);
    chdir_fails("dirops 3: a file on the way: ", "/tmp/do-f/x", -ENOTDIR);
    chdir_fails("dirops 3: a file with a trailing '/': ", "/tmp/do-f/", -ENOTDIR);
    chdir_fails("dirops 3: a 256-byte name: ", long_name(256), -ENAMETOOLONG);
    chdir_fails("dirops 3: no NUL within 4096 bytes: ", no_nul(), -ENAMETOOLONG);
    chdir_fails("dirops 3: NULL: ", 0, -EFAULT);
    chdir_fails("dirops 3: unmapped: ", (const char *)UNMAPPED, -EFAULT);
    chdir_fails("dirops 3: kernel: ", (const char *)K_TEXT, -EFAULT);
    want("dirops 3: chdir /: ", sys_chdir("/"), 0);
    want("dirops 3: unlink do-f: ", sys_unlink("/tmp/do-f"), 0);
}

static void case_relative(void)
{
    long tmp = ino_of("/tmp");
    want("dirops 4: chdir /tmp: ", sys_chdir("/tmp"), 0);
    want("dirops 4: open do-r O_CREAT (relative): ",
         sys_open("do-r", O_CREAT | O_WRONLY, 0644), 3);
    want("dirops 4: close: ", sys_close(3), 0);
    long r = ino_of("/tmp/do-r");
    want("dirops 4: made in /tmp: ", r > 0, 1);
    want("dirops 4: stat do-r (relative): ", ino_of("do-r"), r);
    want("dirops 4: mkdir do-d (relative): ", sys_mkdir("do-d", 0755), 0);
    want("dirops 4: made in /tmp: ", is_dir("/tmp/do-d"), 1);
    want("dirops 4: rename do-r do-d/r2 (relative): ", sys_rename("do-r", "do-d/r2"), 0);
    want("dirops 4: moved within /tmp: ", ino_of("/tmp/do-d/r2"), r);
    want("dirops 4: openat(AT_FDCWD, do-d/r2): ",
         sys_openat((uint64_t)AT_FDCWD, "do-d/r2", O_RDONLY, 0), 3);
    want("dirops 4: fstat: ", sys_fstat(3, &st), 0);
    want("dirops 4: the same node: ", (long)st.st_ino, r);
    want("dirops 4: close: ", sys_close(3), 0);
    want("dirops 4: newfstatat(AT_FDCWD, do-d): ",
         sys_newfstatat((uint64_t)AT_FDCWD, "do-d", &st, 0), 0);
    want("dirops 4: a folder: ", (st.st_mode & S_IFMT) == S_IFDIR, 1);
    want("dirops 4: newfstatat(AT_FDCWD, \"\", AT_EMPTY_PATH): ",
         sys_newfstatat((uint64_t)AT_FDCWD, "", &st, AT_EMPTY_PATH), 0);
    want("dirops 4: it is the current folder /tmp: ", (long)st.st_ino, tmp);
    want("dirops 4: mkdirat(AT_FDCWD, do-d/e): ",
         sys_mkdirat((uint64_t)AT_FDCWD, "do-d/e", 0755), 0);
    want("dirops 4: made in /tmp/do-d: ", is_dir("/tmp/do-d/e"), 1);
    want("dirops 4: unlinkat(AT_FDCWD, do-d/e, AT_REMOVEDIR): ",
         sys_unlinkat((uint64_t)AT_FDCWD, "do-d/e", AT_REMOVEDIR), 0);
    want("dirops 4: unlinkat(AT_FDCWD, do-d/r2, 0): ",
         sys_unlinkat((uint64_t)AT_FDCWD, "do-d/r2", 0), 0);
    want("dirops 4: gone: ", sys_stat("/tmp/do-d/r2", &st), -ENOENT);
    make("do-u", 10, 2);
    want("dirops 4: made in /tmp: ", ino_of("/tmp/do-u") > 0, 1);
    want("dirops 4: unlink do-u (relative): ", sys_unlink("do-u"), 0);
    want("dirops 4: gone from /tmp: ", sys_stat("/tmp/do-u", &st), -ENOENT);
    /* A folder dirfd and an absolute path ignore the current folder. */
    want("dirops 4: open / O_DIRECTORY: ", sys_open("/", O_RDONLY | O_DIRECTORY, 0), 3);
    want("dirops 4: openat(/, tmp): ", sys_openat(3, "tmp", O_RDONLY | O_DIRECTORY, 0), 4);
    want("dirops 4: fstat: ", sys_fstat(4, &st), 0);
    want("dirops 4: /tmp, not /tmp/tmp: ", (long)st.st_ino, tmp);
    want("dirops 4: close 4: ", sys_close(4), 0);
    want("dirops 4: close 3: ", sys_close(3), 0);
    want("dirops 4: stat /home (absolute): ", is_dir("/home"), 1);
    want("dirops 4: rmdir do-d (relative): ", sys_rmdir("do-d"), 0);
    want("dirops 4: chdir /: ", sys_chdir("/"), 0);
}

/* ---- 5-9: mkdir, mkdirat, rmdir, unlink, unlinkat ---- */

static long links_of(const char *path)
{
    return sys_stat(path, &st) == 0 ? (long)st.st_nlink : -1;
}

static void case_mkdir(void)
{
    long l0 = links_of("/tmp");
    want("dirops 5: mkdir do-m: ", sys_mkdir("/tmp/do-m", 0755), 0);
    want("dirops 5: stat: ", sys_stat("/tmp/do-m", &st), 0);
    want("dirops 5: S_IFDIR|0755: ", st.st_mode, S_IFDIR | 0755);
    want("dirops 5: the parent's links + 1: ", links_of("/tmp"), l0 + 1);
    want("dirops 5: mkdir do-m2/ (a trailing '/'): ", sys_mkdir("/tmp/do-m2/", 0700), 0);
    want("dirops 5: stat: ", sys_stat("/tmp/do-m2", &st), 0);
    want("dirops 5: S_IFDIR|0700: ", st.st_mode, S_IFDIR | 0700);
    want("dirops 5: mkdir do-s 07777: ", sys_mkdir("/tmp/do-s", 07777), 0);
    want("dirops 5: stat: ", sys_stat("/tmp/do-s", &st), 0);
    want("dirops 5: mode & 01777 stored: ", st.st_mode, S_IFDIR | 01777);
    make("/tmp/do-f", 10, 3);
    want("dirops 5: EEXIST a folder: ", sys_mkdir("/tmp/do-m", 0755), -EEXIST);
    want("dirops 5: EEXIST a file: ", sys_mkdir("/tmp/do-f", 0755), -EEXIST);
    want("dirops 5: EEXIST /: ", sys_mkdir("/", 0755), -EEXIST);
    want("dirops 5: EEXIST .: ", sys_mkdir(".", 0755), -EEXIST);
    want("dirops 5: EEXIST ..: ", sys_mkdir("..", 0755), -EEXIST);
    want("dirops 5: EEXIST /tmp/.: ", sys_mkdir("/tmp/.", 0755), -EEXIST);
    want("dirops 5: ENOENT a missing parent: ", sys_mkdir("/tmp/do-nosuch/x", 0755), -ENOENT);
    want("dirops 5: ENOENT the empty path: ", sys_mkdir("", 0755), -ENOENT);
    want("dirops 5: chdir do-m2: ", sys_chdir("/tmp/do-m2"), 0);
    want("dirops 5: rmdir it: ", sys_rmdir("/tmp/do-m2"), 0);
    want("dirops 5: ENOENT inside a removed folder: ", sys_mkdir("x", 0755), -ENOENT);
    want("dirops 5: chdir /: ", sys_chdir("/"), 0);
    want("dirops 5: ENOTDIR a file on the way: ", sys_mkdir("/tmp/do-f/x", 0755), -ENOTDIR);
    want("dirops 5: ENAMETOOLONG: ", sys_mkdir(long_name(256), 0755), -ENAMETOOLONG);
    want("dirops 5: EFAULT NULL: ", sys_mkdir(0, 0755), -EFAULT);
    want("dirops 5: EFAULT unmapped: ", sys_mkdir((const char *)UNMAPPED, 0755), -EFAULT);
    want("dirops 5: EFAULT kernel: ", sys_mkdir((const char *)K_TEXT, 0755), -EFAULT);
    want("dirops 5: rmdir do-m: ", sys_rmdir("/tmp/do-m"), 0);
    want("dirops 5: rmdir do-s: ", sys_rmdir("/tmp/do-s"), 0);
    want("dirops 5: unlink do-f: ", sys_unlink("/tmp/do-f"), 0);
    want("dirops 5: the parent's links back: ", links_of("/tmp"), l0);
}

static void case_mkdirat(void)
{
    want("dirops 6: open /tmp: ", sys_open("/tmp", O_RDONLY | O_DIRECTORY, 0), 3);
    want("dirops 6: mkdirat(/tmp, do-a): ", sys_mkdirat(3, "do-a", 0755), 0);
    want("dirops 6: made: ", is_dir("/tmp/do-a"), 1);
    want("dirops 6: mkdirat(AT_FDCWD, tmp/do-b): ",
         sys_mkdirat((uint64_t)AT_FDCWD, "tmp/do-b", 0755), 0);
    want("dirops 6: made: ", is_dir("/tmp/do-b"), 1);
    want("dirops 6: mkdirat(99, /tmp/do-c) (absolute): ", sys_mkdirat(99, "/tmp/do-c", 0755), 0);
    want("dirops 6: made: ", is_dir("/tmp/do-c"), 1);
    want("dirops 6: EBADF (not open, relative): ", sys_mkdirat(99, "do-x", 0755), -EBADF);
    want("dirops 6: EBADF (fd 9): ", sys_mkdirat(9, "do-x", 0755), -EBADF);
    make("/tmp/do-f", 10, 4);
    want("dirops 6: open do-f: ", sys_open("/tmp/do-f", O_RDONLY, 0), 4);
    want("dirops 6: ENOTDIR a file as dirfd: ", sys_mkdirat(4, "x", 0755), -ENOTDIR);
    want("dirops 6: ENOTDIR the console as dirfd: ", sys_mkdirat(1, "x", 0755), -ENOTDIR);
    want("dirops 6: do-x not made: ", sys_stat("/tmp/do-x", &st), -ENOENT);
    want("dirops 6: close 4: ", sys_close(4), 0);
    want("dirops 6: close 3: ", sys_close(3), 0);
    want("dirops 6: rmdir do-a: ", sys_rmdir("/tmp/do-a"), 0);
    want("dirops 6: rmdir do-b: ", sys_rmdir("/tmp/do-b"), 0);
    want("dirops 6: rmdir do-c: ", sys_rmdir("/tmp/do-c"), 0);
    want("dirops 6: unlink do-f: ", sys_unlink("/tmp/do-f"), 0);
}

static void case_rmdir(void)
{
    long l0 = links_of("/tmp");
    want("dirops 7: mkdir do-r: ", sys_mkdir("/tmp/do-r", 0755), 0);
    want("dirops 7: links + 1: ", links_of("/tmp"), l0 + 1);
    want("dirops 7: rmdir do-r: ", sys_rmdir("/tmp/do-r"), 0);
    want("dirops 7: links back: ", links_of("/tmp"), l0);
    want("dirops 7: then ENOENT: ", sys_stat("/tmp/do-r", &st), -ENOENT);
    want("dirops 7: mkdir do-r: ", sys_mkdir("/tmp/do-r", 0755), 0);
    want("dirops 7: rmdir do-r/ (a trailing '/'): ", sys_rmdir("/tmp/do-r/"), 0);
    want("dirops 7: EBUSY /: ", sys_rmdir("/"), -EBUSY);
    want("dirops 7: EINVAL .: ", sys_rmdir("."), -EINVAL);
    want("dirops 7: mkdir do-d: ", sys_mkdir("/tmp/do-d", 0755), 0);
    want("dirops 7: EINVAL d/.: ", sys_rmdir("/tmp/do-d/."), -EINVAL);
    want("dirops 7: ENOTEMPTY ..: ", sys_rmdir(".."), -ENOTEMPTY);
    want("dirops 7: ENOTEMPTY d/..: ", sys_rmdir("/tmp/do-d/.."), -ENOTEMPTY);
    make("/tmp/do-d/f", 10, 5);
    want("dirops 7: ENOTEMPTY a folder with entries: ", sys_rmdir("/tmp/do-d"), -ENOTEMPTY);
    want("dirops 7: ENOTDIR a file: ", sys_rmdir("/tmp/do-d/f"), -ENOTDIR);
    want("dirops 7: ENOTDIR file/: ", sys_rmdir("/tmp/do-d/f/"), -ENOTDIR);
    want("dirops 7: ENOENT missing: ", sys_rmdir("/tmp/do-nosuch"), -ENOENT);
    want("dirops 7: ENOENT the empty path: ", sys_rmdir(""), -ENOENT);
    want("dirops 7: ENAMETOOLONG: ", sys_rmdir(long_name(256)), -ENAMETOOLONG);
    want("dirops 7: EFAULT NULL: ", sys_rmdir(0), -EFAULT);
    want("dirops 7: EFAULT unmapped: ", sys_rmdir((const char *)UNMAPPED), -EFAULT);
    want("dirops 7: EFAULT kernel: ", sys_rmdir((const char *)K_TEXT), -EFAULT);
    want("dirops 7: do-d still there: ", is_dir("/tmp/do-d"), 1);
    want("dirops 7: unlink do-d/f: ", sys_unlink("/tmp/do-d/f"), 0);
    want("dirops 7: rmdir do-d: ", sys_rmdir("/tmp/do-d"), 0);
}

static void case_unlink(void)
{
    make("/tmp/do-u", 10, 6);
    want("dirops 8: unlink do-u: ", sys_unlink("/tmp/do-u"), 0);
    want("dirops 8: then ENOENT: ", sys_stat("/tmp/do-u", &st), -ENOENT);
    want("dirops 8: mkdir do-d: ", sys_mkdir("/tmp/do-d", 0755), 0);
    want("dirops 8: EISDIR a folder: ", sys_unlink("/tmp/do-d"), -EISDIR);
    want("dirops 8: EISDIR /: ", sys_unlink("/"), -EISDIR);
    want("dirops 8: EISDIR .: ", sys_unlink("."), -EISDIR);
    want("dirops 8: EISDIR ..: ", sys_unlink(".."), -EISDIR);
    want("dirops 8: EISDIR d/: ", sys_unlink("/tmp/do-d/"), -EISDIR);
    make("/tmp/do-f", 10, 7);
    want("dirops 8: ENOTDIR file/: ", sys_unlink("/tmp/do-f/"), -ENOTDIR);
    want("dirops 8: ENOTDIR a file on the way: ", sys_unlink("/tmp/do-f/x"), -ENOTDIR);
    want("dirops 8: ENOENT missing: ", sys_unlink("/tmp/do-nosuch"), -ENOENT);
    want("dirops 8: ENOENT missing/: ", sys_unlink("/tmp/do-nosuch/"), -ENOENT);
    want("dirops 8: ENOENT the empty path: ", sys_unlink(""), -ENOENT);
    want("dirops 8: ENAMETOOLONG: ", sys_unlink(long_name(256)), -ENAMETOOLONG);
    want("dirops 8: EFAULT NULL: ", sys_unlink(0), -EFAULT);
    want("dirops 8: EFAULT unmapped: ", sys_unlink((const char *)UNMAPPED), -EFAULT);
    want("dirops 8: EFAULT kernel: ", sys_unlink((const char *)K_TEXT), -EFAULT);
    want("dirops 8: do-f still there: ", sys_stat("/tmp/do-f", &st), 0);
    want("dirops 8: do-d still there: ", is_dir("/tmp/do-d"), 1);
    want("dirops 8: unlink do-f: ", sys_unlink("/tmp/do-f"), 0);
    want("dirops 8: rmdir do-d: ", sys_rmdir("/tmp/do-d"), 0);
}

static void case_unlinkat(void)
{
    make("/tmp/do-u", 10, 8);
    want("dirops 9: open /tmp: ", sys_open("/tmp", O_RDONLY | O_DIRECTORY, 0), 3);
    want("dirops 9: unlinkat(/tmp, do-u, 0): ", sys_unlinkat(3, "do-u", 0), 0);
    want("dirops 9: gone: ", sys_stat("/tmp/do-u", &st), -ENOENT);
    want("dirops 9: mkdir do-d: ", sys_mkdir("/tmp/do-d", 0755), 0);
    want("dirops 9: flags 0 on a folder EISDIR: ", sys_unlinkat(3, "do-d", 0), -EISDIR);
    want("dirops 9: AT_REMOVEDIR on a folder: ", sys_unlinkat(3, "do-d", AT_REMOVEDIR), 0);
    want("dirops 9: gone: ", sys_stat("/tmp/do-d", &st), -ENOENT);
    make("/tmp/do-f", 10, 9);
    want("dirops 9: AT_REMOVEDIR on a file ENOTDIR: ",
         sys_unlinkat(3, "do-f", AT_REMOVEDIR), -ENOTDIR);
    want("dirops 9: EINVAL 0x100: ", sys_unlinkat(3, "do-f", 0x100), -EINVAL);
    want("dirops 9: EINVAL 0x400: ", sys_unlinkat(3, "do-f", 0x400), -EINVAL);
    want("dirops 9: EINVAL 0x1000: ", sys_unlinkat(3, "do-f", 0x1000), -EINVAL);
    want("dirops 9: EINVAL combined: ", sys_unlinkat(3, "do-f", AT_REMOVEDIR | 0x100), -EINVAL);
    want("dirops 9: do-f still there: ", sys_stat("/tmp/do-f", &st), 0);
    want("dirops 9: EBADF: ", sys_unlinkat(99, "do-f", 0), -EBADF);
    want("dirops 9: open do-f: ", sys_open("/tmp/do-f", O_RDONLY, 0), 4);
    want("dirops 9: ENOTDIR a file as dirfd: ", sys_unlinkat(4, "x", 0), -ENOTDIR);
    want("dirops 9: ENOTDIR the console as dirfd: ", sys_unlinkat(1, "x", 0), -ENOTDIR);
    want("dirops 9: chdir /tmp: ", sys_chdir("/tmp"), 0);
    want("dirops 9: unlinkat(AT_FDCWD, do-f, 0): ", sys_unlinkat((uint64_t)AT_FDCWD, "do-f", 0), 0);
    want("dirops 9: gone: ", sys_stat("/tmp/do-f", &st), -ENOENT);
    want("dirops 9: chdir /: ", sys_chdir("/"), 0);
    want("dirops 9: close 4: ", sys_close(4), 0);
    want("dirops 9: close 3: ", sys_close(3), 0);
}

/* ---- 10, 11: rename ---- */

static void case_rename(void)
{
    want("dirops 10: mkdir do-a: ", sys_mkdir("/tmp/do-a", 0755), 0);
    want("dirops 10: mkdir do-b: ", sys_mkdir("/tmp/do-b", 0755), 0);
    make("/tmp/do-a/f", 100, 10);
    long f = ino_of("/tmp/do-a/f");
    want("dirops 10: a file across folders: ", sys_rename("/tmp/do-a/f", "/tmp/do-b/f"), 0);
    want("dirops 10: the same node: ", ino_of("/tmp/do-b/f"), f);
    want("dirops 10: the old name ENOENT: ", sys_stat("/tmp/do-a/f", &st), -ENOENT);
    make("/tmp/do-b/g", 50, 11);
    want("dirops 10: a file over a file: ", sys_rename("/tmp/do-b/f", "/tmp/do-b/g"), 0);
    want("dirops 10: the new name has the moved node: ", ino_of("/tmp/do-b/g"), f);
    want("dirops 10: its size: ", (long)st.st_size, 100);
    want("dirops 10: f gone: ", sys_stat("/tmp/do-b/f", &st), -ENOENT);
    want("dirops 10: mkdir do-a/x: ", sys_mkdir("/tmp/do-a/x", 0755), 0);
    want("dirops 10: mkdir do-b/y: ", sys_mkdir("/tmp/do-b/y", 0755), 0);
    long x = ino_of("/tmp/do-a/x");
    want("dirops 10: a folder over an empty folder: ", sys_rename("/tmp/do-a/x", "/tmp/do-b/y"),
         0);
    want("dirops 10: y is the moved folder: ", ino_of("/tmp/do-b/y"), x);
    want("dirops 10: x gone: ", sys_stat("/tmp/do-a/x", &st), -ENOENT);
    want("dirops 10: mkdir do-a/w: ", sys_mkdir("/tmp/do-a/w", 0755), 0);
    want("dirops 10: chdir into it: ", sys_chdir("/tmp/do-a/w"), 0);
    want("dirops 10: a folder across folders, the current folder inside: ",
         sys_rename("/tmp/do-a/w", "/tmp/do-b/w2"), 0);
    want("dirops 10: getcwd shows the new path: ", cwd_is("/tmp/do-b/w2"), 1);
    want("dirops 10: chdir /: ", sys_chdir("/"), 0);
    want("dirops 10: the same name: ", sys_rename("/tmp/do-b/g", "/tmp/do-b/g"), 0);
    want("dirops 10: unchanged: ", ino_of("/tmp/do-b/g"), f);
    want("dirops 10: the same node by two paths: ",
         sys_rename("/tmp/do-b/g", "/tmp/do-a/../do-b/./g"), 0);
    want("dirops 10: unchanged: ", ino_of("/tmp/do-b/g"), f);
    want("dirops 10: unlink g: ", sys_unlink("/tmp/do-b/g"), 0);
    want("dirops 10: rmdir y: ", sys_rmdir("/tmp/do-b/y"), 0);
    want("dirops 10: rmdir w2: ", sys_rmdir("/tmp/do-b/w2"), 0);
    want("dirops 10: rmdir do-a: ", sys_rmdir("/tmp/do-a"), 0);
    want("dirops 10: rmdir do-b: ", sys_rmdir("/tmp/do-b"), 0);
}

static void rename_fails(const char *what, const char *o, const char *n, long err)
{
    want(what, sys_rename(o, n), err);
}

static void case_rename_errors(void)
{
    want("dirops 11: mkdir do-d: ", sys_mkdir("/tmp/do-d", 0755), 0);
    want("dirops 11: mkdir do-d/sub: ", sys_mkdir("/tmp/do-d/sub", 0755), 0);
    want("dirops 11: mkdir do-e: ", sys_mkdir("/tmp/do-e", 0755), 0);
    want("dirops 11: mkdir do-n: ", sys_mkdir("/tmp/do-n", 0755), 0);
    make("/tmp/do-n/k", 10, 12);
    make("/tmp/do-f", 100, 13);
    long f = ino_of("/tmp/do-f"), d = ino_of("/tmp/do-d");
    rename_fails("dirops 11: ENOENT missing source: ", "/tmp/do-nosuch", "/tmp/do-z", -ENOENT);
    rename_fails("dirops 11: ENOENT missing target folder: ", "/tmp/do-f", "/tmp/do-nosuch/z",
                 -ENOENT);
    rename_fails("dirops 11: ENOENT empty source: ", "", "/tmp/do-z", -ENOENT);
    rename_fails("dirops 11: ENOENT empty target: ", "/tmp/do-f", "", -ENOENT);
    rename_fails("dirops 11: ENOTDIR a folder onto a file: ", "/tmp/do-d", "/tmp/do-f", -ENOTDIR);
    rename_fails("dirops 11: ENOTDIR a file on the way: ", "/tmp/do-f/x", "/tmp/do-z", -ENOTDIR);
    rename_fails("dirops 11: ENOTDIR file/ as source: ", "/tmp/do-f/", "/tmp/do-z", -ENOTDIR);
    rename_fails("dirops 11: ENOTDIR file onto z/: ", "/tmp/do-f", "/tmp/do-z/", -ENOTDIR);
    rename_fails("dirops 11: EISDIR a file onto a folder: ", "/tmp/do-f", "/tmp/do-e", -EISDIR);
    rename_fails("dirops 11: ENOTEMPTY onto a folder with entries: ", "/tmp/do-e", "/tmp/do-n",
                 -ENOTEMPTY);
    rename_fails("dirops 11: ENOTEMPTY a/b onto a: ", "/tmp/do-d/sub", "/tmp/do-d", -ENOTEMPTY);
    rename_fails("dirops 11: EINVAL into its own subfolder: ", "/tmp/do-d", "/tmp/do-d/sub/x",
                 -EINVAL);
    rename_fails("dirops 11: EINVAL into itself: ", "/tmp/do-d", "/tmp/do-d/x", -EINVAL);
    rename_fails("dirops 11: EBUSY / as source: ", "/", "/tmp/do-z", -EBUSY);
    rename_fails("dirops 11: EBUSY / as target: ", "/tmp/do-f", "/", -EBUSY);
    rename_fails("dirops 11: EBUSY . as source: ", ".", "/tmp/do-z", -EBUSY);
    rename_fails("dirops 11: EBUSY d/. as source: ", "/tmp/do-d/.", "/tmp/do-z", -EBUSY);
    rename_fails("dirops 11: EBUSY .. as target: ", "/tmp/do-f", "/tmp/..", -EBUSY);
    rename_fails("dirops 11: ENAMETOOLONG source: ", long_name(256), "/tmp/do-z", -ENAMETOOLONG);
    rename_fails("dirops 11: ENAMETOOLONG target: ", "/tmp/do-f", long_name(256), -ENAMETOOLONG);
    rename_fails("dirops 11: EFAULT NULL source: ", 0, "/tmp/do-z", -EFAULT);
    rename_fails("dirops 11: EFAULT NULL target: ", "/tmp/do-f", 0, -EFAULT);
    rename_fails("dirops 11: EFAULT unmapped source: ", (const char *)UNMAPPED, "/tmp/do-z",
                 -EFAULT);
    rename_fails("dirops 11: EFAULT kernel target: ", "/tmp/do-f", (const char *)K_TEXT, -EFAULT);
    want("dirops 11: nothing changed: do-f: ", ino_of("/tmp/do-f"), f);
    want("dirops 11: its size: ", (long)st.st_size, 100);
    want("dirops 11: do-d: ", ino_of("/tmp/do-d"), d);
    want("dirops 11: do-d/sub: ", is_dir("/tmp/do-d/sub"), 1);
    want("dirops 11: do-e: ", is_dir("/tmp/do-e"), 1);
    want("dirops 11: do-n/k: ", sys_stat("/tmp/do-n/k", &st), 0);
    want("dirops 11: no do-z: ", sys_stat("/tmp/do-z", &st), -ENOENT);
    want("dirops 11: unlink do-f: ", sys_unlink("/tmp/do-f"), 0);
    want("dirops 11: unlink do-n/k: ", sys_unlink("/tmp/do-n/k"), 0);
    want("dirops 11: rmdir do-n: ", sys_rmdir("/tmp/do-n"), 0);
    want("dirops 11: rmdir do-e: ", sys_rmdir("/tmp/do-e"), 0);
    want("dirops 11: rmdir do-d/sub: ", sys_rmdir("/tmp/do-d/sub"), 0);
    want("dirops 11: rmdir do-d: ", sys_rmdir("/tmp/do-d"), 0);
}

/* ---- 12-15: getdents64 ---- */

/* The records of one call, in order. */
#define RECS 64
static long rec_n DATA_DATA;
static const struct dent *recs[RECS] DATA_DATA;

/* Splits n bytes into records; 0 if any is malformed or they do not add
 * up to n exactly. */
static long split(const uint8_t *b, long n)
{
    rec_n = 0;
    long pos = 0;
    while (pos < n) {
        const struct dent *d = rec(b, pos);
        if (d->reclen < 24 || pos + d->reclen > n || !rec_ok(d) || rec_n == RECS)
            return 0;
        recs[rec_n++] = d;
        pos += d->reclen;
    }
    return pos == n;
}

static long named(long i, const char *name)
{
    return i < rec_n && streq(recs[i]->name, name);
}

/* d_off strictly increasing over the records of one call (after `prev`). */
static long offs_rise(int64_t prev)
{
    for (long i = 0; i < rec_n; i++) {
        if (recs[i]->off <= prev)
            return 0;
        prev = recs[i]->off;
    }
    return 1;
}

static void case_list(void)
{
    want("dirops 12: mkdir do-l: ", sys_mkdir("/tmp/do-l", 0755), 0);
    want("dirops 12: mkdir do-l/sub: ", sys_mkdir("/tmp/do-l/sub", 0755), 0);
    make("/tmp/do-l/fa", 10, 14);
    make("/tmp/do-l/fb", 10, 15);
    long l = ino_of("/tmp/do-l"), t = ino_of("/tmp"), sub = ino_of("/tmp/do-l/sub");
    long fa = ino_of("/tmp/do-l/fa"), fb = ino_of("/tmp/do-l/fb");
    want("dirops 12: open do-l: ", sys_open("/tmp/do-l", O_RDONLY | O_DIRECTORY, 0), 3);
    long n = sys_getdents64(3, dbuf, 4096);
    want("dirops 12: 5 records of 24 bytes: ", n, 5 * 24);
    want("dirops 12: well formed: ", split(dbuf, n), 1);
    want("dirops 12: 5 records: ", rec_n, 5);
    want("dirops 12: . first: ", named(0, "."), 1);
    want("dirops 12: .'s node is the folder's: ", rec_n > 0 && (long)recs[0]->ino == l, 1);
    want("dirops 12: .. second: ", named(1, ".."), 1);
    want("dirops 12: ..'s node is the parent's: ", rec_n > 1 && (long)recs[1]->ino == t, 1);
    want("dirops 12: . and .. DT_DIR: ",
         rec_n > 1 && recs[0]->type == DT_DIR && recs[1]->type == DT_DIR, 1);
    want("dirops 12: sub third, DT_DIR: ",
         named(2, "sub") && recs[2]->type == DT_DIR && (long)recs[2]->ino == sub, 1);
    want("dirops 12: fa fourth, DT_REG: ",
         named(3, "fa") && recs[3]->type == DT_REG && (long)recs[3]->ino == fa, 1);
    want("dirops 12: fb fifth, DT_REG: ",
         named(4, "fb") && recs[4]->type == DT_REG && (long)recs[4]->ino == fb, 1);
    want("dirops 12: d_off rising: ", offs_rise(0), 1);
    want("dirops 12: then 0 at the end: ", sys_getdents64(3, dbuf, 4096), 0);
    want("dirops 12: close: ", sys_close(3), 0);
    want("dirops 12: open /: ", sys_open("/", O_RDONLY | O_DIRECTORY, 0), 3);
    n = sys_getdents64(3, dbuf, 4096);
    want("dirops 12: / well formed: ", n > 0 && split(dbuf, n), 1);
    want("dirops 12: / . and .. both node 1: ",
         named(0, ".") && named(1, "..") && recs[0]->ino == 1 && recs[1]->ino == 1, 1);
    want("dirops 12: / then home and tmp: ", named(2, "home") && named(3, "tmp"), 1);
    want("dirops 12: close: ", sys_close(3), 0);
    want("dirops 12: unlink fa: ", sys_unlink("/tmp/do-l/fa"), 0);
    want("dirops 12: unlink fb: ", sys_unlink("/tmp/do-l/fb"), 0);
    want("dirops 12: rmdir sub: ", sys_rmdir("/tmp/do-l/sub"), 0);
    want("dirops 12: rmdir do-l: ", sys_rmdir("/tmp/do-l"), 0);
}

static void case_small(void)
{
    want("dirops 13: mkdir do-s: ", sys_mkdir("/tmp/do-s", 0755), 0);
    make("/tmp/do-s/e1", 1, 16);
    make("/tmp/do-s/e2", 1, 17);
    make("/tmp/do-s/e3", 1, 18);
    static const char *const names[] = { ".", "..", "e1", "e2", "e3" };
    want("dirops 13: open: ", sys_open("/tmp/do-s", O_RDONLY | O_DIRECTORY, 0), 3);
    for (int i = 0; i < 5; i++) {
        long n = sys_getdents64(3, dbuf, 24);
        want("dirops 13: one record per call: ", n, 24);
        want("dirops 13: in order: ", split(dbuf, n) && named(0, names[i]), 1);
    }
    want("dirops 13: then 0: ", sys_getdents64(3, dbuf, 24), 0);
    want("dirops 13: lseek 0: ", sys_lseek(3, 0, SEEK_SET), 0);
    want("dirops 13: a buffer one byte short EINVAL: ", sys_getdents64(3, dbuf, 23), -EINVAL);
    want("dirops 13: count 0 EINVAL: ", sys_getdents64(3, dbuf, 0), -EINVAL);
    want("dirops 13: the offset unchanged: ", sys_lseek(3, 0, SEEK_CUR), 0);
    long n = sys_getdents64(3, dbuf, 24);
    want("dirops 13: still . first: ", n == 24 && split(dbuf, n) && named(0, "."), 1);
    n = sys_getdents64(3, dbuf, 60);
    want("dirops 13: 60 bytes hold 2 whole records: ", n, 48);
    want("dirops 13: .. and e1: ", split(dbuf, n) && named(0, "..") && named(1, "e1"), 1);
    n = sys_getdents64(3, dbuf, 4096);
    want("dirops 13: the rest: ", n, 48);
    want("dirops 13: e2 and e3: ", split(dbuf, n) && named(0, "e2") && named(1, "e3"), 1);
    want("dirops 13: close: ", sys_close(3), 0);
    want("dirops 13: unlink e1: ", sys_unlink("/tmp/do-s/e1"), 0);
    want("dirops 13: unlink e2: ", sys_unlink("/tmp/do-s/e2"), 0);
    want("dirops 13: unlink e3: ", sys_unlink("/tmp/do-s/e3"), 0);
    want("dirops 13: rmdir: ", sys_rmdir("/tmp/do-s"), 0);
}

static void case_between(void)
{
    static const char *const files[] = {
        "/tmp/do-t/t0", "/tmp/do-t/t1", "/tmp/do-t/t2", "/tmp/do-t/t3", "/tmp/do-t/t4",
        "/tmp/do-t/t5",
    };
    want("dirops 14: mkdir do-t: ", sys_mkdir("/tmp/do-t", 0755), 0);
    for (int i = 0; i < 6; i++)
        make(files[i], 1, 20);
    want("dirops 14: open: ", sys_open("/tmp/do-t", O_RDONLY | O_DIRECTORY, 0), 3);
    long n = sys_getdents64(3, dbuf, 72);
    want("dirops 14: 3 records first: ", n, 72);
    want("dirops 14: ., .., t0: ",
         split(dbuf, n) && named(0, ".") && named(1, "..") && named(2, "t0"), 1);
    want("dirops 14: d_off rising: ", offs_rise(0), 1);
    int64_t last = rec_n == 3 ? recs[2]->off : 0;
    /* t0 already read, t1 not yet: both removed; t6 created. */
    want("dirops 14: unlink t0 (read): ", sys_unlink("/tmp/do-t/t0"), 0);
    want("dirops 14: unlink t1 (not read): ", sys_unlink("/tmp/do-t/t1"), 0);
    make("/tmp/do-t/t6", 1, 21);
    n = sys_getdents64(3, dbuf, 4096);
    want("dirops 14: the rest: 5 records: ", n, 5 * 24);
    want("dirops 14: t2 t3 t4 t5 t6 (t1 not returned, t0 not again, t6 once at the end): ",
         split(dbuf, n) && named(0, "t2") && named(1, "t3") && named(2, "t4") &&
             named(3, "t5") && named(4, "t6"), 1);
    want("dirops 14: d_off still rising: ", offs_rise(last), 1);
    int64_t after_t3 = rec_n > 1 ? recs[1]->off : 0;
    want("dirops 14: then 0: ", sys_getdents64(3, dbuf, 4096), 0);
    want("dirops 14: lseek to t3's d_off: ", sys_lseek(3, after_t3, SEEK_SET), after_t3);
    n = sys_getdents64(3, dbuf, 4096);
    want("dirops 14: resumes after t3: t4 t5 t6: ",
         n == 72 && split(dbuf, n) && named(0, "t4") && named(1, "t5") && named(2, "t6"), 1);
    want("dirops 14: lseek 0: ", sys_lseek(3, 0, SEEK_SET), 0);
    n = sys_getdents64(3, dbuf, 4096);
    want("dirops 14: again from the start: 7 records: ", n, 7 * 24);
    want("dirops 14: ., .., t2 .. t6: ",
         split(dbuf, n) && named(0, ".") && named(1, "..") && named(2, "t2") &&
             named(6, "t6"), 1);
    want("dirops 14: close: ", sys_close(3), 0);
    for (int i = 2; i < 6; i++)
        want("dirops 14: unlink: ", sys_unlink(files[i]), 0);
    want("dirops 14: unlink t6: ", sys_unlink("/tmp/do-t/t6"), 0);
    want("dirops 14: rmdir: ", sys_rmdir("/tmp/do-t"), 0);
}

static void case_dents_errors(void)
{
    want("dirops 15: EBADF closed: ", sys_getdents64(3, dbuf, 4096), -EBADF);
    want("dirops 15: EBADF 16: ", sys_getdents64(16, dbuf, 4096), -EBADF);
    want("dirops 15: EBADF -1: ", sys_getdents64((uint64_t)-1, dbuf, 4096), -EBADF);
    make("/tmp/do-f", 10, 22);
    want("dirops 15: open a file: ", sys_open("/tmp/do-f", O_RDONLY, 0), 3);
    want("dirops 15: ENOTDIR a file fd: ", sys_getdents64(3, dbuf, 4096), -ENOTDIR);
    want("dirops 15: ENOTDIR fd 0: ", sys_getdents64(0, dbuf, 4096), -ENOTDIR);
    want("dirops 15: ENOTDIR fd 1: ", sys_getdents64(1, dbuf, 4096), -ENOTDIR);
    want("dirops 15: close: ", sys_close(3), 0);
    want("dirops 15: mkdir do-g: ", sys_mkdir("/tmp/do-g", 0755), 0);
    make("/tmp/do-g/x", 1, 23);
    want("dirops 15: open do-g: ", sys_open("/tmp/do-g", O_RDONLY | O_DIRECTORY, 0), 3);
    want("dirops 15: EFAULT NULL: ", sys_getdents64(3, 0, 4096), -EFAULT);
    want("dirops 15: EFAULT unmapped: ", sys_getdents64(3, (void *)UNMAPPED, 4096), -EFAULT);
    want("dirops 15: EFAULT read-only: ", sys_getdents64(3, (void *)(uintptr_t)ro, 160), -EFAULT);
    want("dirops 15: EFAULT kernel: ", sys_getdents64(3, (void *)K_TEXT, 4096), -EFAULT);
    want("dirops 15: the offset unchanged: ", sys_lseek(3, 0, SEEK_CUR), 0);
    /* The last 24 bytes of the last mapped data page: room for '.', then
     * unmapped. */
    uint8_t *tail = (uint8_t *)(uintptr_t)(data_end() - 24);
    want("dirops 15: the tail is inside the area: ",
         (uint64_t)(uintptr_t)tail >= (uint64_t)(uintptr_t)dbuf + 32768 + 4600, 1);
    want("dirops 15: a buffer bad after the first record: ", sys_getdents64(3, tail, 100), 24);
    want("dirops 15: that record is .: ", split(tail, 24) && named(0, "."), 1);
    long n = sys_getdents64(3, dbuf, 4096);
    want("dirops 15: the next call goes on with ..: ", n == 48 && split(dbuf, n) &&
         named(0, "..") && named(1, "x"), 1);
    want("dirops 15: close: ", sys_close(3), 0);
    want("dirops 15: unlink do-g/x: ", sys_unlink("/tmp/do-g/x"), 0);
    want("dirops 15: rmdir do-g: ", sys_rmdir("/tmp/do-g"), 0);
    want("dirops 15: unlink do-f: ", sys_unlink("/tmp/do-f"), 0);
}

/* ---- 16: limits ---- */

static void case_limits(void)
{
    want("dirops 16: mkdir do-L: ", sys_mkdir("/tmp/do-L", 0755), 0);
    const char *b = build(pbuf, "/tmp/do-L/", 255, 'b');
    const char *c = build(qbuf, "/tmp/do-L/", 255, 'c');
    want("dirops 16: mkdir a 255-byte name: ", sys_mkdir(b, 0755), 0);
    want("dirops 16: rename it to another: ", sys_rename(b, c), 0);
    want("dirops 16: open do-L: ", sys_open("/tmp/do-L", O_RDONLY | O_DIRECTORY, 0), 3);
    long n = sys_getdents64(3, dbuf, 4096);
    want("dirops 16: ., .. and a 280-byte record: ", n, 24 + 24 + 280);
    want("dirops 16: well formed: ", split(dbuf, n) && rec_n == 3 && recs[2]->reclen == 280, 1);
    want("dirops 16: the name: ", rec_n == 3 && streq(recs[2]->name, c + 10), 1);
    want("dirops 16: close: ", sys_close(3), 0);
    want("dirops 16: rmdir it: ", sys_rmdir(c), 0);
    const char *b256 = build(pbuf, "/tmp/do-L/", 256, 'b');
    want("dirops 16: mkdir a 256-byte name ENAMETOOLONG: ", sys_mkdir(b256, 0755), -ENAMETOOLONG);
    make("/tmp/do-L/f", 1, 24);
    want("dirops 16: rename to a 256-byte name ENAMETOOLONG: ", sys_rename("/tmp/do-L/f", b256),
         -ENAMETOOLONG);
    want("dirops 16: unlink f: ", sys_unlink("/tmp/do-L/f"), 0);
    /* Folders of 255-byte names under /tmp/do-L (9 bytes): at depth 15
     * the path is 9 + 15 x 256 = 3849 bytes (3850 with its NUL); at 16,
     * 4105 (4106 with its NUL: over 4096). */
    const char *name = build(pbuf, "", 255, 'a');
    want("dirops 16: chdir do-L: ", sys_chdir("/tmp/do-L"), 0);
    int depth = 0;
    for (; depth < 16; depth++) {
        if (sys_mkdir(name, 0755) != 0 || sys_chdir(name) != 0)
            break;
        if (depth == 14)
            want("dirops 16: getcwd at depth 15: ", sys_getcwd(cbuf, 4096), 3850);
    }
    want("dirops 16: 16 folders deep: ", depth, 16);
    want("dirops 16: getcwd at depth 16 ENAMETOOLONG: ", sys_getcwd(cbuf, 4096), -ENAMETOOLONG);
    for (int i = 0; i < depth; i++) {
        want("dirops 16: chdir ..: ", sys_chdir(".."), 0);
        want("dirops 16: rmdir: ", sys_rmdir(name), 0);
    }
    want("dirops 16: back in do-L: ", cwd_is("/tmp/do-L"), 1);
    want("dirops 16: chdir /: ", sys_chdir("/"), 0);
    want("dirops 16: rmdir do-L: ", sys_rmdir("/tmp/do-L"), 0);
}

/* ---- 17-21: with the kernel ---- */

static void case_enospc(void)
{
    want("dirops 17: mkdir with no node left: ", sys_mkdir("/tmp/do-x", 0755), -ENOSPC);
    want("dirops 17: not made: ", sys_stat("/tmp/do-x", &st), -ENOENT);
    want("dirops 17: rename still works: ",
         sys_rename("/tmp/dio-nodes/n0", "/tmp/dio-nodes/m0"), 0);
    want("dirops 17: and back: ", sys_rename("/tmp/dio-nodes/m0", "/tmp/dio-nodes/n0"), 0);
}

static void case_cwd_end(long how)
{
    static const char *const dirs[] = { "/tmp/cw1", "/tmp/cw2", "/tmp/cw3" };
    want("dirops 18-20: chdir: ", sys_chdir(dirs[how]), 0);
    if (bad)
        return;
    if (how == 1)
        *(volatile uint64_t *)0 = 1;    /* a page fault: the process ends here */
    if (how == 2)
        for (;;)
            sys_sched_yield();          /* until killed */
}

static void case_big(void)
{
    want("dirops 21: open do-big: ", sys_open("/tmp/do-big", O_RDONLY | O_DIRECTORY, 0), 3);
    long recs_seen = 0, calls = 0, bytes = 0, n, okay = 1;
    while ((n = sys_getdents64(3, dbuf, 4096)) > 0 && calls < 100) {
        calls++;
        bytes += n;
        if (n > 4096 || n % 24 != 0)
            okay = 0;
        for (long pos = 0; pos + 24 <= n; pos += 24)
            if (!rec_ok(rec(dbuf, pos)) || rec(dbuf, pos)->reclen != 24)
                okay = 0;
        recs_seen += n / 24;
    }
    want("dirops 21: the last call 0: ", n, 0);
    want("dirops 21: 1002 records (., .. and 1000): ", recs_seen, 1002);
    want("dirops 21: in calls of at most 4096 bytes, whole records: ", okay, 1);
    want("dirops 21: 6 calls: ", calls, 6);
    want("dirops 21: 24048 bytes in all: ", bytes, 1002 * 24);
    want("dirops 21: lseek 0: ", sys_lseek(3, 0, SEEK_SET), 0);
    want("dirops 21: one call of 32 KiB returns all 24048 bytes: ",
         sys_getdents64(3, dbuf, BLOCK), 1002 * 24);
    want("dirops 21: close: ", sys_close(3), 0);
}

/* ---- 30-41: test=fsunlink ---- */

static long holds_words(uint64_t from, uint64_t n)
{
    /* The kernel's files hold, at each multiple of 8, that offset. */
    for (uint64_t i = 0; i + 8 <= n; i += 8)
        if (areaw[i / 8] != from + i)
            return 0;
    return 1;
}

static void case_unlinked_open(void)
{
    want("dirops 30: unlink /tmp/un-a (open as fd 3): ", sys_unlink("/tmp/un-a"), 0);
    want("dirops 30: the name gone: ", sys_stat("/tmp/un-a", &st), -ENOENT);
    want("dirops 30: fstat: ", sys_fstat(3, &st), 0);
    want("dirops 30: links 0: ", (long)st.st_nlink, 0);
    want("dirops 30: size kept: ", st.st_size, 10000);
    want("dirops 30: read 10000: ", sys_read(3, dbuf, 10000), 10000);
    want("dirops 30: the data: ", holds_words(0, 10000), 1);
    want("dirops 30: write 100 more: ", sys_write(3, dbuf, 100), 100);
    want("dirops 30: fstat: ", sys_fstat(3, &st), 0);
    want("dirops 30: size 10100: ", st.st_size, 10100);
}

static long poll_for(const char *path)
{
    int i = 0;
    while (i < POLL_MAX && sys_stat(path, &st) != 0) {
        tick();
        i++;
    }
    u_put_num("dirops: polls of 300: ", i);
    return i < POLL_MAX;
}

static void case_last_close(void)
{
    want("dirops 32: /tmp/un-go: ", poll_for("/tmp/un-go"), 1);
    want("dirops 32: fstat: ", sys_fstat(3, &s2), 0);
    want("dirops 32: links 0: ", (long)s2.st_nlink, 0);
    want("dirops 32: size 2 MiB: ", s2.st_size, 2 << 20);
    want("dirops 32: close (the last): ", sys_close(3), 0);
}

static void case_writer(long which)
{
    const char *path = which == 0 ? "/tmp/un-w" : "/tmp/un-v";
    want("dirops 35-36: open: ", sys_open(path, O_CREAT | O_RDWR | O_TRUNC, 0644), 3);
    want("dirops 35-36: fstat: ", sys_fstat(3, &s2), 0);
    long mine = (long)s2.st_ino;
    for (uint32_t k = 0; k < 32; k++) {
        if (k == 8) {
            /* Wait until the remover has unlinked (renamed over) it. */
            int i = 0;
            while (i < POLL_MAX &&
                   (which == 0 ? sys_stat(path, &st) == 0
                               : sys_stat(path, &st) != 0 || (long)st.st_ino == mine)) {
                tick();
                i++;
            }
            u_put_num("dirops: polls of 300: ", i);
            want("dirops 35-36: replaced while writing: ", i < POLL_MAX, 1);
        }
        for (uint32_t i = 0; i < BLOCK; i++)
            dbuf[i] = pat((uint64_t)k * BLOCK + i, 25);
        want("dirops 35-36: a 32 KiB write, the full count: ", sys_write(3, dbuf, BLOCK), BLOCK);
    }
    want("dirops 35-36: fstat: ", sys_fstat(3, &s2), 0);
    want("dirops 35-36: links 0: ", (long)s2.st_nlink, 0);
    want("dirops 35-36: size 1 MiB: ", s2.st_size, 32 * BLOCK);
    want("dirops 35-36: lseek 0: ", sys_lseek(3, 0, SEEK_SET), 0);
    long same = 1;
    for (uint32_t k = 0; k < 32; k++) {
        want("dirops 35-36: read 32 KiB: ", sys_read(3, dbuf, BLOCK), BLOCK);
        for (uint32_t i = 0; i < BLOCK; i++)
            if (dbuf[i] != pat((uint64_t)k * BLOCK + i, 25))
                same = 0;
    }
    want("dirops 35-36: every byte read back: ", same, 1);
    want("dirops 35-36: close: ", sys_close(3), 0);
}

static void case_rename_onto_open(void)
{
    want("dirops 39: open un-old: ", sys_open("/tmp/un-old", O_RDONLY, 0), 3);
    want("dirops 39: rename un-new over it: ", sys_rename("/tmp/un-new", "/tmp/un-old"), 0);
    want("dirops 39: the open fd reads the old data (100 bytes): ",
         sys_read(3, dbuf, 4096), 100);
    want("dirops 39: fstat: ", sys_fstat(3, &st), 0);
    want("dirops 39: links 0: ", (long)st.st_nlink, 0);
    want("dirops 39: open the name again: ", sys_open("/tmp/un-old", O_RDONLY, 0), 4);
    want("dirops 39: the new data (200 bytes): ", sys_read(4, dbuf, 4096), 200);
    want("dirops 39: un-new gone: ", sys_stat("/tmp/un-new", &st), -ENOENT);
    want("dirops 39: close 3 (the replaced node's last): ", sys_close(3), 0);
    want("dirops 39: close 4: ", sys_close(4), 0);
}

static void case_rmdir_cwd(void)
{
    want("dirops 40: mkdir un-c: ", sys_mkdir("/tmp/un-c", 0755), 0);
    want("dirops 40: chdir into it: ", sys_chdir("/tmp/un-c"), 0);
    want("dirops 40: rmdir it: ", sys_rmdir("/tmp/un-c"), 0);
    want("dirops 40: getcwd ENOENT: ", sys_getcwd(cbuf, 4096), -ENOENT);
    want("dirops 40: creating a relative name ENOENT: ",
         sys_open("x", O_CREAT | O_WRONLY, 0644), -ENOENT);
    want("dirops 40: mkdir a relative name ENOENT: ", sys_mkdir("y", 0755), -ENOENT);
    want("dirops 40: stat .: ", sys_stat(".", &st), 0);
    want("dirops 40: links 0: ", (long)st.st_nlink, 0);
    want("dirops 40: stat .. ENOENT (S6-4): ", sys_stat("..", &st), -ENOENT);
    want("dirops 40: chdir .. ENOENT: ", sys_chdir(".."), -ENOENT);
    want("dirops 40: chdir /: ", sys_chdir("/"), 0);
    want("dirops 40: at /: ", cwd_is("/"), 1);
}

static void case_rmdir_open(void)
{
    want("dirops 41: mkdir un-o: ", sys_mkdir("/tmp/un-o", 0755), 0);
    want("dirops 41: open it: ", sys_open("/tmp/un-o", O_RDONLY | O_DIRECTORY, 0), 3);
    want("dirops 41: rmdir it: ", sys_rmdir("/tmp/un-o"), 0);
    want("dirops 41: getdents64 ENOENT: ", sys_getdents64(3, dbuf, 4096), -ENOENT);
    want("dirops 41: fstat: ", sys_fstat(3, &st), 0);
    want("dirops 41: a folder with links 0: ",
         (st.st_mode & S_IFMT) == S_IFDIR && st.st_nlink == 0, 1);
    want("dirops 41: close: ", sys_close(3), 0);
}

int main(long arg)
{
    switch (arg) {
    case 1: case_moves(); break;
    case 2: case_getcwd_errors(); break;
    case 3: case_chdir_errors(); break;
    case 4: case_relative(); break;
    case 5: case_mkdir(); break;
    case 6: case_mkdirat(); break;
    case 7: case_rmdir(); break;
    case 8: case_unlink(); break;
    case 9: case_unlinkat(); break;
    case 10: case_rename(); break;
    case 11: case_rename_errors(); break;
    case 12: case_list(); break;
    case 13: case_small(); break;
    case 14: case_between(); break;
    case 15: case_dents_errors(); break;
    case 16: case_limits(); break;
    case 17: case_enospc(); break;
    case 18: case 19: case 20: case_cwd_end(arg - 18); break;
    case 21: case_big(); break;
    case 30: case_unlinked_open(); break;
    case 31:
        want("dirops 31: unlink un-big: ", sys_unlink("/tmp/un-big"), 0);
        want("dirops 31: gone: ", sys_stat("/tmp/un-big", &st), -ENOENT);
        break;
    case 32: case_last_close(); break;
    case 33:
        for (;;)
            sys_sched_yield();          /* holds fd 3 until killed */
        break;
    case 34:
        want("dirops 34: /tmp/un-go: ", poll_for("/tmp/un-go"), 1);
        if (!bad)
            *(volatile uint64_t *)0 = 1;    /* a page fault with fd 3 open */
        break;
    case 35: case 36: case_writer(arg - 35); break;
    case 37: want("dirops 37: unlink un-w: ", sys_unlink("/tmp/un-w"), 0); break;
    case 38:
        make("/tmp/un-x", 10, 26);
        want("dirops 38: rename un-x over un-v: ", sys_rename("/tmp/un-x", "/tmp/un-v"), 0);
        want("dirops 38: un-v is it: ", sys_stat("/tmp/un-v", &st), 0);
        want("dirops 38: size 10: ", st.st_size, 10);
        break;
    case 39: case_rename_onto_open(); break;
    case 40: case_rmdir_cwd(); break;
    case 41: case_rmdir_open(); break;
    default: bad = 99; break;
    }
    return bad > 255 ? 255 : bad;
}
