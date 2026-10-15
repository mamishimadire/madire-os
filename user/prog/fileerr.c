/* fileerr (1.0 stage 5, test=fileio): the errno matrix of open, stat,
 * lstat, fstat, openat, newfstatat, lseek and ftruncate, one group per
 * arg; the kernel makes /tmp/ferr-f (a file of 100 bytes) first. Exits
 * with the number of wrong results (each one logged).
 *   1 open   2 stat, lstat   3 fstat   4 dirfd (openat, newfstatat)
 *   5 lseek  6 ftruncate */
#include "user/ulib.h"

static char path[4200];
static int bad;
static struct stat st;
static const char ro[160] = { 1 };      /* read-only data: not writable */

#define UNMAPPED 0x10000000ull

static void want(const char *what, long got, long expected)
{
    bad += u_expect(what, got, expected);
}

/* "/tmp/" and a name of n bytes 'n'. */
static const char *long_name(unsigned n)
{
    const char *head = "/tmp/";
    unsigned p = 0;
    while (head[p]) {
        path[p] = head[p];
        p++;
    }
    for (unsigned i = 0; i < n; i++)
        path[p++] = 'n';
    path[p] = '\0';
    return path;
}

/* 4096 bytes with no NUL (a NUL only after them). */
static const char *no_nul(void)
{
    for (unsigned i = 0; i < 4096; i++)
        path[i] = 'a';
    path[4096] = '\0';
    return path;
}

static void group_open(void)
{
    want("fileerr 1: NULL: ", sys_open(0, O_RDONLY, 0), -EFAULT);
    want("fileerr 1: unmapped: ", sys_open((const char *)UNMAPPED, O_RDONLY, 0), -EFAULT);
    want("fileerr 1: kernel: ", sys_open((const char *)K_TEXT, O_RDONLY, 0), -EFAULT);
    want("fileerr 1: missing file: ", sys_open("/tmp/ferr-nosuch", O_RDONLY, 0), -ENOENT);
    want("fileerr 1: missing folder on the way: ",
         sys_open("/tmp/ferr-nodir/x", O_CREAT | O_WRONLY, 0644), -ENOENT);
    want("fileerr 1: empty path: ", sys_open("", O_RDONLY, 0), -ENOENT);
    want("fileerr 1: a file on the way: ", sys_open("/tmp/ferr-f/x", O_RDONLY, 0), -ENOTDIR);
    want("fileerr 1: O_DIRECTORY on a file: ",
         sys_open("/tmp/ferr-f", O_RDONLY | O_DIRECTORY, 0), -ENOTDIR);
    want("fileerr 1: trailing '/' on a file: ", sys_open("/tmp/ferr-f/", O_RDONLY, 0), -ENOTDIR);
    want("fileerr 1: O_CREAT|O_EXCL on a file: ",
         sys_open("/tmp/ferr-f", O_CREAT | O_EXCL | O_WRONLY, 0644), -EEXIST);
    want("fileerr 1: O_CREAT|O_EXCL on /: ", sys_open("/", O_CREAT | O_EXCL | O_WRONLY, 0),
         -EEXIST);
    want("fileerr 1: a folder O_WRONLY: ", sys_open("/tmp", O_WRONLY, 0), -EISDIR);
    want("fileerr 1: a folder O_RDWR: ", sys_open("/tmp", O_RDWR, 0), -EISDIR);
    want("fileerr 1: O_TRUNC on a folder: ", sys_open("/tmp", O_RDONLY | O_TRUNC, 0), -EISDIR);
    want("fileerr 1: O_CREAT on a folder: ", sys_open("/tmp", O_CREAT | O_RDONLY, 0), -EISDIR);
    want("fileerr 1: O_CREAT with a trailing '/': ",
         sys_open("/tmp/ferr-new/", O_CREAT | O_WRONLY, 0644), -EISDIR);
    want("fileerr 1: a 256-byte name: ", sys_open(long_name(256), O_RDONLY, 0), -ENAMETOOLONG);
    want("fileerr 1: a 256-byte name, O_CREAT: ",
         sys_open(long_name(256), O_CREAT | O_WRONLY, 0644), -ENAMETOOLONG);
    want("fileerr 1: no NUL within 4096 bytes: ", sys_open(no_nul(), O_RDONLY, 0), -ENAMETOOLONG);
    want("fileerr 1: access mode 3: ", sys_open("/tmp/ferr-f", 3, 0), -EINVAL);
    want("fileerr 1: ferr-new not created: ", sys_stat("/tmp/ferr-new", &st), -ENOENT);
    want("fileerr 1: no descriptor taken: ", sys_open("/tmp/ferr-f", O_RDONLY, 0), 3);
    want("fileerr 1: close: ", sys_close(3), 0);
}

static void group_stat(void)
{
    for (int l = 0; l < 2; l++) {
        long (*call)(const char *, struct stat *) = l ? sys_lstat : sys_stat;
        want("fileerr 2: NULL path (0 stat, 1 lstat): ", call(0, &st) == -EFAULT ? 0 : l + 1, 0);
        want("fileerr 2: unmapped path: ", call((const char *)UNMAPPED, &st), -EFAULT);
        want("fileerr 2: unmapped buffer: ", call("/tmp/ferr-f", (struct stat *)UNMAPPED), -EFAULT);
        want("fileerr 2: read-only buffer: ", call("/tmp/ferr-f", (struct stat *)(uintptr_t)ro),
             -EFAULT);
        want("fileerr 2: kernel buffer: ", call("/tmp/ferr-f", (struct stat *)K_TEXT), -EFAULT);
        want("fileerr 2: missing: ", call("/tmp/ferr-nosuch", &st), -ENOENT);
        want("fileerr 2: a file on the way: ", call("/tmp/ferr-f/x", &st), -ENOTDIR);
        want("fileerr 2: a 256-byte name: ", call(long_name(256), &st), -ENAMETOOLONG);
        want("fileerr 2: no NUL within 4096 bytes: ", call(no_nul(), &st), -ENAMETOOLONG);
        want("fileerr 2: empty path: ", call("", &st), -ENOENT);
        want("fileerr 2: a good call: ", call("/tmp/ferr-f", &st), 0);
        want("fileerr 2: its size: ", st.st_size, 100);
    }
}

static void group_fstat(void)
{
    want("fileerr 3: open: ", sys_open("/tmp/ferr-f", O_RDONLY, 0), 3);
    want("fileerr 3: close: ", sys_close(3), 0);
    want("fileerr 3: closed fd: ", sys_fstat(3, &st), -EBADF);
    want("fileerr 3: fd 16: ", sys_fstat(16, &st), -EBADF);
    want("fileerr 3: fd -1: ", sys_fstat((uint64_t)-1, &st), -EBADF);
    want("fileerr 3: open again: ", sys_open("/tmp/ferr-f", O_RDONLY, 0), 3);
    want("fileerr 3: fd 2^32+3 (not fd 3): ", sys_fstat((1ull << 32) + 3, &st), -EBADF);
    want("fileerr 3: unmapped buffer: ", sys_fstat(3, (struct stat *)UNMAPPED), -EFAULT);
    want("fileerr 3: kernel buffer: ", sys_fstat(3, (struct stat *)K_TEXT), -EFAULT);
    want("fileerr 3: a good call: ", sys_fstat(3, &st), 0);
    want("fileerr 3: close: ", sys_close(3), 0);
}

static void group_dirfd(void)
{
    want("fileerr 4: open the file: ", sys_open("/tmp/ferr-f", O_RDONLY, 0), 3);
    want("fileerr 4: openat, dirfd not open: ", sys_openat(9, "ferr-f", O_RDONLY, 0), -EBADF);
    want("fileerr 4: openat, dirfd a file: ", sys_openat(3, "x", O_RDONLY, 0), -ENOTDIR);
    want("fileerr 4: openat, dirfd the console: ", sys_openat(1, "x", O_RDONLY, 0), -ENOTDIR);
    want("fileerr 4: newfstatat, dirfd not open: ", sys_newfstatat(9, "ferr-f", &st, 0), -EBADF);
    want("fileerr 4: newfstatat, dirfd a file: ", sys_newfstatat(3, "x", &st, 0), -ENOTDIR);
    want("fileerr 4: newfstatat, dirfd the console: ", sys_newfstatat(1, "x", &st, 0), -ENOTDIR);
    want("fileerr 4: newfstatat flag 0x1: ",
         sys_newfstatat((uint64_t)AT_FDCWD, "/tmp/ferr-f", &st, 0x1), -EINVAL);
    want("fileerr 4: newfstatat AT_REMOVEDIR: ",
         sys_newfstatat((uint64_t)AT_FDCWD, "/tmp/ferr-f", &st, AT_REMOVEDIR), -EINVAL);
    want("fileerr 4: newfstatat flag 0x4000: ",
         sys_newfstatat((uint64_t)AT_FDCWD, "/tmp/ferr-f", &st, 0x4000), -EINVAL);
    want("fileerr 4: \"\" without AT_EMPTY_PATH: ",
         sys_newfstatat((uint64_t)AT_FDCWD, "", &st, 0), -ENOENT);
    want("fileerr 4: AT_SYMLINK_NOFOLLOW: ",
         sys_newfstatat((uint64_t)AT_FDCWD, "/tmp/ferr-f", &st, AT_SYMLINK_NOFOLLOW), 0);
    want("fileerr 4: AT_NO_AUTOMOUNT: ",
         sys_newfstatat((uint64_t)AT_FDCWD, "/tmp/ferr-f", &st, AT_NO_AUTOMOUNT), 0);
    want("fileerr 4: both: ", sys_newfstatat((uint64_t)AT_FDCWD, "/tmp/ferr-f", &st,
                                             AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT), 0);
    want("fileerr 4: close: ", sys_close(3), 0);
}

static void group_lseek(void)
{
    want("fileerr 5: fd not open: ", sys_lseek(9, 0, SEEK_SET), -EBADF);
    for (uint64_t fd = 0; fd < 3; fd++)
        want("fileerr 5: console fd: ", sys_lseek(fd, 0, SEEK_SET), -ESPIPE);
    want("fileerr 5: open: ", sys_open("/tmp/ferr-f", O_RDONLY, 0), 3);
    want("fileerr 5: whence 3: ", sys_lseek(3, 0, 3), -EINVAL);
    want("fileerr 5: whence 4: ", sys_lseek(3, 0, 4), -EINVAL);
    want("fileerr 5: whence 5: ", sys_lseek(3, 0, 5), -EINVAL);
    want("fileerr 5: whence -1: ", sys_lseek(3, 0, (uint64_t)-1), -EINVAL);
    want("fileerr 5: offset unchanged: ", sys_lseek(3, 0, SEEK_CUR), 0);
    want("fileerr 5: close: ", sys_close(3), 0);
}

static void group_ftruncate(void)
{
    want("fileerr 6: fd not open: ", sys_ftruncate(9, 0), -EBADF);
    want("fileerr 6: open read-only: ", sys_open("/tmp/ferr-f", O_RDONLY, 0), 3);
    want("fileerr 6: read-only fd: ", sys_ftruncate(3, 0), -EINVAL);
    want("fileerr 6: console fd: ", sys_ftruncate(1, 0), -EINVAL);
    want("fileerr 6: open the folder: ", sys_open("/tmp", O_RDONLY | O_DIRECTORY, 0), 4);
    want("fileerr 6: folder fd: ", sys_ftruncate(4, 0), -EINVAL);
    want("fileerr 6: open for writing: ", sys_open("/tmp/ferr-f", O_WRONLY, 0), 5);
    want("fileerr 6: negative length: ", sys_ftruncate(5, -1), -EINVAL);
    want("fileerr 6: past 2 MiB: ", sys_ftruncate(5, (2 << 20) + 1), -EFBIG);
    want("fileerr 6: stat: ", sys_stat("/tmp/ferr-f", &st), 0);
    want("fileerr 6: size unchanged: ", st.st_size, 100);
    for (uint64_t fd = 3; fd < 6; fd++)
        want("fileerr 6: close: ", sys_close(fd), 0);
}

int main(long arg)
{
    switch (arg) {
    case 1: group_open(); break;
    case 2: group_stat(); break;
    case 3: group_fstat(); break;
    case 4: group_dirfd(); break;
    case 5: group_lseek(); break;
    case 6: group_ftruncate(); break;
    default: bad = 99; break;
    }
    return bad > 255 ? 255 : bad;
}
