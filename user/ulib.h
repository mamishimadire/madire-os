#ifndef MADIRE_USER_ULIB_H
#define MADIRE_USER_ULIB_H

/* A few helpers for the built-in user programs (0.10): no C library yet. */

#include <stdint.h>
#include "user/syscall.h"

/* Kernel addresses a program must never be able to touch (vm_layout.h). */
#define K_TEXT      0xFFFFFFFF80100000ull   /* kernel image */
#define K_PML4      0xFFFFFF7FBFDFE000ull   /* the PML4 through the recursive slot */
#define K_HEAP      0xFFFFFE8000000000ull   /* kernel heap */
#define K_PHYS_VGA  0xFFFF8000000B8000ull   /* VGA text buffer in the physical view */
#define USER_TOP    0x00007FFFFFFFF000ull

static inline uint64_t u_strlen(const char *s)
{
    uint64_t n = 0;
    while (s[n])
        n++;
    return n;
}

static inline long u_puts(const char *s)
{
    return sys_write(1, s, u_strlen(s));
}

/* "<text><number>\n" in one write, so it is one log line. */
static inline long u_put_num(const char *text, int64_t v)
{
    char buf[96];
    uint64_t n = 0;
    while (text[n] && n < 60) {
        buf[n] = text[n];
        n++;
    }
    uint64_t m = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    char digits[20];
    int nd = 0;
    do {
        digits[nd++] = (char)('0' + m % 10);
        m /= 10;
    } while (m);
    if (v < 0)
        buf[n++] = '-';
    while (nd)
        buf[n++] = digits[--nd];
    buf[n++] = '\n';
    return sys_write(1, buf, n);
}

/* Records one expectation: prints it if it failed. Returns 1 on failure. */
static inline int u_expect(const char *what, long got, long want)
{
    if (got == want)
        return 0;
    u_put_num(what, got);
    return 1;
}

#endif
