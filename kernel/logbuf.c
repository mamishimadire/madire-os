/* Kernel log ring buffer (see logbuf.h). */
#include "kernel/logbuf.h"

void logbuf_init(struct logbuf *b, char *storage, uint32_t cap)
{
    b->data = storage;
    b->cap = cap;
    b->head = 0;
    b->total = 0;
}

void logbuf_putc(struct logbuf *b, char c)
{
    if (!b->data || b->cap == 0)
        return;
    b->data[b->head] = c;
    b->head = b->head + 1 == b->cap ? 0 : b->head + 1;
    b->total++;
}

uint32_t logbuf_used(const struct logbuf *b)
{
    return b->total < b->cap ? (uint32_t)b->total : b->cap;
}

bool logbuf_wrapped(const struct logbuf *b)
{
    return b->total > b->cap;
}

uint32_t logbuf_copy(const struct logbuf *b, char *out, uint32_t cap)
{
    uint32_t used = logbuf_used(b);
    uint32_t start = b->total <= b->cap ? 0 : b->head;     /* the oldest byte */
    uint32_t n = used < cap ? used : cap;
    /* With a short out buffer, keep the newest bytes. */
    uint32_t skip = used - n;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t pos = start + skip + i;
        if (pos >= b->cap)
            pos -= b->cap;
        out[i] = b->data[pos];
    }
    return n;
}

uint32_t logbuf_tail(const char *text, uint32_t len, bool wrapped, uint32_t n,
                     uint32_t *lines)
{
    uint32_t first = 0;                 /* where the first whole line starts */
    if (wrapped) {
        while (first < len && text[first] != '\n')
            first++;
        if (first < len)
            first++;                    /* past the cut line's newline */
    }
    *lines = 0;
    if (n == 0 || first >= len)
        return len;

    /* Walk back from the end. A final newline ends the last line; it
     * doesn't start an empty one. */
    uint32_t end = len;
    if (text[end - 1] == '\n')
        end--;
    uint32_t pos = end;
    for (;;) {
        while (pos > first && text[pos - 1] != '\n')
            pos--;
        (*lines)++;
        if (*lines == n || pos <= first)
            return pos;
        pos--;                          /* onto the newline before this line */
    }
}

uint32_t logbuf_fit(const char *line, uint32_t len, char *out, uint32_t width)
{
    if (len && line[len - 1] == '\n')
        len--;
    bool cut = len > width;
    uint32_t keep = cut ? (width >= 3 ? width - 3 : 0) : len;
    uint32_t n = 0;
    for (uint32_t i = 0; i < keep; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c == '\t')
            c = ' ';
        else if (c < 0x20 || c > 0x7E)
            c = '?';
        out[n++] = (char)c;
    }
    if (cut)
        for (uint32_t i = 0; i < 3 && n < width; i++)
            out[n++] = '.';
    out[n] = '\0';
    return n;
}
