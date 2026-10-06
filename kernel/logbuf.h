#ifndef MADIRE_LOGBUF_H
#define MADIRE_LOGBUF_H

/*
 * Kernel log ring buffer (0.9.1), for dmesg: every character kprintf and
 * klogf write is also kept here, the oldest overwritten once it is full.
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_logbuf.c). The caller provides the storage
 * (the kernel: a static 32 KiB array in kprintf.c) and does the locking
 * (kprintf.c already writes with preemption off).
 *
 * Reading is done on a snapshot: logbuf_copy() gives the kept text, oldest
 * first; logbuf_tail() finds where its last n lines start. Once the buffer
 * has wrapped, the oldest kept line is usually cut at its start, so it is
 * left out. A last line without a newline (still being written) counts.
 */

#include <stdbool.h>
#include <stdint.h>

#define LOGBUF_KERNEL_SIZE  32768u  /* the kernel's buffer */
#define LOGBUF_LINE_WIDTH   79u     /* dmesg lines fit one screen row */

struct logbuf {
    char *data;
    uint32_t cap;           /* bytes of storage */
    uint32_t head;          /* next write position */
    uint64_t total;         /* bytes ever written */
};

void logbuf_init(struct logbuf *b, char *storage, uint32_t cap);
void logbuf_putc(struct logbuf *b, char c);

/* Bytes kept (min(total, cap)). */
uint32_t logbuf_used(const struct logbuf *b);
/* True once older text has been overwritten. */
bool logbuf_wrapped(const struct logbuf *b);

/* Copies the kept text, oldest first, into out (at most cap bytes, no NUL
 * added). Returns the bytes copied. */
uint32_t logbuf_copy(const struct logbuf *b, char *out, uint32_t cap);

/* In a snapshot text[0..len): the offset where its last `n` lines start,
 * and in *lines how many lines that is (fewer if the text has fewer). With
 * `wrapped`, the text before the first newline is a cut line and never
 * shown. n = 0 gives len and 0 lines. */
uint32_t logbuf_tail(const char *text, uint32_t len, bool wrapped, uint32_t n,
                     uint32_t *lines);

/* One line for the screen: line[0..len) without its newline, control
 * characters (tab included) shown as spaces or '?', and cut to `width`
 * characters ending in "..." if longer. out needs width + 1 bytes; it is
 * NUL-terminated. Returns its length. */
uint32_t logbuf_fit(const char *line, uint32_t len, char *out, uint32_t width);

#endif
