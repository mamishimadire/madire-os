/* Unit tests for kernel/logbuf.c (kernel log ring buffer, 0.9.1). */
#include <stdio.h>
#include <string.h>
#include "kernel/logbuf.h"
#include "tests/unit/unit.h"

static char store[64];
static char snap[256];
static struct logbuf b;

static void put(const char *s)
{
    while (*s)
        logbuf_putc(&b, *s++);
}

/* The last n lines of the buffer as one string (via copy + tail). */
static const char *tail(uint32_t n, uint32_t *lines)
{
    static char out[256];
    uint32_t len = logbuf_copy(&b, snap, sizeof(snap));
    uint32_t start = logbuf_tail(snap, len, logbuf_wrapped(&b), n, lines);
    memcpy(out, snap + start, len - start);
    out[len - start] = '\0';
    return out;
}

static void test_empty(void)
{
    uint32_t lines = 99;
    logbuf_init(&b, store, sizeof(store));
    CHECK_EQ(logbuf_used(&b), 0);
    CHECK(!logbuf_wrapped(&b));
    CHECK_EQ(logbuf_copy(&b, snap, sizeof(snap)), 0);
    CHECK(strcmp(tail(5, &lines), "") == 0);
    CHECK_EQ(lines, 0);
    /* No storage: writes are ignored, nothing breaks. */
    struct logbuf none;
    logbuf_init(&none, NULL, 0);
    logbuf_putc(&none, 'x');
    CHECK_EQ(logbuf_used(&none), 0);
}

static void test_before_wrap(void)
{
    uint32_t lines;
    logbuf_init(&b, store, sizeof(store));
    put("one\ntwo\nthree\n");
    CHECK_EQ(logbuf_used(&b), 14);
    CHECK(!logbuf_wrapped(&b));
    CHECK(strcmp(tail(2, &lines), "two\nthree\n") == 0);
    CHECK_EQ(lines, 2);
    CHECK(strcmp(tail(3, &lines), "one\ntwo\nthree\n") == 0);
    CHECK_EQ(lines, 3);
    CHECK(strcmp(tail(20, &lines), "one\ntwo\nthree\n") == 0);   /* fewer exist */
    CHECK_EQ(lines, 3);
    CHECK(strcmp(tail(0, &lines), "") == 0);
    CHECK_EQ(lines, 0);
}

static void test_unfinished_last_line(void)
{
    uint32_t lines;
    logbuf_init(&b, store, sizeof(store));
    put("one\ntwo\npart");
    CHECK(strcmp(tail(1, &lines), "part") == 0);   /* counts as a line */
    CHECK_EQ(lines, 1);
    CHECK(strcmp(tail(2, &lines), "two\npart") == 0);
    CHECK_EQ(lines, 2);
}

static void test_empty_lines_count(void)
{
    uint32_t lines;
    logbuf_init(&b, store, sizeof(store));
    put("a\n\nb\n");
    CHECK(strcmp(tail(2, &lines), "\nb\n") == 0);
    CHECK_EQ(lines, 2);
    CHECK(strcmp(tail(3, &lines), "a\n\nb\n") == 0);
    CHECK_EQ(lines, 3);
}

static void test_wrap_drops_cut_line(void)
{
    uint32_t lines;
    char line[16];
    logbuf_init(&b, store, sizeof(store));
    /* 20 lines of 8 bytes ("line 00\n" ...) through a 64-byte buffer. */
    for (int i = 0; i < 20; i++) {
        snprintf(line, sizeof(line), "line %02d\n", i);
        put(line);
    }
    CHECK(logbuf_wrapped(&b));
    CHECK_EQ(logbuf_used(&b), 64);
    /* 160 bytes written: 64 kept = exactly lines 12..19, no cut line... */
    CHECK(strcmp(tail(100, &lines), "line 13\nline 14\nline 15\nline 16\nline 17\n"
                                    "line 18\nline 19\n") == 0);
    /* ...but a wrapped buffer can't know the first line is whole, so it is
     * left out: 7 lines shown. */
    CHECK_EQ(lines, 7);
    /* Now cut mid-line: 3 more bytes. */
    put("abc");
    CHECK(strcmp(tail(2, &lines), "line 19\nabc") == 0);
    CHECK_EQ(lines, 2);
    CHECK(strncmp(tail(100, &lines), "line 13\n", 8) == 0);      /* " 12\n" cut off */
    CHECK_EQ(lines, 8);
}

static void test_wrapped_without_newline(void)
{
    uint32_t lines;
    logbuf_init(&b, store, sizeof(store));
    for (int i = 0; i < 200; i++)
        logbuf_putc(&b, 'x');               /* one endless line */
    CHECK(strcmp(tail(5, &lines), "") == 0); /* all of it is a cut line */
    CHECK_EQ(lines, 0);
}

static void test_copy_into_short_buffer(void)
{
    char small[5];
    logbuf_init(&b, store, sizeof(store));
    put("abcdefgh");
    CHECK_EQ(logbuf_copy(&b, small, sizeof(small)), 5);
    CHECK(memcmp(small, "defgh", 5) == 0);  /* the newest bytes */
}

static void test_copy_order_after_wrap(void)
{
    char tiny[8];
    struct logbuf t;
    char out[8];
    logbuf_init(&t, tiny, sizeof(tiny));
    const char *s = "0123456789AB";
    for (const char *p = s; *p; p++)
        logbuf_putc(&t, *p);
    CHECK_EQ(logbuf_copy(&t, out, sizeof(out)), 8);
    CHECK(memcmp(out, "456789AB", 8) == 0);
    /* Exactly full is not wrapped: nothing is lost yet. */
    logbuf_init(&t, tiny, sizeof(tiny));
    for (int i = 0; i < 8; i++)
        logbuf_putc(&t, (char)('a' + i));
    CHECK(!logbuf_wrapped(&t));
    CHECK_EQ(logbuf_copy(&t, out, sizeof(out)), 8);
    CHECK(memcmp(out, "abcdefgh", 8) == 0);
    logbuf_putc(&t, 'i');
    CHECK(logbuf_wrapped(&t));
}

static void test_fit(void)
{
    char out[LOGBUF_LINE_WIDTH + 1];
    char longline[200];
    CHECK_EQ(logbuf_fit("hello\n", 6, out, LOGBUF_LINE_WIDTH), 5);
    CHECK(strcmp(out, "hello") == 0);
    CHECK_EQ(logbuf_fit("", 0, out, LOGBUF_LINE_WIDTH), 0);
    CHECK(strcmp(out, "") == 0);
    CHECK_EQ(logbuf_fit("a\tb\x01" "c\x7f", 6, out, LOGBUF_LINE_WIDTH), 6);
    CHECK(strcmp(out, "a b?c?") == 0);
    /* Exactly 79: unchanged; 80 or more: 76 characters and "...". */
    memset(longline, 'x', sizeof(longline));
    CHECK_EQ(logbuf_fit(longline, 79, out, LOGBUF_LINE_WIDTH), 79);
    CHECK(out[78] == 'x' && out[79] == '\0');
    CHECK_EQ(logbuf_fit(longline, 80, out, LOGBUF_LINE_WIDTH), 79);
    CHECK(strcmp(out + 76, "...") == 0 && out[75] == 'x');
    CHECK_EQ(logbuf_fit(longline, 200, out, LOGBUF_LINE_WIDTH), 79);
    longline[79] = '\n';                    /* 79 characters plus newline: fits */
    CHECK_EQ(logbuf_fit(longline, 80, out, LOGBUF_LINE_WIDTH), 79);
    CHECK(out[78] == 'x');
    /* Tiny widths never write past width. */
    CHECK_EQ(logbuf_fit("abcdef", 6, out, 2), 2);
    CHECK(strcmp(out, "..") == 0);
}

static void test_kernel_size(void)
{
    static char big[LOGBUF_KERNEL_SIZE];
    static char copy[LOGBUF_KERNEL_SIZE];
    struct logbuf k;
    char line[80];
    uint32_t lines;
    logbuf_init(&k, big, sizeof(big));
    /* 1000 lines of 40 bytes: more than 32 KiB, so it wraps. */
    for (int i = 0; i < 1000; i++) {
        snprintf(line, sizeof(line), "Kernel log line number %06d, padding.\n", i);
        for (const char *p = line; *p; p++)
            logbuf_putc(&k, *p);
    }
    CHECK(logbuf_wrapped(&k));
    CHECK_EQ(logbuf_used(&k), LOGBUF_KERNEL_SIZE);
    uint32_t len = logbuf_copy(&k, copy, sizeof(copy));
    uint32_t start = logbuf_tail(copy, len, true, 20, &lines);
    CHECK_EQ(lines, 20);
    CHECK(strncmp(copy + start, "Kernel log line number 000980,", 30) == 0);
    CHECK(copy[len - 1] == '\n');
}

int main(void)
{
    printf("logbuf unit tests\n");
    RUN(test_empty);
    RUN(test_before_wrap);
    RUN(test_unfinished_last_line);
    RUN(test_empty_lines_count);
    RUN(test_wrap_drops_cut_line);
    RUN(test_wrapped_without_newline);
    RUN(test_copy_into_short_buffer);
    RUN(test_copy_order_after_wrap);
    RUN(test_fit);
    RUN(test_kernel_size);
    return unit_report("logbuf");
}
