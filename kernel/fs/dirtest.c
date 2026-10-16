/* The folder call diagnostics (see dirtest.h). */
#include <stddef.h>
#include <stdint.h>
#include "kernel/fs/dirtest.h"
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/path_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/process.h"
#include "kernel/proc/sched_core.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

#define DT_WAIT  300u                   /* ticks allowed for any one wait */
#define NO_FILE  0xFFFFFFFFu
#define MIB      (1024u * 1024u)
#define BIG_PAGES 513u                  /* a 2 MiB file: 512 data pages and its index page */
#define BIG_HOLDS 9u                    /* its pages given back in 9 holds of at most 64 */

static struct vfs *v;
static struct ramfs *rfs;
static const char *tag;
static unsigned bad_lines;
static uint64_t kbuf[FD_CHUNK / 8];     /* static: not on thread 0's stack */
static char name[64];
static uint64_t wait_longest;           /* ticks, the longest bounded wait (yield_until) */

static bool line(bool ok, const char *text)
{
    klogf("%s: %s: %s\n", tag, text, ok ? "OK" : "MISMATCH");
    if (!ok)
        bad_lines++;
    return ok;
}

/* ---- files and folders, made and checked by the kernel ---- */

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

static int32_t start(uint64_t arg, uint32_t fd3)
{
    struct fd_grant g = { 3, fd3 };
    return process_start_fds("dirops", arg, &g, fd3 != NO_FILE ? 1 : 0);
}

static struct proc_result wait_for(int32_t pid, enum proc_status *st)
{
    struct proc_result res = { PROC_END_FAULT, -1 };
    *st = pid > 0 ? process_wait(pid, &res) : PROC_ERR_NO_SUCH;
    return res;
}

static bool ended(int32_t pid, enum proc_status st, struct proc_result r, enum proc_end end,
                  int32_t code)
{
    return pid > 0 && st == PROC_OK && r.end == end && r.code == code;
}

static bool exited(int32_t pid, enum proc_status st, struct proc_result r)
{
    return ended(pid, st, r, PROC_END_EXIT, 0);
}

/* Thread 0 yields until cond(arg) holds, at most DT_WAIT ticks; the
 * longest wait is reported with its margin. */
static bool yield_until(bool (*cond)(uint64_t), uint64_t arg)
{
    uint64_t t0 = timer_ticks();
    bool ok = true;
    while (!cond(arg)) {
        if (timer_ticks() - t0 > DT_WAIT) {
            ok = false;
            break;
        }
        thread_yield();
    }
    uint64_t w = timer_ticks() - t0;
    if (w > wait_longest)
        wait_longest = w;
    return ok;
}

static void waits_line(void)
{
    uint64_t margin = wait_longest < DT_WAIT ? (DT_WAIT - wait_longest) * 100 / DT_WAIT : 0;
    klogf("%s: waits: the longest bounded wait %llu ticks of its %u-tick limit (margin %llu%%)\n",
          tag, (unsigned long long)wait_longest, DT_WAIT, (unsigned long long)margin);
}

/* One program run: its result line. */
static bool run_case(uint64_t arg, const char *what)
{
    enum proc_status st;
    int32_t pid = start(arg, NO_FILE);
    struct proc_result res = wait_for(pid, &st);
    char text[320];
    ksnprintf(text, sizeof(text), "dirops %u (%s): process %d %s, status %d (expected 0)",
              (unsigned)arg, what, (int)pid, proc_end_str(res.end), (int)res.code);
    return line(exited(pid, st, res), text);
}

struct snapshot {
    uint32_t open, nodes, data, heap;
};

static void snap(struct snapshot *s)
{
    while (thread_reap())
        ;
    s->open = fd_open_files();
    s->nodes = rfs->nodes;
    s->data = rfs->data_pages;
    s->heap = kheap_stats()->used_bytes;
}

static void cleanup_line(const struct snapshot *s0)
{
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
    wait_longest = 0;
}

static uint64_t trunc_holds(void)
{
    struct fd_figures fg;
    fd_figures(&fg);
    return fg.trunc_holds;
}

/* "<dir>/e" and three digits of i. */
static const char *entry_name(const char *dir, uint32_t i)
{
    ksnprintf(name, sizeof(name), "%s/e%u%u%u", dir, (unsigned)(i / 100 % 10),
              (unsigned)(i / 10 % 10), (unsigned)(i % 10));
    return name;
}

/* ================= test=dirops ================= */

static void dirops_enospc(void)
{
    bool ok = make_dir("/tmp/dio-nodes");
    uint32_t made = 0;
    while (ok && rfs->nodes < RAMFS_MAX_NODES) {
        ksnprintf(name, sizeof(name), "/tmp/dio-nodes/n%u", made);
        fd_vfs_acquire();
        int r = vfs_create(v, v->root, name, VNODE_FILE, 0644, 0);
        fd_vfs_release();
        if (r < 0)
            break;
        made++;
    }
    uint32_t full = rfs->nodes;
    enum proc_status st;
    int32_t pid = start(17, NO_FILE);
    struct proc_result r = wait_for(pid, &st);
    bool not_created = find("/tmp/do-x") == 0;
    bool kept = find("/tmp/dio-nodes/n0") != 0;
    uint32_t after = rfs->nodes;
    for (uint32_t i = 0; i < made; i++) {
        ksnprintf(name, sizeof(name), "/tmp/dio-nodes/n%u", i);
        remove_file(name);
    }
    remove_dir("/tmp/dio-nodes");
    char text[360];
    ksnprintf(text, sizeof(text), "ENOSPC: %u nodes in use (the limit %u; %u made by the kernel); "
              "mkdir ENOSPC, /tmp/do-x %s, nodes still %u; a rename (and back) still worked "
              "(dirops 17: process %d %s, status %d, expected 0)", full, RAMFS_MAX_NODES,
              made + 1, not_created ? "not created" : "CREATED", after, (int)pid,
              proc_end_str(r.end), (int)r.code);
    line(exited(pid, st, r) && full == RAMFS_MAX_NODES && not_created && kept &&
         after == RAMFS_MAX_NODES, text);
}

static struct vnode *cw3;

static bool cw3_held(uint64_t unused)
{
    (void)unused;
    preempt_disable();
    bool held = cw3->opens >= 1;
    preempt_enable();
    return held;
}

static void dirops_cwds(void)
{
    static const char *const dirs[] = { "/tmp/cw1", "/tmp/cw2", "/tmp/cw3" };
    uint32_t nodes0 = rfs->nodes;
    bool made = make_dir(dirs[0]) && make_dir(dirs[1]) && make_dir(dirs[2]);
    struct vnode *c[3] = { find(dirs[0]), find(dirs[1]), find(dirs[2]) };
    struct fd_figures fg0, fg1;
    fd_figures(&fg0);
    enum proc_status s1, s2, s3;
    int32_t p1 = made ? start(18, NO_FILE) : -1;
    struct proc_result r1 = wait_for(p1, &s1);
    int32_t p2 = made ? start(19, NO_FILE) : -1;
    struct proc_result r2 = wait_for(p2, &s2);
    int32_t p3 = made ? start(20, NO_FILE) : -1;
    cw3 = c[2];
    bool in3 = p3 > 0 && cw3 && yield_until(cw3_held, 0);
    if (p3 > 0)
        process_kill(p3);
    struct proc_result r3 = wait_for(p3, &s3);
    fd_figures(&fg1);
    uint32_t opens[3] = { 99, 99, 99 };
    fd_vfs_acquire();
    for (int i = 0; i < 3; i++)
        if (c[i])
            opens[i] = c[i]->opens;
    fd_vfs_release();
    bool removed = true;
    for (int i = 0; i < 3; i++) {
        fd_vfs_acquire();
        int r = vfs_rmdir(v, v->root, dirs[i]);
        fd_vfs_release();
        removed = removed && r == 0;
    }
    uint32_t nodes1 = rfs->nodes;
    uint64_t dropped = fg1.cwd_dropped - fg0.cwd_dropped;
    char text[420];
    ksnprintf(text, sizeof(text), "current folders: dirops 18 in /tmp/cw1 %s (status %d), dirops "
              "19 in /tmp/cw2 %s (vector %d), dirops 20 in /tmp/cw3 %s; their holds given back "
              "(opens %u %u %u), %llu current folders dropped by their teardowns; the folders "
              "%s; nodes %u -> %u", proc_end_str(r1.end), (int)r1.code, proc_end_str(r2.end),
              (int)r2.code, proc_end_str(r3.end), opens[0], opens[1], opens[2],
              (unsigned long long)dropped, removed ? "removed" : "NOT removed", nodes0, nodes1);
    line(made && exited(p1, s1, r1) && ended(p2, s2, r2, PROC_END_FAULT, 14) && in3 &&
         ended(p3, s3, r3, PROC_END_KILLED, 0) && opens[0] == 0 && opens[1] == 0 &&
         opens[2] == 0 && dropped == 3 && removed && nodes1 == nodes0, text);
}

/* dirops 21 on a folder of 1000 entries; then the holds of the whole test. */
static void dirops_big_and_holds(void)
{
    bool ok = make_dir("/tmp/do-big");
    uint32_t made = 0;
    while (ok && made < 1000) {
        fd_vfs_acquire();
        int r = vfs_create(v, v->root, entry_name("/tmp/do-big", made), VNODE_FILE, 0644, 0);
        fd_vfs_release();
        if (r < 0)
            break;
        made++;
    }
    enum proc_status st;
    int32_t pid = made == 1000 ? start(21, NO_FILE) : -1;
    struct proc_result r = wait_for(pid, &st);
    for (uint32_t i = 0; i < made; i++)
        remove_file(entry_name("/tmp/do-big", i));
    remove_dir("/tmp/do-big");
    struct fd_figures fg;
    fd_figures(&fg);
    struct klock *lk = vfs_lock();
    char text[480];
    ksnprintf(text, sizeof(text), "VFS lock holds: the folder calls and dirops 21's listing of a "
              "folder of %u entries made by the kernel (process %d %s, status %d, expected 0; "
              "1002 records, at most 4096 bytes of records per hold) held the VFS lock %llu "
              "times, longest hold %llu ticks; getdents64 and lseek holds %llu, longest %llu "
              "ticks; each at most 2", made, (int)pid, proc_end_str(r.end), (int)r.code,
              (unsigned long long)lk->takes, (unsigned long long)lk->hold_longest,
              (unsigned long long)fg.holds, (unsigned long long)fg.hold_longest);
    line(made == 1000 && exited(pid, st, r) && lk->hold_longest <= 2 && fg.holds > 0 &&
         fg.hold_longest <= 2, text);
}

/* 1.0 stage 7b (report only): the hold work of the whole test in
 * microseconds, and one adversarial path lookup under the VFS lock (now
 * held with preemption off): a 4092-byte path of 512 steps into the last
 * entry of a folder of 1001 entries, 511 of them followed by '..', so
 * each step scans the whole folder. Madire has no symbolic links, so a
 * lexical '..' simplification before the lookup would remove this case
 * (recorded for the owner's decision; not in stage 7b). */
static char advpath[PATH_MAX_BYTES];

static void dirops_lookup_cost(void)
{
    fd_work_line(tag);
    bool ok = make_dir("/tmp/do-adv");
    uint32_t made = 0;
    while (ok && made < 1000) {
        fd_vfs_acquire();
        int r = vfs_create(v, v->root, entry_name("/tmp/do-adv", made), VNODE_FILE, 0644, 0);
        fd_vfs_release();
        if (r < 0)
            break;
        made++;
    }
    ok = ok && made == 1000 && make_dir("/tmp/do-adv/e99z");
    static const char step[] = "e99z/../";
    size_t p = 0;
    for (unsigned i = 0; i < 511; i++)
        for (size_t j = 0; j < sizeof(step) - 1; j++)
            advpath[p++] = step[j];
    for (size_t j = 0; j < 4; j++)
        advpath[p++] = step[j];
    advpath[p] = '\0';
    struct vnode *dir = find("/tmp/do-adv"), *n = 0;
    uint64_t spent = 0, per = fd_tsc_per_tick(&spent);
    int r = -1;
    uint64_t cycles = 0, ticks = 0;
    if (ok && dir) {
        fd_vfs_acquire();
        uint64_t t0 = timer_ticks(), c0 = fd_tsc();
        r = vfs_lookup(v, dir, advpath, &n);
        cycles = fd_tsc() - c0;
        ticks = timer_ticks() - t0;
        fd_vfs_release();
    }
    klogf("%s: adversarial lookup (report only): a %u-byte path of 512 'e99z' steps (511 "
          "followed by '..') in a folder of %u entries, the folder last: %s in %llu us (%llu "
          "ticks, under the VFS lock); TSC %llu cycles per tick (calibrated over %llu ticks, "
          "limit %u)\n", (unsigned)p, made + 1, r == 0 && n ? "found" : "NOT found",
          (unsigned long long)fd_cycles_us(cycles, per), (unsigned long long)ticks,
          (unsigned long long)per, (unsigned long long)spent, FD_TSC_LIMIT);
    remove_dir("/tmp/do-adv/e99z");
    for (uint32_t i = 0; i < made; i++)
        remove_file(entry_name("/tmp/do-adv", i));
    remove_dir("/tmp/do-adv");
}

static const char *const dirops_what[] = {
    "", "getcwd and chdir moving", "getcwd errors", "chdir errors",
    "relative paths and AT_FDCWD", "mkdir", "mkdirat", "rmdir", "unlink", "unlinkat", "rename",
    "rename errors", "getdents64 listing", "getdents64 small buffers",
    "getdents64 between calls", "getdents64 errors", "name and path limits",
};

bool dirtest_dirops(void)
{
    init("Dir test");
    struct snapshot s0, e0, e1;
    snap(&s0);
    klock_init(vfs_lock());                         /* the lock's figures from here on */
    fd_figures_reset();
    thread_turn_reset();                            /* the stack peaks from here on */
    snap(&e0);
    for (uint64_t g = 1; g <= 16; g++)
        run_case(g, dirops_what[g]);
    snap(&e1);
    char text[260];
    ksnprintf(text, sizeof(text), "errno matrix: nothing changed: nodes %u -> %u, data pages %u -> "
              "%u, open files %u -> %u, heap in use %u -> %u bytes", e0.nodes, e1.nodes, e0.data,
              e1.data, e0.open, e1.open, e0.heap, e1.heap);
    line(e1.nodes == e0.nodes && e1.data == e0.data && e1.open == e0.open && e1.heap == e0.heap,
         text);
    uint32_t peak = thread_peak_max();
    klogf("%s: stack: the highest stack peak of dirops 1-16 %u of %u bytes (%u%% left)\n", tag,
          peak, KSTACK_SIZE, (unsigned)((KSTACK_SIZE - peak) * 100 / KSTACK_SIZE));
    dirops_enospc();
    dirops_cwds();
    dirops_big_and_holds();
    dirops_lookup_cost();                           /* stage 7b, report only */
    waits_line();
    cleanup_line(&s0);
    return finish();
}

/* ================= test=fsunlink ================= */

static void un_open(void)
{
    struct vnode *a = make_file("/tmp/un-a", 10000);
    uint32_t fa = kopen(a, OF_READ | OF_WRITE);
    uint32_t n0 = rfs->nodes, d0 = rfs->data_pages;
    enum proc_status st;
    int32_t pid = fa != NO_FILE ? start(30, fa) : -1;
    struct proc_result r = wait_for(pid, &st);
    uint32_t nlink = 9, pages = 0;
    uint64_t size = 0;
    bool data_ok = false;
    if (a) {
        fd_vfs_acquire();
        nlink = a->nlink;
        size = a->size;
        pages = ramfs_file_pages(a);
        int64_t got = ramfs_read(rfs, a, 10000, kbuf, 100);   /* its first 100 bytes, again */
        fd_vfs_release();
        data_ok = got == 100;
        for (uint32_t i = 0; i < 12; i++)
            data_ok = data_ok && kbuf[i] == 8u * i;
    }
    uint32_t n1 = rfs->nodes, d1 = rfs->data_pages;
    kclose(fa);
    uint32_t n2 = rfs->nodes, d2 = rfs->data_pages;
    char text[420];
    ksnprintf(text, sizeof(text), "unlinked open file: dirops 30 unlinked /tmp/un-a (10000 bytes) "
              "open as its fd 3, read and wrote it (process %d %s, status %d, expected 0); after "
              "it the node lived on the kernel's hold: links %u, size %llu, %u pages, the 100 "
              "bytes it wrote %s, nodes %u -> %u, data pages %u -> %u; freed at the last close: "
              "nodes %u, data pages %u", (int)pid, proc_end_str(r.end), (int)r.code, nlink,
              (unsigned long long)size, pages, data_ok ? "read back" : "WRONG", n0, n1, d0, d1,
              n2, d2);
    line(exited(pid, st, r) && nlink == 0 && size == 10100 && pages == 4 && data_ok &&
         n1 == n0 && d1 == d0 && n2 == n0 - 1 && d2 == d0 - 4, text);
}

static void un_big(void)
{
    struct vnode *b = make_file("/tmp/un-big", 2 * MIB);
    uint32_t pages = b ? ramfs_file_pages(b) : 0;
    uint32_t n0 = rfs->nodes, d0 = rfs->data_pages;
    uint64_t h0 = trunc_holds();
    enum proc_status st;
    int32_t pid = b ? start(31, NO_FILE) : -1;
    struct proc_result r = wait_for(pid, &st);
    uint64_t holds = trunc_holds() - h0;
    char text[320];
    ksnprintf(text, sizeof(text), "unlink of a file not open: dirops 31 unlinked /tmp/un-big (2 MiB, "
              "%u pages; process %d %s, status %d, expected 0): its pages given back in %llu "
              "holds of at most %u; nodes %u -> %u, data pages %u -> %u", pages, (int)pid,
              proc_end_str(r.end), (int)r.code, (unsigned long long)holds, FSYS_TRUNC_STEP, n0,
              rfs->nodes, d0, rfs->data_pages);
    line(exited(pid, st, r) && pages == BIG_PAGES && holds == BIG_HOLDS &&
         rfs->nodes == n0 - 1 && rfs->data_pages == d0 - BIG_PAGES, text);
}

static void un_last_close(void)
{
    struct vnode *b = make_file("/tmp/un-big2", 2 * MIB);
    uint32_t fb = kopen(b, OF_READ);
    uint32_t n0 = rfs->nodes, d0 = rfs->data_pages;
    enum proc_status st;
    int32_t pid = fb != NO_FILE ? start(32, fb) : -1;
    remove_file("/tmp/un-big2");
    kclose(fb);                                     /* not the last: the program holds it */
    uint32_t d_kclose = rfs->data_pages;
    uint64_t h0 = trunc_holds();
    bool go = make_dir("/tmp/un-go");
    struct proc_result r = wait_for(pid, &st);
    uint64_t holds = trunc_holds() - h0;
    remove_dir("/tmp/un-go");
    char text[400];
    ksnprintf(text, sizeof(text), "last close by a program: /tmp/un-big2 (2 MiB) unlinked and the "
              "kernel's hold closed while dirops 32 held it as fd 3 (data pages still %u, %u "
              "before); its close(3) freed it (process %d %s, status %d, expected 0): pages given "
              "back in %llu holds of at most %u; nodes %u -> %u, data pages %u -> %u", d_kclose,
              d0, (int)pid, proc_end_str(r.end), (int)r.code, (unsigned long long)holds,
              FSYS_TRUNC_STEP, n0, rfs->nodes, d0, rfs->data_pages);
    line(exited(pid, st, r) && go && d_kclose == d0 && holds == BIG_HOLDS &&
         rfs->nodes == n0 - 1 && rfs->data_pages == d0 - BIG_PAGES, text);
}

static void un_teardown(void)
{
    struct vnode *bk = make_file("/tmp/un-k", 2 * MIB), *bp = make_file("/tmp/un-p", 2 * MIB);
    uint32_t fk = kopen(bk, OF_READ), fp = kopen(bp, OF_READ);
    uint32_t n0 = rfs->nodes, d0 = rfs->data_pages;
    enum proc_status sk, sp;
    int32_t pk = fk != NO_FILE ? start(33, fk) : -1;
    int32_t pp = fp != NO_FILE ? start(34, fp) : -1;
    remove_file("/tmp/un-k");
    remove_file("/tmp/un-p");
    kclose(fk);
    kclose(fp);
    uint32_t d_kclose = rfs->data_pages;
    uint64_t h0 = trunc_holds();
    if (pk > 0)
        process_kill(pk);
    struct proc_result rk = wait_for(pk, &sk);
    bool go = make_dir("/tmp/un-go");
    struct proc_result rp = wait_for(pp, &sp);
    uint64_t holds = trunc_holds() - h0;
    remove_dir("/tmp/un-go");
    char text[440];
    ksnprintf(text, sizeof(text), "teardowns: dirops 33 (%s) and dirops 34 (%s, vector %d) held "
              "the last fd of an unlinked 2 MiB file each (data pages still %u, "
              "%u before); their teardowns gave the pages back in %llu holds of at most %u; nodes "
              "%u -> %u, data pages %u -> %u", proc_end_str(rk.end), proc_end_str(rp.end),
              (int)rp.code, d_kclose, d0, (unsigned long long)holds, FSYS_TRUNC_STEP, n0,
              rfs->nodes, d0, rfs->data_pages);
    line(ended(pk, sk, rk, PROC_END_KILLED, 0) && ended(pp, sp, rp, PROC_END_FAULT, 14) && go &&
         d_kclose == d0 && holds == 2 * BIG_HOLDS && rfs->nodes == n0 - 2 &&
         rfs->data_pages == d0 - 2 * BIG_PAGES, text);
}

static bool size_reached(uint64_t which)
{
    struct vnode *n = 0;
    fd_vfs_acquire();
    bool ok = vfs_lookup(v, v->root, which ? "/tmp/un-v" : "/tmp/un-w", &n) == 0 &&
              n->size >= 8u * 32768u;
    fd_vfs_release();
    return ok;
}

static void un_writers(void)
{
    uint32_t n0 = rfs->nodes, d0 = rfs->data_pages;
    enum proc_status sw, sv, su, sr;
    int32_t pw = start(35, NO_FILE), pv = start(36, NO_FILE);
    bool w8 = pw > 0 && yield_until(size_reached, 0);
    int32_t pu = w8 ? start(37, NO_FILE) : -1;
    struct proc_result ru = wait_for(pu, &su);
    bool v8 = pv > 0 && yield_until(size_reached, 1);
    int32_t pr = v8 ? start(38, NO_FILE) : -1;
    struct proc_result rr = wait_for(pr, &sr);
    struct proc_result rw = wait_for(pw, &sw), rv = wait_for(pv, &sv);
    uint32_t n1 = rfs->nodes, d1 = rfs->data_pages;
    remove_file("/tmp/un-v");                       /* dirops 38's 10-byte file */
    char text[440];
    ksnprintf(text, sizeof(text), "writers: dirops 35 and 36 wrote 32 x 32 KiB each to /tmp/un-w "
              "and /tmp/un-v, every write the full count; after 8 writes un-w was unlinked "
              "(dirops 37: %s, status %d) and un-v renamed over (dirops 38: %s, status %d); both "
              "read their 1 MiB back through the fd (statuses %d, %d); freed at their close: "
              "nodes %u -> %u, data pages %u -> %u (+2: dirops 38's file), then %u, %u",
              proc_end_str(ru.end), (int)ru.code, proc_end_str(rr.end), (int)rr.code,
              (int)rw.code, (int)rv.code, n0, n1, d0, d1, rfs->nodes, rfs->data_pages);
    line(w8 && v8 && exited(pu, su, ru) && exited(pr, sr, rr) && exited(pw, sw, rw) &&
         exited(pv, sv, rv) && n1 == n0 + 1 && d1 == d0 + 2 && rfs->nodes == n0 &&
         rfs->data_pages == d0, text);
}

static void un_rename_open(void)
{
    uint32_t n0 = rfs->nodes, d0 = rfs->data_pages;
    bool made = make_file("/tmp/un-old", 100) && make_file("/tmp/un-new", 200);
    enum proc_status st;
    int32_t pid = made ? start(39, NO_FILE) : -1;
    struct proc_result r = wait_for(pid, &st);
    uint32_t n1 = rfs->nodes, d1 = rfs->data_pages;
    remove_file("/tmp/un-old");
    char text[360];
    ksnprintf(text, sizeof(text), "rename onto an open file: dirops 39 (process %d %s, status %d, "
              "expected 0) renamed /tmp/un-new over /tmp/un-old open as its fd 3, read the old "
              "100 bytes through it and the new 200 through the name; the replaced node freed at "
              "its last close: nodes %u -> %u, data pages %u -> %u (the remaining file), then "
              "%u, %u", (int)pid, proc_end_str(r.end), (int)r.code, n0, n1, d0, d1, rfs->nodes,
              rfs->data_pages);
    line(exited(pid, st, r) && n1 == n0 + 1 && d1 == d0 + 2 && rfs->nodes == n0 &&
         rfs->data_pages == d0, text);
}

static void un_folder(uint64_t arg, const char *what)
{
    uint32_t n0 = rfs->nodes;
    enum proc_status st;
    int32_t pid = start(arg, NO_FILE);
    struct proc_result r = wait_for(pid, &st);
    char text[360];
    ksnprintf(text, sizeof(text), "%s (dirops %u: process %d %s, status %d, expected 0): nodes %u "
              "-> %u", what, (unsigned)arg, (int)pid, proc_end_str(r.end), (int)r.code, n0,
              rfs->nodes);
    line(exited(pid, st, r) && rfs->nodes == n0, text);
}

bool dirtest_fsunlink(void)
{
    init("Unlink test");
    struct snapshot s0;
    snap(&s0);
    un_open();
    un_big();
    un_last_close();
    un_teardown();
    un_writers();
    un_rename_open();
    un_folder(40, "rmdir of the current folder: getcwd, creating, stat '.' and '..' and chdir "
                  "'..' answered, chdir / freed it");
    un_folder(41, "rmdir of a folder open as an fd: getdents64 ENOENT, fstat links 0, freed at "
                  "its close");
    waits_line();
    cleanup_line(&s0);
    return finish();
}
