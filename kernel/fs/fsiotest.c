/* The file system call diagnostics (see fsiotest.h). */
#include <stddef.h>
#include <stdint.h>
#include "kernel/fs/fsiotest.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/process.h"
#include "kernel/proc/syscall.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

#define FT_WAIT  300u                   /* ticks allowed for any one wait */
#define NO_FILE  0xFFFFFFFFu
#define MIB      (1024u * 1024u)

static struct vfs *v;
static struct ramfs *rfs;
static const char *tag;
static unsigned bad_lines;
static uint64_t kbuf[FD_CHUNK / 8];     /* static: not on thread 0's stack */
static char name[64];

static bool line(bool ok, const char *text)
{
    klogf("%s: %s: %s\n", tag, text, ok ? "OK" : "MISMATCH");
    if (!ok)
        bad_lines++;
    return ok;
}

/* ---- files, made and checked by the kernel (one piece per hold) ---- */

static struct vnode *find(const char *path)
{
    struct vnode *n = 0;
    fd_vfs_acquire();
    int r = vfs_lookup(v, v->root, path, &n);
    fd_vfs_release();
    return r == 0 ? n : 0;
}

static bool make_dir(const char *path)
{
    fd_vfs_acquire();
    int r = vfs_create(v, v->root, path, VNODE_DIR, 0755, 0);
    fd_vfs_release();
    return r == 0;
}

/* A file of `size` bytes holding, at each multiple of 8, that offset as a
 * 64-bit word; NULL if it cannot be made. */
static struct vnode *make_file(const char *path, uint64_t size)
{
    struct vnode *n = 0;
    fd_vfs_acquire();
    int r = vfs_create(v, v->root, path, VNODE_FILE, 0644, &n);
    fd_vfs_release();
    if (r < 0)
        return 0;
    for (uint64_t off = 0; off < size; off += FD_CHUNK) {
        uint32_t len = fd_chunk(off, size);
        for (uint32_t i = 0; i < FD_CHUNK / 8; i++)
            kbuf[i] = off + 8 * i;
        fd_vfs_acquire();
        int64_t put = ramfs_write(rfs, n, off, kbuf, len);
        fd_vfs_release();
        if (put != (int64_t)len)
            return 0;
    }
    return n;
}

static void remove_file(const char *path)
{
    fd_vfs_acquire();
    vfs_unlink(v, v->root, path);
    fd_vfs_release();
}

static void remove_dir(const char *path)
{
    fd_vfs_acquire();
    vfs_rmdir(v, v->root, path);
    fd_vfs_release();
}

static uint32_t kopen(struct vnode *n, uint8_t access)
{
    uint32_t file = NO_FILE;
    if (!n)
        return NO_FILE;
    fd_vfs_acquire();
    if (fd_kernel_open(n, access, &file) < 0)
        file = NO_FILE;
    fd_vfs_release();
    return file;
}

static void kclose(uint32_t file)
{
    if (file != NO_FILE)
        fd_kernel_close(file);
}

/* ---- processes ---- */

static int32_t start(const char *image, uint64_t arg, uint32_t fd3)
{
    struct fd_grant g = { 3, fd3 };
    return process_start_fds(image, arg, &g, fd3 != NO_FILE ? 1 : 0);
}

static struct proc_result wait_for(int32_t pid, enum proc_status *st)
{
    struct proc_result res = { PROC_END_FAULT, -1 };
    *st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
    return res;
}

static bool exited(int32_t pid, enum proc_status st, struct proc_result r, int32_t code)
{
    return pid > 0 && st == PROC_OK && r.end == PROC_END_EXIT && r.code == code;
}

/* Thread 0 yields until cond(arg) holds, at most FT_WAIT ticks (it may
 * hold a reserved turn of the VFS lock meanwhile, never the lock itself:
 * a holder never yields, stage 7b). */
static bool yield_until(bool (*cond)(uint64_t), uint64_t arg)
{
    uint64_t t0 = timer_ticks();
    while (!cond(arg)) {
        if (timer_ticks() - t0 > FT_WAIT)
            return false;
        thread_yield();
    }
    return true;
}

static uint64_t opens_made(void)
{
    struct syscall_counts c;
    syscall_counts(&c);
    return c.by_id[SC_OPEN];
}

static bool opens_reached(uint64_t n)
{
    return opens_made() >= n;
}

/* One program run: its result line ("<extra>" before the verdict). */
static bool run_case(const char *image, uint64_t arg, const char *what, uint32_t fd3,
                     bool (*after)(char *buf, size_t size))
{
    enum proc_status st;
    int32_t pid = start(image, arg, fd3);
    struct proc_result res = wait_for(pid, &st);
    char extra[200] = "";
    bool extra_ok = after ? after(extra, sizeof(extra)) : true;
    char text[320];
    ksnprintf(text, sizeof(text), "%s %u (%s): process %d %s, status %d (expected 0)%s", image,
              (unsigned)arg, what, (int)pid, proc_end_str(res.end), (int)res.code, extra);
    return line(exited(pid, st, res, 0) && extra_ok, text);
}

struct snapshot {
    uint32_t open, nodes, data, heap;
};

static void snap(struct snapshot *s)
{
    s->open = fd_open_files();
    s->nodes = rfs->nodes;
    s->data = rfs->data_pages;
    s->heap = kheap_stats()->used_bytes;
}

/* The closing line: open files, nodes, data pages, heap bytes, processes
 * and the VFS lock back to the start. */
static void cleanup_line(const struct snapshot *s0)
{
    while (thread_reap())
        ;
    struct snapshot s1;
    snap(&s1);
    struct klock *l = vfs_lock();
    uint32_t left = process_live();
    bool lock_free = l->owner == KLOCK_FREE && l->next == l->serving;
    char text[260];
    ksnprintf(text, sizeof(text), "cleanup: open files %u -> %u, nodes %u -> %u, data pages %u -> "
              "%u, heap in use %u -> %u bytes, processes left %u, the VFS lock %s", s0->open,
              s1.open, s0->nodes, s1.nodes, s0->data, s1.data, s0->heap, s1.heap, left,
              lock_free ? "free with no ticket out" : "NOT free");
    line(s1.open == s0->open && s1.nodes == s0->nodes && s1.data == s0->data &&
         s1.heap == s0->heap && left == 0 && lock_free, text);
}

static bool finish(void)
{
    bool ok = bad_lines == 0 && diag_wait_failures() == 0;
    klogf("%s: %s\n", tag, ok ? "PASS" : "FAIL");
    return ok;
}

static void init(const char *t)
{
    tag = t;
    v = vfs_root();
    rfs = vfs_ramfs();
    bad_lines = 0;
}

/* ================= test=fileio ================= */

static bool after_times(char *b, size_t n)
{
    struct vnode *f = find("/tmp/fio-e");
    int64_t m = 0, c = 0;
    if (f) {
        fd_vfs_acquire();
        m = f->mtime;
        c = f->ctime;
        fd_vfs_release();
    }
    int64_t boot = vfs_boot_epoch(), now = vfs_now();
    ksnprintf(b, n, "; times between the boot time and now (boot %lld, mtime %lld, ctime %lld, "
              "now %lld)", (long long)boot, (long long)m, (long long)c, (long long)now);
    return f && boot > 946684800 && m >= boot && m <= now && c >= boot && c <= now;
}

static void fileio_emfile(void)
{
    uint32_t before = fd_open_files();
    uint64_t o0 = opens_made();
    int32_t pid = start("fileio", 10, NO_FILE);
    bool seen = pid > 0 && yield_until(opens_reached, o0 + 14);
    uint32_t during = fd_open_files();
    enum proc_status st;
    struct proc_result r = wait_for(pid, &st);
    char text[320];
    ksnprintf(text, sizeof(text), "fileio 10 (EMFILE from open): process %d %s, status %d "
              "(expected 0); open files while it held 13 files and the console: %u (%u before + "
              "15): the refused open took none", (int)pid, proc_end_str(r.end), (int)r.code,
              during, before);
    line(exited(pid, st, r, 0) && seen && during == before + 15, text);
}

static void fileio_enfile(void)
{
    static uint32_t held[FILE_MAX];
    uint32_t before = fd_open_files(), taken = 0;
    uint64_t o0 = opens_made();
    preempt_disable();
    int32_t pid = start("fileio", 11, NO_FILE);    /* takes its 2 console files now */
    while (pid > 0 && fd_open_files() < FILE_MAX && taken < FILE_MAX) {
        if (fd_kernel_console(&held[taken]) < 0)
            break;
        taken++;
    }
    uint32_t full = fd_open_files();
    preempt_enable();
    bool refused = pid > 0 && yield_until(opens_reached, o0 + 2);
    uint32_t still = fd_open_files();
    bool not_created = find("/tmp/fio-new") == 0;
    if (taken)
        kclose(held[--taken]);                      /* one free: its open goes through */
    enum proc_status st;
    struct proc_result r = wait_for(pid, &st);
    for (uint32_t i = 0; i < taken; i++)
        kclose(held[i]);
    uint32_t back = fd_open_files();
    char text[400];
    ksnprintf(text, sizeof(text), "ENFILE: the open-file table filled to %u (%u taken by the "
              "kernel); open refused twice (also O_CREAT on a new name), still %u, /tmp/fio-new "
              "%s; one freed, then the open succeeded at fd 3 (fileio 11: process %d %s, status "
              "%d, expected 0); all given back (%u -> %u)", full, taken + 1, still,
              not_created ? "not created" : "CREATED", (int)pid, proc_end_str(r.end),
              (int)r.code, before, back);
    line(exited(pid, st, r, 0) && full == FILE_MAX && refused && still == FILE_MAX &&
         not_created && back == before, text);
}

static void fileio_enospc(void)
{
    bool ok = make_dir("/tmp/fio-nodes");
    uint32_t made = 0;
    while (ok && rfs->nodes < RAMFS_MAX_NODES) {
        ksnprintf(name, sizeof(name), "/tmp/fio-nodes/n%u", made);
        fd_vfs_acquire();
        int r = vfs_create(v, v->root, name, VNODE_FILE, 0644, 0);
        fd_vfs_release();
        if (r < 0)
            break;
        made++;
    }
    uint32_t full = rfs->nodes;
    enum proc_status st;
    int32_t pid = start("fileio", 12, NO_FILE);
    struct proc_result r = wait_for(pid, &st);
    bool not_created = find("/tmp/fio-x") == 0;
    uint32_t after = rfs->nodes;
    for (uint32_t i = 0; i < made; i++) {
        ksnprintf(name, sizeof(name), "/tmp/fio-nodes/n%u", i);
        remove_file(name);
    }
    remove_dir("/tmp/fio-nodes");
    char text[360];
    ksnprintf(text, sizeof(text), "ENOSPC: %u nodes in use (the limit %u; %u made by the "
              "kernel); open with O_CREAT ENOSPC, /tmp/fio-x %s, nodes still %u; no descriptor "
              "taken, an existing file opened at fd 3 (fileio 12: process %d %s, status %d, "
              "expected 0)", full, RAMFS_MAX_NODES, made + 1,
              not_created ? "not created" : "CREATED", after, (int)pid, proc_end_str(r.end),
              (int)r.code);
    line(exited(pid, st, r, 0) && full == RAMFS_MAX_NODES && not_created &&
         after == RAMFS_MAX_NODES, text);
}

static const char *const fileerr_what[] = {
    "", "open", "stat and lstat", "fstat", "openat and newfstatat with a dirfd", "lseek",
    "ftruncate",
};

bool fsiotest_fileio(void)
{
    init("File test");
    struct snapshot s0;
    snap(&s0);
    run_case("fileio", 1, "open, write, close, reopen, read back", NO_FILE, 0);
    run_case("fileio", 2, "O_EXCL, O_TRUNC, O_CLOEXEC, unknown flags", NO_FILE, 0);
    run_case("fileio", 3, "O_APPEND", NO_FILE, 0);
    run_case("fileio", 4, "lseek", NO_FILE, 0);
    run_case("fileio", 5, "the stat calls agree on a file", NO_FILE, after_times);
    bool dirs = make_dir("/tmp/fio-sub1") && make_dir("/tmp/fio-sub2");
    if (!dirs)
        line(false, "folders: /tmp/fio-sub1 and -sub2 could not be made");
    run_case("fileio", 6, "stat on folders", NO_FILE, 0);
    remove_dir("/tmp/fio-sub1");
    remove_dir("/tmp/fio-sub2");
    run_case("fileio", 7, "ftruncate", NO_FILE, 0);
    run_case("fileio", 8, "openat and newfstatat", NO_FILE, 0);
    run_case("fileio", 9, "the console descriptors", NO_FILE, 0);
    fileio_emfile();

    /* The errno matrix: nothing may change. */
    if (!make_file("/tmp/ferr-f", 100))
        line(false, "files: /tmp/ferr-f could not be made");
    struct snapshot e0, e1;
    snap(&e0);
    for (uint64_t g = 1; g <= 6; g++)
        run_case("fileerr", g, fileerr_what[g], NO_FILE, 0);
    while (thread_reap())
        ;
    snap(&e1);
    char text[260];
    ksnprintf(text, sizeof(text), "errno matrix: nothing changed: nodes %u -> %u, data pages %u -> "
              "%u, open files %u -> %u, heap in use %u -> %u bytes", e0.nodes, e1.nodes, e0.data,
              e1.data, e0.open, e1.open, e0.heap, e1.heap);
    line(e1.nodes == e0.nodes && e1.data == e0.data && e1.open == e0.open && e1.heap == e0.heap,
         text);

    fileio_enfile();
    fileio_enospc();

    static const char *const files[] = {
        "/tmp/fio-a", "/tmp/fio-b", "/tmp/fio-c", "/tmp/fio-d", "/tmp/fio-e", "/tmp/fio-f",
        "/tmp/ferr-f",
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
        remove_file(files[i]);
    cleanup_line(&s0);
    return finish();
}

/* ================= test=bigio ================= */

static uint64_t trunc_case;              /* truncate holds of bigio 5 */

static bool after_limit(char *b, size_t n)
{
    struct vnode *f = find("/tmp/bio-l");
    uint64_t size = f ? f->size : 0;
    uint32_t pages = f ? ramfs_file_pages(f) : 0;
    ksnprintf(b, n, "; the file holds %llu bytes in %u pages", (unsigned long long)size, pages);
    return size == 2 * MIB && pages == 513;
}

static bool after_cross(char *b, size_t n)
{
    struct vnode *f = find("/tmp/bio-l");
    uint32_t pages = f ? ramfs_file_pages(f) : 99;
    ksnprintf(b, n, "; then truncated to 0 in %llu holds of at most %u pages, %u pages left",
              (unsigned long long)trunc_case, FSYS_TRUNC_STEP, pages);
    return pages == 0 && trunc_case == 9;
}

/* Fill files under /tmp/bio-fill until exactly `left` budget pages are
 * left; *files: how many were made. */
static bool fill_budget(uint32_t left, uint32_t *files)
{
    *files = 0;
    if (!make_dir("/tmp/bio-fill"))
        return false;
    uint32_t avail = rfs->budget_pages - rfs->data_pages;
    if (avail < left + 2)
        return false;
    uint32_t take = avail - left;
    for (uint32_t i = 0; i < FD_CHUNK / 8; i++)
        kbuf[i] = i;
    while (take) {
        uint32_t k = take - 1 <= RAMFS_SLOTS ? take - 1 : RAMFS_SLOTS;
        if (take - (k + 1) == 1)
            k--;                                    /* never leave a single page */
        ksnprintf(name, sizeof(name), "/tmp/bio-fill/f%u", *files);
        struct vnode *f = 0;
        fd_vfs_acquire();
        int r = vfs_create(v, v->root, name, VNODE_FILE, 0644, &f);
        fd_vfs_release();
        if (r < 0)
            return false;
        (*files)++;
        for (uint32_t j = 0; j < k; j++) {
            fd_vfs_acquire();
            int64_t put = ramfs_write(rfs, f, (uint64_t)j * RAMFS_PAGE, kbuf, RAMFS_PAGE);
            fd_vfs_release();
            if (put != RAMFS_PAGE)
                return false;
        }
        take -= k + 1;
    }
    return rfs->budget_pages - rfs->data_pages == left;
}

static void unfill(uint32_t files)
{
    for (uint32_t i = 0; i < files; i++) {
        ksnprintf(name, sizeof(name), "/tmp/bio-fill/f%u", i);
        remove_file(name);
    }
    remove_dir("/tmp/bio-fill");
}

static uint32_t budget_left_before;

static bool after_budget(char *b, size_t n)
{
    uint32_t left = rfs->budget_pages - rfs->data_pages;
    ksnprintf(b, n, "; %u pages left before it, %u after", budget_left_before, left);
    return budget_left_before == 5 && left == 0;
}

/* ---- two writers ---- */

static struct vnode *app;
static int32_t w1_tid;

static bool w1_queued(uint64_t unused)
{
    (void)unused;
    struct klock *l = vfs_lock();
    preempt_disable();
    bool ok = fsys_wmark_holder(app) == w1_tid && l->next - l->serving >= 2;
    preempt_enable();
    return ok;
}

static bool w2_queued(uint64_t unused)
{
    (void)unused;
    preempt_disable();
    bool ok = fsys_wmark_waiting(app) >= 2;
    preempt_enable();
    return ok;
}

static void bigio_writers(void)
{
    struct fd_figures fg0;
    fd_figures(&fg0);                               /* the write marks' figures before */
    app = make_file("/tmp/bio-app", 0);
    uint32_t f1 = kopen(app, OF_WRITE | OF_APPEND), f2 = kopen(app, OF_WRITE | OF_APPEND);
    bool q1 = false, q2 = false;
    int32_t p1 = -1, p2 = -1;
    if (app && f1 != NO_FILE && f2 != NO_FILE) {
        fd_vfs_test_reserve();                      /* thread 0's turn while they queue */
        p1 = start("bigio", 9, f1);
        w1_tid = p1 > 0 ? process_tid(p1) : -1;
        q1 = p1 > 0 && yield_until(w1_queued, 0);
        if (q1) {
            p2 = start("bigio", 10, f2);
            q2 = p2 > 0 && yield_until(w2_queued, 0);
        }
        fd_vfs_test_take();
        fd_vfs_release();
    }
    enum proc_status s1, s2;
    struct proc_result r1 = wait_for(p1, &s1), r2 = wait_for(p2, &s2);
    kclose(f1);
    kclose(f2);

    /* Each 32 KiB block: one write's words, in order; each writer's 8
     * writes in order. */
    uint64_t size = app ? app->size : 0;
    uint32_t blocks = 0, interleaved = 0, per[3] = { 0, 0, 0 };
    uint64_t next_c[3] = { 0, 0, 0 };
    bool order = true;
    for (uint64_t blk = 0; app && blk < size / 32768; blk++) {
        uint64_t w = 0, c = 0;
        bool whole = true;
        for (uint64_t off = 0; off < 32768; off += FD_CHUNK) {
            fd_vfs_acquire();
            int64_t got = ramfs_read(rfs, app, blk * 32768 + off, kbuf, FD_CHUNK);
            fd_vfs_release();
            if (got != FD_CHUNK) {
                whole = false;
                break;
            }
            for (uint32_t i = 0; i < FD_CHUNK / 8; i++) {
                uint64_t word = kbuf[i];
                if (off == 0 && i == 0) {
                    w = word >> 56;
                    c = (word >> 48) & 0xFF;
                }
                if (word != ((w << 56) | (c << 48) | (off / 8 + i)))
                    whole = false;
            }
        }
        blocks++;
        if (!whole || (w != 1 && w != 2)) {
            interleaved++;
            continue;
        }
        per[w]++;
        if (c != next_c[w])
            order = false;
        next_c[w] = c + 1;
    }
    /* The marks' figures are fd.c's, for every node: only these two
     * writers write at the same time in test=bigio. */
    struct fd_figures fg1;
    fd_figures(&fg1);
    uint64_t contended = fg1.wmark_contended - fg0.wmark_contended;
    uint32_t ahead = fg1.wmark_ahead_max;
    char text[400];
    ksnprintf(text, sizeof(text), "two writers: 2 processes appended 8 writes of 32 KiB each "
              "through separate O_APPEND open files (statuses %d, %d): the file holds %llu bytes, "
              "%u blocks of 32 KiB, each one write's (writer 1: %u, writer 2: %u, each in order: "
              "%s), %u interleaved; the write mark waited on %llu times (forced), most tickets "
              "ahead %u", (int)r1.code, (int)r2.code, (unsigned long long)size, blocks, per[1],
              per[2], order ? "yes" : "NO", interleaved, (unsigned long long)contended, ahead);
    line(q1 && q2 && exited(p1, s1, r1, 0) && exited(p2, s2, r2, 0) && size == 524288 &&
         blocks == 16 && per[1] == 8 && per[2] == 8 && order && interleaved == 0 &&
         contended >= 1, text);
}

bool fsiotest_bigio(void)
{
    init("Big I/O test");
    struct snapshot s0;
    snap(&s0);
    fd_figures_reset();
    run_case("bigio", 1, "one 61440-byte write and one 61440-byte read", NO_FILE, 0);
    run_case("bigio", 2, "a 1 MiB file in 32 writes and 32 reads of 32 KiB", NO_FILE, 0);
    run_case("bigio", 3, "the 2 MiB limit in 64 writes of 32 KiB", NO_FILE, after_limit);
    run_case("bigio", 4, "one more byte at 2 MiB", NO_FILE, 0);
    struct fd_figures fg;
    fd_figures(&fg);
    uint64_t t0 = fg.trunc_holds;
    enum proc_status st;
    int32_t pid = start("bigio", 5, NO_FILE);
    struct proc_result r = wait_for(pid, &st);
    fd_figures(&fg);
    trunc_case = fg.trunc_holds - t0;
    char extra[200] = "";
    bool eok = after_cross(extra, sizeof(extra));
    char text[400];
    ksnprintf(text, sizeof(text), "bigio 5 (a write crossing 2 MiB): process %d %s, status %d "
              "(expected 0)%s", (int)pid, proc_end_str(r.end), (int)r.code, extra);
    line(exited(pid, st, r, 0) && eok, text);
    remove_file("/tmp/bio-l");

    uint32_t fill_files = 0;
    bool filled = fill_budget(5, &fill_files);
    budget_left_before = rfs->budget_pages - rfs->data_pages;
    if (!filled)
        line(false, "budget: could not leave exactly 5 pages");
    run_case("bigio", 6, "the budget", NO_FILE, after_budget);
    remove_file("/tmp/bio-b");
    unfill(fill_files);

    run_case("bigio", 7, "a write whose buffer turns unmapped after 6000 bytes", NO_FILE, 0);
    run_case("bigio", 8, "a read whose buffer turns unmapped after 6000 bytes", NO_FILE, 0);
    bigio_writers();

    fd_figures(&fg);
    ksnprintf(text, sizeof(text), "VFS lock holds of the chunk loop and the truncates: %llu holds "
              "(%llu chunks, %llu truncate holds; the 2 MiB file truncated to 0 in %llu holds of "
              "at most %u pages), longest hold %llu ticks (at most 2)",
              (unsigned long long)fg.holds, (unsigned long long)fg.chunks,
              (unsigned long long)fg.trunc_holds, (unsigned long long)trunc_case,
              FSYS_TRUNC_STEP, (unsigned long long)fg.hold_longest);
    line(fg.chunks > 0 && trunc_case == 9 && fg.hold_longest <= 2, text);
    fd_work_line(tag);                              /* stage 7b, report only */

    static const char *const files[] = {
        "/tmp/bio-1", "/tmp/bio-m", "/tmp/bio-e", "/tmp/bio-app",
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
        remove_file(files[i]);
    cleanup_line(&s0);
    return finish();
}

/* ================= test=filecontend ================= */

#define SHARERS 4
#define WORKERS 4
#define SPINNERS 2
#define OTHERS 6                        /* other runnable threads: 3 workers, 2 spinners, thread 0 */
#define TURN_MAX 6                      /* policy (b) */
/* Ticks: a timeout for thread 0's loop, which ends when the workers have
 * finished (raised from 6000 so their finish under load is measured, not
 * cut off). */
#define FC_LIMIT 15000u

static uint32_t share_file;
static int32_t share_tid;

static bool s1_queued(uint64_t unused)
{
    (void)unused;
    struct ofile o;
    fd_file_info(share_file, &o);
    struct klock *l = vfs_lock();
    return o.busy_owner == share_tid && l->next - l->serving >= 2;
}

static bool sk_queued(uint64_t k)
{
    struct ofile o;
    fd_file_info(share_file, &o);
    return o.busy_next - o.busy_serving >= k;
}

static void contend_sharers(void)
{
    struct vnode *fs = make_file("/tmp/fc-share", MIB);
    share_file = kopen(fs, OF_READ);
    int32_t ps[SHARERS];
    bool queued = share_file != NO_FILE;
    fd_figures_reset();
    fd_vfs_test_reserve();                          /* the first holds the mark, the rest queue */
    for (int k = 0; k < SHARERS; k++) {
        ps[k] = queued ? start("bigio", 11, share_file) : -1;
        if (k == 0) {
            share_tid = ps[0] > 0 ? process_tid(ps[0]) : -1;
            queued = queued && ps[0] > 0 && yield_until(s1_queued, 0);
        } else {
            queued = queued && ps[k] > 0 && yield_until(sk_queued, (uint64_t)k + 1);
        }
    }
    fd_vfs_test_take();
    fd_vfs_release();
    int32_t code[SHARERS];
    bool fifo = queued;
    for (int k = 0; k < SHARERS; k++) {
        enum proc_status st;
        struct proc_result r = wait_for(ps[k], &st);
        code[k] = r.code;
        fifo = fifo && ps[k] > 0 && st == PROC_OK && r.end == PROC_END_EXIT && r.code == k;
    }
    struct ofile o = { 0 };
    if (share_file != NO_FILE)
        fd_file_info(share_file, &o);
    struct fd_figures fg;
    fd_figures(&fg);
    kclose(share_file);
    remove_file("/tmp/fc-share");
    char text[400];
    ksnprintf(text, sizeof(text), "busy mark: %d sharers, %d forced to queue behind the first (most "
              "tickets ahead %u); first reads at %d, %d, %d, %d (x 4096, in start order); %llu "
              "takes, final offset %llu, after waiting %llu, longest wait %llu ticks", SHARERS,
              SHARERS - 1, (unsigned)o.busy_ahead_max, (int)code[0], (int)code[1], (int)code[2],
              (int)code[3], (unsigned long long)o.busy_takes, (unsigned long long)o.offset,
              (unsigned long long)o.busy_contended, (unsigned long long)fg.busy_wait_longest);
    line(fifo && o.busy_ahead_max == SHARERS - 1 && o.busy_takes == SHARERS * 64 &&
         o.offset == MIB && o.busy_contended >= SHARERS - 1, text);
}

static int32_t wk[WORKERS];

/* The three plain workers (the last, the truncating one, is not waited
 * for here): thread 0's loop contends while they run, then stops, so the
 * truncating worker's 2 MiB rounds finish without thread 0's
 * yield-while-holding holds (reserved turns since stage 7b; stage 5's
 * second filecontend fix: those holds, not the workers' own, set the
 * test's length). */
static bool plain_workers_ended(void)
{
    for (int i = 0; i < WORKERS - 1; i++)
        if (wk[i] > 0 && !process_ended(wk[i]))
            return false;
    return true;
}

static void contend_load(void)
{
    struct klock *lk = vfs_lock();
    struct vnode *probe = make_file("/tmp/fc-probe", FD_CHUNK);
    char text[600], who[80];

    klock_init(lk);                                 /* the lock's figures from here on */
    fd_figures_reset();
    thread_turn_reset();
    uint64_t checks0 = klock_thread0_checks();
    int32_t sp[SPINNERS], sp_tid[SPINNERS], wtid[WORKERS];
    for (int i = 0; i < SPINNERS; i++) {
        sp[i] = process_start("spin", 0);
        sp_tid[i] = sp[i] > 0 ? process_tid(sp[i]) : -1;
    }
    for (int i = 0; i < WORKERS; i++) {
        wk[i] = start("bigio", i == WORKERS - 1 ? 13 : 12, NO_FILE);
        wtid[i] = wk[i] > 0 ? process_tid(wk[i]) : -1;
    }
    uint64_t t_begin = timer_ticks(), iters = 0;
    uint64_t y_holds = 0, y_ticked = 0, y_longest = 0;  /* thread 0's reserved turns */
    struct diag_site s;
    diag_wait_begin(&s, FC_LIMIT);
    while (probe && !plain_workers_ended() && diag_wait_poll(&s)) {
        /* Stage 7b: every 4th time a reserved turn (it was a yield while
         * holding): the workers queue behind it while thread 0 yields,
         * then it takes the lock; a holder never yields. */
        bool reserved = (iters & 3) == 0;
        uint64_t h0;
        if (reserved) {
            fd_vfs_test_reserve();
            h0 = timer_ticks();
            thread_yield();                         /* not holding: the workers queue behind */
            fd_vfs_test_take();
        } else {
            fd_vfs_acquire();
            h0 = timer_ticks();
        }
        ramfs_read(rfs, probe, 0, kbuf, FD_CHUNK);
        uint64_t held = timer_ticks() - h0;
        fd_vfs_release();
        if (reserved) {
            y_holds++;
            if (held >= 1)
                y_ticked++;
            if (held > y_longest)
                y_longest = held;
        }
        iters++;
    }
    bool at_limit = !plain_workers_ended();
    uint64_t duration = timer_ticks() - t_begin;
    /* The workers finish under the same load: the spinners run until they
     * have (a blocking wait, the documented exception of the bounded
     * waits: the workers end by themselves). */
    int32_t code[WORKERS];
    bool workers_ok = probe != 0;
    for (int i = 0; i < WORKERS; i++) {
        enum proc_status st;
        struct proc_result r = wait_for(wk[i], &st);
        code[i] = r.end == PROC_END_EXIT ? r.code : -1;
        workers_ok = workers_ok && exited(wk[i], st, r, 0);
    }
    uint64_t finished = timer_ticks() - t_begin;
    uint64_t margin = finished < FC_LIMIT ? (FC_LIMIT - finished) * 100 / FC_LIMIT : 0;
    uint64_t spin_ticks[SPINNERS];
    for (int i = 0; i < SPINNERS; i++) {
        spin_ticks[i] = sp_tid[i] >= 0 ? thread_ticks_run(sp_tid[i]) : 0;
        if (sp[i] > 0)
            process_kill(sp[i]);
    }
    bool spinners_killed = true;
    for (int i = 0; i < SPINNERS; i++) {
        enum proc_status st;
        struct proc_result r = wait_for(sp[i], &st);
        spinners_killed = spinners_killed && st == PROC_OK && r.end == PROC_END_KILLED;
    }
    klogf("%s: timing: the workers finished after %llu ticks, the loop's limit %u ticks (margin "
          "%llu%%); thread 0's loop ended after %llu ticks, %s\n", tag,
          (unsigned long long)finished, FC_LIMIT, (unsigned long long)margin,
          (unsigned long long)duration,
          at_limit ? "AT ITS LIMIT, before the three plain workers finished"
                   : "when the three plain workers finished");

    /* The figures. */
    uint64_t hold = lk->hold_longest, yields0 = lk->yields0;
    uint32_t ahead0 = lk->ahead_max0;
    uint64_t checks = klock_thread0_checks() - checks0;
    struct fd_figures fg;
    fd_figures(&fg);
    struct fd_handoff ho;
    fd_handoffs(&ho);
    uint64_t waited[WORKERS], wlong[WORKERS], waited0, wlong0, wmax = 0;
    bool all_waited = true;
    for (int i = 0; i < WORKERS; i++) {
        fd_thread_lock(wtid[i], &waited[i], &wlong[i]);
        all_waited = all_waited && waited[i] >= 1;
        if (wlong[i] > wmax)
            wmax = wlong[i];
    }
    fd_thread_lock(0, &waited0, &wlong0);
    uint64_t turn = thread_turn_max();
    thread_turn_owner(who, sizeof(who));

    ksnprintf(text, sizeof(text), "contention: %d workers and thread 0 contended for the VFS lock "
              "for %llu ticks with %d ring-3 spinners running: statuses %d %d %d %d (0: every "
              "worker's 256 KiB written and read back identical; the last also truncated 2 MiB "
              "to 0 twice); takes after waiting per worker %llu %llu %llu %llu, thread 0 %llu",
              WORKERS, (unsigned long long)duration, SPINNERS, (int)code[0], (int)code[1],
              (int)code[2], (int)code[3], (unsigned long long)waited[0],
              (unsigned long long)waited[1], (unsigned long long)waited[2],
              (unsigned long long)waited[3], (unsigned long long)waited0);
    line(workers_ok && all_waited && waited0 >= 1, text);

    /* Stage 7b: work holds run with preemption off: at most 2 ticks, none
     * spanning a switch; thread 0's reserved turns (a yield inside) keep
     * the convoy bound. */
    uint64_t hold_bound = 2 + (uint64_t)TURN_MAX * OTHERS;
    ksnprintf(text, sizeof(text), "holds under load: work holds (chunk loop, truncates, lseek) "
              "%llu, %llu of 1 tick or more, %llu spanning a switch, longest %llu ticks (at most "
              "2: preemption off while held); truncate holds %llu (longest %llu ticks); thread "
              "0's reserved turns %llu, %llu of 1 tick or more, longest %llu ticks (at most %llu: "
              "2 + 6 x %d other threads); longest of all holds %llu ticks (at most %llu)",
              (unsigned long long)fg.holds, (unsigned long long)fg.holds_ticked,
              (unsigned long long)fg.holds_switched, (unsigned long long)fg.hold_longest,
              (unsigned long long)fg.trunc_holds, (unsigned long long)fg.trunc_hold_longest,
              (unsigned long long)y_holds, (unsigned long long)y_ticked,
              (unsigned long long)y_longest, (unsigned long long)hold_bound, OTHERS,
              (unsigned long long)hold, (unsigned long long)hold_bound);
    line(fg.hold_longest <= 2 && fg.trunc_hold_longest <= 2 && fg.holds_switched == 0 &&
         y_longest <= hold_bound && hold <= hold_bound && y_holds > 0 && fg.trunc_holds >= 18,
         text);

    uint64_t gap_bound = (uint64_t)TURN_MAX * OTHERS;
    uint64_t bound0 = (uint64_t)ahead0 * (hold + 1 + gap_bound) + 1;
    uint64_t bound_w = (uint64_t)WORKERS * (hold + 1 + gap_bound) + 1;
    int32_t gt = ho.longest_tid;
    ksnprintf(text, sizeof(text), "waits: %llu hand-offs, %llu gaps of 1 tick or more, longest gap "
              "%llu ticks (thread %d '%s'), at most %llu (6 x %d other threads); longest waits: "
              "thread 0 %llu ticks (bound %llu: %u tickets ahead x (longest hold %llu + 1 + %llu) "
              "+ 1), workers %llu %llu %llu %llu ticks (bound %llu: %d tickets ahead); %llu wait "
              "steps of thread 0, all by yielding, its self-check at each",
              (unsigned long long)ho.count, (unsigned long long)ho.ticked,
              (unsigned long long)ho.longest, (int)gt, gt >= 0 ? thread_name(gt) : "none",
              (unsigned long long)gap_bound, OTHERS, (unsigned long long)wlong0,
              (unsigned long long)bound0, ahead0, (unsigned long long)hold,
              (unsigned long long)gap_bound, (unsigned long long)wlong[0],
              (unsigned long long)wlong[1], (unsigned long long)wlong[2],
              (unsigned long long)wlong[3], (unsigned long long)bound_w, WORKERS,
              (unsigned long long)yields0);
    line(ho.count > 0 && ho.longest <= gap_bound && wlong0 <= bound0 && wmax <= bound_w &&
         yields0 > 0 && checks == yields0, text);

    ksnprintf(text, sizeof(text), "longest turn: %llu ticks (at most %d), taken by %s; the "
              "spinners ran %llu and %llu ticks, killed at the end", (unsigned long long)turn,
              TURN_MAX, who, (unsigned long long)spin_ticks[0], (unsigned long long)spin_ticks[1]);
    line(turn <= TURN_MAX && spin_ticks[0] >= 1 && spin_ticks[1] >= 1 && spinners_killed, text);

    klogf("%s: figures: duration %llu, finished %llu, margin %llu, holds %llu, ticked %llu, "
          "switched %llu, work hold %llu, reserved turn %llu, all holds %llu, truncate holds %llu, "
          "longest truncate hold %llu, hand-offs %llu, gaps ticked %llu, longest gap %llu, "
          "thread 0 wait %llu, worker wait %llu, turn %llu, thread 0 loop %llu\n", tag,
          (unsigned long long)duration, (unsigned long long)finished,
          (unsigned long long)margin, (unsigned long long)fg.holds,
          (unsigned long long)fg.holds_ticked, (unsigned long long)fg.holds_switched,
          (unsigned long long)fg.hold_longest, (unsigned long long)y_longest,
          (unsigned long long)hold, (unsigned long long)fg.trunc_holds,
          (unsigned long long)fg.trunc_hold_longest, (unsigned long long)ho.count,
          (unsigned long long)ho.ticked, (unsigned long long)ho.longest,
          (unsigned long long)wlong0, (unsigned long long)wmax, (unsigned long long)turn,
          (unsigned long long)iters);
    fd_work_line(tag);                              /* stage 7b, report only */

    for (int i = 0; i < WORKERS; i++) {
        if (wk[i] > 0) {
            ksnprintf(name, sizeof(name), "/tmp/fc-%d", (int)wk[i]);
            remove_file(name);
        }
    }
    remove_file("/tmp/fc-big");
    remove_file("/tmp/fc-probe");
}

bool fsiotest_contend(void)
{
    init("File contention test");
    struct snapshot s0;
    snap(&s0);
    contend_sharers();
    contend_load();
    cleanup_line(&s0);
    return finish();
}
