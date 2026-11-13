/* The disk's ext2 volume at /home (1.4 stage 8; read-write since stage 10):
 * see ext2_disk.h. Every static is ext2_d_-prefixed. */
#include "kernel/fs/ext2_disk.h"
#include "kernel/fs/ext2_core.h"
#include "kernel/fs/ext2_vfs.h"
#include "kernel/fs/ext2_vfs_rw.h"
#include "kernel/fs/ext2_write.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/mount_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/panic.h"
#include "kernel/proc/disklock.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/thread.h"
#include "kernel/storage/ahci.h"
#include "kernel/storage/blk.h"
#include "kernel/storage/blk_core.h"
#include "kernel/storage/blk_core_save.h"
#include "kernel/timer.h"

typedef unsigned long long ull;

#define EXT2_D_SECTOR   512u
#define EXT2_D_BUF      8192u           /* bytes per blk_read at most: 16 sectors */
#define EXT2_D_PIECE    8u              /* disk commands per piece (C5): since 1.5 S4 a hold of the disk lock */
#define EXT2_D_SAVES    8u              /* saves in one full save at most */
#define EXT2_D_MIN_PAGES 128u           /* the store's pages at least (512 chunks) */
#define EXT2_D_AUTOSAVE (5u * TIMER_HZ)
#define EXT2_D_RESTART  (10u * TIMER_HZ)

/* 1.5 stage 5 (docs/plans/1.5.md, C2): the write tables' area at
 * VM_EXT2_WT_BASE: the write core's tables, then the store's entries (one
 * per 1 KiB chunk the store may have). The read core's buffers (struct
 * ext2_bufs) are VM_EXT2_CORE_BASE's. Neither is in .bss any more. */
struct ext2_d_wtab {
    struct ext2_wbufs wb;
    struct blk_sv_ent ents[VM_SV_PAGES * 4u];
};
#define EXT2_D_CORE_PAGES ((uint32_t)((sizeof(struct ext2_bufs) + VM_PAGE_SIZE - 1u) / VM_PAGE_SIZE))
#define EXT2_D_WT_PAGES   ((uint32_t)((sizeof(struct ext2_d_wtab) + VM_PAGE_SIZE - 1u) / VM_PAGE_SIZE))
_Static_assert(EXT2_D_CORE_PAGES <= VM_EXT2_CORE_PAGES, "the read core's buffers fit VM_EXT2_CORE_BASE's area");
_Static_assert(EXT2_D_WT_PAGES <= VM_EXT2_WT_PAGES, "the write tables fit VM_EXT2_WT_BASE's area");

static struct ext2_wfs ext2_d_w;        /* the write core, with the read core (buffers: C2's pages) */
static struct ext2_vfs ext2_d_vfs;
static struct ext2_vfs_writer ext2_d_wr;
static struct blk_sv ext2_d_sv;
static uint32_t ext2_d_core_n, ext2_d_wt_n; /* 1.5 stage 5: each area's pages mapped (0: none) */
static uint32_t ext2_d_run_phys[VM_SV_RUN_PAGES];
static uint32_t ext2_d_store_n;         /* the store's pages mapped (0: no read-write mount) */
static uint64_t ext2_d_first;           /* the partition's first sector */
static uint64_t ext2_d_bytes;           /* and its size in bytes */
static uint8_t ext2_d_buf[EXT2_D_BUF];
static uint32_t ext2_d_reads, ext2_d_failed;
static uint64_t ext2_d_per_tick;        /* TSC cycles per tick, calibrated after the mount */
static uint32_t ext2_d_flags;           /* EXT2_DISK_* (kernel.c, before the mount) */
static bool ext2_d_on;                  /* a volume is mounted on /home */
static bool ext2_d_dbg_read;            /* test=blkdebug: the read error injected (once) */
static int32_t ext2_d_saver = -1;       /* the automatic save's thread */
static struct ext2_disk_figs ext2_d_figs;
static bool ext2_d_cut_log, ext2_d_cut_on;  /* test=diskcut: writes logged; the cut armed */
static uint32_t ext2_d_cut_after;       /* the cut after this write */
static uint32_t ext2_d_cut_n;           /* writes sent so far */
static uint32_t ext2_d_port, ext2_d_part;   /* stage 12: the mounted volume's disk port and partition */

uint64_t ext2_disk_us(uint64_t cycles)
{
    return fd_cycles_us(cycles, ext2_d_per_tick);
}

void ext2_disk_options(uint32_t flags)
{
    ext2_d_flags = flags;
}

void ext2_disk_cut(bool cut, uint32_t after)
{
    ext2_d_cut_log = true;
    ext2_d_cut_on = cut;
    ext2_d_cut_after = after;
}

/* test=diskcut=N: the power cut. No disk command follows; the CPU stops
 * with interrupts off (the test kills QEMU and checks the image). */
static void ext2_d_cut_now(void)
{
    klogf("blk: test=diskcut=%u: the power cut after write %u: no more disk commands; halted\n", ext2_d_cut_after,
          ext2_d_cut_n);
    for (;;)
        __asm__ volatile ("cli; hlt");
}

/* ---- the block layer's three calls: reads, writes, flushes ---- */

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

/* One write command of the save (or the error state's superblock): its
 * bytes go to the run's pages (DMA), then WRITE DMA EXT through blk_write,
 * which the AHCI driver refuses unless the guard allows. 0 or -1. */
static int ext2_d_write(void *ctx, uint64_t off, uint32_t len, const uint8_t *buf, uint32_t cls)
{
    (void)ctx;
    uint8_t *run = (uint8_t *)(uintptr_t)VM_SV_RUN_BASE;
    if (!ext2_d_store_n || off % EXT2_D_SECTOR || len % EXT2_D_SECTOR || len == 0 ||
        len > VM_SV_RUN_PAGES * VM_PAGE_SIZE || off > ext2_d_bytes || len > ext2_d_bytes - off)
        return -1;
    if (ext2_d_cut_on && ext2_d_cut_n >= ext2_d_cut_after)
        ext2_d_cut_now();               /* test=diskcut=0: before the first write */
    if (buf != run)
        for (uint32_t i = 0; i < len; i++)
            run[i] = buf[i];
    uint32_t pages = (len + VM_PAGE_SIZE - 1u) / VM_PAGE_SIZE;
    uint64_t c0 = fd_tsc();
    int r = blk_write(ext2_d_first + off / EXT2_D_SECTOR, len / EXT2_D_SECTOR, ext2_d_run_phys, pages);
    uint64_t c = fd_tsc() - c0;
    ext2_d_figs.writes++;
    ext2_d_figs.write_bytes += len;
    ext2_d_figs.write_cycles += c;
    if (r)
        return -1;
    if (ext2_d_cut_log) {
        ext2_d_cut_n++;
        klogf("blk: write %u class %u: partition byte %llu, %u bytes\n", ext2_d_cut_n, cls, (ull)off, len);
        if (ext2_d_cut_on && ext2_d_cut_n == ext2_d_cut_after)
            ext2_d_cut_now();
    }
    return 0;
}

static int ext2_d_flush(void *ctx)
{
    (void)ctx;
    uint64_t c0 = fd_tsc();
    int r = blk_flush();
    uint64_t c = fd_tsc() - c0;
    ext2_d_figs.flushes++;
    ext2_d_figs.flush_cycles += c;
    if (c > ext2_d_figs.flush_longest)
        ext2_d_figs.flush_longest = c;
    return r ? -1 : 0;
}

static const struct blk_sv_ops ext2_d_sv_ops = { ext2_d_read, ext2_d_write, ext2_d_flush, 0 };

/* ---- memory ---- */

static void *ext2_d_heap(size_t bytes)
{
    return bytes > 0xFFFFFFFFu ? 0 : kmalloc((uint32_t)bytes);
}

/* The first time the node table is full (under the VFS lock: one line,
 * as the AHCI driver logs a disk error from under it). */
static void ext2_d_full(uint32_t max_nodes)
{
    klogf("ext2: the node table is full (%u nodes) and none can be freed (each is open, a current folder, the "
          "folder of another or in use now): further files and folders under /home answer ENOMEM until some are "
          "closed\n", max_nodes);
}

/* test=blkdebug: the first disk read command during a file's data read
 * fails once (the driver's error path runs; cat prints its I/O error). */
static void ext2_d_reading(bool on)
{
    if (!(ext2_d_flags & EXT2_DISK_BLKDEBUG) || ext2_d_dbg_read)
        return;
    if (on) {
        ahci_debug_arm(true, true);
        return;
    }
    ext2_d_dbg_read = !ahci_debug_read_pending();   /* used by a real read command */
    ahci_debug_arm(false, true);
}

static const struct ext2_vfs_mem ext2_d_mem = { ext2_d_heap, kfree, ext2_d_full, ext2_d_reading };

static uint32_t ext2_d_now(void *ctx)
{
    (void)ctx;
    int64_t t = vfs_now();
    return t > 0 ? (uint32_t)t : 0u;
}

static const struct ext2_w_ops ext2_d_wops = { ext2_d_now, 0, 0 };

/* The store of unsaved changes (`pages` pages from VM_SV_BASE) and the
 * run's pages, frames of free RAM mapped write-back; false (nothing kept)
 * if memory runs out. */
static bool ext2_d_store(uint32_t pages)
{
    uint32_t i = 0, k = 0;
    for (; i < pages; i++) {
        uint32_t frame = memory_alloc_page();
        if (!frame)
            break;
        if (paging_map(VM_SV_BASE + (uint64_t)i * VM_PAGE_SIZE, frame, PTE_WRITE | paging_nx_flag()) != VMM_OK) {
            memory_free_page(frame);
            break;
        }
    }
    for (; i == pages && k < VM_SV_RUN_PAGES; k++) {
        uint32_t frame = memory_alloc_page();
        if (!frame)
            break;
        if (paging_map(VM_SV_RUN_BASE + (uint64_t)k * VM_PAGE_SIZE, frame, PTE_WRITE | paging_nx_flag()) != VMM_OK) {
            memory_free_page(frame);
            break;
        }
        ext2_d_run_phys[k] = frame;
    }
    if (i == pages && k == VM_SV_RUN_PAGES) {
        ext2_d_store_n = pages;
        return true;
    }
    while (k) {                         /* give everything back */
        uint64_t got = 0;
        k--;
        if (paging_unmap(VM_SV_RUN_BASE + (uint64_t)k * VM_PAGE_SIZE, &got) == VMM_OK)
            memory_free_page((uint32_t)got);
    }
    while (i) {
        uint64_t got = 0;
        i--;
        if (paging_unmap(VM_SV_BASE + (uint64_t)i * VM_PAGE_SIZE, &got) == VMM_OK)
            memory_free_page((uint32_t)got);
    }
    return false;
}

/* The store and the run's pages given back (a mount that ended refused or
 * read-only never puts a change).
 * 1.5 stage 6, the D9 fix (docs/testing.md; present since 1.4): when save 0
 * or the recount failed at the mount (D9), chunks can still be dirty here
 * (sv.n > 0: save 0's superblock, the recount's descriptors). Before the
 * pages go, the store forgets them: no entry, no save in progress. So a
 * later read (blk_sv_read finds no dirty chunk) reads the disk's bytes, the
 * volume as it really is: the chunks a failed save wrote before it failed
 * are there, the rest never reached the disk; the volume is read-only from
 * then on (nothing will ever be saved), and the in-memory figures it keeps
 * (the recounted free counts, the superblock's state) are only shown, never
 * written. Before this fix a read searched entries in unmapped pages (since
 * 1.5 stage 5 a null pointer; in 1.4 the bytes of an unmapped chunk) and
 * faulted. The number dropped is logged after the mount (ext2_d_dropped). */
static uint32_t ext2_d_dropped;

static void ext2_d_unstore(void)
{
    ext2_d_dropped = ext2_d_sv.n;
    ext2_d_sv.n = 0;
    ext2_d_sv.saving = false;
    ext2_d_sv.cur_i = 0;
    ext2_d_sv.cur_wrote = false;
    for (uint32_t i = 0; ext2_d_store_n && i < ext2_d_store_n; i++) {
        uint64_t got = 0;
        if (paging_unmap(VM_SV_BASE + (uint64_t)i * VM_PAGE_SIZE, &got) == VMM_OK)
            memory_free_page((uint32_t)got);
    }
    for (uint32_t k = 0; ext2_d_store_n && k < VM_SV_RUN_PAGES; k++) {
        uint64_t got = 0;
        if (paging_unmap(VM_SV_RUN_BASE + (uint64_t)k * VM_PAGE_SIZE, &got) == VMM_OK)
            memory_free_page((uint32_t)got);
        ext2_d_run_phys[k] = 0;
    }
    ext2_d_store_n = 0;
    ext2_d_sv.data = 0;
    ext2_d_sv.run = 0;
    ext2_d_sv.cap = 0;
}

uint32_t ext2_disk_store_pages(void)
{
    return ext2_d_store_n ? ext2_d_store_n + VM_SV_RUN_PAGES : 0;
}

/* 1.5 stage 5 (C2): the first `pages` pages of an area from `base`
 * unmapped and their frames given back. */
static void ext2_d_unarea(uint64_t base, uint32_t pages)
{
    while (pages) {
        uint64_t got = 0;
        pages--;
        if (paging_unmap(base + (uint64_t)pages * VM_PAGE_SIZE, &got) == VMM_OK)
            memory_free_page((uint32_t)got);
    }
}

/* 1.5 stage 5 (C2): `pages` frames of free RAM mapped write-back from
 * `base`, each ZEROED through its new mapping before anything uses it (the
 * .bss these tables replace was zero-filled): true, or false with every
 * page given back (nothing kept, nothing left mapped). `fail_last`
 * (test=diskrmem, test=diskwmem): the last page's allocation fails on
 * purpose, after the others were mapped, so the give-back runs. */
static bool ext2_d_area(uint64_t base, uint32_t pages, bool fail_last)
{
    uint32_t i = 0;
    for (; i < pages; i++) {
        uint32_t frame = (fail_last && i + 1u == pages) ? 0u : memory_alloc_page();
        if (!frame)
            break;
        if (paging_map(base + (uint64_t)i * VM_PAGE_SIZE, frame, PTE_WRITE | paging_nx_flag()) != VMM_OK) {
            memory_free_page(frame);
            break;
        }
        uint64_t *p = (uint64_t *)(uintptr_t)(base + (uint64_t)i * VM_PAGE_SIZE);
        for (uint32_t k = 0; k < VM_PAGE_SIZE / 8u; k++)
            p[k] = 0;
    }
    if (i == pages)
        return true;
    ext2_d_unarea(base, i);
    return false;
}

/* 1.5 stage 5 (C2): the write tables given back (a mount that ended
 * refused or read-only never writes), and with `core` the read core's
 * buffers too (a refused mount). The cores' pointers are cleared first, so
 * no pointer into an unmapped page is left (a later use faults at a low
 * address instead of touching a reused frame). */
static void ext2_d_untables(bool core)
{
    ext2_write_init(&ext2_d_w, core ? 0 : ext2_d_w.fs.bufs, 0);
    ext2_d_sv.ents = 0;
    ext2_d_unarea(VM_EXT2_WT_BASE, ext2_d_wt_n);
    ext2_d_wt_n = 0;
    if (core) {
        ext2_d_unarea(VM_EXT2_CORE_BASE, ext2_d_core_n);
        ext2_d_core_n = 0;
    }
}

/* ---- the mount ---- */

/* What the mount found, logged after the lock is given back. */
enum ext2_d_end {
    EXT2_D_MOUNTED,
    EXT2_D_REFUSED,                     /* ext2_mount said no: ext2_d_err */
    EXT2_D_NO_ROOT,                     /* the root folder's node could not be made */
    EXT2_D_NO_HOME,                     /* no RAM /home folder to mount on */
    EXT2_D_NO_MOUNT,                    /* mount_add said no: ext2_d_err */
};

static int32_t ext2_d_autosave(void *arg);

bool ext2_disk_mount(uint32_t port, uint32_t partition, uint64_t first, uint64_t sectors, char *tail,
                     size_t tail_size)
{
    struct vfs *v = vfs_root();
    enum ext2_d_end end = EXT2_D_MOUNTED;
    int err = 0, id = -1;
    uint64_t ram_max = 0, disk_max = 0;
    ext2_d_port = port;
    ext2_d_part = partition;
    ext2_d_first = first;
    ext2_d_bytes = sectors * EXT2_D_SECTOR;
    if (!v->root) {                     /* no filesystem booted (the kernel heap is off) */
        klogf("ext2: port %u partition %u: not mounted: the filesystem did not start\n", port, partition);
        ksnprintf(tail, tail_size, "; read only, not mounted: no filesystem");
        return false;
    }
    /* Stage 10: the store of unsaved changes, unless test=diskro: as many
     * pages as the cache may have (2 MiB or 1/16 of the free pages). */
    const char *ro_mine = 0;
    uint32_t pages = blk_cache_pages(pmm_free_pages());
    if (pages > VM_SV_PAGES)
        pages = VM_SV_PAGES;
    /* 1.5 stage 5 (C2): the read core's buffers first, in pages mapped now
     * (no longer .bss): without them the disk is not mounted (Live mode;
     * every page already taken given back). Then, unless test=diskro, the
     * write tables: without them the volume mounts read-only. */
    if (!ext2_d_area(VM_EXT2_CORE_BASE, EXT2_D_CORE_PAGES, (ext2_d_flags & EXT2_DISK_RMEM) != 0)) {
        klogf("ext2: too little memory for the read core: not mounted (Live mode)\n");
        ksnprintf(tail, tail_size, "; not mounted: too little memory");
        return false;
    }
    ext2_d_core_n = EXT2_D_CORE_PAGES;
    bool wt_short = false;
    struct ext2_d_wtab *wt = (struct ext2_d_wtab *)(uintptr_t)VM_EXT2_WT_BASE;
    if (ext2_d_flags & EXT2_DISK_RO) {
        ro_mine = "test=diskro";
    } else if (!ext2_d_area(VM_EXT2_WT_BASE, EXT2_D_WT_PAGES, (ext2_d_flags & EXT2_DISK_WMEM) != 0)) {
        ro_mine = "too little memory for the write tables";
        wt_short = true;
    } else {
        ext2_d_wt_n = EXT2_D_WT_PAGES;
        if (pages < EXT2_D_MIN_PAGES || !ext2_d_store(pages))
            ro_mine = "too little memory to keep changes";
    }
    ext2_write_init(&ext2_d_w, (struct ext2_bufs *)(uintptr_t)VM_EXT2_CORE_BASE, ro_mine ? 0 : &wt->wb);
    blk_sv_init(&ext2_d_sv, &ext2_d_sv_ops, ro_mine ? 0 : wt->ents, ro_mine ? 0 : (uint8_t *)(uintptr_t)VM_SV_BASE,
                ro_mine ? 0u : ext2_d_store_n * 4u, ro_mine ? 0 : (uint8_t *)(uintptr_t)VM_SV_RUN_BASE,
                ext2_d_bytes);

    /* 1.5 stage 4 (C1): Disk mode from here (thread 0; no other thread
     * makes a file call yet): the disk lock first. The mount's reads and
     * save 0 (inside ext2_write_mount; nothing else can reach the volume
     * yet) run under the disk lock only; then the VFS lock for the
     * adapter's root and the mount table (L1: disk lock, then VFS lock).
     * A mount that ends refused leaves Live mode: the lock off again. */
    disklock_start();
    disklock_acquire();
    /* 1.5 stage 6, test=disksavefail: every disk write of the mount fails
     * (the AHCI driver's injected error, as test=blkdebug's), so save 0
     * fails (D9) and the volume mounts read-only with dirty chunks left in
     * the store: the D9 fix's path (ext2_d_unstore). */
    bool savefail = (ext2_d_flags & EXT2_DISK_SAVEFAIL) != 0 && ro_mine == 0;
    if (savefail)
        ahci_debug_arm(false, true);
    err = ext2_write_mount(&ext2_d_w, &ext2_d_sv, ext2_d_bytes, &ext2_d_wops, ro_mine == 0);
    if (savefail)
        ahci_debug_arm(false, false);
    klock_acquire(vfs_lock());
    uint64_t c0 = fd_tsc(), t0 = timer_ticks();
    /* Stage 10: any thread that asks for the lock during the hold takes a
     * ticket (next) and counts its wait steps (yields) (report only). */
    const struct klock *vl = vfs_lock();
    uint32_t asked0 = vl->next;
    uint64_t steps0 = vl->yields;
    if (err != EXT2_OK) {
        end = EXT2_D_REFUSED;
    } else if (ext2_vfs_init(&ext2_d_vfs, &ext2_d_w.fs, &ext2_d_mem, EXT2_VFS_NODES) != 0) {
        end = EXT2_D_NO_ROOT;
    } else {
        if (ext2_d_w.rw) {
            ext2_vfs_rw_init(&ext2_d_wr, &ext2_d_w);
            ext2_vfs_set_writer(&ext2_d_vfs, &ext2_d_wr);
        }
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
                ext2_d_on = true;
            }
        }
    }
    uint64_t hold_cycles = fd_tsc() - c0, hold_ticks = timer_ticks() - t0;
    uint32_t asked = vl->next - asked0;
    uint64_t steps = vl->yields - steps0;
    if (end != EXT2_D_MOUNTED || !ext2_d_w.rw) {
        ext2_d_unstore();               /* no change will ever be put */
        ext2_d_untables(end != EXT2_D_MOUNTED);     /* 1.5 stage 5: and no write; refused: no read */
    }
    klock_release(vfs_lock());
    disklock_release();
    if (end != EXT2_D_MOUNTED)
        disklock_stop();                /* Live mode: the disk is never used again */

    const struct ext2_fs *fs = &ext2_d_w.fs;
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
          fs->incompat, fs->ro_compat, (unsigned)ext2_d_w.state_read, ext2_d_w.clean_read ? "clean" : "not clean",
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
    /* The D11, D12 and C7 lines: each reason the volume is read-only, and
     * the recount of a volume not cleanly unmounted. */
    bool rw = ext2_d_w.rw;
    if (wt_short)                       /* 1.5 stage 5 (C2) */
        klogf("ext2: too little memory for the write tables: mounted read-only\n");
    if (ext2_d_dropped)                 /* 1.5 stage 6: the D9 fix (ext2_d_unstore) */
        klogf("ext2: %u unsaved chunks dropped with the store of changes (%s): reads show the disk as it is; "
              "mounted read-only (D9)\n", ext2_d_dropped, ext2_d_w.ro_why ? ext2_d_w.ro_why : "read-only");
    if (fs->compat_unknown)
        klogf("ext2: unknown COMPAT flags 0x%x: mounted read-only (reading allowed; writing needs every COMPAT "
              "flag known)\n", fs->compat_unknown);
    if (fs->ro_compat_unknown)
        klogf("ext2: unknown RO_COMPAT flags 0x%x: mounted read-only (D11)\n", fs->ro_compat_unknown);
    if (fs->state_error)
        klogf("ext2: the error bit is set (state 0x%x): mounted read-only (C7)\n", (unsigned)ext2_d_w.state_read);
    else if (ext2_d_w.recounted)
        klogf("ext2: warning: the volume was not cleanly unmounted (state 0x%x); the free blocks, free inodes and "
              "folders of every group recounted from the bitmaps and inode tables (D12): %u blocks free, %u inodes "
              "free; mounted read-write\n", (unsigned)ext2_d_w.state_read, fs->free_blocks, fs->free_inodes);
    else if (!ext2_d_w.clean_read && !rw)
        klogf("ext2: warning: the volume was not cleanly unmounted (state 0x%x); mounted read-only, not recounted "
              "(%s)\n", (unsigned)ext2_d_w.state_read,
              ro_mine ? ro_mine : ext2_d_w.ro_why ? ext2_d_w.ro_why : "read-only");
    if (rw) {
        klogf("mount: ext2 '%s' (port %u, partition %u) on /home as filesystem %d, read-write; the RAM folder "
              "/home is under it, / and /tmp stay in memory; %u disk reads, %u failed; save 0 (the volume marked "
              "not clean on the disk): %llu writes, %llu flushes\n", fs->label, port, partition, id, ext2_d_reads,
              ext2_d_failed, (ull)ext2_d_figs.writes, (ull)ext2_d_figs.flushes);
    } else {
        const char *ro = ro_mine ? ro_mine : ext2_d_w.ro_why ? ext2_d_w.ro_why : "read-only";
        klogf("mount: ext2 '%s' (port %u, partition %u) on /home as filesystem %d, read-only (%s); the RAM folder "
              "/home is under it, / and /tmp stay in memory; %u disk reads, %u failed\n", fs->label, port,
              partition, id, ro, ext2_d_reads, ext2_d_failed);
    }
    uint64_t spent = 0;
    ext2_d_per_tick = fd_tsc_per_tick(&spent);
    klogf("mount: the mount held the VFS lock %llu us (%llu ticks) (report only; TSC %llu cycles per tick)\n",
          (ull)ext2_disk_us(hold_cycles), (ull)hold_ticks, (ull)ext2_d_per_tick);
    /* "blk:": the lines between PCI: and Storage: are the driver's and the
     * storage layers' (ahci, blk, part, guard, ext2, mount), and the mount:
     * and ext2: lines are counted exactly by earlier checks. */
    klogf("blk: the mount: save 0 under the disk lock only; %llu write or FLUSH commands sent while the VFS lock was "
          "held (since boot); %u other threads asked for the VFS lock during its hold, %llu wait steps (report "
          "only)\n", (ull)blk_vfs_commands(), asked, (ull)steps);
    klogf("mount: file-size limits from each filesystem's ops table: / %llu bytes, /home %llu bytes\n",
          (ull)ram_max, (ull)disk_max);
    if (rw) {
        bool tested = (ext2_d_flags & EXT2_DISK_AUTOSAVE) != 0;
        bool slow = (ext2_d_flags & EXT2_DISK_BLKSLOW) != 0;    /* 1.5 stage 4 */
        bool autosave = !(ext2_d_flags & EXT2_DISK_TEST_BOOT) || tested || slow;
        if (autosave)
            ext2_d_saver = thread_create("autosave", ext2_d_autosave, 0, true);
        klogf("mount: changes are kept in memory (%u KiB, %u chunks of 1 KiB) until a save writes them, at most "
              "%u disk commands per hold of the VFS lock; automatic save every 5 s: %s\n", ext2_d_store_n * 4u,
              ext2_d_store_n * 4u, EXT2_D_PIECE,
              autosave ? (ext2_d_saver < 0 ? "off (no thread)" : tested ? "on (test=autosave)"
                          : slow ? "on (test=blkslow)" : "on")
                       : "off (a test= boot)");
        if (ext2_d_flags & EXT2_DISK_BLKDEBUG) {
            ahci_debug_arm(false, true);
            klogf("blk: test=blkdebug: armed: the first disk read during a file read fails once, and every disk "
                  "write fails\n");
        }
    }
    ksnprintf(tail, tail_size, "; ext2 '%s' mounted %s on /home", fs->label, rw ? "read-write" : "read-only");
    return true;
}

const char *ext2_disk_mode(void)
{
    return ext2_d_on && ext2_d_w.rw ? "Disk mode" : "Disk mode, read-only";
}

bool ext2_disk_mounted(void)
{
    return ext2_d_on;
}

bool ext2_disk_rw(void)
{
    return ext2_d_on && ext2_d_w.rw;
}

bool ext2_disk_again(void)
{
    return ext2_d_on && ext2_vfs_again(&ext2_d_vfs);
}

/* ---- saves in pieces (the caller holds neither the VFS lock nor the disk lock) ---- */

enum ext2_d_piece_end {
    EXT2_D_IDLE,                        /* no save in progress, none started */
    EXT2_D_MORE,                        /* a save in progress, commands left */
    EXT2_D_DONE,                        /* a save finished in this piece */
    EXT2_D_FAIL,                        /* a save failed: read-only from now on */
    EXT2_D_RO,                          /* the volume is read-only */
};

/* One piece: the save in progress continued, or one started if `start`
 * and something is dirty or pending; at most EXT2_D_PIECE disk commands.
 * 1.5 stage 4 (C1): under the disk lock ONLY (no VFS lock): in Disk mode
 * no file call runs without the disk lock, the save store is frozen while
 * a save is in progress (blk_sv_put refuses), and the holder keeps
 * preemption on, so the desktop, the clock and programs that make no file
 * call run while the disk works. The hold figures (hold_cycles,
 * hold_ticks; the log lines still say "VFS lock hold") are this hold's.
 * 1.5 stage 6: the piece itself is ext2_d_piece_held(), for a caller that
 * already owns the disk lock (the save before power-off keeps it from the
 * first piece to the S5 write: taking it again would break L3). */
static enum ext2_d_piece_end ext2_d_piece_held(bool start);

static enum ext2_d_piece_end ext2_d_piece(bool start)
{
    disklock_acquire();
    enum ext2_d_piece_end res = ext2_d_piece_held(start);
    disklock_release();
    return res;
}

/* One piece as ext2_d_piece's, by the disk lock's owner: the lock is
 * neither taken nor given back here. Called without it, it panics (as L5
 * would at the first disk command, but before any change). */
static enum ext2_d_piece_end ext2_d_piece_held(bool start)
{
    struct ext2_wfs *w = &ext2_d_w;
    enum ext2_d_piece_end res;
    if (!disklock_held())
        panicf("ext2: a save piece without the disk lock (ext2_d_piece_held, thread %d)", (int)thread_current());
    uint64_t c0 = fd_tsc(), t0 = timer_ticks();
    if (!w->rw) {
        res = w->failed ? EXT2_D_FAIL : EXT2_D_RO;
    } else if (!ext2_write_saving(w) && (!start || !ext2_write_busy(w))) {
        res = EXT2_D_IDLE;
    } else {
        bool done = false;
        int r = ext2_write_save_step(w, EXT2_D_PIECE, &done);
        res = r ? EXT2_D_FAIL : done ? EXT2_D_DONE : EXT2_D_MORE;
        ext2_d_figs.pieces++;
        ext2_d_figs.saves += res == EXT2_D_DONE;
        uint64_t c = fd_tsc() - c0, t = timer_ticks() - t0;
        if (c > ext2_d_figs.hold_cycles)
            ext2_d_figs.hold_cycles = c;
        if (t > ext2_d_figs.hold_ticks)
            ext2_d_figs.hold_ticks = t;
    }
    return res;
}

/* A full save: saves until nothing is dirty or pending (at most
 * EXT2_D_SAVES), or until `deadline` (timer ticks; 0: none). 1.5 stage 6:
 * with `held`, by the disk lock's owner (ext2_d_piece_held; it keeps the
 * lock between pieces, so it does not yield between them either). */
static enum ext2_disk_save ext2_d_full_save_as(uint64_t deadline, uint32_t *saves, bool held)
{
    *saves = 0;
    for (;;) {
        enum ext2_d_piece_end p = held ? ext2_d_piece_held(true) : ext2_d_piece(true);
        if (p == EXT2_D_FAIL)
            return EXT2_DISK_FAILED;
        if (p == EXT2_D_RO)
            return EXT2_DISK_READONLY;
        if (p == EXT2_D_IDLE)
            return EXT2_DISK_SAVED;
        if (p == EXT2_D_DONE && ++*saves >= EXT2_D_SAVES)
            return EXT2_DISK_SAVED;
        if (deadline && timer_ticks() >= deadline)
            return EXT2_DISK_TIMEOUT;
        if (!held)
            thread_yield();
    }
}

static enum ext2_disk_save ext2_d_full_save(uint64_t deadline, uint32_t *saves)
{
    return ext2_d_full_save_as(deadline, saves, false);
}

/* One save: the one in progress finished, or one run if something is
 * dirty or pending. */
static enum ext2_disk_save ext2_d_one_save(void)
{
    bool first = true;
    for (;;) {
        enum ext2_d_piece_end p = ext2_d_piece(first);
        first = false;
        if (p == EXT2_D_FAIL)
            return EXT2_DISK_FAILED;
        if (p == EXT2_D_RO)
            return EXT2_DISK_READONLY;
        if (p == EXT2_D_DONE || p == EXT2_D_IDLE)
            return EXT2_DISK_SAVED;
        thread_yield();
    }
}

bool ext2_disk_retry(int *r, uint32_t *tries)
{
    if (*r != -FS_EAGAIN)
        return false;
    if (!ext2_d_on || *tries >= EXT2_W_SAVE_TRIES) {
        *r = -FS_ENOSPC;                /* R4: at most 3 saves for one operation */
        return false;
    }
    (*tries)++;
    ext2_d_figs.retries++;
    enum ext2_disk_save s = ext2_d_one_save();
    if (s == EXT2_DISK_FAILED) {
        *r = -FS_EIO;
        return false;
    }
    if (s == EXT2_DISK_READONLY) {
        *r = -FS_EROFS;
        return false;
    }
    return true;
}

enum ext2_disk_save ext2_disk_sync(uint64_t *blocks)
{
    *blocks = 0;
    if (!ext2_d_on)
        return EXT2_DISK_NONE;
    if (!ext2_d_w.rw)
        return ext2_d_w.failed ? EXT2_DISK_FAILED : EXT2_DISK_READONLY;
    uint64_t bytes0 = ext2_d_figs.write_bytes, w0 = ext2_d_figs.writes, f0 = ext2_d_figs.flushes;
    uint32_t saves = 0;
    enum ext2_disk_save s = ext2_d_full_save(0, &saves);
    uint64_t bytes = ext2_d_figs.write_bytes - bytes0, bs = ext2_d_w.fs.block_size ? ext2_d_w.fs.block_size : 1024u;
    *blocks = (bytes + bs - 1u) / bs;
    klogf("ext2: sync: %s; %llu blocks saved (%u saves, %llu writes, %llu flushes); the longest VFS lock hold in a "
          "save so far %llu us (%llu ticks) (report only)\n", s == EXT2_DISK_SAVED ? "saved" : "not saved (a disk write failed)",
          (ull)*blocks, saves, (ull)(ext2_d_figs.writes - w0), (ull)(ext2_d_figs.flushes - f0),
          (ull)ext2_disk_us(ext2_d_figs.hold_cycles), (ull)ext2_d_figs.hold_ticks);
    return s;
}

enum ext2_disk_save ext2_disk_restart_save(void)
{
    if (!ext2_d_on)
        return EXT2_DISK_NONE;
    if (!ext2_d_w.rw) {
        klogf("ext2: save before restart: nothing saved (/home is read-only: %s)\n",
              ext2_d_w.ro_why ? ext2_d_w.ro_why : "read-only");
        return ext2_d_w.failed ? EXT2_DISK_FAILED : EXT2_DISK_READONLY;
    }
    uint64_t t0 = timer_ticks(), deadline = t0 + EXT2_D_RESTART, w0 = ext2_d_figs.writes;
    fd_vfs_acquire();
    ext2_d_vfs.closed = true;           /* no more changes */
    uint32_t orphans = ext2_vfs_forget_orphans(&ext2_d_vfs);
    fd_vfs_release();
    uint32_t s1 = 0, s2 = 0;
    enum ext2_disk_save s = ext2_d_full_save(deadline, &s1);
    if (s == EXT2_DISK_SAVED) {
        fd_vfs_acquire();
        int r = ext2_write_mark_clean(&ext2_d_w);
        fd_vfs_release();
        s = r ? EXT2_DISK_FAILED : ext2_d_full_save(deadline, &s2);
    }
    uint64_t ms = (timer_ticks() - t0) * 1000u / TIMER_HZ;
    if (s == EXT2_DISK_SAVED)
        klogf("ext2: save before restart: done in %llu ms (%u saves, %llu writes; %u open files with no name "
              "left forgotten first); the volume marked clean\n", (ull)ms, s1 + s2,
              (ull)(ext2_d_figs.writes - w0), orphans);
    else
        klogf("ext2: save before restart: not finished (%s after %llu ms): restarting anyway (D9)\n",
              s == EXT2_DISK_TIMEOUT ? "not finished within 10 s" : "a disk write failed", (ull)ms);
    return s;
}

/* 1.5 stage 6 (docs/plans/1.5.md, C3, "Shut down during a save"): the save
 * before power-off. The automatic save is stopped first (no new one
 * starts; one between its pieces waits for the disk lock, for ever), then
 * the disk lock is taken (after the current save piece, yielding) and KEPT:
 * this function returns with the caller owning it, and the caller never
 * gives it back (the S5 write, or the fallback screen's halt with
 * interrupts off, follows), so nothing is changed or written after the last
 * save. Inside the hold: the volume closed to changes and the orphans
 * forgotten (under the VFS lock, taken after the disk lock, L1), the final
 * full save (it finishes a save in progress first), the mark-clean (VFS
 * lock), and the full save that writes the clean superblock, each FLUSH
 * included; the pieces as the lock's owner (ext2_d_piece_held, never
 * taking it again, L3). At most 10 s from `clicked` (timer ticks; D9: a
 * failure or the limit is logged and the power-off goes on). */
static bool ext2_d_auto_stopped;

enum ext2_disk_save ext2_disk_poweroff_save(uint64_t clicked)
{
    if (!ext2_d_on)
        return EXT2_DISK_NONE;          /* Live mode: no disk lock (L6) */
    ext2_d_auto_stopped = true;
    disklock_acquire();                 /* kept: never given back */
    if (!ext2_d_w.rw) {
        klogf("ext2: save before power-off: nothing saved (/home is read-only: %s)\n",
              ext2_d_w.ro_why ? ext2_d_w.ro_why : "read-only");
        return ext2_d_w.failed ? EXT2_DISK_FAILED : EXT2_DISK_READONLY;
    }
    uint64_t deadline = clicked + EXT2_D_RESTART, w0 = ext2_d_figs.writes;
    klock_acquire(vfs_lock());
    ext2_d_vfs.closed = true;           /* no more changes */
    uint32_t orphans = ext2_vfs_forget_orphans(&ext2_d_vfs);
    klock_release(vfs_lock());
    uint32_t s1 = 0, s2 = 0;
    enum ext2_disk_save s = ext2_d_full_save_as(deadline, &s1, true);
    if (s == EXT2_DISK_SAVED) {
        klock_acquire(vfs_lock());
        int r = ext2_write_mark_clean(&ext2_d_w);
        klock_release(vfs_lock());
        s = r ? EXT2_DISK_FAILED : ext2_d_full_save_as(deadline, &s2, true);
    }
    uint64_t now = timer_ticks(), ms = (now > clicked ? now - clicked : 0) * 1000u / TIMER_HZ;
    if (s == EXT2_DISK_SAVED)
        klogf("ext2: save before power-off: done in %llu ms (%u saves, %llu writes; %u open files with no name "
              "left forgotten first); the volume marked clean\n", (ull)ms, s1 + s2,
              (ull)(ext2_d_figs.writes - w0), orphans);
    else
        klogf("ext2: save before power-off: not finished (%s after %llu ms): powering off anyway (D9)\n",
              s == EXT2_DISK_TIMEOUT ? "not finished within 10 s" : "a disk write failed", (ull)ms);
    return s;
}

/* The automatic save (C5, D8): every 5 s, a full save if something is
 * dirty or pending; not on test= boots but test=autosave (stage 11), which
 * logs each automatic save that wrote. 1.5 stage 6: none after the
 * power-off began (ext2_disk_poweroff_save). */
static int32_t ext2_d_autosave(void *arg)
{
    (void)arg;
    for (;;) {
        thread_sleep(EXT2_D_AUTOSAVE);
        if (!ext2_d_w.rw || ext2_d_auto_stopped)
            continue;
        uint32_t saves = 0;
        uint64_t w0 = ext2_d_figs.writes, f0 = ext2_d_figs.flushes;
        enum ext2_disk_save s = ext2_d_full_save(0, &saves);
        if (s == EXT2_DISK_SAVED && saves)
            ext2_d_figs.autosaves++;
        if ((ext2_d_flags & EXT2_DISK_AUTOSAVE) && saves)
            klogf("ext2: automatic save (test=autosave): %s; %u saves, %llu writes, %llu flushes; the longest VFS "
                  "lock hold in a save so far %llu us (%llu ticks) (report only)\n",
                  s == EXT2_DISK_SAVED ? "saved" : "not saved", saves, (ull)(ext2_d_figs.writes - w0),
                  (ull)(ext2_d_figs.flushes - f0), (ull)ext2_disk_us(ext2_d_figs.hold_cycles),
                  (ull)ext2_d_figs.hold_ticks);
    }
    return 0;
}

/* ---- stage 12: the home folders (D18), du's KiB, df's figures ---- */

static const char *const ext2_d_home_names[7] = { "Desktop", "Documents", "Downloads", "Pictures", "Music",
                                                  "Videos", "Trash" };

/* The folder at path made if missing (again after a save when the disk
 * needs one first, R4): 1 made, 0 a folder already there (never touched),
 * or an error (-FS_ENOTDIR: something else has the name). */
static int ext2_d_home_dir(const char *path)
{
    struct vfs *v = vfs_root();
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        r = vfs_create(v, v->root, path, VNODE_DIR, 0755u, 0);
        if (r == -FS_EEXIST) {
            struct vnode *n = 0;
            r = (vfs_lookup(v, v->root, path, &n) == 0 && n->type == VNODE_DIR) ? 0 : -FS_ENOTDIR;
            fd_vfs_release();
            return r;
        }
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r == 0 ? 1 : r;
}

void ext2_disk_home(void)
{
    if (!ext2_d_on || !ext2_d_w.rw || (ext2_d_flags & EXT2_DISK_TEST_BOOT))
        return;
    char path[40];
    uint32_t made = 0, there = 0;
    int r = ext2_d_home_dir("/home/user");
    const char *bad = "/home/user";
    if (r >= 0) {
        made += (uint32_t)r;
        there += r == 0;
        for (uint32_t i = 0; i < 7 && r >= 0; i++) {
            ksnprintf(path, sizeof(path), "/home/user/%s", ext2_d_home_names[i]);
            bad = ext2_d_home_names[i];
            r = ext2_d_home_dir(path);
            made += r > 0;
            there += r == 0;
        }
    }
    if (r < 0) {
        klogf("home: not finished: %s: %s (%u folders made, %u already there)\n", bad,
              r == -FS_ENOTDIR ? "not a folder" : fs_errno_name(r), made, there);
        return;
    }
    if (!made) {
        klogf("home: /home/user and its 7 folders are already there; nothing made (D18: never recreated)\n");
        return;
    }
    uint32_t saves = 0;
    uint64_t w0 = ext2_d_figs.writes;
    enum ext2_disk_save s = ext2_d_full_save(0, &saves);
    klogf("home: %u folders made, %u already there (/home/user with Desktop, Documents, Downloads, Pictures, "
          "Music, Videos and Trash); %s: %u saves, %llu writes; the longest VFS lock hold in a save %llu us "
          "(%llu ticks) (report only, before System ready)\n", made, there, s == EXT2_DISK_SAVED ? "saved" : "NOT saved",
          saves, (ull)(ext2_d_figs.writes - w0), (ull)ext2_disk_us(ext2_d_figs.hold_cycles),
          (ull)ext2_d_figs.hold_ticks);
    ext2_d_figs.hold_cycles = 0;            /* the later figures: after the boot */
    ext2_d_figs.hold_ticks = 0;
}

uint64_t ext2_disk_kib(const struct vnode *n)
{
    struct ext2_inode in;
    if (!ext2_d_on || !n || n->fs_id == 0 || n->ino == 0 || n->ino > 0xFFFFFFFFull)
        return 0;
    if (ext2_inode_get(&ext2_d_w.fs, (uint32_t)n->ino, &in) != EXT2_OK)
        return 0;
    return in.blocks512 / 2u;
}

bool ext2_disk_df(struct ext2_disk_dfinfo *out)
{
    if (!ext2_d_on)
        return false;
    const struct ext2_fs *fs = &ext2_d_w.fs;
    for (uint32_t i = 0; i < sizeof(out->label); i++)
        out->label[i] = fs->label[i];
    out->label[sizeof(out->label) - 1] = 0;
    out->port = ext2_d_port;
    out->partition = ext2_d_part;
    out->block_size = fs->block_size;
    out->blocks = fs->blocks_count;
    out->blocks_free = fs->free_blocks;
    out->inodes = fs->inodes_count;
    out->inodes_free = fs->free_inodes;
    return true;
}

/* Field by field: no structure is copied whole (no libc call in the kernel). */
void ext2_disk_figures(struct ext2_disk_figs *f)
{
    const struct ext2_disk_figs *s = &ext2_d_figs;
    f->saves = s->saves;
    f->pieces = s->pieces;
    f->autosaves = s->autosaves;
    f->retries = s->retries;
    f->writes = s->writes;
    f->write_bytes = s->write_bytes;
    f->write_cycles = s->write_cycles;
    f->flushes = s->flushes;
    f->flush_cycles = s->flush_cycles;
    f->flush_longest = s->flush_longest;
    f->hold_cycles = s->hold_cycles;
    f->hold_ticks = s->hold_ticks;
    f->evicted = ext2_d_vfs.evicted;
    f->nodes = ext2_d_vfs.count;
}

struct ext2_vfs *ext2_disk_vfs(void)
{
    return ext2_d_on ? &ext2_d_vfs : 0;
}

struct ext2_wfs *ext2_disk_wfs(void)
{
    return ext2_d_on ? &ext2_d_w : 0;
}

uint32_t ext2_disk_port(void)
{
    return blk_port_number();
}
