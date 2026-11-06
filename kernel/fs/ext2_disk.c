/* The disk's ext2 volume at /home (1.4 stage 8): see ext2_disk.h. Read
 * only. Every static is ext2_d_-prefixed. */
#include "kernel/fs/ext2_disk.h"
#include "kernel/fs/ext2_core.h"
#include "kernel/fs/ext2_vfs.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/mount_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/klock.h"
#include "kernel/storage/blk.h"
#include "kernel/timer.h"

typedef unsigned long long ull;

#define EXT2_D_SECTOR   512u
#define EXT2_D_BUF      8192u           /* bytes per blk_read at most: 16 sectors */

static struct ext2_fs ext2_d_fs;        /* the core's state and its block buffers (about 25 KiB) */
static struct ext2_vfs ext2_d_vfs;
static uint64_t ext2_d_first;           /* the partition's first sector */
static uint64_t ext2_d_bytes;           /* and its size in bytes */
static uint8_t ext2_d_buf[EXT2_D_BUF];
static uint32_t ext2_d_reads, ext2_d_failed;
static uint64_t ext2_d_per_tick;        /* TSC cycles per tick, calibrated after the mount */

uint64_t ext2_disk_us(uint64_t cycles)
{
    return fd_cycles_us(cycles, ext2_d_per_tick);
}

/* The core's reads: bytes [off, off + len) of the partition through the
 * block cache, at most EXT2_D_BUF bytes per blk_read. 0, or -1 (beyond the
 * partition, or a sector that could not be read: the ahci: line says why). */
static int ext2_d_read(void *ctx, uint64_t off, uint32_t len, uint8_t *buf)
{
    (void)ctx;
    ext2_d_reads++;
    if (off > ext2_d_bytes || len > ext2_d_bytes - off) {
        ext2_d_failed++;
        return -1;
    }
    while (len) {
        uint32_t within = (uint32_t)(off % EXT2_D_SECTOR);
        uint32_t n = len < EXT2_D_BUF - within ? len : EXT2_D_BUF - within;
        uint32_t sectors = (within + n + EXT2_D_SECTOR - 1u) / EXT2_D_SECTOR;
        if (blk_read(ext2_d_first + off / EXT2_D_SECTOR, sectors, ext2_d_buf) != 0) {
            ext2_d_failed++;
            return -1;
        }
        for (uint32_t i = 0; i < n; i++)
            buf[i] = ext2_d_buf[within + i];
        buf += n;
        off += n;
        len -= n;
    }
    return 0;
}

static void *ext2_d_heap(size_t bytes)
{
    return bytes > 0xFFFFFFFFu ? 0 : kmalloc((uint32_t)bytes);
}

/* The first time the node table is full (under the VFS lock: one line,
 * as the AHCI driver logs a disk error from under it). */
static void ext2_d_full(uint32_t max_nodes)
{
    klogf("ext2: the node table is full (%u nodes): further files and folders under /home answer ENOMEM "
          "until the volume is unmounted (eviction comes in stage 10)\n", max_nodes);
}

static const struct ext2_vfs_mem ext2_d_mem = { ext2_d_heap, kfree, ext2_d_full };

/* What the mount found, logged after the lock is given back. */
enum ext2_d_end {
    EXT2_D_MOUNTED,
    EXT2_D_REFUSED,                     /* ext2_mount said no: ext2_d_err */
    EXT2_D_NO_ROOT,                     /* the root folder's node could not be made */
    EXT2_D_NO_HOME,                     /* no RAM /home folder to mount on */
    EXT2_D_NO_MOUNT,                    /* mount_add said no: ext2_d_err */
};

bool ext2_disk_mount(uint32_t port, uint32_t partition, uint64_t first, uint64_t sectors, char *tail,
                     size_t tail_size)
{
    struct vfs *v = vfs_root();
    const struct ext2_ops ops = { ext2_d_read, 0 };
    enum ext2_d_end end = EXT2_D_MOUNTED;
    int err = 0, id = -1;
    uint64_t ram_max = 0, disk_max = 0;
    ext2_d_first = first;
    ext2_d_bytes = sectors * EXT2_D_SECTOR;
    if (!v->root) {                     /* no filesystem booted (the kernel heap is off) */
        klogf("ext2: port %u partition %u: not mounted: the filesystem did not start\n", port, partition);
        ksnprintf(tail, tail_size, "; read only, not mounted: no filesystem");
        return false;
    }

    klock_acquire(vfs_lock());
    uint64_t c0 = fd_tsc(), t0 = timer_ticks();
    err = ext2_mount(&ext2_d_fs, &ops, ext2_d_bytes);
    if (err != EXT2_OK) {
        end = EXT2_D_REFUSED;
    } else if (ext2_vfs_init(&ext2_d_vfs, &ext2_d_fs, &ext2_d_mem, EXT2_VFS_NODES) != 0) {
        end = EXT2_D_NO_ROOT;
    } else {
        struct vnode *home = 0;
        if (vfs_lookup(v, v->root, "/home", &home) != 0 || home->type != VNODE_DIR || home->fs_id != 0) {
            end = EXT2_D_NO_HOME;
        } else {
            id = mount_add(&v->mounts, &ext2_vfs_ops, &ext2_d_vfs, &ext2_d_vfs.root->vn, home);
            if (id < 0) {
                err = id;
                end = EXT2_D_NO_MOUNT;
            } else {
                ram_max = vfs_file_max(v, home);
                disk_max = vfs_file_max(v, &ext2_d_vfs.root->vn);
            }
        }
    }
    uint64_t hold_cycles = fd_tsc() - c0, hold_ticks = timer_ticks() - t0;
    klock_release(vfs_lock());

    const struct ext2_fs *fs = &ext2_d_fs;
    if (end == EXT2_D_REFUSED) {
        const char *why = fs->why ? fs->why : ext2_err_str(err);
        const char *brief = err == EXT2_EINCOMPAT ? "unknown INCOMPAT flag"
                          : err == EXT2_EIO ? "the disk could not be read"
                          : "not a usable ext2 volume";
        if (err == EXT2_EINCOMPAT)
            klogf("ext2: port %u partition %u: not mounted: %s: %s (INCOMPAT 0x%x, unknown 0x%x)\n", port,
                  partition, ext2_err_str(err), why, fs->incompat, fs->incompat_unknown);
        else
            klogf("ext2: port %u partition %u: not mounted: %s: %s\n", port, partition, ext2_err_str(err), why);
        ksnprintf(tail, tail_size, "; read only, not mounted: %s", brief);
        return false;
    }
    klogf("ext2: port %u partition %u: revision %u, %u-byte blocks, %u blocks (%u free), %u inodes (%u free), "
          "%u groups, %u-byte inodes, label '%s'; features COMPAT 0x%x, INCOMPAT 0x%x, RO_COMPAT 0x%x; state "
          "0x%x (%s, error bit %s)\n", port, partition, fs->rev, fs->block_size, fs->blocks_count,
          fs->free_blocks, fs->inodes_count, fs->free_inodes, fs->groups, fs->inode_size, fs->label, fs->compat,
          fs->incompat, fs->ro_compat, (unsigned)fs->state, fs->state_clean ? "clean" : "not clean",
          fs->state_error ? "set" : "not set");
    if (end != EXT2_D_MOUNTED) {
        const char *brief = end == EXT2_D_NO_ROOT ? "its root folder could not be read"
                          : end == EXT2_D_NO_HOME ? "no RAM folder /home" : "the mount table refused it";
        klogf("ext2: port %u partition %u: not mounted: %s (%s)\n", port, partition, brief,
              end == EXT2_D_NO_ROOT ? (ext2_d_vfs.why ? ext2_d_vfs.why : "out of kernel memory")
              : end == EXT2_D_NO_MOUNT ? fs_errno_name(err) : "lookup");
        ksnprintf(tail, tail_size, "; read only, not mounted: %s", brief);
        return false;
    }
    /* The D11 and D12 lines: each reason the volume would be read-only even
     * with a write path, and an unknown COMPAT flag. */
    if (fs->compat_unknown)
        klogf("ext2: unknown COMPAT flags 0x%x: reading allowed (logged; the rule for writes comes in stage 10)\n",
              fs->compat_unknown);
    if (fs->ro_compat_unknown)
        klogf("ext2: unknown RO_COMPAT flags 0x%x: mounted read-only (D11)\n", fs->ro_compat_unknown);
    if (fs->state_error)
        klogf("ext2: the error bit is set (state 0x%x): mounted read-only (C7)\n", (unsigned)fs->state);
    else if (!fs->state_clean)
        klogf("ext2: warning: the volume was not cleanly unmounted (state 0x%x); mounted read-only in stage 8 "
              "(the recount of free blocks and inodes from the bitmaps comes with read-write in stage 10, D12)\n",
              (unsigned)fs->state);
    const char *ro = fs->ro_compat_unknown ? "an unknown RO_COMPAT flag"
                   : fs->state_error ? "the error bit is set"
                   : !fs->state_clean ? "not cleanly unmounted" : "stage 8 has no write path";
    klogf("mount: ext2 '%s' (port %u, partition %u) on /home as filesystem %d, read-only (%s); the RAM folder "
          "/home is under it, / and /tmp stay in memory; %u disk reads, %u failed\n", fs->label, port, partition, id,
          ro, ext2_d_reads, ext2_d_failed);
    uint64_t spent = 0;
    ext2_d_per_tick = fd_tsc_per_tick(&spent);
    klogf("mount: the mount held the VFS lock %llu us (%llu ticks) (report only; TSC %llu cycles per tick)\n",
          (ull)ext2_disk_us(hold_cycles), (ull)hold_ticks, (ull)ext2_d_per_tick);
    klogf("mount: file-size limits from each filesystem's ops table: / %llu bytes, /home %llu bytes\n",
          (ull)ram_max, (ull)disk_max);
    ksnprintf(tail, tail_size, "; ext2 '%s' mounted read-only on /home", fs->label);
    return true;
}
