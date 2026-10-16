/* File descriptors, the pure part (see fdtable_core.h). */
#include <stddef.h>
#include "kernel/proc/fdtable_core.h"

static void clear(struct ofile *f)
{
    f->kind = OFILE_FREE;
    f->access = 0;
    f->refs = 0;
    f->offset = 0;
    f->node = NULL;
    f->busy_next = f->busy_serving = 0;
    f->busy_owner = FD_TICKET_NONE;
    f->busy_takes = f->busy_contended = f->busy_waits = 0;
    f->busy_ahead_max = 0;
}

void ofile_table_init(struct ofile_table *t)
{
    for (uint32_t i = 0; i < FILE_MAX; i++)
        clear(&t->f[i]);
    t->used = 0;
}

int ofile_alloc(struct ofile_table *t, enum ofile_kind kind, uint8_t access, void *node,
                uint32_t *idx)
{
    for (uint32_t i = 0; i < FILE_MAX; i++) {
        struct ofile *f = &t->f[i];
        if (f->kind != OFILE_FREE)
            continue;
        clear(f);
        f->kind = (uint8_t)kind;
        f->access = access;
        f->refs = 1;
        f->node = node;
        t->used++;
        *idx = i;
        return 0;
    }
    return -FDT_ENFILE;
}

void ofile_get(struct ofile_table *t, uint32_t idx)
{
    t->f[idx].refs++;
}

bool ofile_put(struct ofile_table *t, uint32_t idx, void **node_out)
{
    struct ofile *f = &t->f[idx];
    *node_out = NULL;
    if (f->refs == 0 || --f->refs)
        return false;
    *node_out = f->node;
    clear(f);
    t->used--;
    return true;
}

void fd_table_init(struct fd_table *d)
{
    for (uint32_t i = 0; i < FD_MAX; i++)
        d->fd[i] = FD_NONE;
    d->cloexec = 0;
    d->cwd = NULL;                      /* 1.0 stage 6: the root */
}

int fd_lookup(const struct fd_table *d, uint64_t fd, uint32_t *idx)
{
    if (fd >= FD_MAX || d->fd[fd] == FD_NONE)
        return -FDT_EBADF;
    *idx = d->fd[fd];
    return 0;
}

int ofile_access(const struct ofile_table *t, uint32_t idx, uint8_t want)
{
    return (t->f[idx].access & want) == want ? 0 : -FDT_EBADF;
}

uint32_t fd_count(const struct fd_table *d)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < FD_MAX; i++)
        if (d->fd[i] != FD_NONE)
            n++;
    return n;
}

int fd_install(struct fd_table *d, uint32_t idx)
{
    for (uint32_t i = 0; i < FD_MAX; i++)
        if (d->fd[i] == FD_NONE) {
            d->fd[i] = (uint16_t)idx;
            return (int)i;
        }
    return -FDT_EMFILE;
}

bool fd_install_at(struct fd_table *d, uint64_t fd, uint32_t idx)
{
    if (fd >= FD_MAX || d->fd[fd] != FD_NONE)
        return false;
    d->fd[fd] = (uint16_t)idx;
    return true;
}

int fd_close(struct fd_table *d, struct ofile_table *t, uint64_t fd, void **node_out)
{
    uint32_t idx;
    *node_out = NULL;
    if (fd_lookup(d, fd, &idx) < 0)
        return -FDT_EBADF;
    d->fd[fd] = FD_NONE;
    d->cloexec &= (uint16_t)~(1u << fd);
    ofile_put(t, idx, node_out);
    return 0;
}

int fd_dup(struct fd_table *d, struct ofile_table *t, uint64_t oldfd)
{
    uint32_t idx;
    if (fd_lookup(d, oldfd, &idx) < 0)
        return -FDT_EBADF;
    int fd = fd_install(d, idx);
    if (fd >= 0) {
        ofile_get(t, idx);
        d->cloexec &= (uint16_t)~(1u << fd);
    }
    return fd;
}

int fd_dup2(struct fd_table *d, struct ofile_table *t, uint64_t oldfd, uint64_t newfd,
            void **node_out)
{
    uint32_t idx;
    *node_out = NULL;
    if (fd_lookup(d, oldfd, &idx) < 0 || newfd >= FD_MAX)
        return -FDT_EBADF;
    if (oldfd == newfd)
        return (int)newfd;
    if (d->fd[newfd] != FD_NONE)
        fd_close(d, t, newfd, node_out);
    d->fd[newfd] = (uint16_t)idx;
    d->cloexec &= (uint16_t)~(1u << newfd);
    ofile_get(t, idx);
    return (int)newfd;
}

bool fd_set_cloexec(struct fd_table *d, uint64_t fd, bool on)
{
    if (fd >= FD_MAX || d->fd[fd] == FD_NONE)
        return false;
    if (on)
        d->cloexec |= (uint16_t)(1u << fd);
    else
        d->cloexec &= (uint16_t)~(1u << fd);
    return true;
}

bool fd_cloexec(const struct fd_table *d, uint64_t fd)
{
    return fd < FD_MAX && d->fd[fd] != FD_NONE && (d->cloexec >> fd) & 1u;
}

uint32_t fd_close_all(struct fd_table *d, struct ofile_table *t, void *nodes[FD_MAX],
                      uint32_t *nnodes)
{
    uint32_t closed = 0;
    *nnodes = 0;
    for (uint32_t i = 0; i < FD_MAX; i++) {
        if (d->fd[i] == FD_NONE)
            continue;
        void *node = NULL;
        fd_close(d, t, i, &node);
        closed++;
        if (node)
            nodes[(*nnodes)++] = node;
    }
    return closed;
}

uint64_t fd_rw_count(uint64_t count)
{
    return count < FD_RW_MAX ? count : FD_RW_MAX;
}

uint32_t fd_chunk(uint64_t done, uint64_t total)
{
    uint64_t left = done < total ? total - done : 0;
    return left < FD_CHUNK ? (uint32_t)left : FD_CHUNK;
}

uint32_t ofile_busy_ahead(const struct ofile_table *t, uint32_t idx, uint32_t ticket)
{
    return ticket - t->f[idx].busy_serving;
}

uint32_t ofile_busy_ticket(struct ofile_table *t, uint32_t idx)
{
    struct ofile *f = &t->f[idx];
    uint32_t ticket = f->busy_next++;
    uint32_t ahead = ofile_busy_ahead(t, idx, ticket);
    if (ahead > f->busy_ahead_max)
        f->busy_ahead_max = ahead;
    return ticket;
}

bool ofile_busy_try(struct ofile_table *t, uint32_t idx, uint32_t ticket, int32_t tid,
                    uint32_t waited)
{
    struct ofile *f = &t->f[idx];
    if (f->busy_serving != ticket || f->busy_owner != FD_TICKET_NONE)
        return false;
    f->busy_owner = tid;
    f->busy_takes++;
    if (waited) {
        f->busy_contended++;
        f->busy_waits += waited;
    }
    return true;
}

bool ofile_busy_release(struct ofile_table *t, uint32_t idx, int32_t tid)
{
    struct ofile *f = &t->f[idx];
    if (f->busy_owner != tid || tid == FD_TICKET_NONE)
        return false;
    f->busy_owner = FD_TICKET_NONE;
    f->busy_serving++;
    return true;
}
