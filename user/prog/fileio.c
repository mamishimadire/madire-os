/* fileio (1.0 stage 5, test=fileio): open, stat, lstat, fstat, newfstatat,
 * lseek, ftruncate and openat on the RAM filesystem, one case per arg;
 * files under /tmp/fio-*. Exits with the number of wrong results (each one
 * logged).
 *   1  O_CREAT, write, close, reopen, read back      2  O_EXCL, O_TRUNC, flags
 *   3  O_APPEND                                       4  lseek
 *   5  the stat calls agree on a file; times          6  stat on folders
 *   7  ftruncate                                      8  openat, newfstatat
 *   9  the console descriptors                        10 EMFILE from open
 *   11 ENFILE (the kernel fills the open-file table first)
 *   12 ENOSPC (the kernel fills the nodes first) */
#include "user/ulib.h"

static uint8_t buf[16384];
static int bad;
static struct stat st, a, t, tmp;     /* file scope: never read uninitialized */

static void want(const char *what, long got, long expected)
{
    bad += u_expect(what, got, expected);
}

static void fill(uint64_t n, uint32_t seed)
{
    for (uint64_t i = 0; i < n; i++)
        buf[i] = (uint8_t)(i * 31 + seed);
}

static long holds(uint64_t n, uint32_t seed)
{
    for (uint64_t i = 0; i < n; i++)
        if (buf[i] != (uint8_t)(i * 31 + seed))
            return 0;
    return 1;
}

static long zeros(uint64_t n)
{
    for (uint64_t i = 0; i < n; i++)
        if (buf[i])
            return 0;
    return 1;
}

static void clear(uint64_t n)
{
    for (uint64_t i = 0; i < n; i++)
        buf[i] = 0;
}

/* A new file of n bytes (seed's pattern), closed again. */
static void make(const char *path, uint64_t n, uint32_t seed)
{
    long fd = sys_open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    want("fileio: make: open: ", fd >= 0, 1);
    fill(n, seed);
    want("fileio: make: write: ", sys_write((uint64_t)fd, buf, n), (long)n);
    want("fileio: make: close: ", sys_close((uint64_t)fd), 0);
}

static void case_create(void)
{
    long fd = sys_open("/tmp/fio-a", O_CREAT | O_WRONLY, 0644);
    want("fileio 1: open O_CREAT: ", fd, 3);
    fill(100, 1);
    want("fileio 1: write 100: ", sys_write(3, buf, 100), 100);
    want("fileio 1: close: ", sys_close(3), 0);
    want("fileio 1: open O_RDONLY: ", sys_open("/tmp/fio-a", O_RDONLY, 0), 3);
    clear(100);
    want("fileio 1: read 100: ", sys_read(3, buf, 100), 100);
    want("fileio 1: bytes: ", holds(100, 1), 1);
    want("fileio 1: read at the end: ", sys_read(3, buf, 10), 0);

    want("fileio 1: fstat: ", sys_fstat(3, &st), 0);
    want("fileio 1: mode: ", st.st_mode, S_IFREG | 0644);
    want("fileio 1: size: ", st.st_size, 100);
    want("fileio 1: close again: ", sys_close(3), 0);
}

static void case_flags(void)
{

    want("fileio 2: O_EXCL existing: ", sys_open("/tmp/fio-a", O_CREAT | O_EXCL | O_WRONLY, 0644),
         -EEXIST);
    want("fileio 2: O_EXCL new: ", sys_open("/tmp/fio-b", O_CREAT | O_EXCL | O_WRONLY, 0644), 3);
    want("fileio 2: close: ", sys_close(3), 0);
    want("fileio 2: stat new: ", sys_stat("/tmp/fio-b", &st), 0);
    want("fileio 2: new size: ", st.st_size, 0);
    make("/tmp/fio-b", 5000, 2);
    want("fileio 2: stat 5000: ", sys_stat("/tmp/fio-b", &st), 0);
    want("fileio 2: blocks of 5000 bytes: ", st.st_blocks, 24);    /* index + 2 data pages */
    want("fileio 2: O_TRUNC: ", sys_open("/tmp/fio-b", O_WRONLY | O_TRUNC, 0), 3);
    want("fileio 2: fstat after O_TRUNC: ", sys_fstat(3, &st), 0);
    want("fileio 2: size after O_TRUNC: ", st.st_size, 0);
    want("fileio 2: blocks after O_TRUNC: ", st.st_blocks, 0);
    want("fileio 2: write after O_TRUNC: ", sys_write(3, buf, 100), 100);
    want("fileio 2: close: ", sys_close(3), 0);
    want("fileio 2: O_RDONLY|O_TRUNC: ", sys_open("/tmp/fio-b", O_RDONLY | O_TRUNC, 0), 3);
    want("fileio 2: fstat after O_RDONLY|O_TRUNC: ", sys_fstat(3, &st), 0);
    want("fileio 2: size after O_RDONLY|O_TRUNC: ", st.st_size, 0);
    want("fileio 2: blocks after O_RDONLY|O_TRUNC: ", st.st_blocks, 0);
    want("fileio 2: close: ", sys_close(3), 0);
    want("fileio 2: O_CLOEXEC: ", sys_open("/tmp/fio-a", O_RDONLY | O_CLOEXEC, 0), 3);
    want("fileio 2: read through it: ", sys_read(3, buf, 10), 10);
    want("fileio 2: close: ", sys_close(3), 0);
    want("fileio 2: unknown flag bits: ", sys_open("/tmp/fio-a", O_RDONLY | 0x40000000, 0), 3);
    want("fileio 2: read through it: ", sys_read(3, buf, 10), 10);
    want("fileio 2: close: ", sys_close(3), 0);
}

static void case_append(void)
{
    make("/tmp/fio-c", 100, 3);
    want("fileio 3: open O_APPEND: ", sys_open("/tmp/fio-c", O_WRONLY | O_APPEND, 0), 3);
    want("fileio 3: lseek 0: ", sys_lseek(3, 0, SEEK_SET), 0);
    fill(10, 9);
    want("fileio 3: write 10: ", sys_write(3, buf, 10), 10);
    want("fileio 3: offset after: ", sys_lseek(3, 0, SEEK_CUR), 110);
    want("fileio 3: close: ", sys_close(3), 0);
    want("fileio 3: reopen: ", sys_open("/tmp/fio-c", O_RDONLY, 0), 3);
    clear(110);
    want("fileio 3: read 110: ", sys_read(3, buf, 110), 110);
    want("fileio 3: the first 100 bytes kept: ", holds(100, 3), 1);
    long tail = 1;
    for (unsigned i = 0; i < 10; i++)
        if (buf[100 + i] != (uint8_t)(i * 31 + 9))
            tail = 0;
    want("fileio 3: the 10 bytes at the end: ", tail, 1);
    want("fileio 3: close: ", sys_close(3), 0);
}

static void case_lseek(void)
{
    make("/tmp/fio-d", 1000, 4);
    want("fileio 4: open: ", sys_open("/tmp/fio-d", O_RDWR, 0), 3);
    want("fileio 4: SEEK_SET 50: ", sys_lseek(3, 50, SEEK_SET), 50);
    want("fileio 4: SEEK_CUR +10: ", sys_lseek(3, 10, SEEK_CUR), 60);
    want("fileio 4: SEEK_CUR -70: ", sys_lseek(3, -70, SEEK_CUR), -EINVAL);
    want("fileio 4: unchanged: ", sys_lseek(3, 0, SEEK_CUR), 60);
    want("fileio 4: SEEK_END -10: ", sys_lseek(3, -10, SEEK_END), 990);
    want("fileio 4: past the end: ", sys_lseek(3, 9192, SEEK_SET), 9192);
    want("fileio 4: write there: ", sys_write(3, "Z", 1), 1);

    want("fileio 4: fstat: ", sys_fstat(3, &st), 0);
    want("fileio 4: size: ", st.st_size, 9193);
    want("fileio 4: back into the hole: ", sys_lseek(3, 1000, SEEK_SET), 1000);
    for (unsigned i = 0; i < 4096; i++)
        buf[i] = 0xAA;
    want("fileio 4: read the hole: ", sys_read(3, buf, 4096), 4096);
    want("fileio 4: the hole reads as zeros: ", zeros(4096), 1);
    want("fileio 4: offset before the errors: ", sys_lseek(3, 100, SEEK_SET), 100);
    want("fileio 4: SEEK_DATA: ", sys_lseek(3, 0, 3), -EINVAL);
    want("fileio 4: SEEK_HOLE: ", sys_lseek(3, 0, 4), -EINVAL);
    want("fileio 4: whence 5: ", sys_lseek(3, 0, 5), -EINVAL);
    want("fileio 4: unchanged: ", sys_lseek(3, 0, SEEK_CUR), 100);
    want("fileio 4: INT64_MAX: ", sys_lseek(3, INT64_MAX, SEEK_SET), INT64_MAX);
    want("fileio 4: overflow: ", sys_lseek(3, 1, SEEK_CUR), -EINVAL);
    want("fileio 4: unchanged at INT64_MAX: ", sys_lseek(3, 0, SEEK_CUR), INT64_MAX);
    want("fileio 4: 3 MiB: ", sys_lseek(3, 3 << 20, SEEK_SET), 3 << 20);
    want("fileio 4: write at 3 MiB: ", sys_write(3, "Z", 1), -EFBIG);
    want("fileio 4: read at 3 MiB: ", sys_read(3, buf, 10), 0);
    want("fileio 4: close: ", sys_close(3), 0);
}

/* 160 bytes: the 144-byte stat structure and 16 bytes after it that must
 * stay untouched. */
static uint64_t statbuf[20];

static long stat_exact(void)
{
    const uint8_t *b = (const uint8_t *)statbuf;
    for (unsigned i = 144; i < 160; i++)
        if (b[i] != 0xAA)
            return 0;
    const struct stat *st = (const struct stat *)statbuf;
    return st->pad0 == 0 && st->reserved[0] == 0 && st->reserved[1] == 0 && st->reserved[2] == 0 &&
           st->st_atime_nsec == 0 && st->st_mtime_nsec == 0 && st->st_ctime_nsec == 0;
}

static void poison(void)
{
    uint8_t *b = (uint8_t *)statbuf;
    for (unsigned i = 0; i < sizeof(statbuf); i++)
        b[i] = 0xAA;
}

static void case_stat(void)
{
    struct stat *st = (struct stat *)statbuf;
    want("fileio 5: open O_CREAT: ", sys_open("/tmp/fio-e", O_CREAT | O_RDWR | O_TRUNC, 0644), 3);
    fill(5000, 5);
    want("fileio 5: write 5000: ", sys_write(3, buf, 5000), 5000);
    uint64_t ino[4], dev[4];
    for (int k = 0; k < 4; k++) {
        poison();
        long r = k == 0 ? sys_stat("/tmp/fio-e", st)
               : k == 1 ? sys_lstat("/tmp/fio-e", st)
               : k == 2 ? sys_fstat(3, st)
               : sys_newfstatat((uint64_t)AT_FDCWD, "tmp/fio-e", st, 0);
        want("fileio 5: call (0 stat, 1 lstat, 2 fstat, 3 newfstatat): ", r == 0 ? 0 : k, 0);
        want("fileio 5: exactly 144 bytes written: ", stat_exact(), 1);
        want("fileio 5: mode: ", st->st_mode, S_IFREG | 0644);
        want("fileio 5: size: ", st->st_size, 5000);
        want("fileio 5: links: ", (long)st->st_nlink, 1);
        want("fileio 5: blksize: ", st->st_blksize, 4096);
        want("fileio 5: blocks (3 pages x 8): ", st->st_blocks, 24);
        want("fileio 5: uid: ", st->st_uid, 0);
        want("fileio 5: gid: ", st->st_gid, 0);
        want("fileio 5: rdev: ", (long)st->st_rdev, 0);
        want("fileio 5: mtime after 2000: ", st->st_mtime_sec > 946684800, 1);
        ino[k] = st->st_ino;
        dev[k] = st->st_dev;
    }
    for (int k = 1; k < 4; k++) {
        want("fileio 5: the same node number: ", ino[k] == ino[0], 1);
        want("fileio 5: the same device: ", dev[k] == dev[0], 1);
    }
    int64_t m1 = st->st_mtime_sec, c1 = st->st_ctime_sec;
    want("fileio 5: ftruncate 100: ", sys_ftruncate(3, 100), 0);
    want("fileio 5: fstat after: ", sys_fstat(3, st), 0);
    want("fileio 5: size after ftruncate: ", st->st_size, 100);
    want("fileio 5: mtime kept or later: ", st->st_mtime_sec >= m1, 1);
    want("fileio 5: ctime kept or later: ", st->st_ctime_sec >= c1, 1);
    want("fileio 5: close: ", sys_close(3), 0);
}

static void case_folders(void)
{

    want("fileio 6: stat /: ", sys_stat("/", &st), 0);
    want("fileio 6: / mode: ", st.st_mode, S_IFDIR | 0755);
    want("fileio 6: / node: ", (long)st.st_ino, 1);
    want("fileio 6: / links (home, tmp): ", (long)st.st_nlink, 4);
    want("fileio 6: / size: ", st.st_size, 0);
    want("fileio 6: stat /tmp: ", sys_stat("/tmp", &tmp), 0);
    want("fileio 6: /tmp mode: ", tmp.st_mode, S_IFDIR | 01777);
    /* The kernel made /tmp/fio-sub1 and /tmp/fio-sub2 before this case. */
    want("fileio 6: /tmp links (2 subfolders): ", (long)tmp.st_nlink, 4);
    want("fileio 6: stat /tmp/fio-sub1: ", sys_stat("/tmp/fio-sub1", &st), 0);
    want("fileio 6: an empty folder's links: ", (long)st.st_nlink, 2);
    want("fileio 6: blocks of a folder: ", st.st_blocks, 0);
}

static void case_ftruncate(void)
{

    want("fileio 7: open: ", sys_open("/tmp/fio-f", O_CREAT | O_RDWR | O_TRUNC, 0644), 3);
    fill(12288, 7);
    want("fileio 7: write 12288: ", sys_write(3, buf, 12288), 12288);
    want("fileio 7: fstat: ", sys_fstat(3, &st), 0);
    want("fileio 7: blocks (index + 3 pages): ", st.st_blocks, 32);
    want("fileio 7: grow to 1 MiB: ", sys_ftruncate(3, 1 << 20), 0);
    want("fileio 7: fstat: ", sys_fstat(3, &st), 0);
    want("fileio 7: size 1 MiB: ", st.st_size, 1 << 20);
    want("fileio 7: growing took no page: ", st.st_blocks, 32);
    want("fileio 7: offset unchanged: ", sys_lseek(3, 0, SEEK_CUR), 12288);
    want("fileio 7: to 512 KiB: ", sys_lseek(3, 512 << 10, SEEK_SET), 512 << 10);
    for (unsigned i = 0; i < 4096; i++)
        buf[i] = 0xAA;
    want("fileio 7: read in the hole: ", sys_read(3, buf, 4096), 4096);
    want("fileio 7: the hole reads as zeros: ", zeros(4096), 1);
    want("fileio 7: shrink to 10: ", sys_ftruncate(3, 10), 0);
    want("fileio 7: fstat: ", sys_fstat(3, &st), 0);
    want("fileio 7: size 10: ", st.st_size, 10);
    want("fileio 7: pages freed (index + 1): ", st.st_blocks, 16);
    want("fileio 7: offset unchanged: ", sys_lseek(3, 0, SEEK_CUR), (512 << 10) + 4096);
    want("fileio 7: to 0: ", sys_lseek(3, 0, SEEK_SET), 0);
    clear(10);
    want("fileio 7: read the 10 left: ", sys_read(3, buf, 100), 10);
    want("fileio 7: the 10 bytes kept: ", holds(10, 7), 1);
    want("fileio 7: exactly 2 MiB: ", sys_ftruncate(3, 2 << 20), 0);
    want("fileio 7: fstat: ", sys_fstat(3, &st), 0);
    want("fileio 7: size 2 MiB: ", st.st_size, 2 << 20);
    want("fileio 7: offset unchanged: ", sys_lseek(3, 0, SEEK_CUR), 10);
    want("fileio 7: close: ", sys_close(3), 0);
}

static void case_openat(void)
{

    want("fileio 8: stat fio-a: ", sys_stat("/tmp/fio-a", &a), 0);
    want("fileio 8: stat /tmp: ", sys_stat("/tmp", &t), 0);
    want("fileio 8: open /tmp O_DIRECTORY: ", sys_open("/tmp", O_RDONLY | O_DIRECTORY, 0), 3);
    want("fileio 8: openat(dirfd, fio-a): ", sys_openat(3, "fio-a", O_RDONLY, 0), 4);
    want("fileio 8: fstat: ", sys_fstat(4, &st), 0);
    want("fileio 8: the same node (dirfd): ", st.st_ino == a.st_ino, 1);
    want("fileio 8: close: ", sys_close(4), 0);
    want("fileio 8: openat(AT_FDCWD, tmp/fio-a): ",
         sys_openat((uint64_t)AT_FDCWD, "tmp/fio-a", O_RDONLY, 0), 4);
    want("fileio 8: fstat: ", sys_fstat(4, &st), 0);
    want("fileio 8: the same node (AT_FDCWD, the root): ", st.st_ino == a.st_ino, 1);
    want("fileio 8: close: ", sys_close(4), 0);
    want("fileio 8: openat(99, /tmp/fio-a): ", sys_openat(99, "/tmp/fio-a", O_RDONLY, 0), 4);
    want("fileio 8: fstat: ", sys_fstat(4, &st), 0);
    want("fileio 8: the same node (absolute, bad dirfd): ", st.st_ino == a.st_ino, 1);
    want("fileio 8: close: ", sys_close(4), 0);
    want("fileio 8: newfstatat(dirfd, \"\", AT_EMPTY_PATH): ",
         sys_newfstatat(3, "", &st, AT_EMPTY_PATH), 0);
    want("fileio 8: it is /tmp: ", st.st_ino == t.st_ino && (st.st_mode & S_IFMT) == S_IFDIR, 1);
    want("fileio 8: newfstatat(AT_FDCWD, \"\", AT_EMPTY_PATH): ",
         sys_newfstatat((uint64_t)AT_FDCWD, "", &st, AT_EMPTY_PATH), 0);
    want("fileio 8: it is / (node 1): ", (long)st.st_ino, 1);
    want("fileio 8: read on a folder fd: ", sys_read(3, buf, 10), -EISDIR);
    want("fileio 8: close: ", sys_close(3), 0);
}

static void case_console(void)
{

    for (uint64_t fd = 0; fd < 3; fd++)
        want("fileio 9: lseek on a console fd: ", sys_lseek(fd, 0, SEEK_SET), -ESPIPE);
    for (uint64_t fd = 0; fd < 3; fd++) {
        want("fileio 9: fstat on a console fd: ", sys_fstat(fd, &st), 0);
        want("fileio 9: a character device: ", st.st_mode, S_IFCHR | 0620);
        want("fileio 9: rdev 5:1: ", (long)st.st_rdev, 0x501);
        want("fileio 9: size 0: ", st.st_size, 0);
    }
}

static void case_emfile(void)
{
    for (long k = 3; k < 16; k++)
        want("fileio 10: open: ", sys_open("/tmp/fio-a", O_RDONLY, 0), k);
    want("fileio 10: open with 16 open: ", sys_open("/tmp/fio-a", O_RDONLY, 0), -EMFILE);
    /* The kernel counts the open files now (13 + the console's 2). */
    struct timespec ts = { 0, 300000000 };
    sys_nanosleep(&ts, 0);
    want("fileio 10: close 7: ", sys_close(7), 0);
    want("fileio 10: open after close(7): ", sys_open("/tmp/fio-a", O_RDONLY, 0), 7);
    for (uint64_t fd = 3; fd < 16; fd++)
        want("fileio 10: close: ", sys_close(fd), 0);
}

static void case_enfile(void)
{

    want("fileio 11: open with the table full: ", sys_open("/tmp/fio-a", O_RDONLY, 0), -ENFILE);
    want("fileio 11: O_CREAT with the table full: ",
         sys_open("/tmp/fio-new", O_CREAT | O_WRONLY, 0644), -ENFILE);
    long r = -ENFILE;
    for (int i = 0; i < 3000 && r == -ENFILE; i++) {
        r = sys_open("/tmp/fio-a", O_RDONLY, 0);
        if (r == -ENFILE)
            sys_sched_yield();
    }
    want("fileio 11: open once one is free: ", r, 3);
    want("fileio 11: fio-new not created: ", sys_stat("/tmp/fio-new", &st), -ENOENT);
    want("fileio 11: close: ", sys_close(3), 0);
}

static void case_enospc(void)
{

    want("fileio 12: O_CREAT with no node left: ",
         sys_open("/tmp/fio-x", O_CREAT | O_WRONLY, 0644), -ENOSPC);
    want("fileio 12: fio-x not created: ", sys_stat("/tmp/fio-x", &st), -ENOENT);
    want("fileio 12: an existing file opens at fd 3: ", sys_open("/tmp/fio-a", O_RDONLY, 0), 3);
    want("fileio 12: close: ", sys_close(3), 0);
}

int main(int argc, char **argv)
{
    long arg = u_arg(argc, argv);
    switch (arg) {
    case 1: case_create(); break;
    case 2: case_flags(); break;
    case 3: case_append(); break;
    case 4: case_lseek(); break;
    case 5: case_stat(); break;
    case 6: case_folders(); break;
    case 7: case_ftruncate(); break;
    case 8: case_openat(); break;
    case 9: case_console(); break;
    case 10: case_emfile(); break;
    case 11: case_enfile(); break;
    case 12: case_enospc(); break;
    default: bad = 99; break;
    }
    return bad > 255 ? 255 : bad;
}
