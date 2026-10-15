/* The file system calls, the pure part (see fsys_core.h). The host unit
 * tests build this file too (tests/unit/test_syscall_fs.c). */
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/proc/fdtable_core.h"

int fsys_open_flags(uint64_t flags, struct fsys_open *o)
{
    uint32_t f = (uint32_t)flags;       /* int flags: the low 32 bits */
    uint32_t acc = f & FS_O_ACCMODE;
    if (acc == FS_O_ACCMODE)
        return -FS_EINVAL;
    o->access = acc == FS_O_WRONLY ? OF_WRITE : acc == FS_O_RDWR ? (OF_READ | OF_WRITE) : OF_READ;
    o->create = (f & FS_O_CREAT) != 0;
    o->excl = (f & FS_O_EXCL) != 0;
    o->trunc = (f & FS_O_TRUNC) != 0;
    o->append = (f & FS_O_APPEND) != 0;
    o->directory = (f & FS_O_DIRECTORY) != 0;
    o->cloexec = (f & FS_O_CLOEXEC) != 0;
    return 0;
}

uint8_t fsys_open_access(const struct fsys_open *o)
{
    return (uint8_t)(o->access | (o->append ? OF_APPEND : 0));
}

int fsys_open_existing(const struct fsys_open *o, enum vnode_type type)
{
    if (type != VNODE_DIR)
        return o->directory ? -FS_ENOTDIR : 0;
    /* A folder: never for writing; O_TRUNC asks for write access too. */
    if ((o->access & OF_WRITE) || o->trunc)
        return -FS_EISDIR;
    return 0;
}

bool fsys_trailing_slash(const char *path)
{
    size_t n = 0;
    while (path[n])
        n++;
    if (n == 0 || path[n - 1] != '/')
        return false;
    for (size_t i = 0; i < n; i++)
        if (path[i] != '/')
            return true;
    return false;                       /* only slashes: the root */
}

bool fsys_absolute(const char *path)
{
    return path[0] == '/';
}

void fsys_stat_fill(struct fs_stat *st, const struct fsys_facts *f)
{
    st->st_dev = FSYS_DEV;
    st->st_ino = f->kind == FSYS_CONSOLE ? 0 : f->ino;
    st->st_uid = 0;
    st->st_gid = 0;
    st->pad0 = 0;
    st->st_blksize = FSYS_BLKSIZE;
    st->st_atime_nsec = st->st_mtime_nsec = st->st_ctime_nsec = 0;
    st->reserved[0] = st->reserved[1] = st->reserved[2] = 0;
    st->st_atime_sec = f->atime;
    st->st_mtime_sec = f->mtime;
    st->st_ctime_sec = f->ctime;
    switch (f->kind) {
    case FSYS_FILE:
        st->st_mode = FS_S_IFREG | (f->mode & 07777u);
        st->st_nlink = f->nlink ? 1 : 0;
        st->st_rdev = 0;
        st->st_size = (int64_t)f->size;
        st->st_blocks = (int64_t)f->pages * (FSYS_BLKSIZE / 512);
        break;
    case FSYS_DIR:
        st->st_mode = FS_S_IFDIR | (f->mode & 07777u);
        st->st_nlink = f->nlink ? 2u + f->subdirs : 0;
        st->st_rdev = 0;
        st->st_size = 0;
        st->st_blocks = 0;
        break;
    default:
        st->st_mode = FS_S_IFCHR | FSYS_CONSOLE_MODE;
        st->st_nlink = 1;
        st->st_rdev = FSYS_CONSOLE_RDEV;
        st->st_size = 0;
        st->st_blocks = 0;
        break;
    }
}

int64_t fsys_lseek(uint64_t cur, uint64_t size, int64_t off, uint64_t whence)
{
    uint64_t base;
    switch ((uint32_t)whence) {         /* unsigned int whence: the low 32 bits */
    case FS_SEEK_SET:
        base = 0;
        break;
    case FS_SEEK_CUR:
        base = cur;
        break;
    case FS_SEEK_END:
        base = size;
        break;
    default:
        return -FS_EINVAL;              /* SEEK_DATA, SEEK_HOLE (a deviation) and others */
    }
    if (base > (uint64_t)INT64_MAX)
        return -FS_EINVAL;
    int64_t b = (int64_t)base;
    if (off > 0 && b > INT64_MAX - off)
        return -FS_EINVAL;              /* past INT64_MAX */
    int64_t r = b + off;
    if (r < 0)
        return -FS_EINVAL;
    return r;
}

int fsys_at_flags(uint64_t flags, bool *empty_path)
{
    uint32_t f = (uint32_t)flags;
    uint32_t known = FS_AT_SYMLINK_NOFOLLOW | FS_AT_NO_AUTOMOUNT | FS_AT_EMPTY_PATH;
    if (f & ~known)
        return -FS_EINVAL;
    *empty_path = (f & FS_AT_EMPTY_PATH) != 0;
    return 0;
}

int64_t fsys_epoch(const struct rtc_time *t)
{
    int64_t days = 0;
    for (uint32_t y = 1970; y < t->year; y++)
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    for (uint8_t m = 1; m < t->month; m++)
        days += rtc_days_in_month(t->year, m);
    days += t->day - 1;
    return days * 86400 + (int64_t)t->hour * 3600 + (int64_t)t->minute * 60 + t->second;
}

uint64_t fsys_page_step(uint64_t addr, uint64_t left)
{
    uint64_t to_end = 4096 - (addr & 4095);
    return left < to_end ? left : to_end;
}

uint32_t fsys_wmark_ticket(struct vnode *n)
{
    uint32_t ticket = n->wr_next++;
    uint32_t ahead = ticket - n->wr_serving;
    if (ahead > n->wr_ahead_max)
        n->wr_ahead_max = ahead;
    return ticket;
}

bool fsys_wmark_try(struct vnode *n, uint32_t ticket, int32_t tid, uint32_t waited)
{
    if (tid < 0 || n->wr_serving != ticket || n->wr_holder != 0)
        return false;
    n->wr_holder = tid + 1;
    n->wr_takes++;
    if (waited)
        n->wr_contended++;
    return true;
}

bool fsys_wmark_release(struct vnode *n, int32_t tid)
{
    if (tid < 0 || n->wr_holder != tid + 1)
        return false;
    n->wr_holder = 0;
    n->wr_serving++;
    return true;
}

int32_t fsys_wmark_holder(const struct vnode *n)
{
    return n->wr_holder - 1;
}
