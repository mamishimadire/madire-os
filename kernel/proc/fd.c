/* File descriptors, the kernel side (see fd.h). */
#include <stddef.h>
#include "kernel/fs/fs_errno.h"
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
static uint32_t busy_held[SCHED_MAX_THREADS];  /* busy marks each thread holds */
static struct fd_figures figs;

/* ---- the VFS side: nodes are dropped, and data moved, under its lock ---- */

static void drop_nodes(void *const *nodes, uint32_t n)
{
    if (n == 0)
        return;
    klock_acquire(vfs_lock());
    for (uint32_t i = 0; i < n; i++)
        vfs_drop(vfs_root(), nodes[i]);
    klock_release(vfs_lock());
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
    void *nodes[FD_MAX] = { 0 };
    uint32_t nn = 0;
    preempt_disable();
    uint32_t closed = fd_close_all(d, &files, nodes, &nn);
    figs.teardown_closed += closed;
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

void fd_busy_check(const char *where)
{
    int32_t me = thread_current();
    if (busy_held[me])
        panicf("%s: thread %d '%s' still holds %u busy mark(s)\n of open files.", where,
               (int)me, thread_name(me), (unsigned)busy_held[me]);
}

/* ---- read and write on files: the chunk loop ---- */

static void note_hold(uint64_t t0)
{
    uint64_t held = timer_ticks() - t0;
    preempt_disable();
    figs.chunks++;
    if (held > figs.hold_longest)
        figs.hold_longest = held;
    preempt_enable();
}

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
        if (!user_writable(buf + done, want)) {
            err = -FS_EFAULT;
            break;
        }
        /* The data and the offset change together, under the lock (only
         * the busy mark's holder moves the offset). */
        klock_acquire(vfs_lock());
        uint64_t t0 = timer_ticks();
        uint64_t off = files.f[file].offset;
        int64_t got = ramfs_read(vfs_ramfs(), n, off, kbuf, want);
        if (got > 0)
            files.f[file].offset = off + (uint64_t)got;
        note_hold(t0);
        klock_release(vfs_lock());
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
        if ((uint64_t)got < want)
            break;                                  /* the end of the file */
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
    busy_take(file);
    while (done < total) {
        uint32_t want = fd_chunk(done, total);
        preempt_disable();
        bool copied = copy_from_user(kbuf, buf + done, want);
        preempt_enable();
        if (!copied) {
            err = -FS_EFAULT;
            break;
        }
        klock_acquire(vfs_lock());
        uint64_t t0 = timer_ticks();
        uint64_t off = files.f[file].offset;
        int64_t put = ramfs_write(vfs_ramfs(), n, off, kbuf, want);
        if (put > 0)
            files.f[file].offset = off + (uint64_t)put;
        note_hold(t0);
        klock_release(vfs_lock());
        if (put < 0) {
            err = put;
            break;
        }
        done += (uint64_t)put;
        if ((uint64_t)put < want)
            break;                                  /* the file-size limit or the budget */
        if (thread_need_resched())
            thread_yield();
    }
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
    *out = figs;
    preempt_enable();
}

void fd_file_info(uint32_t file, struct ofile *out)
{
    preempt_disable();
    *out = files.f[file];
    preempt_enable();
}
