#ifndef MADIRE_PROC_FAULTOP_H
#define MADIRE_PROC_FAULTOP_H

/*
 * The instruction bytes at a faulting RIP (0.10), pure:
 * tests/unit/test_faultop.c. When a CPU exception ends a process, the
 * kernel copies the first bytes at RIP (copy_from_user, before the
 * teardown); a fault test lists the opcode each case must fault on, so a
 * fault raised by any other instruction fails the check.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAULTOP_MAX 4u

struct faultop {
    uint8_t n;                      /* bytes held; 0: unreadable (or none expected) */
    uint8_t b[FAULTOP_MAX];
};

/* True if `got` starts with all of `want` (want->n > 0). An unreadable
 * `got`, or one shorter than `want`, never matches. */
static inline bool faultop_match(const struct faultop *got, const struct faultop *want)
{
    if (want->n == 0 || want->n > FAULTOP_MAX || got->n < want->n || got->n > FAULTOP_MAX)
        return false;
    for (unsigned i = 0; i < want->n; i++)
        if (got->b[i] != want->b[i])
            return false;
    return true;
}

/* "0f 0b", or "unreadable" when n is 0; out must hold 16 bytes. */
static inline const char *faultop_str(const struct faultop *op, char out[16])
{
    static const char hex[] = "0123456789abcdef";
    if (op->n == 0 || op->n > FAULTOP_MAX)
        return "unreadable";
    size_t k = 0;
    for (unsigned i = 0; i < op->n; i++) {
        if (i)
            out[k++] = ' ';
        out[k++] = hex[op->b[i] >> 4];
        out[k++] = hex[op->b[i] & 15];
    }
    out[k] = 0;
    return out;
}

#endif
