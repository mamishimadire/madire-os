/* File descriptors, the kernel side (see fd.h). */
#include <stddef.h>
#include "kernel/fs/fs_errno.h"
#include "kernel/fs/fsys_core.h"
#include "kernel/fs/ramfs_core.h"
#include "kernel/fs/vfs.h"
#include "kernel/memory/usercopy.h"
#include "kernel/panic.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/klock.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

/* Zeroed at boot: every open file free (OFILE_FREE is 0), so processes
 * started before anything else here (the boot self-test's) work too. */
static struct ofile_table files;
static uint32_t busy_held[SCHED_MAX_THREADS];  /* busy and write marks each thread holds */
static struct fd_figures figs;
/* 1.0 stage 5: each thread's VFS-lock takes after waiting, and its
 * longest wait in ticks, in the chunk loop and the truncates. */
static uint64_t lock_waited[SCHED_MAX_THREADS];
static uint64_t lock_wait_longest[SCHED_MAX_THREADS];
static uint64_t last_release;           /* the tick of the last release through fd_vfs_release */
static struct fd_handoff handoff;

/* ---- the VFS lock with its figures (1.0 stage 5) ---- */

uint32_t fd_vfs_acquire(void)
{
    int32_t me = thread_current();
    uint64_t asked = timer_ticks();
    uint32_t steps = klock_acquire(vfs_lock());
    uint64_t now = timer_ticks();
    preempt_disable();
    if (steps) {
        /* FIFO: the release that served this ticket is the last one. */
        uint64_t gap = now - last_release;
        lock_waited[me]++;
        handoff.count++;
        if (gap >= 1)
            handoff.ticked++;
        if (gap > handoff.longest || handoff.longest_tid < 0) {
            handoff.longest = gap;
            handoff.longest_tid = me;
        }
    }
    if (now - asked > lock_wait_longest[me])
        lock_wait_longest[me] = now - asked;
    preempt_enable();
    return steps;
}

void fd_vfs_release(void)
{
    preempt_disable();
    last_release = timer_ticks();
    preempt_enable();
    klock_release(vfs_lock());
}

/* ---- the VFS side: nodes are dropped, and data moved, under its lock ---- */

/* 1.0 stage 6 (S6-6 (a)): true when dropping this hold frees a file that
 * still has pages: no name left and no other hold. Its pages are then
 * given back in steps of at most FSYS_TRUNC_STEP per hold of the VFS lock
 * (as ftruncate, K7 (b)) before the node itself is freed. The VFS lock
 * held. */
static bool dies_with_pages(const struct vnode *n)
{
    return n->type == VNODE_FILE && n->nlink == 0 && n->opens == 1 && ramfs_file_pages(n) > 0;
}

/* Drops one hold of each node, in order (a node may come twice: its other
 * holds are dropped first, so the last one is seen as the last). At most
 * FD_MAX + 1 nodes (a process's descriptors and its current folder). */
static void drop_nodes(void *const *nodes, uint32_t n)
{
    if (n == 0)
        return;
    struct vnode *big[FD_MAX + 1];
    uint32_t nb = 0;
    fd_vfs_acquire();
    for (uint32_t i = 0; i < n; i++) {
        struct vnode *x = nodes[i];
        if (nb < FD_MAX + 1 && dies_with_pages(x))
            big[nb++] = x;              /* kept held: freed in steps below */
        else
            vfs_drop(vfs_root(), x);
    }
    fd_vfs_release();
    if (nb == 0)
        return;
    /* No name and no other hold: nobody else can reach these nodes, so no
     * writer holds their write marks. */
    for (uint32_t j = 0; j < nb; j++)
        fd_node_truncate(big[j], 0);
    fd_vfs_acquire();
    for (uint32_t j = 0; j < nb; j++)
        vfs_drop(vfs_root(), big[j]);
    fd_vfs_release();
}

void fd_node_drop(struct vnode *n)
{
    void *one = n;
    drop_nodes(&one, 1);
}

static void put_file(uint32_t file)
{
    void *node = NULL;
    preempt_disable();
    ofile_put(&files, file, &node);
    preempt_enable();
    drop_nodes(&node, node ? 1 : 0);
}

/* ---- process start and end ---- */

int fd_start_console(struct fd_table *d)
{
    uint32_t in = 0, out = 0;
    preempt_disable();
    int r = ofile_alloc(&files, OFILE_CONSOLE_IN, OF_READ, NULL, &in);
    if (r == 0) {
        r = ofile_alloc(&files, OFILE_CONSOLE_OUT, OF_WRITE, NULL, &out);
        if (r < 0) {
            void *none;
            ofile_put(&files, in, &none);
        }
    }
    if (r == 0) {
        ofile_get(&files, out);                     /* fds 1 and 2 share it */
        fd_install_at(d, 0, in);
        fd_install_at(d, 1, out);
        fd_install_at(d, 2, out);
    }
    preempt_enable();
    return r;
}

bool fd_start_grants(struct fd_table *d, const struct fd_grant *g, uint32_t n)
{
    bool ok = true;
    preempt_disable();
    for (uint32_t i = 0; i < n && ok; i++) {
        for (uint32_t j = 0; j < i; j++)
            if (g[j].fd == g[i].fd)
                ok = false;
        if (!ok || g[i].fd >= FD_MAX || g[i].file >= FILE_MAX ||
            files.f[g[i].file].kind == OFILE_FREE) {
            ok = false;
            break;
        }
        void *node = NULL;
        fd_close(d, &files, g[i].fd, &node);        /* a console fd: no node */
        ofile_get(&files, g[i].file);
        fd_install_at(d, g[i].fd, g[i].file);
    }
    preempt_enable();
    return ok;
}

uint32_t fd_teardown(struct fd_table *d)
{
    void *nodes[FD_MAX + 1];
    uint32_t nn = 0;
    preempt_disable();
    uint32_t closed = fd_close_all(d, &files, nodes, &nn);
    figs.teardown_closed += closed;
    if (d->cwd) {                                   /* 1.0 stage 6: its current folder */
        nodes[nn++] = d->cwd;
        d->cwd = NULL;
        figs.cwd_dropped++;
    }
    preempt_enable();
    drop_nodes(nodes, nn);
    return closed;
}

int fd_kernel_open(struct vnode *n, uint8_t access, uint32_t *file)
{
    preempt_disable();
    int r = ofile_alloc(&files, OFILE_FILE, access, n, file);
    preempt_enable();
    if (r == 0)
        vfs_hold(n);                                /* the caller holds the VFS lock */
    return r;
}

int fd_kernel_console(uint32_t *file)
{
    preempt_disable();
    int r = ofile_alloc(&files, OFILE_CONSOLE_IN, OF_READ, NULL, file);
    preempt_enable();
    return r;
}

void fd_kernel_close(uint32_t file)
{
    put_file(file);
}

int fd_resolve(const struct fd_table *d, uint64_t fd, uint8_t want, uint32_t *file,
               uint8_t *kind)
{
    uint32_t i = 0;
    preempt_disable();
    int r = fd_lookup(d, fd, &i);
    if (r == 0)
        r = ofile_access(&files, i, want);
    if (r == 0) {
        *file = i;
        *kind = files.f[i].kind;
    }
    preempt_enable();
    return r;
}

/* ---- 1.0 stage 5: open, and holding an open file for a call ---- */

int fd_open_begin(const struct fd_table *d, uint8_t access, uint32_t *file)
{
    if (fd_count(d) >= FD_MAX)
        return -FDT_EMFILE;                         /* before ENFILE, as Linux */
    preempt_disable();
    int r = ofile_alloc(&files, OFILE_FILE, access, NULL, file);
    preempt_enable();
    return r;
}

void fd_open_node(uint32_t file, struct vnode *n)
{
    vfs_hold(n);                                    /* the caller holds the VFS lock */
    preempt_disable();
    files.f[file].node = n;
    preempt_enable();
}

int fd_open_finish(struct fd_table *d, uint32_t file, bool cloexec)
{
    preempt_disable();
    int fd = fd_install(d, file);
    if (fd >= 0 && cloexec)
        fd_set_cloexec(d, (uint64_t)fd, true);
    preempt_enable();
    if (fd < 0)                                     /* fd_open_begin found a free one */
        put_file(file);
    return fd;
}

void fd_open_abort(uint32_t file)
{
    put_file(file);
}

int fd_get(const struct fd_table *d, uint64_t fd, uint32_t *file, uint8_t *kind,
           uint8_t *access, struct vnode **node)
{
    uint32_t i = 0;
    preempt_disable();
    int r = fd_lookup(d, fd, &i);
    if (r == 0) {
        ofile_get(&files, i);
        *file = i;
        *kind = files.f[i].kind;
        *access = files.f[i].access;
        *node = files.f[i].node;
    }
    preempt_enable();
    return r;
}

void fd_put(uint32_t file)
{
    put_file(file);
}

/* ---- the busy mark (FIFO, waiting by yielding) ---- */

static const char *state_str(enum sched_state s)
{
    switch (s) {
    case SCHED_FREE:    return "gone";
    case SCHED_NEW:     return "not started";
    case SCHED_READY:   return "ready";
    case SCHED_RUNNING: return "running";
    case SCHED_BLOCKED: return "blocked";
    case SCHED_ZOMBIE:  return "exited";
    }
    return "?";
}

/* Takes a reference for the call and the mark; returns when held. */
static void busy_take(uint32_t file)
{
    int32_t me = thread_current();
    preempt_disable();
    ofile_get(&files, file);
    uint32_t ticket = ofile_busy_ticket(&files, file);
    uint64_t t0 = timer_ticks();
    preempt_enable();
    uint32_t waited = 0;
    for (;;) {
        preempt_disable();
        bool held = ofile_busy_try(&files, file, ticket, me, waited);
        int32_t owner = files.f[file].busy_owner;
        enum sched_state os = owner >= 0 ? thread_state(owner) : SCHED_READY;
        if (held) {
            busy_held[me]++;
            uint64_t w = timer_ticks() - t0;
            if (w > figs.busy_wait_longest)
                figs.busy_wait_longest = w;
        }
        preempt_enable();
        if (held)
            return;
        /* The holder never sleeps, joins or exits inside a call: a wait
         * behind one that cannot run would never end. */
        if (owner >= 0 && os != SCHED_READY)
            panicf("busy mark: thread %d '%s' waits for an open file held\n by thread %d "
                   "'%s', which is %s.", (int)me, thread_name(me), (int)owner,
                   thread_name(owner), state_str(os));
        waited++;
        thread_yield();
    }
}

static void busy_give(uint32_t file)
{
    int32_t me = thread_current();
    preempt_disable();
    bool ok = ofile_busy_release(&files, file, me);
    if (ok)
        busy_held[me]--;
    preempt_enable();
    if (!ok)
        panicf("busy mark: thread %d '%s' released an open file it did not hold.", (int)me,
               thread_name(me));
    put_file(file);
}

/* ---- 1.0 stage 5: the node write mark (FIFO, waiting by yielding) ---- */

/* The node is held by the caller's open file for the whole call. */
static void wmark_take(struct vnode *n)
{
    int32_t me = thread_current();
    uint32_t ahead = 0;
    preempt_disable();
    uint16_t ticket = fsys_wmark_ticket(n, &ahead);
    if (ahead > figs.wmark_ahead_max)
        figs.wmark_ahead_max = ahead;
    uint64_t t0 = timer_ticks();
    preempt_enable();
    uint32_t waited = 0;
    for (;;) {
        preempt_disable();
        bool held = fsys_wmark_try(n, ticket, me);
        int32_t owner = fsys_wmark_holder(n);
        enum sched_state os = owner >= 0 ? thread_state(owner) : SCHED_READY;
        if (held) {
            busy_held[me]++;
            figs.wmark_takes++;
            if (waited)
                figs.wmark_contended++;
            uint64_t w = timer_ticks() - t0;
            if (w > figs.wmark_wait_longest)
                figs.wmark_wait_longest = w;
        }
        preempt_enable();
        if (held)
            return;
        if (owner >= 0 && os != SCHED_READY)
            panicf("write mark: thread %d '%s' waits for a file written\n by thread %d "
                   "'%s', which is %s.", (int)me, thread_name(me), (int)owner,
                   thread_name(owner), state_str(os));
        waited++;
        thread_yield();
    }
}

static void wmark_give(struct vnode *n)
{
    int32_t me = thread_current();
    preempt_disable();
    bool ok = fsys_wmark_release(n, me);
    if (ok)
        busy_held[me]--;
    preempt_enable();
    if (!ok)
        panicf("write mark: thread %d '%s' released a file it did not hold.", (int)me,
               thread_name(me));
}

void fd_busy_check(const char *where)
{
    int32_t me = thread_current();
    if (busy_held[me])
        panicf("%s: thread %d '%s' still holds %u busy or write\n mark(s) of open files.",
               where, (int)me, thread_name(me), (unsigned)busy_held[me]);
}

/* ---- the VFS lock around one piece of work, with its figures ---- */

enum hold_kind { HOLD_DATA, HOLD_TRUNC, HOLD_OTHER };

struct hold {
    uint64_t t0, switches0;
};

static void hold_begin(struct hold *h)
{
    fd_vfs_acquire();
    h->t0 = timer_ticks();
    h->switches0 = thread_switches();
}

static void hold_end(const struct hold *h, enum hold_kind kind)
{
    uint64_t held = timer_ticks() - h->t0;
    bool switched = thread_switches() != h->switches0;
    preempt_disable();
    if (kind == HOLD_DATA)
        figs.chunks++;
    if (kind == HOLD_TRUNC) {
        figs.trunc_holds++;
        if (held > figs.trunc_hold_longest)
            figs.trunc_hold_longest = held;
    }
    figs.holds++;
    if (held >= 1)
        figs.holds_ticked++;
    if (switched)
        figs.holds_switched++;
    if (held > figs.hold_longest)
        figs.hold_longest = held;
    preempt_enable();
    fd_vfs_release();
}

/* The bytes from addr on, at most len, that are user memory readable (or
 * writable): whole pages until the first that is not (1.0 stage 5: a
 * call whose buffer turns bad part-way moves the bytes before it). */
static uint64_t valid_prefix(uint64_t addr, uint64_t len, bool writable)
{
    uint64_t ok = 0;
    while (ok < len) {
        uint64_t step = fsys_page_step(addr + ok, len - ok);
        if (!(writable ? user_writable(addr + ok, step) : user_readable(addr + ok, step)))
            break;
        ok += step;
    }
    return ok;
}

static void stamp(struct vnode *n)
{
    n->mtime = n->ctime = vfs_root()->now();        /* under the VFS lock */
}

/* ---- read and write on files: the chunk loop ---- */

int64_t fd_file_read(uint32_t file, uint64_t buf, uint64_t count)
{
    struct vnode *n = files.f[file].node;           /* fixed while the call holds a reference */
    if (n->type == VNODE_DIR)
        return -FS_EISDIR;
    uint64_t total = fd_rw_count(count);
    if (total == 0)
        return 0;
    uint8_t kbuf[FD_CHUNK];
    uint64_t done = 0;
    int64_t err = 0;
    busy_take(file);
    while (done < total) {
        uint32_t want = fd_chunk(done, total);
        bool last = false;
        if (!user_writable(buf + done, want)) {
            uint32_t ok = (uint32_t)valid_prefix(buf + done, want, true);
            if (ok == 0) {
                err = -FS_EFAULT;
                break;
            }
            want = ok;                              /* the bytes before the bad page, then stop */
            last = true;
        }
        /* The data and the offset change together, under the lock (only
         * the busy mark's holder moves the offset). */
        struct hold h;
        hold_begin(&h);
        uint64_t off = files.f[file].offset;
        int64_t got = ramfs_read(vfs_ramfs(), n, off, kbuf, want);
        if (got > 0)
            files.f[file].offset = off + (uint64_t)got;
        hold_end(&h, HOLD_DATA);
        if (got <= 0) {
            err = got;
            break;
        }
        preempt_disable();
        bool copied = copy_to_user(buf + done, kbuf, (uint64_t)got);
        if (!copied)                                /* checked above: cannot happen */
            files.f[file].offset = off;
        preempt_enable();
        if (!copied) {
            err = -FS_EFAULT;
            break;
        }
        done += (uint64_t)got;
        if ((uint64_t)got < want || last)
            break;                                  /* the end of the file, or a bad page next */
        if (thread_need_resched())
            thread_yield();
    }
    busy_give(file);
    return done ? (int64_t)done : err;
}

int64_t fd_file_write(uint32_t file, uint64_t buf, uint64_t count)
{
    struct vnode *n = files.f[file].node;
    if (n->type == VNODE_DIR)
        return -FS_EISDIR;
    uint64_t total = fd_rw_count(count);
    if (total == 0)
        return 0;
    uint8_t kbuf[FD_CHUNK];
    uint64_t done = 0;
    int64_t err = 0;
    bool append = (files.f[file].access & OF_APPEND) != 0;
    busy_take(file);
    wmark_take(n);                                  /* stage 5: one writer per node at a time */
    if (append) {                                   /* O_APPEND: the end, claimed once */
        struct hold h;
        hold_begin(&h);
        files.f[file].offset = n->size;
        hold_end(&h, HOLD_OTHER);
    }
    while (done < total) {
        uint32_t want = fd_chunk(done, total);
        bool last = false;
        if (!user_readable(buf + done, want)) {
            uint32_t ok = (uint32_t)valid_prefix(buf + done, want, false);
            if (ok == 0) {
                err = -FS_EFAULT;
                break;
            }
            want = ok;
            last = true;
        }
        preempt_disable();
        bool copied = copy_from_user(kbuf, buf + done, want);
        preempt_enable();
        if (!copied) {
            err = -FS_EFAULT;
            break;
        }
        struct hold h;
        hold_begin(&h);
        uint64_t off = files.f[file].offset;
        int64_t put = ramfs_write(vfs_ramfs(), n, off, kbuf, want);
        if (put > 0) {
            files.f[file].offset = off + (uint64_t)put;
            stamp(n);
        }
        hold_end(&h, HOLD_DATA);
        if (put < 0) {
            err = put;
            break;
        }
        done += (uint64_t)put;
        if ((uint64_t)put < want || last)
            break;                                  /* the file-size limit, the budget, a bad page */
        if (thread_need_resched())
            thread_yield();
    }
    wmark_give(n);
    busy_give(file);
    return done ? (int64_t)done : err;
}

/* ---- 1.0 stage 5: truncate and lseek ---- */

int fd_node_truncate(struct vnode *n, uint64_t length)
{
    if (n->type != VNODE_FILE)
        return -FS_EISDIR;
    if (length > RAMFS_FILE_MAX)
        return -FS_EFBIG;
    wmark_take(n);
    int r;
    bool first = true;
    do {
        struct hold h;
        hold_begin(&h);
        r = ramfs_truncate_step(vfs_ramfs(), n, length, FSYS_TRUNC_STEP);
        if (first && r >= 0)
            stamp(n);
        first = false;
        hold_end(&h, HOLD_TRUNC);
        if (r == 1 && thread_need_resched())
            thread_yield();
    } while (r == 1);
    wmark_give(n);
    return r < 0 ? r : 0;
}

int64_t fd_file_lseek(uint32_t file, int64_t off, uint64_t whence)
{
    struct vnode *n = files.f[file].node;
    busy_take(file);
    struct hold h;
    hold_begin(&h);
    int64_t r = fsys_lseek(files.f[file].offset, n->type == VNODE_FILE ? n->size : 0, off, whence);
    if (r >= 0)
        files.f[file].offset = (uint64_t)r;
    hold_end(&h, HOLD_OTHER);
    busy_give(file);
    return r;
}

/* ---- 1.0 stage 6: getdents64 ---- */

int64_t fd_file_getdents(uint32_t file, uint64_t buf, uint64_t count)
{
    struct vnode *n = files.f[file].node;           /* fixed while the call holds a reference */
    if (n->type != VNODE_DIR)
        return -FS_ENOTDIR;
    uint32_t total = (uint32_t)count;               /* unsigned int count, as Linux (F17) */
    uint8_t kbuf[FD_CHUNK];
    uint32_t done = 0;
    int64_t err = 0;
    busy_take(file);
    do {                                            /* once at least: count 0 is EINVAL too */
        uint32_t room = total - done < FD_CHUNK ? total - done : FD_CHUNK;
        /* At most one buffer of records per hold; the offset moves only
         * once they are in the caller's buffer (only the busy mark's
         * holder moves it). */
        struct hold h;
        hold_begin(&h);
        uint64_t cur = files.f[file].offset, next = cur;
        bool more = false, gone = n->nlink == 0;
        uint32_t used = gone ? 0 : fsys_dir_fill(n, cur, kbuf, room, &next, &more);
        hold_end(&h, HOLD_OTHER);
        if (gone) {
            if (done == 0)
                err = -FS_ENOENT;                   /* a removed folder (Linux) */
            break;
        }
        if (used == 0) {
            if (more && done == 0)
                err = -FS_EINVAL;                   /* the next record does not fit */
            break;                                  /* or the end of the folder */
        }
        uint32_t keep = used;
        if (!user_writable(buf + done, used)) {
            uint32_t valid = (uint32_t)valid_prefix(buf + done, used, true);
            next = fsys_dir_keep(kbuf, used, valid, cur, &keep);
            if (keep == 0) {
                if (done == 0)
                    err = -FS_EFAULT;
                break;
            }
        }
        preempt_disable();
        bool copied = copy_to_user(buf + done, kbuf, keep);
        if (copied)
            files.f[file].offset = next;
        preempt_enable();
        if (!copied) {                              /* checked above: cannot happen */
            if (done == 0)
                err = -FS_EFAULT;
            break;
        }
        done += keep;
        if (keep < used || !more)
            break;                                  /* a bad page next, or the end */
        if (thread_need_resched())
            thread_yield();
    } while (done < total);
    busy_give(file);
    return done ? (int64_t)done : err;
}

/* ---- close, dup, dup2 ---- */

int64_t fd_sys_close(struct fd_table *d, uint64_t fd)
{
    void *node = NULL;
    preempt_disable();
    int r = fd_close(d, &files, fd, &node);
    preempt_enable();
    drop_nodes(&node, node ? 1 : 0);
    return r;
}

int64_t fd_sys_dup(struct fd_table *d, uint64_t oldfd)
{
    preempt_disable();
    int r = fd_dup(d, &files, oldfd);
    preempt_enable();
    return r;
}

int64_t fd_sys_dup2(struct fd_table *d, uint64_t oldfd, uint64_t newfd)
{
    void *node = NULL;
    preempt_disable();
    int r = fd_dup2(d, &files, oldfd, newfd, &node);
    preempt_enable();
    drop_nodes(&node, node ? 1 : 0);
    return r;
}

/* ---- figures ---- */

uint32_t fd_open_files(void)
{
    return files.used;
}

void fd_figures(struct fd_figures *out)
{
    preempt_disable();
    /* Field by field: no struct copy, which could become a memcpy call. */
    out->teardown_closed = figs.teardown_closed;
    out->chunks = figs.chunks;
    out->hold_longest = figs.hold_longest;
    out->busy_wait_longest = figs.busy_wait_longest;
    out->holds = figs.holds;
    out->holds_ticked = figs.holds_ticked;
    out->holds_switched = figs.holds_switched;
    out->trunc_holds = figs.trunc_holds;
    out->trunc_hold_longest = figs.trunc_hold_longest;
    out->wmark_wait_longest = figs.wmark_wait_longest;
    out->wmark_takes = figs.wmark_takes;
    out->wmark_contended = figs.wmark_contended;
    out->wmark_ahead_max = figs.wmark_ahead_max;
    out->cwd_dropped = figs.cwd_dropped;
    preempt_enable();
}

void fd_figures_reset(void)
{
    preempt_disable();
    figs.chunks = figs.hold_longest = figs.busy_wait_longest = 0;
    figs.holds = figs.holds_ticked = figs.holds_switched = 0;
    figs.trunc_holds = figs.trunc_hold_longest = figs.wmark_wait_longest = 0;
    figs.wmark_takes = figs.wmark_contended = 0;
    figs.wmark_ahead_max = 0;
    for (uint32_t i = 0; i < SCHED_MAX_THREADS; i++)
        lock_waited[i] = lock_wait_longest[i] = 0;
    handoff.count = handoff.ticked = handoff.longest = 0;
    handoff.longest_tid = -1;
    last_release = timer_ticks();
    preempt_enable();
}

void fd_handoffs(struct fd_handoff *out)
{
    preempt_disable();
    out->count = handoff.count;
    out->ticked = handoff.ticked;
    out->longest = handoff.longest;
    out->longest_tid = handoff.longest_tid;
    preempt_enable();
}

void fd_thread_lock(int32_t tid, uint64_t *waited, uint64_t *longest)
{
    preempt_disable();
    bool ok = tid >= 0 && tid < (int32_t)SCHED_MAX_THREADS;
    *waited = ok ? lock_waited[tid] : 0;
    *longest = ok ? lock_wait_longest[tid] : 0;
    preempt_enable();
}

void fd_file_info(uint32_t file, struct ofile *out)
{
    preempt_disable();
    *out = files.f[file];
    preempt_enable();
}
