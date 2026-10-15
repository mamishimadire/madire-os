/* Unit tests for kernel/proc/syscall_core.c (system-call numbers and
 * argument rules, 0.10). */
#include <stdint.h>
#include <string.h>
#include "kernel/proc/syscall_core.h"
#include "tests/unit/unit.h"

static void test_implemented_numbers(void)
{
    CHECK_EQ(syscall_lookup(0), SC_READ);                   /* 1.0 stage 4 */
    CHECK_EQ(syscall_lookup(3), SC_CLOSE);
    CHECK_EQ(syscall_lookup(32), SC_DUP);
    CHECK_EQ(syscall_lookup(33), SC_DUP2);
    CHECK(strcmp(syscall_name(SC_DUP2), "dup2") == 0);
    CHECK_EQ(syscall_lookup(2), SC_OPEN);                   /* 1.0 stage 5 */
    CHECK_EQ(syscall_lookup(4), SC_STAT);
    CHECK_EQ(syscall_lookup(5), SC_FSTAT);
    CHECK_EQ(syscall_lookup(6), SC_LSTAT);
    CHECK_EQ(syscall_lookup(8), SC_LSEEK);
    CHECK_EQ(syscall_lookup(77), SC_FTRUNCATE);
    CHECK_EQ(syscall_lookup(257), SC_OPENAT);
    CHECK_EQ(syscall_lookup(262), SC_NEWFSTATAT);
    CHECK(strcmp(syscall_name(SC_NEWFSTATAT), "newfstatat") == 0);
    CHECK_EQ(syscall_lookup(1), SC_WRITE);
    CHECK_EQ(syscall_lookup(24), SC_SCHED_YIELD);
    CHECK_EQ(syscall_lookup(35), SC_NANOSLEEP);
    CHECK_EQ(syscall_lookup(39), SC_GETPID);
    CHECK_EQ(syscall_lookup(60), SC_EXIT);
    CHECK_EQ(syscall_lookup(231), SC_EXIT_GROUP);
    CHECK(strcmp(syscall_name(SC_NANOSLEEP), "nanosleep") == 0);
    CHECK(strcmp(syscall_name(SC_EXIT_GROUP), "exit_group") == 0);
}

static void test_everything_else_is_unknown(void)
{
    unsigned implemented = 0;
    for (uint64_t nr = 0; nr < 1024; nr++)
        if (syscall_lookup(nr) != SC_UNKNOWN)
            implemented++;
    CHECK_EQ(implemented, 18);                      /* 6 until 1.0 stage 4: + 0, 3, 32, 33;
                                                     * 10 until stage 5: + 2, 4, 5, 6, 8, 77,
                                                     * 257, 262 */
    static const uint64_t others[] = {
        /* 16 (ioctl, deferred) replaced 2 (open) in 1.0 stage 5 */
        7, 16, 9, 23, 25, 34, 36, 38, 40, 57, 59, 62, 230, 232, 334, 335, 547,
        0x40000001ull,                  /* x32 ABI write */
        0x40000000ull + 60,             /* x32 ABI exit */
        (1ull << 32) + 1,               /* write's number with high bits set */
        (1ull << 32) + 60,
        0xFFFFFFFFull, UINT64_MAX,      /* -1 */
        0x8000000000000001ull,
        SYS_MADIRE_FIRST, SYS_MADIRE_FIRST + 1, SYS_MADIRE_LAST,
    };
    for (unsigned i = 0; i < sizeof(others) / sizeof(others[0]); i++)
        CHECK_EQ(syscall_lookup(others[i]), SC_UNKNOWN);
}

static void test_madire_range(void)
{
    CHECK(!syscall_is_madire(SYS_MADIRE_FIRST - 1));
    CHECK(syscall_is_madire(0x10000000ull));
    CHECK(syscall_is_madire(0x1000FFFFull));
    CHECK(!syscall_is_madire(0x10010000ull));
    CHECK(!syscall_is_madire(1));
    CHECK_EQ(MADIRE_ENOSYS, 38);
    CHECK_EQ(MADIRE_EBADF, 9);
    CHECK_EQ(MADIRE_EFAULT, 14);
    CHECK_EQ(MADIRE_EINVAL, 22);
}

/* 1.0 stage 4: the console's count (the 5 count CHECKs of the former
 * test_write_check); which descriptors may be written is decided by the
 * descriptor table, and its 10 CHECKs moved to tests/unit/test_fdtable.c
 * (test_write_check_moved), one for one. */
static void test_console_count(void)
{
    CHECK_EQ(syscall_console_count(5), 5);
    CHECK_EQ(syscall_console_count(0), 0);
    CHECK_EQ(syscall_console_count(4096), 4096);
    CHECK_EQ(syscall_console_count(4097), 4096);        /* short count beyond the maximum */
    CHECK_EQ(syscall_console_count(UINT64_MAX), 4096);
}

static void test_timespec_ticks(void)
{
    uint32_t t = 123;
    CHECK_EQ(syscall_timespec_ticks(0, 0, 100, &t), 0);
    CHECK_EQ(t, 0);                                     /* a yield */
    CHECK_EQ(syscall_timespec_ticks(0, 1, 100, &t), 0);
    CHECK_EQ(t, 1);                                     /* rounded up */
    CHECK_EQ(syscall_timespec_ticks(0, 10000000, 100, &t), 0);
    CHECK_EQ(t, 1);                                     /* exactly 10 ms */
    CHECK_EQ(syscall_timespec_ticks(0, 10000001, 100, &t), 0);
    CHECK_EQ(t, 2);
    CHECK_EQ(syscall_timespec_ticks(0, 100000000, 100, &t), 0);
    CHECK_EQ(t, 10);                                    /* 100 ms */
    CHECK_EQ(syscall_timespec_ticks(1, 500000000, 100, &t), 0);
    CHECK_EQ(t, 150);
    CHECK_EQ(syscall_timespec_ticks(0, 999999999, 100, &t), 0);
    CHECK_EQ(t, 100);
    CHECK_EQ(syscall_timespec_ticks(INT64_MAX, 999999999, 100, &t), 0);
    CHECK_EQ(t, UINT32_MAX);                            /* clamped */
    CHECK_EQ(syscall_timespec_ticks(42949672, 0, 100, &t), 0);
    CHECK_EQ(t, 4294967200u);
    CHECK_EQ(syscall_timespec_ticks(42949673, 0, 100, &t), 0);
    CHECK_EQ(t, UINT32_MAX);
    t = 5;
    CHECK_EQ(syscall_timespec_ticks(-1, 0, 100, &t), -22);
    CHECK_EQ(syscall_timespec_ticks(0, -1, 100, &t), -22);
    CHECK_EQ(syscall_timespec_ticks(0, 1000000000, 100, &t), -22);
    CHECK_EQ(syscall_timespec_ticks(INT64_MIN, 0, 100, &t), -22);
    CHECK_EQ(t, 5);                                     /* untouched on error */
}

static void test_exit_status(void)
{
    CHECK_EQ(syscall_exit_status(0), 0);
    CHECK_EQ(syscall_exit_status(42), 42);
    CHECK_EQ(syscall_exit_status(255), 255);
    CHECK_EQ(syscall_exit_status(256), 0);
    CHECK_EQ(syscall_exit_status(257), 1);
    CHECK_EQ(syscall_exit_status(UINT64_MAX), 255);     /* exit(-1) */
}

/* write_lines: collect the emitted lines into one buffer, '|' between. */
static char got[2048];
static size_t got_len;
static void collect(void *ctx, const char *line, size_t len)
{
    (void)ctx;
    memcpy(got + got_len, line, len);
    got_len += len;
    got[got_len++] = '|';
    got[got_len] = 0;
}
static uint32_t lines(int32_t pid, const char *s, size_t n)
{
    got_len = 0;
    got[0] = 0;
    return syscall_write_lines(pid, (const uint8_t *)s, n, collect, 0);
}

static void test_write_lines(void)
{
    CHECK_EQ(lines(3, "Hello from user mode\n", 21), 1);
    CHECK(strcmp(got, "[pid 3] Hello from user mode|") == 0);
    CHECK_EQ(lines(12, "a\nb\n", 4), 2);
    CHECK(strcmp(got, "[pid 12] a|[pid 12] b|") == 0);
    CHECK_EQ(lines(7, "no newline", 10), 1);                /* still ends its own line */
    CHECK(strcmp(got, "[pid 7] no newline|") == 0);
    CHECK_EQ(lines(7, "\n", 1), 1);
    CHECK(strcmp(got, "[pid 7] |") == 0);
    CHECK_EQ(lines(7, "", 0), 0);
    CHECK_EQ(got_len, 0);
    CHECK_EQ(lines(2147483647, "x", 1), 1);
    CHECK(strcmp(got, "[pid 2147483647] x|") == 0);
}

static void test_write_lines_sanitised(void)
{
    /* Control characters, escape sequences, CR and 8-bit bytes become '?':
     * a program cannot move the cursor or fake a line of its own. */
    static const char s[] = "A\rKERNEL PANIC\x1b[2J\tB\x7f\x80\xff\x00Z";
    CHECK_EQ(lines(5, s, sizeof(s) - 1), 1);
    CHECK(strcmp(got, "[pid 5] A?KERNEL PANIC?[2J?B????Z|") == 0);
    for (size_t i = 0; i < got_len; i++)
        CHECK(got[i] >= 0x20 && got[i] <= 0x7E);
}

static void test_write_lines_split_long(void)
{
    char s[250];
    memset(s, 'x', sizeof(s));
    CHECK_EQ(lines(1, s, 250), 3);                          /* 100 + 100 + 50 */
    const char *p = got;
    size_t sizes[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++) {
        CHECK(strncmp(p, "[pid 1] ", 8) == 0);
        const char *bar = strchr(p, '|');
        sizes[k] = (size_t)(bar - p) - 8;
        p = bar + 1;
    }
    CHECK_EQ(sizes[0], 100);
    CHECK_EQ(sizes[1], 100);
    CHECK_EQ(sizes[2], 50);
    memset(s, 'y', 100);
    s[100] = '\n';
    CHECK_EQ(lines(1, s, 101), 1);                          /* exactly 100, then the newline */
}

static void test_user_rflags(void)
{
    CHECK_EQ(syscall_user_rflags(0), 0x202);                /* IF + reserved bit 1 */
    CHECK_EQ(syscall_user_rflags(0x202), 0x202);
    CHECK_EQ(syscall_user_rflags(0x3000 | 0x202), 0x202);   /* IOPL 3 dropped */
    CHECK_EQ(syscall_user_rflags(0x4000), 0x202);           /* NT */
    CHECK_EQ(syscall_user_rflags(0x10000), 0x202);          /* RF */
    CHECK_EQ(syscall_user_rflags(0x20000), 0x202);          /* VM */
    CHECK_EQ(syscall_user_rflags(0x180000), 0x202);         /* VIF, VIP */
    CHECK_EQ(syscall_user_rflags(0x8D5), 0x8D5 | 0x202);    /* CF PF AF ZF SF OF */
    CHECK_EQ(syscall_user_rflags(0x100), 0x302);            /* TF kept: a program may single-step */
    CHECK_EQ(syscall_user_rflags(0x400), 0x602);            /* DF */
    CHECK_EQ(syscall_user_rflags(0x40000), 0x40202);        /* AC */
    CHECK_EQ(syscall_user_rflags(0x200000), 0x200202);      /* ID */
    CHECK_EQ(syscall_user_rflags(UINT64_MAX), 0x240DD5ull | 0x202);  /* exactly the kept flags */
}

int main(void)
{
    RUN(test_implemented_numbers);
    RUN(test_everything_else_is_unknown);
    RUN(test_madire_range);
    RUN(test_console_count);
    RUN(test_timespec_ticks);
    RUN(test_exit_status);
    RUN(test_write_lines);
    RUN(test_write_lines_sanitised);
    RUN(test_write_lines_split_long);
    RUN(test_user_rflags);
    return unit_report("syscall");
}
