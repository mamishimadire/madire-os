/* The file system calls, the pure part (see fsys_core.h). The host unit
 * tests build this file too (tests/unit/test_syscall_fs.c, and since stage 6
 * tests/unit/test_syscall_dir.c). */
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/ramfs_core.h"
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

uint16_t fsys_wmark_ticket(struct vnode *n, uint32_t *ahead)
{
    uint16_t ticket = n->wr_next;
    n->wr_next = (uint16_t)(ticket + 1);
    *ahead = (uint16_t)(ticket - n->wr_serving);
    return ticket;
}

bool fsys_wmark_try(struct vnode *n, uint16_t ticket, int32_t tid)
{
    if (tid < 0 || tid >= 0xFFFF || n->wr_serving != ticket || n->wr_holder != 0)
        return false;
    n->wr_holder = (uint16_t)(tid + 1);
    return true;
}

bool fsys_wmark_release(struct vnode *n, int32_t tid)
{
    if (tid < 0 || tid >= 0xFFFF || n->wr_holder != (uint16_t)(tid + 1))
        return false;
    n->wr_holder = 0;
    n->wr_serving = (uint16_t)(n->wr_serving + 1);
    return true;
}

int32_t fsys_wmark_holder(const struct vnode *n)
{
    return (int32_t)n->wr_holder - 1;
}

uint32_t fsys_wmark_waiting(const struct vnode *n)
{
    return (uint16_t)(n->wr_next - n->wr_serving);
}

/* ---- 1.0 stage 6: the folder calls ---- */

int fsys_unlinkat_flags(uint64_t flags, bool *removedir)
{
    uint32_t f = (uint32_t)flags;       /* int flags: the low 32 bits */
    if (f & ~FS_AT_REMOVEDIR)
        return -FS_EINVAL;
    *removedir = (f & FS_AT_REMOVEDIR) != 0;
    return 0;
}

uint32_t fsys_mkdir_mode(uint64_t mode)
{
    return (uint32_t)(mode & 01777u);
}

uint32_t fsys_dirent_reclen(size_t len)
{
    return (uint32_t)((FSYS_DIRENT_HEAD + len + 1 + 7) & ~(size_t)7);
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

uint32_t fsys_dirent_put(uint8_t *buf, uint32_t room, uint64_t ino, uint64_t off, uint8_t type,
                         const char *name, size_t len)
{
    uint32_t rl = fsys_dirent_reclen(len);
    if (rl > room)
        return 0;
    put64(buf, ino);
    put64(buf + 8, off);
    buf[16] = (uint8_t)(rl & 0xFF);
    buf[17] = (uint8_t)(rl >> 8);
    buf[18] = type;
    for (size_t i = 0; i < len; i++)
        buf[FSYS_DIRENT_HEAD + i] = (uint8_t)name[i];
    for (uint32_t i = FSYS_DIRENT_HEAD + (uint32_t)len; i < rl; i++)
        buf[i] = 0;                     /* the NUL and the padding */
    return rl;
}

uint32_t fsys_dir_fill(const struct vnode *dir, uint64_t cursor, uint8_t *buf, uint32_t room,
                       uint64_t *next, bool *more)
{
    uint32_t used = 0;
    const struct vnode *e = NULL;
    bool looked = false;
    *next = cursor;
    *more = false;
    for (;;) {
        uint64_t ino, off;
        uint8_t type = FS_DT_DIR;
        const char *name;
        size_t len;
        if (*next == 0) {
            ino = dir->ino;
            off = 1;
            name = ".";
            len = 1;
        } else if (*next == 1) {
            ino = dir->parent ? dir->parent->ino : dir->ino;
            off = 2;
            name = "..";
            len = 2;
        } else {
            if (!looked) {
                e = ramfs_entry_from(dir, *next - 2);
                looked = true;
            }
            if (!e)
                return used;            /* the end of the folder */
            ino = e->ino;
            off = (uint64_t)ramfs_seq(e) + 3;
            type = e->type == VNODE_DIR ? FS_DT_DIR : FS_DT_REG;
            name = ramfs_name(e, &len);
        }
        uint32_t r = fsys_dirent_put(buf + used, room - used, ino, off, type, name, len);
        if (r == 0) {
            *more = true;               /* the next record does not fit */
            return used;
        }
        used += r;
        *next = off;
        if (e)
            e = ramfs_next(e);          /* entries are in stamp order */
    }
}

uint64_t fsys_dir_keep(const uint8_t *buf, uint32_t used, uint32_t valid, uint64_t cursor,
                       uint32_t *keep)
{
    uint32_t pos = 0;
    uint64_t cur = cursor;
    *keep = 0;
    while (pos + FSYS_DIRENT_HEAD <= used) {
        uint32_t rl = (uint32_t)buf[pos + 16] | ((uint32_t)buf[pos + 17] << 8);
        if (rl == 0 || pos + rl > used || pos + rl > valid)
            break;
        cur = get64(buf + pos + 8);
        pos += rl;
        *keep = pos;
    }
    return cur;
}

int64_t fsys_cwd_path(const struct vnode *cwd, const struct vnode *root, char *buf, size_t size)
{
    if (cwd->nlink == 0)
        return -FS_ENOENT;              /* removed: no path names it */
    if (size < 2)
        return -FS_ENAMETOOLONG;
    if (cwd == root) {
        buf[0] = '/';
        buf[1] = '\0';
        return 2;
    }
    /* Built from the end of buf backwards, one name per folder up to the
     * root, then moved to its start. */
    size_t pos = size;
    buf[--pos] = '\0';
    for (const struct vnode *n = cwd; n != root; n = n->parent) {
        if (!n->parent)
            return -FS_ENOENT;          /* cannot happen: only an empty folder is removed */
        size_t len;
        const char *name = ramfs_name(n, &len);
        if (pos < len + 1)
            return -FS_ENAMETOOLONG;
        pos -= len;
        for (size_t i = 0; i < len; i++)
            buf[pos + i] = name[i];
        buf[--pos] = '/';
    }
    size_t total = size - pos;
    for (size_t i = 0; i < total; i++)
        buf[i] = buf[pos + i];
    return (int64_t)total;
}
