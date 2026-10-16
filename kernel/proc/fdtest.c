/* Descriptor diagnostics (see fdtest.h). */
#include <stddef.h>
#include <stdint.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/usercopy.h"
#include "kernel/memory/uspace.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/panic.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/fdtest.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/process.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

#define FT_WAIT  300u                   /* ticks allowed for any one wait */
#define NO_FILE  0xFFFFFFFFu

static struct vfs *v;
static struct ramfs *rfs;
static uint64_t kbuf[FD_CHUNK / 8];     /* static: not on thread 0's stack */
static unsigned bad_lines;

static bool line(const char *t, bool ok, const char *text)
{
    klogf("%s: %s: %s\n", t, text, ok ? "OK" : "MISMATCH");
    if (!ok)
        bad_lines++;
    return ok;
}

/* ---- files, made and checked by the kernel (one chunk per hold) ---- */

/* A file of `size` bytes holding, at each multiple of 8, that offset as a
 * 64-bit word (the pattern fdops checks); NULL if it cannot be made. */
static struct vnode *make_file(const char *path, uint64_t size)
{
    struct vnode *n = 0;
    klock_acquire(vfs_lock());
    int r = vfs_create(v, v->root, path, VNODE_FILE, 0644, &n);
    klock_release(vfs_lock());
    if (r < 0)
        return 0;
    for (uint64_t off = 0; off < size; off += FD_CHUNK) {
        uint32_t len = fd_chunk(off, size);
        for (uint32_t i = 0; i < FD_CHUNK / 8; i++)
            kbuf[i] = off + 8 * i;
        klock_acquire(vfs_lock());
        int64_t put = ramfs_write(rfs, n, off, kbuf, len);
        klock_release(vfs_lock());
        if (put != (int64_t)len)
            return 0;
    }
    return n;
}

static void remove_file(const char *path)
{
    klock_acquire(vfs_lock());
    vfs_unlink(v, v->root, path);
    klock_release(vfs_lock());
}

/* An open file of the kernel's on node n: its index, or NO_FILE. */
static uint32_t kopen(struct vnode *n, uint8_t access)
{
    uint32_t file = NO_FILE;
    if (!n)
        return NO_FILE;
    klock_acquire(vfs_lock());
    if (fd_kernel_open(n, access, &file) < 0)
        file = NO_FILE;
    klock_release(vfs_lock());
    return file;
}

static void kclose(uint32_t file)
{
    if (file != NO_FILE)
        fd_kernel_close(file);
}

static uint64_t offset_of(uint32_t file)
{
    struct ofile o;
    if (file == NO_FILE)
        return ~0ull;
    fd_file_info(file, &o);
    return o.offset;
}

/* fdops case 11's bytes: the file holds exactly `size` bytes i*13+5. */
static bool written_ok(struct vnode *n, uint64_t size)
{
    uint8_t *b = (uint8_t *)kbuf;
    if (!n || n->size != size)
        return false;
    for (uint64_t off = 0; off < size; off += FD_CHUNK) {
        uint32_t len = fd_chunk(off, size);
        klock_acquire(vfs_lock());
        int64_t got = ramfs_read(rfs, n, off, b, len);
        klock_release(vfs_lock());
        if (got != (int64_t)len)
            return false;
        for (uint32_t i = 0; i < len; i++)
            if (b[i] != (uint8_t)((off + i) * 13 + 5))
                return false;
    }
    return true;
}

/* ---- processes ---- */

/* fdops or fdhold with fd 3 and fd 4 given (NO_FILE: none). */
static int32_t start(const char *image, uint64_t arg, uint32_t fd3, uint32_t fd4)
{
    struct fd_grant g[2] = { { 0, 0 }, { 0, 0 } };
    uint32_t n = 0;
    if (fd3 != NO_FILE)
        g[n++] = (struct fd_grant){ 3, fd3 };
    if (fd4 != NO_FILE)
        g[n++] = (struct fd_grant){ 4, fd4 };
    return process_start_fds(image, arg, g, n);
}

static struct proc_result wait_for(int32_t pid, enum proc_status *st)
{
    struct proc_result res = { PROC_END_FAULT, -1 };
    *st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
    return res;
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

/* ---- test=fdsys ---- */

static const char *const sys_t = "Descriptor test";

/* One fdops run: its result line ("<extra>" before the verdict) and
 * whether it exited with `want` and extra_ok held. */
static bool sys_case(uint64_t arg, const char *what, uint32_t fd3, uint32_t fd4, int32_t want,
                     bool (*after)(char *buf, size_t size))
{
    enum proc_status st;
    int32_t pid = start("fdops", arg, fd3, fd4);
    struct proc_result res = wait_for(pid, &st);
    char extra[120] = "";
    bool extra_ok = after ? after(extra, sizeof(extra)) : true;
    bool ok = pid > 0 && st == PROC_OK && res.end == PROC_END_EXIT && res.code == want && extra_ok;
    char text[240];
    ksnprintf(text, sizeof(text), "fdops %u (%s): process %d %s, status %d (expected %d)%s",
              (unsigned)arg, what, (int)pid, proc_end_str(res.end), (int)res.code, (int)want,
              extra);
    return line(sys_t, ok, text);
}

static struct vnode *f_read, *f_write;
static uint32_t c_file, c_file2;        /* the open files the current case was given */

static bool after_read(char *b, size_t n)
{
    uint64_t off = offset_of(c_file);
    ksnprintf(b, n, "; offset %llu", (unsigned long long)off);
    return off == 10000;
}

static bool after_write(char *b, size_t n)
{
    bool ok = written_ok(f_write, 9000);
    ksnprintf(b, n, "; the file holds %llu bytes%s",
              (unsigned long long)(f_write ? f_write->size : 0),
              ok ? ", each as written" : ", NOT as written");
    return ok;
}

static bool after_share(char *b, size_t n)
{
    uint64_t off = offset_of(c_file);
    ksnprintf(b, n, "; the shared offset %llu", (unsigned long long)off);
    return off == 200;
}

static bool after_access(char *b, size_t n)
{
    uint64_t o3 = offset_of(c_file), o4 = offset_of(c_file2);
    uint64_t sr = f_read ? f_read->size : 0, sw = f_write ? f_write->size : 0;
    bool ok = o3 == 0 && o4 == 0 && sr == 10000 && written_ok(f_write, 9000);
    ksnprintf(b, n, "; both files unchanged (offsets %llu, %llu; sizes %llu and %llu)",
              (unsigned long long)o3, (unsigned long long)o4, (unsigned long long)sr,
              (unsigned long long)sw);
    return ok;
}

static bool after_efault(char *b, size_t n)
{
    uint64_t off = offset_of(c_file);
    ksnprintf(b, n, "; offset %llu (unchanged by the 3 EFAULT reads)", (unsigned long long)off);
    return off == 100;
}

/* The busy mark's conditions, polled by thread 0 holding the VFS lock. */
static uint32_t busy_file;
static int32_t busy_tid;

static bool a_queued(uint64_t unused)
{
    (void)unused;
    struct ofile o;
    fd_file_info(busy_file, &o);
    struct klock *l = vfs_lock();
    return o.busy_owner == busy_tid && l->next - l->serving >= 2;
}

static bool b_queued(uint64_t unused)
{
    (void)unused;
    struct ofile o;
    fd_file_info(busy_file, &o);
    return o.busy_next - o.busy_serving >= 2;
}

#define BUSY_A   40960u                 /* fdops 15's read: 10 chunks */
#define SHARERS  3
#define SHARE_RD 64u                    /* fdops 17's reads of 4096 bytes */

static bool sys_busy(struct vnode *fb, struct vnode *fs)
{
    /* Two readers of one open file: thread 0 holds a reserved turn of the
     * VFS lock (stage 7b; it held the lock itself before) until A holds
     * the busy mark and waits for the lock, and B waits for the mark;
     * then A's call (10 chunks) ends before B's begins. */
    busy_file = kopen(fb, OF_READ);
    bool queued_a = false, queued_b = false;
    int32_t pa = -1, pb = -1;
    klock_test_reserve(vfs_lock());
    if (busy_file != NO_FILE) {
        pa = start("fdops", 15, busy_file, NO_FILE);
        busy_tid = pa > 0 ? process_tid(pa) : -1;
        queued_a = pa > 0 && yield_until(a_queued, 0);
        if (queued_a) {
            pb = start("fdops", 16, busy_file, NO_FILE);
            queued_b = pb > 0 && yield_until(b_queued, 0);
        }
    }
    klock_test_take(vfs_lock());
    klock_release(vfs_lock());
    enum proc_status sa, sb;
    struct proc_result ra = wait_for(pa, &sa), rb = wait_for(pb, &sb);
    struct ofile o = { 0 };
    if (busy_file != NO_FILE)
        fd_file_info(busy_file, &o);
    bool two_ok = queued_a && queued_b && sa == PROC_OK && sb == PROC_OK &&
                  ra.end == PROC_END_EXIT && ra.code == 0 && rb.end == PROC_END_EXIT &&
                  rb.code == (int32_t)(BUSY_A / FD_CHUNK) && o.busy_contended >= 1 &&
                  o.offset == BUSY_A + FD_CHUNK;
    uint64_t waits_b = o.busy_waits;
    kclose(busy_file);

    /* Three sharers reading in turn (FIFO): the fairness figures. */
    uint32_t sf = kopen(fs, OF_READ);
    int32_t ps[SHARERS];
    for (int i = 0; i < SHARERS; i++)
        ps[i] = sf != NO_FILE ? start("fdops", 17, sf, NO_FILE) : -1;
    bool share_ok = sf != NO_FILE;
    for (int i = 0; i < SHARERS; i++) {
        enum proc_status s;
        struct proc_result r = wait_for(ps[i], &s);
        share_ok = share_ok && s == PROC_OK && r.end == PROC_END_EXIT && r.code == 0;
    }
    struct ofile so = { 0 };
    so.busy_ahead_max = 99;
    if (sf != NO_FILE)
        fd_file_info(sf, &so);
    kclose(sf);
    struct fd_figures fg;
    fd_figures(&fg);
    uint64_t total = (uint64_t)SHARERS * SHARE_RD * FD_CHUNK;
    share_ok = share_ok && so.offset == total && so.busy_takes == SHARERS * SHARE_RD &&
               so.busy_ahead_max <= SHARERS - 1;
    char text[400];
    ksnprintf(text, sizeof(text), "busy mark: the second read waited (%llu wait steps) until the "
              "first call (%u bytes, %u chunks) ended, then read at its end offset (status %d, "
              "in 4096-byte units); %d sharers: %u reads, final offset %llu, takes %llu, after "
              "waiting %llu, most tickets ahead %u (at most %d), longest wait %llu ticks",
              (unsigned long long)waits_b, BUSY_A, BUSY_A / FD_CHUNK, (int)rb.code, SHARERS,
              SHARERS * SHARE_RD, (unsigned long long)so.offset,
              (unsigned long long)so.busy_takes, (unsigned long long)so.busy_contended,
              (unsigned)so.busy_ahead_max, SHARERS - 1,
              (unsigned long long)fg.busy_wait_longest);
    return line(sys_t, two_ok && share_ok, text);
}

static const struct {
    uint64_t arg;
    const char *what;
} console_cases[] = {
    { 0, "fds 0, 1, 2 at start" },
    { 1, "close" },
    { 2, "dup returns the lowest free fd" },
    { 3, "dup2" },
    { 4, "EMFILE" },
    { 5, "console short count through a dup" },
};

bool fdtest_sys(void)
{
    v = vfs_root();
    rfs = vfs_ramfs();
    bad_lines = 0;
    uint32_t open0 = fd_open_files(), nodes0 = rfs->nodes, data0 = rfs->data_pages;
    uint32_t heap0 = kheap_stats()->used_bytes;

    for (size_t i = 0; i < sizeof(console_cases) / sizeof(console_cases[0]); i++)
        sys_case(console_cases[i].arg, console_cases[i].what, NO_FILE, NO_FILE, 0, 0);

    f_read = make_file("/tmp/fdsys-r", 10000);
    f_write = make_file("/tmp/fdsys-w", 0);
    struct vnode *fb = make_file("/tmp/fdsys-b", BUSY_A + FD_CHUNK);
    struct vnode *fs = make_file("/tmp/fdsys-s", (uint64_t)SHARERS * SHARE_RD * FD_CHUNK);
    if (!f_read || !f_write || !fb || !fs)
        line(sys_t, false, "files: /tmp/fdsys-r, -w, -b, -s could not be made");

    c_file = kopen(f_read, OF_READ);
    sys_case(10, "read 10000 bytes through an inherited fd", c_file, NO_FILE, 0, after_read);
    kclose(c_file);
    c_file = kopen(f_write, OF_READ | OF_WRITE);
    sys_case(11, "write 9000 bytes through an inherited O_RDWR fd", c_file, NO_FILE, 0,
             after_write);
    kclose(c_file);
    c_file = kopen(f_read, OF_READ);
    sys_case(12, "dup'd fds share one offset", c_file, NO_FILE, 0, after_share);
    kclose(c_file);
    c_file = kopen(f_read, OF_READ);
    c_file2 = kopen(f_write, OF_WRITE);
    sys_case(13, "write to a read-only fd, read from a write-only fd", c_file, c_file2, 0,
             after_access);
    kclose(c_file);
    kclose(c_file2);
    c_file = kopen(f_read, OF_READ);
    sys_case(14, "reads into bad buffers", c_file, NO_FILE, 0, after_efault);
    kclose(c_file);

    sys_busy(fb, fs);

    struct fd_figures fg;
    fd_figures(&fg);
    char text[200];
    ksnprintf(text, sizeof(text), "VFS lock holds of the chunk loop: %llu chunks, longest hold "
              "%llu ticks (at most 2)", (unsigned long long)fg.chunks,
              (unsigned long long)fg.hold_longest);
    line(sys_t, fg.chunks > 0 && fg.hold_longest <= 2, text);

    remove_file("/tmp/fdsys-r");
    remove_file("/tmp/fdsys-w");
    remove_file("/tmp/fdsys-b");
    remove_file("/tmp/fdsys-s");
    while (thread_reap())
        ;
    struct klock *l = vfs_lock();
    uint32_t open1 = fd_open_files(), nodes1 = rfs->nodes, data1 = rfs->data_pages;
    uint32_t heap1 = kheap_stats()->used_bytes, left = process_live();
    bool lock_free = l->owner == KLOCK_FREE && l->next == l->serving;
    ksnprintf(text, sizeof(text), "cleanup: open files %u -> %u, nodes %u -> %u, data pages "
              "%u -> %u, heap in use %u -> %u bytes, processes left %u, the VFS lock %s",
              open0, open1, nodes0, nodes1, data0, data1, heap0, heap1, left,
              lock_free ? "free with no ticket out" : "NOT free");
    line(sys_t, open1 == open0 && nodes1 == nodes0 && data1 == data0 && heap1 == heap0 &&
                left == 0 && lock_free, text);
    bool ok = bad_lines == 0 && diag_wait_failures() == 0;
    klogf("%s: %s\n", sys_t, ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=fdkill ---- */

static const char *const kill_t = "Descriptor kill test";

#define GROUP 8

static bool full_fds(uint64_t pid)
{
    return process_open_fds((int32_t)pid) == (int32_t)FD_MAX;
}

static uint64_t closed_by_teardowns(void)
{
    struct fd_figures fg;
    fd_figures(&fg);
    return fg.teardown_closed;
}

/* A group of GROUP fdhold <arg> processes with fd 3 = file; `kill`: each
 * is killed once its table is full. True if each ended as `end` with
 * `code`; *closed: descriptors their teardowns closed. */
static bool group(uint64_t arg, uint32_t file, bool kill, enum proc_end end, int32_t code,
                  uint64_t *closed, void (*before_kill)(void))
{
    uint64_t c0 = closed_by_teardowns();
    int32_t pid[GROUP];
    bool ok = true;
    for (int i = 0; i < GROUP; i++)
        pid[i] = start("fdhold", arg, file, NO_FILE);
    if (kill) {
        for (int i = 0; i < GROUP; i++)
            ok = ok && pid[i] > 0 && yield_until(full_fds, (uint64_t)pid[i]);
        if (before_kill)
            before_kill();
        for (int i = 0; i < GROUP; i++)
            if (pid[i] > 0)
                process_kill(pid[i]);
    }
    for (int i = 0; i < GROUP; i++) {
        enum proc_status st;
        struct proc_result r = wait_for(pid[i], &st);
        ok = ok && st == PROC_OK && r.end == end && r.code == code;
    }
    *closed = closed_by_teardowns() - c0;
    return ok;
}

/* The unlinked file: its name removed while the killed group holds it
 * (its node is read only until the group is killed: then it is freed). */
static uint32_t u_file;
static struct vnode *u_node;
static uint32_t u_nodes_after_unlink, u_opens, u_nlink;
static void unlink_u(void)
{
    remove_file("/tmp/fdkill-u");
    kclose(u_file);                     /* the kernel's own reference */
    klock_acquire(vfs_lock());
    u_nodes_after_unlink = rfs->nodes;
    u_opens = u_node ? u_node->opens : 0;
    u_nlink = u_node ? u_node->nlink : 1;
    klock_release(vfs_lock());
}

static uint32_t read_file;
static int32_t read_tid;

static bool reader_queued(uint64_t unused)
{
    (void)unused;
    struct ofile o;
    fd_file_info(read_file, &o);
    struct klock *l = vfs_lock();
    return o.busy_owner == read_tid && l->next - l->serving >= 2;
}

#define READ48 (48u * 1024)

bool fdtest_kill(void)
{
    v = vfs_root();
    rfs = vfs_ramfs();
    bad_lines = 0;
    char text[300];
    uint32_t open0 = fd_open_files(), nodes0 = rfs->nodes, data0 = rfs->data_pages;
    uint32_t heap0 = kheap_stats()->used_bytes;

    struct vnode *fu = make_file("/tmp/fdkill-u", FD_CHUNK);
    struct vnode *ff = make_file("/tmp/fdkill-f", FD_CHUNK);
    struct vnode *fr = make_file("/tmp/fdkill-r", READ48);
    if (!fu || !ff || !fr)
        line(kill_t, false, "files: /tmp/fdkill-u, -f, -r could not be made");
    u_node = fu;
    u_file = kopen(fu, OF_READ);
    uint32_t f_file = kopen(ff, OF_READ);
    read_file = kopen(fr, OF_READ);
    uint32_t u_pages = fu ? ramfs_file_pages(fu) : 0;
    uint32_t nodes_u = rfs->nodes, data_u = rfs->data_pages;

    struct process_teardown_totals t0, t1;
    process_teardown_totals(&t0);
    uint32_t free0 = pmm_free_pages(), tables0 = paging_stats()->page_tables;
    uint32_t stacks0 = thread_stacks_in_use(), live0 = thread_live();
    uint32_t spaces0 = uspace_live_count;

    /* Killed in ring 3 with 16 descriptors each; the unlinked file's last
     * holders. */
    uint64_t closed = 0;
    bool ok = group(0, u_file, true, PROC_END_KILLED, 0, &closed, unlink_u);
    ksnprintf(text, sizeof(text), "killed: %d programs holding 16 descriptors each (an inherited "
              "file among them) killed in ring 3; %llu descriptors closed by their teardowns",
              GROUP, (unsigned long long)closed);
    line(kill_t, ok && closed == GROUP * FD_MAX, text);
    bool u_ok = u_nodes_after_unlink == nodes_u && u_nlink == 0 && u_opens == 1 &&
                rfs->nodes == nodes_u - 1 && rfs->data_pages == data_u - u_pages;
    ksnprintf(text, sizeof(text), "unlinked file: alive after its unlink while the killed "
              "programs held it (nodes %u -> %u, nlink %u, held by %u open file), freed at "
              "their last close (nodes %u, data pages %u -> %u)", nodes_u, u_nodes_after_unlink,
              u_nlink, u_opens, rfs->nodes, data_u, rfs->data_pages);
    line(kill_t, u_ok, text);

    ok = group(1, f_file, false, PROC_END_FAULT, 14, &closed, 0);
    ksnprintf(text, sizeof(text), "fault: %d programs ended by a page fault with 16 descriptors "
              "open; %llu descriptors closed by their teardowns", GROUP,
              (unsigned long long)closed);
    line(kill_t, ok && closed == GROUP * FD_MAX, text);

    ok = group(2, f_file, false, PROC_END_EXIT, 0, &closed, 0);
    ksnprintf(text, sizeof(text), "exit: %d programs exiting without closing their 16 "
              "descriptors; %llu descriptors closed by their teardowns", GROUP,
              (unsigned long long)closed);
    line(kill_t, ok && closed == GROUP * FD_MAX, text);

    /* Killed before its first entry to ring 3 (as test=killrace's mode 0). */
    uint64_t c0 = closed_by_teardowns();
    preempt_disable();
    int32_t pid = start("fdhold", 4, f_file, NO_FILE);
    bool never_ran = pid > 0 && process_user_returns(pid) == 0;
    enum proc_status ks = pid > 0 ? process_kill(pid) : PROC_ERR_NO_SUCH;
    preempt_enable();
    enum proc_status st;
    struct proc_result r = wait_for(pid, &st);
    closed = closed_by_teardowns() - c0;
    ksnprintf(text, sizeof(text), "kill before the first entry to ring 3: process %d %s; %llu "
              "descriptors closed (the 3 console fds and the inherited file)", (int)pid,
              proc_end_str(r.end), (unsigned long long)closed);
    line(kill_t, never_ran && ks == PROC_OK && st == PROC_OK && r.end == PROC_END_KILLED &&
                 closed == 4, text);

    /* A kill requested between the first and the second chunk of a 48 KiB
     * read: thread 0 holds a reserved turn of the VFS lock (stage 7b; it
     * held the lock itself before) until the program holds the busy mark
     * and waits for the lock, then takes the lock, gives it to the program
     * and queues right behind it (klock_requeue), so it has the lock again
     * after exactly one chunk. */
    c0 = closed_by_teardowns();
    klock_test_reserve(vfs_lock());
    pid = read_file != NO_FILE ? start("fdhold", 3, read_file, NO_FILE) : -1;
    read_tid = pid > 0 ? process_tid(pid) : -1;
    bool queued = pid > 0 && yield_until(reader_queued, 0);
    uint64_t at_kill = ~0ull;
    klock_test_take(vfs_lock());
    if (queued) {
        klock_requeue(vfs_lock());
        at_kill = offset_of(read_file);
        ks = process_kill(pid);
    }
    klock_release(vfs_lock());
    r = wait_for(pid, &st);
    closed = closed_by_teardowns() - c0;
    uint64_t at_end = offset_of(read_file);
    unsigned chunk = (unsigned)(at_kill / FD_CHUNK);
    ksnprintf(text, sizeof(text), "kill during a read: requested between chunk %u and chunk %u "
              "of %u (offset %llu of %u at the kill); the read completed (offset %llu), then "
              "process %d %s; %llu descriptors closed", chunk, chunk + 1, READ48 / FD_CHUNK,
              (unsigned long long)at_kill, READ48, (unsigned long long)at_end, (int)pid,
              proc_end_str(r.end), (unsigned long long)closed);
    line(kill_t, queued && ks == PROC_OK && at_kill == FD_CHUNK && at_end == READ48 &&
                 st == PROC_OK && r.end == PROC_END_KILLED && closed == 4, text);

    /* The open-file table filled by the kernel: ENFILE, nothing changed.
     * Its own two files closed first: no process runs now, so the kernel
     * takes all 256. */
    kclose(f_file);
    kclose(read_file);
    static uint32_t held[FILE_MAX];
    uint32_t before = fd_open_files(), taken = 0;
    bool fill_ok = true;
    while (fd_open_files() < FILE_MAX && taken < FILE_MAX) {
        if (fd_kernel_console(&held[taken]) < 0) {
            fill_ok = false;
            break;
        }
        taken++;
    }
    uint32_t extra = 0, full = fd_open_files();
    int enfile = fd_kernel_console(&extra);
    uint32_t after_refusal = fd_open_files();
    for (uint32_t i = 0; i < taken; i++)
        kclose(held[i]);
    if (enfile == 0)
        kclose(extra);
    uint32_t back = fd_open_files();
    ksnprintf(text, sizeof(text), "open-file table: filled to %u (%u taken by the kernel), the "
              "next %s, still %u; all given back (%u -> %u)", full, taken,
              enfile == -FDT_ENFILE ? "ENFILE" : "NOT refused", after_refusal, before, back);
    line(kill_t, fill_ok && taken == FILE_MAX && full == FILE_MAX && enfile == -FDT_ENFILE &&
                 after_refusal == FILE_MAX && back == before, text);

    /* Processes: 3 groups + 2, each torn down by its own thread. */
    while (thread_reap())
        ;
    process_teardown_totals(&t1);
    uint32_t procs = t1.processes - t0.processes, bad = t1.bad_entries - t0.bad_entries;
    uint32_t free1 = pmm_free_pages(), tables1 = paging_stats()->page_tables;
    uint32_t stacks1 = thread_stacks_in_use(), live1 = thread_live(), left = process_live();
    ksnprintf(text, sizeof(text), "%u address spaces torn down by their own threads, %u wrong; "
              "free pages %u -> %u (+%u: the unlinked file's pages), kernel page tables %u -> "
              "%u, stack slots %u -> %u, live threads %u -> %u, live address spaces %u -> %u, "
              "processes left %u", procs, bad, free0, free1, u_pages, tables0, tables1, stacks0,
              stacks1, live0, live1, spaces0, uspace_live_count, left);
    line(kill_t, procs == 3 * GROUP + 2 && bad == 0 && free1 == free0 + u_pages &&
                 tables1 == tables0 && stacks1 == stacks0 && live1 == live0 &&
                 uspace_live_count == spaces0 && left == 0, text);

    /* Nothing leaked. */
    remove_file("/tmp/fdkill-f");
    remove_file("/tmp/fdkill-r");
    struct klock *l = vfs_lock();
    uint32_t open1 = fd_open_files(), nodes1 = rfs->nodes, data1 = rfs->data_pages;
    uint32_t heap1 = kheap_stats()->used_bytes;
    bool lock_free = l->owner == KLOCK_FREE && l->next == l->serving;
    ksnprintf(text, sizeof(text), "leaks: open files %u -> %u, nodes %u -> %u, data pages %u "
              "-> %u, heap in use %u -> %u bytes, the VFS lock %s", open0, open1, nodes0, nodes1,
              data0, data1, heap0, heap1, lock_free ? "free with no ticket out" : "NOT free");
    line(kill_t, open1 == open0 && nodes1 == nodes0 && data1 == data0 && heap1 == heap0 &&
                 lock_free, text);

    ok = bad_lines == 0 && diag_wait_failures() == 0;
    klogf("%s: %s\n", kill_t, ok ? "PASS" : "FAIL");
    return ok;
}

/* ---- test=selfcheckcopylock ---- */

void fdtest_selfcheck_copylock(void)
{
    uint8_t b = 0;
    klock_acquire(vfs_lock());
    copy_to_user(VM_USER_DATA, &b, 1);
    klogf("Self-check test: a user copy under the VFS lock returned (WRONG)\n");
    panic("selfcheckcopylock: the self-check did not fire.");
}

/* ---- test=selfcheckyieldlock (1.0 stage 7b) ---- */

void fdtest_selfcheck_yieldlock(void)
{
    klock_acquire(vfs_lock());
    thread_yield();
    klogf("Self-check test: a yield under the VFS lock returned (WRONG)\n");
    panic("selfcheckyieldlock: the self-check did not fire.");
}
