#ifndef MADIRE_FS_EXT2_DISK_H
#define MADIRE_FS_EXT2_DISK_H

/*
 * The disk's ext2 volume at /home (1.4 stage 8; docs/plans/1.4.md, C8,
 * C9, D11, D12, D16; stage 10: C5, D8, D9): called once at boot by the AHCI
 * driver (ahci.c), after the write guard, for the partition the guard
 * allowed (a marked test disk); never for a disk the guard refused (Live
 * mode, as before). The volume is read through the block cache (blk.h),
 * the partition's bytes only, and mounted on the RAM folder /home through
 * the mount table (mount_core.h), under the VFS lock.
 *
 * Stage 10: READ-WRITE through the write core (ext2_write.h) and the block
 * layer's write-back part (blk_core_save.h), whose writes go to blk_write,
 * which the AHCI driver's one command builder refuses unless the guard
 * allows. The store of unsaved changes is as large as the cache may be (2
 * MiB or 1/16 of the free pages: VM_SV_BASE in slot 508); a save writes at
 * most 8 disk commands per hold of the VFS lock and runs in pieces.
 *
 * D16 since stage 10:
 *   not ext2, a superblock that fails its checks, an unknown INCOMPAT flag
 *       -> not mounted (Live mode): one ext2: line with the reason;
 *   an unknown RO_COMPAT flag, an unknown COMPAT flag (the owner's rule),
 *   the error bit, test=diskro, or too little memory for the store
 *       -> mounted read-only ("Disk mode, read-only"), one ext2: line each;
 *   not cleanly unmounted (no error bit) -> recounted from the bitmaps,
 *       mounted read-write with the D12 warning line;
 *   marked, supported and clean -> mounted read-write ("Disk mode"); save 0
 *       writes the "not clean" state at once.
 *
 * Saves (C5): sync (ext2_disk_sync, the shell's "sync"), the automatic save
 * every 5 s (a thread; not on test= boots), the save before Restart
 * (ext2_disk_restart_save: at most 10 s, then the volume marked clean; a
 * failure is logged and the restart goes on, D9), and the saves an
 * operation needs (ext2_disk_retry, R4: at most 3 for one operation). Every
 * save function is called WITHOUT the VFS lock: it takes the lock for each
 * piece. A failed save makes the volume read-only (D9; the write core
 * writes its error state once if it still can).
 *
 * Its lines, logged after the work (never while the lock is held), all
 * between the guard's lines and the Storage: line:
 *   ext2: port P partition I: revision R, B-byte blocks, ...; state ...
 *   ext2: port P partition I: not mounted: <reason>
 *   ext2: unknown COMPAT flags 0x...: ... / unknown RO_COMPAT ... /
 *         the error bit ... / warning: not cleanly unmounted ...
 *   mount: ext2 '<label>' on /home (filesystem N), read-write | read-only
 *          (<why>); ...
 *   mount: the mount held the VFS lock ...
 *   mount: file-size limits from each filesystem's ops table: ...
 *   mount: changes are kept in memory ... (read-write only)
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The command line's options for the mount (kernel.c, before the PCI
 * scan). */
#define EXT2_DISK_TEST_BOOT  0x1u       /* a test= boot: no automatic save (D8) */
#define EXT2_DISK_RO         0x2u       /* test=diskro: mounted read-only */
#define EXT2_DISK_BLKDEBUG   0x4u       /* test=blkdebug: injected disk errors */
#define EXT2_DISK_AUTOSAVE   0x8u       /* test=autosave: the automatic save on, each save logged (stage 11) */
void ext2_disk_options(uint32_t flags);

/* Stage 11, test=diskcut[=N] (docs/plans/1.4.md, section D (b)): every disk
 * write the saves send is logged ("blk: write <n> class <c>: partition byte
 * <off>, <len> bytes"); with `cut`, the power is cut after write `after`:
 * one "blk: test=diskcut=<N>: ..." line, then no more disk commands and the
 * CPU halts (the test kills QEMU). Before the mount. */
void ext2_disk_cut(bool cut, uint32_t after);

/* Mounts the ext2 volume of partition `partition` (LBA `first`, `sectors`
 * sectors) of the disk on port `port`, attached to the block cache. Fills
 * `tail` with the Storage: line's end ("; ext2 '...' mounted read-write on
 * /home", "...read-only on /home" or "; read only, not mounted: <reason>",
 * without the mode in parentheses). true if mounted. */
bool ext2_disk_mount(uint32_t port, uint32_t partition, uint64_t first, uint64_t sectors, char *tail,
                     size_t tail_size);

/* The Storage: line's mode of a mounted volume: "Disk mode" or "Disk mode,
 * read-only". */
const char *ext2_disk_mode(void);
bool ext2_disk_mounted(void);
bool ext2_disk_rw(void);                /* mounted and read-write now */
/* Pages of slot 508 the store of unsaved changes uses (0 without one). */
uint32_t ext2_disk_store_pages(void);

/* TSC cycles in microseconds, by the calibration the mount made (0 on a
 * boot without a mounted disk); for report-only figures (console.c). */
uint64_t ext2_disk_us(uint64_t cycles);

enum ext2_disk_save {
    EXT2_DISK_SAVED,                    /* saved (or nothing to save) */
    EXT2_DISK_NONE,                     /* no disk in use (Live mode) */
    EXT2_DISK_READONLY,                 /* /home is read-only: nothing saved */
    EXT2_DISK_FAILED,                   /* a disk write failed: read-only now */
    EXT2_DISK_TIMEOUT,                  /* not finished in time (the save before Restart) */
};

/* "sync": a full save; *blocks: the filesystem blocks written. One log
 * line. */
enum ext2_disk_save ext2_disk_sync(uint64_t *blocks);
/* Before Restart (the dialog and "reboot"): no more changes, every open
 * file with no name left forgotten, a full save, the clean state, a full
 * save, within 10 s; one log line ("done" or "FAILED ...: restarting anyway"). */
enum ext2_disk_save ext2_disk_restart_save(void);

/* After a VFS call answered -FS_EAGAIN (the VFS lock given back): true if
 * the caller should try the call again, after one save (*tries counts
 * them, at most 3); false with *r set to -FS_ENOSPC (3 saves were not
 * enough), -FS_EIO (the save failed) or -FS_EROFS. False, *r unchanged, for
 * any other *r. */
bool ext2_disk_retry(int *r, uint32_t *tries);
/* (The VFS lock held.) True once after a write to /home stopped short
 * because a save is needed first (ext2_vfs_again). */
bool ext2_disk_again(void);

/* Figures (report only). */
struct ext2_disk_figs {
    uint64_t saves, pieces, autosaves, retries;
    uint64_t writes, write_bytes, write_cycles;
    uint64_t flushes, flush_cycles, flush_longest;
    uint64_t hold_cycles, hold_ticks;   /* the longest hold of the VFS lock by a save piece */
    uint32_t evicted, nodes;
};
void ext2_disk_figures(struct ext2_disk_figs *f);

/* 1.4 stage 12 (D18, docs/plans/1.4.md C11): the home folders. On a
 * read-write mount, /home/user and its Desktop, Documents, Downloads,
 * Pictures, Music, Videos and Trash are made if missing (one never
 * recreated or touched), then saved; one "home:" line. Nothing on a
 * read-only mount, in Live mode or on a test= boot (as D8's automatic save:
 * the disk tests' writes stay their own). kernel.c, after the "Key events:"
 * line, before the diagnostics (thread 0). The save pieces' longest-hold
 * figure starts again after it (boot-time, before System ready). */
void ext2_disk_home(void);

/* 1.4 stage 12, for "du": the KiB a file or folder on the disk really
 * uses (its i_blocks, indirect blocks included); 0 for a node not on the
 * disk. Under the VFS lock. */
struct vnode;
uint64_t ext2_disk_kib(const struct vnode *n);

/* 1.4 stage 12, for "df": the mounted volume's figures (in memory: the
 * frees not yet saved count as used). false in Live mode. */
struct ext2_disk_dfinfo {
    char label[17];
    uint32_t port, partition, block_size;
    uint64_t blocks, blocks_free, inodes, inodes_free;
};
bool ext2_disk_df(struct ext2_disk_dfinfo *out);

/* For the disk tests (disktest.c): the mounted volume's VFS adapter and
 * write core (NULL in Live mode), and the disk's port. */
struct ext2_vfs *ext2_disk_vfs(void);
struct ext2_wfs *ext2_disk_wfs(void);
uint32_t ext2_disk_port(void);

#endif
