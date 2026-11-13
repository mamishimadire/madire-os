/* The disk write tests (1.4 stage 10): see disktest.h. Every static is
 * dt_-prefixed. */
#include "kernel/fs/disktest.h"
#include "kernel/fs/ext2_disk.h"
#include "kernel/fs/ext2_write.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/vfs.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/memory.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/disklock.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/fdtable_core.h"
#include "kernel/proc/process.h"
#include "kernel/proc/thread.h"
#include "kernel/storage/ahci.h"
#include "kernel/storage/blk.h"
#include "kernel/storage/blk_core.h"
#include "kernel/timer.h"

typedef unsigned long long ull;

#define DT_CHUNK        4096u
#define DT_FILE         (1024u * 1024u)         /* diskwrite's file: 1 MiB */
#define DT_HALF         (512u * 1024u)
#define DT_REWRITE_AT   (256u * 1024u)
#define DT_REWRITE      (64u * 1024u)
#define DT_NODES        1100u
#define DT_BATCH        64u                     /* entries per hold when listing */

static uint8_t dt_buf[DT_CHUNK];

/* The longest hold of a VFS call (cycles, ticks) in a test. */
struct dt_hold {
    uint64_t cycles, ticks;
};

static void dt_note(struct dt_hold *h, uint64_t c, uint64_t t)
{
    if (c > h->cycles)
        h->cycles = c;
    if (t > h->ticks)
        h->ticks = t;
}

/* The file's byte at `pos`; the rewrite's bytes differ. */
static uint8_t dt_byte(uint64_t pos, bool rewrite)
{
    return (uint8_t)(rewrite ? pos * 13u + 0x5Au : pos * 7u + (pos >> 9));
}

static void dt_fill(uint64_t pos, uint32_t n, bool rewrite)
{
    for (uint32_t i = 0; i < n; i++)
        dt_buf[i] = dt_byte(pos + i, rewrite);
}

/* A file or folder made at path (again after a save when needed) and
 * held; 0 or an error. */
static int dt_make(const char *path, enum vnode_type type, struct vnode **out)
{
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        r = vfs_create(vfs_root(), vfs_root()->root, path, type, type == VNODE_DIR ? 0755u : 0644u, out);
        if (r == 0 && out)
            vfs_hold(*out);
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r;
}

/* `len` bytes at `off` of file n, one chunk per hold, again after a save
 * when the disk needs one first. */
static int dt_write(struct vnode *n, uint64_t off, const uint8_t *buf, uint32_t len, struct dt_hold *h)
{
    uint32_t done = 0, tries = 0;
    while (done < len) {
        fd_vfs_acquire();
        uint64_t c0 = fd_tsc(), t0 = timer_ticks();
        int64_t put = vfs_write(vfs_root(), n, off + done, buf + done, len - done);
        bool again = put == -FS_EAGAIN || (put >= 0 && (uint64_t)put < len - done && ext2_disk_again());
        dt_note(h, fd_tsc() - c0, timer_ticks() - t0);
        fd_vfs_release();
        if (put > 0) {
            done += (uint32_t)put;
            tries = 0;
        }
        if (again) {
            int rr = -FS_EAGAIN;
            if (ext2_disk_retry(&rr, &tries))
                continue;
            return rr;
        }
        if (put < 0)
            return (int)put;
        if (put == 0)
            return -FS_EIO;
    }
    return 0;
}

/* The CRC32 of file n's first `len` bytes, read one chunk per hold. */
static int dt_crc(struct vnode *n, uint64_t len, uint32_t *crc)
{
    *crc = 0;
    for (uint64_t off = 0; off < len; off += DT_CHUNK) {
        uint32_t want = len - off < DT_CHUNK ? (uint32_t)(len - off) : DT_CHUNK;
        fd_vfs_acquire();
        int64_t got = vfs_read(vfs_root(), n, off, dt_buf, want);
        fd_vfs_release();
        if (got != (int64_t)want)
            return got < 0 ? (int)got : -FS_EIO;
        *crc = blk_crc32(*crc, dt_buf, want);
    }
    return 0;
}

/* The CRC32 the file should have: the pattern, the rewrite over it. */
static uint32_t dt_expect(uint64_t len, bool rewritten)
{
    uint32_t crc = 0;
    for (uint64_t off = 0; off < len; off += DT_CHUNK) {
        uint32_t n = len - off < DT_CHUNK ? (uint32_t)(len - off) : DT_CHUNK;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t p = off + i;
            dt_buf[i] = dt_byte(p, rewritten && p >= DT_REWRITE_AT && p < DT_REWRITE_AT + DT_REWRITE);
        }
        crc = blk_crc32(crc, dt_buf, n);
    }
    return crc;
}

static int dt_unlink(const char *path)
{
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        r = vfs_unlink(vfs_root(), vfs_root()->root, path);
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r;
}

static int dt_truncate(struct vnode *n, uint64_t len)
{
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        r = vfs_truncate_step(vfs_root(), n, len, 1);
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    return r;
}

static void dt_drop(struct vnode *n)
{
    fd_vfs_acquire();
    vfs_drop(vfs_root(), n);
    fd_vfs_release();
}

/* ---- test=diskwrite ---- */

/* A disk the guard refused: every write and flush must be refused. */
static void dt_refused(uint32_t port)
{
    static const uint64_t lbas[3] = { 0, 2050, 4096 };
    uint32_t frame = memory_alloc_page();
    if (!frame) {
        klogf("diskwrite: FAILED (no page for the attempt)\n");
        return;
    }
    uint64_t w0, f0, r0, t0, w1, f1, r1, t1;
    ahci_write_figures(&w0, &f0, &r0, &t0);
    uint32_t wr_refused = 0, fl_refused = 0;
    for (uint32_t i = 0; i < 3; i++) {
        int wr = 0, fr = 0;
        ahci_write_attempt(port, lbas[i], frame, &wr, &fr);
        wr_refused += wr != 0;
        fl_refused += fr != 0;
    }
    ahci_write_figures(&w1, &f1, &r1, &t1);
    memory_free_page(frame);
    klogf("diskwrite: the disk on port %u: the write guard REFUSED it; 3 WRITE DMA EXT (LBA 0, 2050, 4096) and 3 "
          "FLUSH CACHE EXT tried through the driver's calls: %u and %u refused; %llu writes and %llu flushes sent, "
          "%llu refused by the command builder\n", port, wr_refused, fl_refused, (ull)(w1 - w0), (ull)(f1 - f0),
          (ull)(r1 - r0));
    if (wr_refused == 3 && fl_refused == 3 && w1 == w0 && f1 == f0 && r1 - r0 == 6)
        klogf("diskwrite: DONE (every write and flush refused by the guard, nothing sent to the disk)\n");
    else
        klogf("diskwrite: FAILED (a write or a flush was not refused by the guard)\n");
}

struct dt_work {
    volatile bool done;
    const char *fail;
    uint64_t write_cycles, sync_cycles, sync2_cycles;
    uint64_t sync_blocks;
    uint32_t crc1, want1, crc2, want2;
    struct dt_hold writes;
};

static struct dt_work dt_w;

static int32_t dt_worker(void *arg)
{
    struct dt_work *w = arg;
    struct vnode *n = 0;
    const char *path = "/home/diskwrite.bin";
    w->fail = 0;
    if (dt_make(path, VNODE_FILE, &n) != 0) {
        w->fail = "the file could not be made";
        w->done = true;
        return 0;
    }
    uint64_t c0 = fd_tsc();
    for (uint64_t off = 0; off < DT_FILE && !w->fail; off += DT_CHUNK) {
        dt_fill(off, DT_CHUNK, false);
        if (dt_write(n, off, dt_buf, DT_CHUNK, &w->writes) != 0)
            w->fail = "a write failed";
    }
    w->write_cycles = fd_tsc() - c0;
    uint64_t blocks = 0;
    c0 = fd_tsc();
    if (!w->fail && ext2_disk_sync(&blocks) != EXT2_DISK_SAVED)
        w->fail = "the sync failed";
    w->sync_cycles = fd_tsc() - c0;
    w->sync_blocks = blocks;
    w->want1 = dt_expect(DT_FILE, false);
    if (!w->fail && (dt_crc(n, DT_FILE, &w->crc1) != 0 || w->crc1 != w->want1))
        w->fail = "the file did not read back after the sync";
    for (uint64_t off = DT_REWRITE_AT; off < DT_REWRITE_AT + DT_REWRITE && !w->fail; off += DT_CHUNK) {
        dt_fill(off, DT_CHUNK, true);
        if (dt_write(n, off, dt_buf, DT_CHUNK, &w->writes) != 0)
            w->fail = "a rewrite failed";
    }
    if (!w->fail && dt_truncate(n, DT_HALF) != 0)
        w->fail = "the truncate failed";
    c0 = fd_tsc();
    if (!w->fail && ext2_disk_sync(&blocks) != EXT2_DISK_SAVED)
        w->fail = "the second sync failed";
    w->sync2_cycles = fd_tsc() - c0;
    w->want2 = dt_expect(DT_HALF, true);
    if (!w->fail && (dt_crc(n, DT_HALF, &w->crc2) != 0 || w->crc2 != w->want2))
        w->fail = "the file did not read back after the rewrite and the truncate";
    dt_drop(n);
    if (!w->fail && dt_unlink(path) != 0)
        w->fail = "the file could not be removed";
    if (!w->fail && ext2_disk_sync(&blocks) != EXT2_DISK_SAVED)
        w->fail = "the last sync failed";
    w->done = true;
    return 0;
}

void disktest_write(void)
{
    if (!blk_attached()) {
        klogf("diskwrite: FAILED (no disk attached)\n");
        return;
    }
    uint32_t port = blk_port_number();
    if (!ahci_disk_guard_allows(port)) {
        dt_refused(port);
        return;
    }
    if (!ext2_disk_rw()) {
        klogf("diskwrite: FAILED (the disk is not mounted read-write; this test needs a read-write volume or a "
              "disk the guard refused)\n");
        return;
    }
    struct dt_work *w = &dt_w;
    struct ext2_disk_figs f0, f1;
    uint64_t spent = 0, per = fd_tsc_per_tick(&spent);
    ext2_disk_figures(&f0);
    w->done = false;
    thread_turn_reset();
    int32_t tid = thread_create("diskwrite", dt_worker, w, false);
    if (tid < 0) {
        klogf("diskwrite: FAILED (no thread)\n");
        return;
    }
    for (uint32_t i = 0; i < 300u * TIMER_HZ && !w->done; i++)
        diag_sleep(1);                  /* thread 0 stays ready: the worker's turns end at switches */
    thread_join(tid);
    char owner[96];
    uint64_t turn = thread_turn_max();
    thread_turn_owner(owner, sizeof(owner));
    ext2_disk_figures(&f1);
    uint64_t wus = fd_cycles_us(w->write_cycles, per), sus = fd_cycles_us(w->sync_cycles, per);
    uint64_t dbytes = f1.write_bytes - f0.write_bytes, dus = fd_cycles_us(f1.write_cycles - f0.write_cycles, per);
    uint64_t nfl = f1.flushes - f0.flushes, flus = fd_cycles_us(f1.flush_cycles - f0.flush_cycles, per);
    klogf("diskwrite: wrote %u KiB to /home/diskwrite.bin in %llu us (%llu KiB/s into memory); the first sync: "
          "%llu us, %llu blocks (report only)\n", DT_FILE / 1024u, (ull)wus,
          (ull)(wus ? (uint64_t)(DT_FILE / 1024u) * 1000000u / wus : 0), (ull)sus, (ull)w->sync_blocks);
    /* The count and the total are this test's; the longest flush is kept
     * for the whole mount (ext2_disk.c) and can be the mount's own save 0,
     * before this test: it is named so (1.4 S10: it read as longer than
     * the total). */
    klogf("diskwrite: disk writes: %llu commands, %llu KiB in %llu us, %llu KiB/s; FLUSH CACHE EXT: %llu, %llu us "
          "in all; the longest flush since the mount (its save 0 included) %llu us (report only)\n",
          (ull)(f1.writes - f0.writes), (ull)(dbytes / 1024u),
          (ull)dus, (ull)(dus ? dbytes / 1024u * 1000000u / dus : 0), (ull)nfl, (ull)flus,
          (ull)fd_cycles_us(f1.flush_longest, per));
    klogf("diskwrite: the longest VFS lock hold: a write call %llu us (%llu ticks), a save piece %llu us (%llu "
          "ticks); %llu saves in %llu pieces, %llu saves an operation needed; the longest turn %llu ticks (%s) "
          "(report only)\n", (ull)fd_cycles_us(w->writes.cycles, per), (ull)w->writes.ticks,
          (ull)fd_cycles_us(f1.hold_cycles, per), (ull)f1.hold_ticks, (ull)(f1.saves - f0.saves),
          (ull)(f1.pieces - f0.pieces), (ull)(f1.retries - f0.retries), (ull)turn, owner);
    klogf("diskwrite: read back after the sync: CRC32 0x%08x (expected 0x%08x); after the rewrite, the truncate to "
          "%u KiB and a sync: CRC32 0x%08x (expected 0x%08x)\n", w->crc1, w->want1, DT_HALF / 1024u, w->crc2,
          w->want2);
    if (!w->done || w->fail)
        klogf("diskwrite: FAILED (%s)\n", w->done ? w->fail : "the worker did not finish");
    else
        klogf("diskwrite: DONE (1 MiB written, saved, read back; rewritten, truncated, saved, read back; removed "
              "and saved)\n");
    /* The node table's test in the same boot (part static's time budget). */
    if (w->done && !w->fail)
        disktest_nodes();
}

/* ---- test=disknodes ---- */

static char dt_path[48];

static const char *dt_name(uint32_t i)
{
    ksnprintf(dt_path, sizeof(dt_path), "/home/nodes/f%04u", i);
    return dt_path;
}

/* The entries of folder d, DT_BATCH per hold; with `hold`, each is held
 * (into held[], at most max) and the walk stops at the first NULL. The
 * count; *err: the error op's answer at the end. */
static uint32_t dt_list(struct vnode *d, bool hold, struct vnode **held, uint32_t max, int *err)
{
    uint32_t count = 0;
    uint64_t pos = 0;
    *err = 0;
    for (;;) {
        bool end = false;
        fd_vfs_acquire();
        struct vnode *e = vfs_entry_from(vfs_root(), d, pos);
        for (uint32_t k = 0; k < DT_BATCH && e; k++) {
            count++;
            if (hold && count <= max) {
                vfs_hold(e);
                held[count - 1] = e;
            }
            pos = vfs_entry_pos(vfs_root(), e) + 1;
            e = vfs_entry_next(vfs_root(), d, e);
        }
        if (!e) {
            *err = vfs_lookup_error(vfs_root(), d);
            end = true;
        } else {
            pos = vfs_entry_pos(vfs_root(), e);
        }
        fd_vfs_release();
        if (end)
            return count;
    }
}

void disktest_nodes(void)
{
    if (!ext2_disk_rw()) {
        klogf("disknodes: FAILED (no read-write volume on /home)\n");
        return;
    }
    struct vnode *d = 0;
    if (dt_make("/home/nodes", VNODE_DIR, &d) != 0) {
        klogf("disknodes: FAILED (the folder could not be made)\n");
        return;
    }
    struct ext2_disk_figs f0, f1;
    ext2_disk_figures(&f0);
    uint32_t made = 0;
    for (; made < DT_NODES; made++)
        if (dt_make(dt_name(made), VNODE_FILE, 0) != 0)
            break;
    uint64_t blocks = 0;
    bool saved = ext2_disk_sync(&blocks) == EXT2_DISK_SAVED;
    int err = 0;
    uint32_t listed = dt_list(d, false, 0, 0, &err);
    ext2_disk_figures(&f1);
    klogf("disknodes: %u files made in /home/nodes (%llu saves; %llu an operation needed); listed %u entries "
          "(end: %s); the node table: %u nodes now (at most 1024), %u evicted\n", made, (ull)(f1.saves - f0.saves),
          (ull)(f1.retries - f0.retries), listed, err ? fs_errno_name(err) : "the end", f1.nodes,
          f1.evicted - f0.evicted);
    bool ok = made == DT_NODES && saved && listed == DT_NODES && err == 0 && f1.nodes <= 1024u &&
              f1.evicted > f0.evicted;

    /* The table full: every listed node held until none can be made. */
    struct vnode **held = kmalloc(DT_NODES * sizeof(struct vnode *));
    uint32_t nheld = 0, relisted = 0;
    int64_t rec = 0;
    if (held) {
        uint32_t got = dt_list(d, true, held, DT_NODES, &err);
        nheld = got < DT_NODES ? got : DT_NODES;
        uint64_t next = 0;
        bool more = false;
        fd_vfs_acquire();
        uint64_t cur = nheld ? vfs_entry_pos(vfs_root(), held[nheld - 1]) + 1u + 2u : 2u;
        rec = fd_dir_records(d, cur, dt_buf, sizeof(dt_buf), &next, &more);
        fd_vfs_release();
        klogf("disknodes: the table full: %u nodes held, the listing ended with %s; getdents64's records call "
              "from there answered %d (%s)\n", nheld, err ? fs_errno_name(err) : "the end", (int)rec,
              rec < 0 ? fs_errno_name((int)rec) : "records");
        fd_vfs_acquire();
        for (uint32_t i = 0; i < nheld; i++)
            vfs_drop(vfs_root(), held[i]);
        fd_vfs_release();
        kfree(held);
        relisted = dt_list(d, false, 0, 0, &err);
        klogf("disknodes: the holds given back: %u entries listed again (end: %s)\n", relisted,
              err ? fs_errno_name(err) : "the end");
    }
    ok = ok && held && nheld < DT_NODES && rec == -FS_ENOMEM && relisted == DT_NODES && err == 0;

    uint32_t removed = 0;
    for (uint32_t i = 0; i < made; i++)
        removed += dt_unlink(dt_name(i)) == 0;
    dt_drop(d);
    uint32_t tries = 0;
    int r;
    do {
        fd_vfs_acquire();
        r = vfs_rmdir(vfs_root(), vfs_root()->root, "/home/nodes");
        fd_vfs_release();
    } while (ext2_disk_retry(&r, &tries));
    saved = ext2_disk_sync(&blocks) == EXT2_DISK_SAVED;
    klogf("disknodes: %u files and the folder removed (%s); saved: %s\n", removed, r ? fs_errno_name(r) : "rmdir ok",
          saved ? "yes" : "no");
    ok = ok && removed == made && r == 0 && saved;
    klogf("%s", ok ? "disknodes: DONE (more files than the node table holds, listed with eviction; ENOMEM when "
                     "every node is in use)\n"
                   : "disknodes: FAILED (see the lines above)\n");
}

/* ---- test=diskcut, test=diskcheck, test=diskchurn (stage 11) ---- */

#define DT_CUT_LEN      3000u                   /* the owner's case: A and B, 3000 bytes each */
#define DT_CHURN        1000u
#define DT_CHURN_SAVE   50u

/* Byte j of a file written with `seed`: the same pattern as tests/
 * ext2write.c's model (its ew_gen), so the host tool's expected contents
 * hold for the files the kernel writes. */
static uint8_t dt_gen(uint32_t seed, uint64_t j)
{
    uint64_t x = (j + 1u) * 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed << 32 | seed);
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 32;
    return (uint8_t)x;
}

/* A file made at path, `len` (at most DT_CHUNK) bytes of the pattern
 * written, then closed (the node given back): 0 or an error. */
static int dt_pattern_file(const char *path, uint32_t seed, uint32_t len, struct dt_hold *h)
{
    struct vnode *n = 0;
    int r = dt_make(path, VNODE_FILE, &n);
    if (r)
        return r;
    for (uint32_t i = 0; i < len; i++)
        dt_buf[i] = dt_gen(seed, i);
    r = dt_write(n, 0, dt_buf, len, h);
    dt_drop(n);
    return r;
}

void disktest_cut(void)
{
    if (!ext2_disk_rw()) {
        klogf("diskcut: FAILED (no read-write volume on /home)\n");
        return;
    }
    /* Note D (stage 10): every file is closed before it is removed, so no
     * inode stays unattached while open; unlink-while-open is outside the
     * cut sequences. */
    klogf("diskcut: the owner's case: save 1 writes /home/A; /home/A removed and /home/B made with no save "
          "between, then saves 2 to 4 (R3); each file closed before it is removed\n");
    struct dt_hold h = { 0, 0 };
    uint64_t blocks = 0;
    const char *fail = 0;
    if (dt_pattern_file("/home/A", 1, DT_CUT_LEN, &h) != 0)
        fail = "A could not be written";
    else if (ext2_disk_sync(&blocks) != EXT2_DISK_SAVED)
        fail = "the first sync failed";
    else if (dt_unlink("/home/A") != 0)
        fail = "A could not be removed";
    else if (dt_pattern_file("/home/B", 2, DT_CUT_LEN, &h) != 0)
        fail = "B could not be written";
    else if (ext2_disk_sync(&blocks) != EXT2_DISK_SAVED)
        fail = "the second sync failed";
    struct ext2_disk_figs f;
    ext2_disk_figures(&f);
    if (fail)
        klogf("diskcut: FAILED (%s)\n", fail);
    else
        klogf("diskcut: DONE (uncut: %llu disk writes and %llu flushes since the mount; the longest VFS lock hold in a "
              "save %llu us (%llu ticks))\n", (ull)f.writes, (ull)f.flushes, (ull)ext2_disk_us(f.hold_cycles),
              (ull)f.hold_ticks);
}

void disktest_check(void)
{
    static const char *const names[3] = { "/home/A", "/home/B", "/home/auto.txt" };
    if (!ext2_disk_mounted()) {
        klogf("diskcheck: FAILED (nothing mounted on /home)\n");
        return;
    }
    for (uint32_t i = 0; i < 3; i++) {
        struct vnode *n = 0;
        fd_vfs_acquire();
        int r = vfs_lookup(vfs_root(), vfs_root()->root, names[i], &n);
        bool file = r == 0 && n->type == VNODE_FILE;
        uint64_t size = file ? n->size : 0;
        if (r == 0)
            vfs_hold(n);
        fd_vfs_release();
        if (r != 0) {
            klogf("diskcheck: %s: absent (%s)\n", names[i], fs_errno_name(r));
            continue;
        }
        uint32_t crc = 0;
        int e = file ? dt_crc(n, size, &crc) : 0;
        if (!file)
            klogf("diskcheck: %s: not a file\n", names[i]);
        else if (e != 0)
            klogf("diskcheck: %s: %llu bytes, unreadable (%s)\n", names[i], (ull)size, fs_errno_name(e));
        else
            klogf("diskcheck: %s: %llu bytes, CRC32 0x%08x\n", names[i], (ull)size, crc);
        dt_drop(n);
    }
    klogf("diskcheck: DONE (3 names looked up)\n");
}

/* The volume's free blocks and inodes now (under the VFS lock). */
static void dt_free_counts(uint32_t *blocks, uint32_t *inodes)
{
    fd_vfs_acquire();
    const struct ext2_wfs *w = ext2_disk_wfs();
    *blocks = w ? w->fs.free_blocks : 0;
    *inodes = w ? w->fs.free_inodes : 0;
    fd_vfs_release();
}

void disktest_churn(void)
{
    if (!ext2_disk_rw()) {
        klogf("diskchurn: FAILED (no read-write volume on /home)\n");
        return;
    }
    struct dt_hold h = { 0, 0 };
    struct ext2_disk_figs f0, f1;
    uint32_t b0 = 0, i0 = 0, b1 = 0, i1 = 0, syncs = 0, done = 0;
    uint64_t blocks = 0;
    const char *fail = 0;
    dt_free_counts(&b0, &i0);
    ext2_disk_figures(&f0);
    for (uint32_t i = 0; i < DT_CHURN && !fail; i++) {
        char path[32];
        ksnprintf(path, sizeof(path), "/home/churn-%04u", i);
        if (dt_pattern_file(path, 100u + i, 1000u + (i % 7u) * 512u, &h) != 0)
            fail = "a file could not be written";
        else if (dt_unlink(path) != 0)
            fail = "a file could not be removed";
        else if ((i + 1u) % DT_CHURN_SAVE == 0) {
            syncs++;
            if (ext2_disk_sync(&blocks) != EXT2_DISK_SAVED)
                fail = "a sync failed";
        }
        done += fail ? 0u : 1u;
    }
    if (!fail && ext2_disk_sync(&blocks) != EXT2_DISK_SAVED)
        fail = "the last sync failed";
    dt_free_counts(&b1, &i1);
    ext2_disk_figures(&f1);
    klogf("diskchurn: %u cycles (create, write 1000 to 4072 bytes, close, delete), a sync every %u (%u syncs) and one "
          "at the end; free blocks %u -> %u, free inodes %u -> %u; %llu saves, %llu writes, %llu flushes; the "
          "longest VFS lock hold in a save %llu us (%llu ticks) (report only)\n", done, DT_CHURN_SAVE, syncs, b0, b1,
          i0, i1, (ull)(f1.saves - f0.saves), (ull)(f1.writes - f0.writes), (ull)(f1.flushes - f0.flushes),
          (ull)ext2_disk_us(f1.hold_cycles), (ull)f1.hold_ticks);
    if (fail)
        klogf("diskchurn: FAILED (%s)\n", fail);
    else if (b1 != b0 || i1 != i0)
        klogf("diskchurn: FAILED (the free counts did not come back to their start)\n");
    else
        klogf("diskchurn: DONE (%u cycles; the free blocks and inodes back to their start)\n", done);
}

/* ---- 1.5 stage 4: test=diskcontend ---- */

#define DT_CT_WRITE     32768u                  /* bigio's writer: 8 writes of 32 KiB */
#define DT_CT_WRITES    8u
#define DT_CT_FILE      (DT_CT_WRITE * DT_CT_WRITES)

struct dt_contend {
    volatile bool stop;                 /* the saver ends */
    volatile bool running;              /* the program has not ended yet */
    uint32_t saves, during, failed;     /* syncs; those that ended while the program ran; failed ones */
};

static struct dt_contend dt_ct;

/* The saver: a sync whenever something is dirty or pending (each logs its
 * "ext2: sync:" line), else a yield. Holds neither lock between syncs;
 * ext2_write_busy reads two counters only (a stale answer costs one more
 * sync or one more yield). */
static int32_t dt_ct_saver(void *arg)
{
    struct dt_contend *c = arg;
    const struct ext2_wfs *w = ext2_disk_wfs();
    while (!c->stop) {
        if (!w || !ext2_write_busy(w)) {
            thread_yield();
            continue;
        }
        uint64_t blocks = 0;
        enum ext2_disk_save s = ext2_disk_sync(&blocks);
        c->saves++;
        c->during += c->running ? 1u : 0u;
        c->failed += s != EXT2_DISK_SAVED;
    }
    return 0;
}

/* The file's 8 blocks of 32 KiB: block c holds writer 1's write c, each
 * 8-byte word (1 << 56) | (c << 48) | its index in the write. */
static bool dt_ct_check(struct vnode *n, uint32_t *blocks)
{
    *blocks = 0;
    for (uint32_t c = 0; c < DT_CT_WRITES; c++) {
        for (uint32_t off = 0; off < DT_CT_WRITE; off += DT_CHUNK) {
            fd_vfs_acquire();
            int64_t got = vfs_read(vfs_root(), n, (uint64_t)c * DT_CT_WRITE + off, dt_buf, DT_CHUNK);
            fd_vfs_release();
            if (got != (int64_t)DT_CHUNK)
                return false;
            for (uint32_t i = 0; i < DT_CHUNK / 8u; i++) {
                uint64_t word = 0;
                for (uint32_t b = 0; b < 8u; b++)
                    word |= (uint64_t)dt_buf[i * 8u + b] << (8u * b);
                if (word != ((1ull << 56) | ((uint64_t)c << 48) | (off / 8u + i)))
                    return false;
            }
        }
        (*blocks)++;
    }
    return true;
}

void disktest_contend(void)
{
    if (!ext2_disk_rw()) {
        klogf("diskcontend: FAILED (no read-write volume on /home)\n");
        return;
    }
    const char *path = "/home/contend.bin";
    struct dt_contend *c = &dt_ct;
    struct vnode *n = 0;
    uint32_t file = 0xFFFFFFFFu;
    struct ext2_disk_figs f0, f1;
    ext2_disk_figures(&f0);
    if (dt_make(path, VNODE_FILE, &n) != 0) {
        klogf("diskcontend: FAILED (the file could not be made)\n");
        return;
    }
    fd_vfs_acquire();
    int ro = fd_kernel_open(n, OF_WRITE | OF_APPEND, &file);
    fd_vfs_release();
    if (ro < 0) {
        dt_drop(n);
        klogf("diskcontend: FAILED (the file could not be opened)\n");
        return;
    }
    c->stop = false;
    c->running = true;
    c->saves = c->during = c->failed = 0;
    int32_t saver = thread_create("dt-saver", dt_ct_saver, c, false);
    struct fd_grant g = { 3, file };
    int32_t pid = saver < 0 ? -1 : process_start_fds("bigio", 9, &g, 1);
    struct proc_result res = { PROC_END_FAULT, -1 };
    enum proc_status st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
    c->running = false;
    c->stop = true;
    if (saver >= 0)
        thread_join(saver);
    fd_kernel_close(file);
    uint64_t blocks = 0;
    bool last = ext2_disk_sync(&blocks) == EXT2_DISK_SAVED;
    bool exited = st == PROC_OK && res.end == PROC_END_EXIT && res.code == 0;
    uint32_t good = 0;
    fd_vfs_acquire();
    uint64_t size = n->size;
    fd_vfs_release();
    bool words = size == DT_CT_FILE && dt_ct_check(n, &good);
    dt_drop(n);
    ext2_disk_figures(&f1);
    struct disklock_figs d;
    disklock_figures(&d);
    uint64_t vfs_cmds = blk_vfs_commands();
    klogf("diskcontend: a program (bigio, writer 1) appended %u writes of 32 KiB to %s through an O_APPEND open file "
          "while a kernel thread saved whenever something was dirty: %u saves (%u ended while the program ran), %u "
          "failed; the program %s (exit status %d); %llu saves an operation needed (report only)\n", DT_CT_WRITES,
          path, c->saves, c->during, c->failed, exited ? "finished" : "did NOT finish normally", (int)res.code,
          (ull)(f1.retries - f0.retries));
    klogf("diskcontend: the file holds %llu bytes, %u blocks of 32 KiB each one write's words in order; the last sync: "
          "%s; write or FLUSH commands sent while the VFS lock was held, since boot: %llu; the disk lock: %llu takes, "
          "%llu waited, the longest hold %llu ticks, the longest wait %llu ticks (thread 0: %llu) (report only)\n",
          (ull)size, good, last ? "saved" : "NOT saved", (ull)vfs_cmds, (ull)d.takes, (ull)d.contended,
          (ull)d.hold_longest, (ull)d.wait_longest, (ull)d.wait_longest0);
    const char *fail = saver < 0 ? "no saver thread" : pid <= 0 ? "the program did not start"
                     : !exited ? "the program did not finish normally" : c->failed ? "a save failed"
                     : !c->during ? "no save ended while the program ran" : !words ? "the file is not exact"
                     : !last ? "the last sync failed" : vfs_cmds ? "a write or FLUSH was sent under the VFS lock" : 0;
    if (fail)
        klogf("diskcontend: FAILED (%s)\n", fail);
    else
        klogf("diskcontend: DONE (the program and the saves both finished; the file exact; nothing sent under the VFS "
              "lock)\n");
}
