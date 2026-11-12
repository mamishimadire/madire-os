/* The writer of ext2_vfs.h over the write core (1.4 stage 10): see
 * ext2_vfs_rw.h. Pure. Every static is ext2_rw_-prefixed. */
#include "kernel/fs/ext2_vfs_rw.h"
#include "kernel/fs/fs_errno.h"

int ext2_vfs_rw_errno(int e)
{
    switch (e) {
    case EXT2_OK:           return 0;
    case EXT2_EAGAIN:       return -FS_EAGAIN;
    case EXT2_EEXIST:       return -FS_EEXIST;
    case EXT2_ENOSPC:       return -FS_ENOSPC;
    case EXT2_EROFS:        return -FS_EROFS;
    case EXT2_ENOTEMPTY:    return -FS_ENOTEMPTY;
    case EXT2_EISDIR:       return -FS_EISDIR;
    case EXT2_EINVAL:       return -FS_EINVAL;
    case EXT2_EFBIG:        return -FS_EFBIG;
    case EXT2_EMLINK:       return -FS_EMLINK;
    case EXT2_ENAMETOOLONG: return -FS_ENAMETOOLONG;
    case EXT2_ENOTDIR:      return -FS_ENOTDIR;
    case EXT2_ENOENT:       return -FS_ENOENT;
    default:                return -FS_EIO;     /* EIO, a corrupt number, a bad inode */
    }
}

static struct ext2_wfs *ext2_rw_of(void *ctx)
{
    return ctx;
}

static int ext2_rw_writable(void *ctx)
{
    struct ext2_wfs *w = ext2_rw_of(ctx);
    return w->rw && !w->failed ? 0 : -FS_EROFS;
}

static int ext2_rw_create(void *ctx, uint32_t dir, const char *name, uint32_t len, bool folder, uint32_t mode,
                          uint32_t *ino)
{
    struct ext2_wfs *w = ext2_rw_of(ctx);
    int r = folder ? ext2_write_mkdir(w, dir, name, len, mode, ino) : ext2_write_create(w, dir, name, len, mode, ino);
    return ext2_vfs_rw_errno(r);
}

static int ext2_rw_remove(void *ctx, uint32_t dir, const char *name, uint32_t len, bool folder, uint32_t keep_ino)
{
    struct ext2_wfs *w = ext2_rw_of(ctx);
    w->keep_ino = keep_ino;
    w->kept = false;
    int r = folder ? ext2_write_rmdir(w, dir, name, len) : ext2_write_unlink(w, dir, name, len);
    w->keep_ino = 0;
    return ext2_vfs_rw_errno(r);
}

static int ext2_rw_rename(void *ctx, uint32_t odir, const char *oname, uint32_t olen, uint32_t ndir,
                          const char *nname, uint32_t nlen, uint32_t keep_ino)
{
    struct ext2_wfs *w = ext2_rw_of(ctx);
    w->keep_ino = keep_ino;
    w->kept = false;
    int r = ext2_write_rename(w, odir, oname, olen, ndir, nname, nlen);
    w->keep_ino = 0;
    return ext2_vfs_rw_errno(r);
}

static int64_t ext2_rw_write(void *ctx, uint32_t ino, uint64_t off, const uint8_t *buf, uint32_t len)
{
    int64_t r = ext2_write_pwrite(ext2_rw_of(ctx), ino, off, buf, len);
    return r < 0 ? ext2_vfs_rw_errno((int)r) : r;
}

static int ext2_rw_truncate(void *ctx, uint32_t ino, uint64_t size)
{
    return ext2_vfs_rw_errno(ext2_write_truncate(ext2_rw_of(ctx), ino, size));
}

static void ext2_rw_forget(void *ctx, uint32_t ino)
{
    ext2_write_forget(ext2_rw_of(ctx), ino);
}

void ext2_vfs_rw_init(struct ext2_vfs_writer *wr, struct ext2_wfs *w)
{
    wr->writable = ext2_rw_writable;
    wr->create = ext2_rw_create;
    wr->remove = ext2_rw_remove;
    wr->rename = ext2_rw_rename;
    wr->write = ext2_rw_write;
    wr->truncate = ext2_rw_truncate;
    wr->forget = ext2_rw_forget;
    wr->ctx = w;
}
