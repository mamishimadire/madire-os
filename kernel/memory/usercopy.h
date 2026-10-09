#ifndef MADIRE_MEMORY_USERCOPY_H
#define MADIRE_MEMORY_USERCOPY_H

/*
 * Copying from user memory (0.10), the kernel side of usercopy_core.h:
 * the range is checked against the address space loaded in CR3 (every
 * page present + user), then copied with RFLAGS.AC set only around the
 * copy when SMAP is on. A range that fails the check is not touched, so
 * the copy can never fault in the kernel.
 */

#include <stdbool.h>
#include <stdint.h>

/* false: the range is not entirely readable user memory (nothing copied). */
bool copy_from_user(void *dst, uint64_t src, uint64_t len);

/* True if the whole range is readable user memory (nothing is read): lets
 * a caller refuse a buffer before using any part of it. */
bool user_readable(uint64_t addr, uint64_t len);

#endif
