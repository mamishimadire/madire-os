/* fsmake (1.0 stage 8, a test program): makes a file with known contents
 * for the console's file commands (stage 8 has no command that writes
 * data). Run as a module from a test boot (QEMU -initrd), never from the
 * boot menu.
 *
 *   fsmake <path> <bytes> [text|bin|long]
 *
 * The path is opened with O_CREAT|O_TRUNC (mode 0644), relative paths from
 * the program's current folder (the shell's, since stage 8), and <bytes>
 * bytes are written in pieces of 4096:
 *   text (default): lines of 64 bytes, "line 00001 " then letters, '\n';
 *   bin: the bytes 0-255, repeated;
 *   long: letters a-z, repeated, no newline.
 * One line reports the current folder (getcwd) and the count. Exit status 0,
 * or 1 after a line naming the call that failed and its result. */
#include <stdbool.h>
#include "user/ulib.h"

static char buf[4096];
static char cwd[256];

static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* A decimal number of at most 9 digits, or -1. */
static long number(const char *s)
{
    long v = 0;
    int n = 0;
    for (; *s; s++, n++) {
        if (*s < '0' || *s > '9' || n == 9)
            return -1;
        v = v * 10 + (*s - '0');
    }
    return n ? v : -1;
}

/* The byte at offset off of the contents. */
static char byte_at(int kind, unsigned long off)
{
    if (kind == 1)
        return (char)(off % 256);
    if (kind == 2)
        return (char)('a' + off % 26);
    unsigned long line = off / 64 + 1, col = off % 64;
    if (col == 63)
        return '\n';
    if (col < 5)
        return "line "[col];
    if (col < 10) {
        unsigned long d = line;
        for (unsigned long k = col; k < 9; k++)
            d /= 10;
        return (char)('0' + d % 10);
    }
    if (col == 10)
        return ' ';
    return (char)('a' + (line + col) % 26);
}

static int fail(const char *what, long r)
{
    char line[64];
    uint64_t n = 0;
    const char *p = "fsmake: ";
    while (*p)
        line[n++] = *p++;
    while (*what)
        line[n++] = *what++;
    line[n] = '\0';
    u_put_num(line, r);
    return 1;
}

int main(int argc, char **argv)
{
    int kind = 0;
    if (argc == 4 && same(argv[3], "bin"))
        kind = 1;
    else if (argc == 4 && same(argv[3], "long"))
        kind = 2;
    else if (argc == 4 && !same(argv[3], "text"))
        argc = 0;
    long total = argc == 3 || argc == 4 ? number(argv[2]) : -1;
    if (total < 0) {
        u_puts("fsmake: usage: fsmake <path> <bytes> [text|bin|long]\n");
        return 1;
    }
    long r = sys_getcwd(cwd, sizeof(cwd));
    if (r < 0)
        return fail("getcwd ", r);
    long fd = sys_open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return fail("open ", fd);
    unsigned long done = 0;
    while (done < (unsigned long)total) {
        unsigned long n = (unsigned long)total - done;
        if (n > sizeof(buf))
            n = sizeof(buf);
        for (unsigned long i = 0; i < n; i++)
            buf[i] = byte_at(kind, done + i);
        r = sys_write((uint64_t)fd, buf, n);
        if (r != (long)n)
            return fail("write ", r);
        done += n;
    }
    sys_close((uint64_t)fd);
    char line[96];
    uint64_t k = 0;
    const char *parts[] = { "fsmake: cwd ", cwd, ", wrote to ", argv[1], ": " };
    for (int i = 0; i < 5; i++)
        for (const char *p = parts[i]; *p && k < 60; p++)
            line[k++] = *p;
    line[k] = '\0';
    u_put_num(line, (int64_t)done);
    return 0;
}
