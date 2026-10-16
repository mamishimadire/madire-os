/* Unit tests for kernel/proc/fdtable_core.c (file descriptors, 1.0 stage 4):
 * open files and their references, ENFILE, the lowest free descriptor,
 * EMFILE, close, dup, dup2, closing everything, the chunk arithmetic and
 * the busy mark's FIFO order. */
#include <stdint.h>
#include "kernel/proc/fdtable_core.h"
#include "tests/unit/unit.h"

static struct ofile_table t;
static struct fd_table d;
static int node_a, node_b;              /* stand-ins for VFS nodes */

/* A process's start: fd 0 console input, fds 1 and 2 one console output. */
static void console_table(void)
{
    uint32_t in = 0, out = 0;
    ofile_table_init(&t);
    fd_table_init(&d);
    ofile_alloc(&t, OFILE_CONSOLE_IN, OF_READ, 0, &in);
    ofile_alloc(&t, OFILE_CONSOLE_OUT, OF_WRITE, 0, &out);
    ofile_get(&t, out);
    fd_install_at(&d, 0, in);
    fd_install_at(&d, 1, out);
    fd_install_at(&d, 2, out);
}

/* What write() decides first: 0 if fd is open for writing, else -EBADF. */
static int writable(uint64_t fd, uint32_t *idx)
{
    int r = fd_lookup(&d, fd, idx);
    return r ? r : ofile_access(&t, *idx, OF_WRITE);
}

/* The 10 descriptor CHECKs of test_syscall.c's former test_write_check
 * (0.10), one for one: fd 1 and fd 2 accepted; the counts that were
 * accepted with fd 1 (4096, 4097, UINT64_MAX), now the file path's clamp;
 * fd 0, fd 3, fd -1 and fd 2^32 + 1 refused with -EBADF (9); the result
 * untouched on an error. */
static void test_write_check_moved(void)
{
    uint32_t idx = 7;
    console_table();
    CHECK_EQ(writable(1, &idx), 0);
    CHECK_EQ(writable(2, &idx), 0);
    CHECK_EQ(fd_rw_count(4096), 4096);
    CHECK_EQ(fd_rw_count(4097), 4097);                  /* a file takes it all */
    CHECK_EQ(fd_rw_count(UINT64_MAX), FD_RW_MAX);       /* Linux's MAX_RW_COUNT */
    idx = 7;
    CHECK_EQ(writable(0, &idx), -9);                    /* stdin: read only */
    idx = 7;
    CHECK_EQ(fd_lookup(&d, 3, &idx), -9);
    CHECK_EQ(fd_lookup(&d, UINT64_MAX, &idx), -9);
    CHECK_EQ(fd_lookup(&d, (1ull << 32) + 1, &idx), -9);   /* not fd 1 (S4-2) */
    CHECK_EQ(idx, 7);                                   /* untouched on error */
}

static void test_console_start(void)
{
    uint32_t i0 = 0, i1 = 0, i2 = 0;
    console_table();
    CHECK_EQ(t.used, 2);
    CHECK_EQ(fd_count(&d), 3);
    CHECK_EQ(fd_lookup(&d, 0, &i0), 0);
    CHECK_EQ(fd_lookup(&d, 1, &i1), 0);
    CHECK_EQ(fd_lookup(&d, 2, &i2), 0);
    CHECK_EQ(i1, i2);                                   /* fds 1 and 2 share one */
    CHECK(i0 != i1);
    CHECK_EQ(t.f[i1].refs, 2);
    CHECK_EQ(ofile_access(&t, i0, OF_READ), 0);
    CHECK_EQ(ofile_access(&t, i0, OF_WRITE), -9);
    CHECK_EQ(ofile_access(&t, i1, OF_READ), -9);
}

static void test_enfile(void)
{
    uint32_t idx = 0;
    ofile_table_init(&t);
    for (uint32_t i = 0; i < FILE_MAX; i++)
        CHECK_EQ(ofile_alloc(&t, OFILE_CONSOLE_IN, OF_READ, 0, &idx), 0);
    CHECK_EQ(t.used, FILE_MAX);
    idx = 1234;
    CHECK_EQ(ofile_alloc(&t, OFILE_FILE, OF_READ, &node_a, &idx), -FDT_ENFILE);
    CHECK_EQ(idx, 1234);                                /* nothing changed */
    CHECK_EQ(t.used, FILE_MAX);
    void *node = &node_b;
    CHECK(ofile_put(&t, 17, &node));
    CHECK(node == 0);                                   /* a console holds no node */
    CHECK_EQ(ofile_alloc(&t, OFILE_FILE, OF_READ, &node_a, &idx), 0);
    CHECK_EQ(idx, 17);                                  /* the freed one, reused */
}

static void test_refs_and_node(void)
{
    uint32_t f = 0;
    void *node = 0;
    ofile_table_init(&t);
    CHECK_EQ(ofile_alloc(&t, OFILE_FILE, OF_READ | OF_WRITE, &node_a, &f), 0);
    ofile_get(&t, f);
    CHECK(!ofile_put(&t, f, &node));
    CHECK(node == 0);
    CHECK(ofile_put(&t, f, &node));                     /* the last reference */
    CHECK(node == &node_a);
    CHECK_EQ(t.used, 0);
    CHECK_EQ(t.f[f].kind, OFILE_FREE);
    CHECK(!ofile_put(&t, f, &node));                    /* already free: nothing */
}

static void test_lowest_and_emfile(void)
{
    console_table();
    uint32_t out = d.fd[1];
    for (int fd = 3; fd < (int)FD_MAX; fd++)
        CHECK_EQ(fd_dup(&d, &t, 1), fd);
    CHECK_EQ(fd_count(&d), FD_MAX);
    CHECK_EQ(t.f[out].refs, FD_MAX - 1);
    CHECK_EQ(fd_dup(&d, &t, 1), -FDT_EMFILE);
    CHECK_EQ(t.f[out].refs, FD_MAX - 1);                /* nothing changed */
    void *node = 0;
    CHECK_EQ(fd_close(&d, &t, 7, &node), 0);
    CHECK_EQ(fd_dup(&d, &t, 1), 7);                     /* the lowest free */
    CHECK_EQ(fd_install(&d, out), -FDT_EMFILE);
    CHECK_EQ(fd_dup(&d, &t, 16), -FDT_EBADF);
}

static void test_close(void)
{
    void *node = &node_b;
    console_table();
    CHECK_EQ(fd_close(&d, &t, 2, &node), 0);
    CHECK(node == 0);
    CHECK_EQ(fd_close(&d, &t, 2, &node), -FDT_EBADF);
    CHECK_EQ(fd_close(&d, &t, 16, &node), -FDT_EBADF);
    CHECK_EQ(fd_close(&d, &t, UINT64_MAX, &node), -FDT_EBADF);
    CHECK_EQ(fd_close(&d, &t, (1ull << 32) + 1, &node), -FDT_EBADF);
    CHECK_EQ(t.used, 2);                                /* fd 1 still holds the output */
    CHECK_EQ(fd_dup(&d, &t, 1), 2);                     /* lowest free after close(2) */
}

static void test_dup2(void)
{
    uint32_t f = 0;
    void *node = &node_b;
    console_table();
    CHECK_EQ(fd_dup2(&d, &t, 1, 1, &node), 1);          /* same fd: nothing done */
    CHECK(node == 0);
    CHECK_EQ(t.f[d.fd[1]].refs, 2);
    CHECK_EQ(ofile_alloc(&t, OFILE_FILE, OF_READ, &node_a, &f), 0);
    CHECK(fd_install_at(&d, 5, f));
    CHECK_EQ(fd_dup2(&d, &t, 0, 5, &node), 5);          /* open newfd closed first */
    CHECK(node == &node_a);                             /* its last reference: drop it */
    CHECK_EQ(d.fd[5], d.fd[0]);
    CHECK_EQ(fd_dup2(&d, &t, 1, 16, &node), -FDT_EBADF);
    CHECK_EQ(fd_dup2(&d, &t, 1, UINT64_MAX, &node), -FDT_EBADF);
    CHECK_EQ(fd_dup2(&d, &t, 9, 2, &node), -FDT_EBADF); /* oldfd not open */
    CHECK(d.fd[2] != FD_NONE);                          /* newfd left open */
    CHECK(!fd_install_at(&d, 2, f));                    /* taken */
    CHECK(!fd_install_at(&d, 16, f));
}

static void test_close_all(void)
{
    uint32_t f = 0;
    void *nodes[FD_MAX];
    uint32_t nn = 99;
    console_table();
    ofile_alloc(&t, OFILE_FILE, OF_READ, &node_a, &f);
    fd_install_at(&d, 3, f);
    for (int i = 0; i < 12; i++)
        fd_dup(&d, &t, 3);
    CHECK_EQ(fd_count(&d), FD_MAX);
    CHECK_EQ(fd_close_all(&d, &t, nodes, &nn), FD_MAX);
    CHECK_EQ(nn, 1);                                    /* the file's node, once */
    CHECK(nodes[0] == &node_a);
    CHECK_EQ(fd_count(&d), 0);
    CHECK_EQ(t.used, 0);
    CHECK_EQ(fd_close_all(&d, &t, nodes, &nn), 0);
    CHECK_EQ(nn, 0);
}

/* The chunk loop's arithmetic (plan B2, F18): a call is cut into chunks of
 * at most 4096 bytes whose sum is the clamped count. */
static void test_chunks(void)
{
    CHECK_EQ(fd_chunk(0, 10000), 4096);
    CHECK_EQ(fd_chunk(4096, 10000), 4096);
    CHECK_EQ(fd_chunk(8192, 10000), 1808);
    CHECK_EQ(fd_chunk(10000, 10000), 0);
    CHECK_EQ(fd_chunk(20000, 10000), 0);
    CHECK_EQ(fd_chunk(0, 1), 1);
    uint64_t total = fd_rw_count(49152), done = 0;
    unsigned n = 0;
    while (done < total) {
        done += fd_chunk(done, total);
        n++;
    }
    CHECK_EQ(n, 12);
    CHECK_EQ(done, 49152);
    CHECK_EQ(fd_rw_count(0), 0);
    CHECK_EQ(fd_rw_count(FD_RW_MAX + 1), FD_RW_MAX);
    CHECK_EQ(FD_RW_MAX, 0x7FFFF000ull);
}

/* The busy mark: tickets are served in the order taken (FIFO). */
static void test_busy_fifo(void)
{
    uint32_t f = 0;
    ofile_table_init(&t);
    ofile_alloc(&t, OFILE_FILE, OF_READ, &node_a, &f);
    uint32_t a = ofile_busy_ticket(&t, f);
    uint32_t b = ofile_busy_ticket(&t, f);
    uint32_t c = ofile_busy_ticket(&t, f);
    CHECK_EQ(t.f[f].busy_ahead_max, 2);
    CHECK(!ofile_busy_try(&t, f, b, 11, 0));            /* not its turn */
    CHECK(!ofile_busy_try(&t, f, c, 12, 0));
    CHECK(ofile_busy_try(&t, f, a, 10, 0));
    CHECK_EQ(t.f[f].busy_owner, 10);
    CHECK(!ofile_busy_try(&t, f, b, 11, 3));            /* held */
    CHECK(!ofile_busy_release(&t, f, 11));              /* not the holder */
    CHECK(ofile_busy_release(&t, f, 10));
    CHECK(!ofile_busy_try(&t, f, c, 12, 1));            /* b is next, not c */
    CHECK(ofile_busy_try(&t, f, b, 11, 4));
    CHECK(ofile_busy_release(&t, f, 11));
    CHECK(ofile_busy_try(&t, f, c, 12, 2));
    CHECK(ofile_busy_release(&t, f, 12));
    CHECK_EQ(t.f[f].busy_takes, 3);
    CHECK_EQ(t.f[f].busy_contended, 2);
    CHECK_EQ(t.f[f].busy_waits, 6);
    CHECK_EQ(t.f[f].busy_owner, FD_TICKET_NONE);
    CHECK(!ofile_busy_release(&t, f, FD_TICKET_NONE));
    CHECK_EQ(ofile_busy_ahead(&t, f, ofile_busy_ticket(&t, f)), 0);
}

/* 1.0 stage 5: close-on-exec per descriptor (set, cleared by close, clear
 * on dup's and dup2's new descriptor, never on a closed one), and
 * OF_APPEND alongside the access bits. */
static void test_cloexec_and_append(void)
{
    uint32_t f = 0;
    void *node = 0;
    console_table();
    CHECK_EQ(d.cloexec, 0);
    CHECK_EQ(ofile_alloc(&t, OFILE_FILE, OF_WRITE | OF_APPEND, &node_a, &f), 0);
    CHECK_EQ(ofile_access(&t, f, OF_WRITE), 0);                 /* OF_APPEND does not hide it */
    CHECK_EQ(ofile_access(&t, f, OF_READ), -9);
    int fd = fd_install(&d, f);
    CHECK_EQ(fd, 3);
    CHECK(!fd_cloexec(&d, 3));
    CHECK(fd_set_cloexec(&d, 3, true));
    CHECK(fd_cloexec(&d, 3));
    CHECK(!fd_cloexec(&d, 1));
    CHECK(!fd_set_cloexec(&d, 9, true));                        /* not open */
    CHECK(!fd_set_cloexec(&d, 16, true));
    CHECK(!fd_cloexec(&d, (1ull << 32) + 3));
    CHECK_EQ(fd_dup(&d, &t, 3), 4);
    CHECK(!fd_cloexec(&d, 4));                                  /* dup: clear on the new fd */
    CHECK(fd_cloexec(&d, 3));
    CHECK(fd_set_cloexec(&d, 4, true));
    CHECK_EQ(fd_dup2(&d, &t, 3, 4, &node), 4);                  /* newfd open: closed first */
    CHECK(!fd_cloexec(&d, 4));                                  /* dup2: clear on newfd */
    CHECK_EQ(fd_dup2(&d, &t, 3, 3, &node), 3);                  /* same fd: nothing done */
    CHECK(fd_cloexec(&d, 3));
    CHECK_EQ(fd_close(&d, &t, 3, &node), 0);
    CHECK(!fd_cloexec(&d, 3));                                  /* close clears it */
    CHECK_EQ(d.cloexec, 0);
    CHECK_EQ(fd_close(&d, &t, 4, &node), 0);
    CHECK(node == &node_a);                                     /* the last reference */
    fd_table_init(&d);
    CHECK_EQ(d.cloexec, 0);
}

int main(void)
{
    RUN(test_write_check_moved);
    RUN(test_console_start);
    RUN(test_enfile);
    RUN(test_refs_and_node);
    RUN(test_lowest_and_emfile);
    RUN(test_close);
    RUN(test_dup2);
    RUN(test_close_all);
    RUN(test_chunks);
    RUN(test_busy_fifo);
    RUN(test_cloexec_and_append);
    return unit_report("fdtable");
}
