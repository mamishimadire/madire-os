#ifndef MADIRE_MEMORY_USERCOPY_CORE_H
#define MADIRE_MEMORY_USERCOPY_CORE_H

/*
 * Validation of user buffers for copy_from_user / copy_to_user (0.10).
 *
 * Pure module: the page lookup is a callback (the kernel walks the current
 * process's tables; tests/unit/test_usercopy.c uses a fake), so the kernel
 * side only adds stac/clac around the copy itself.
 *
 * A range [addr, addr + len) is valid when:
 *   - len is 0 (nothing is touched, whatever addr is), or
 *   - addr + len does not wrap and is at most VM_USER_TOP, and every page it
 *     touches is mapped present + user (+ writable, for copy_to_user).
 * Nothing in 0.10 can unmap a process's pages while one of its system calls
 * runs (one thread per process, no memory system calls), so a range checked
 * here stays valid for the copy that follows.
 */

#include <stdbool.h>
#include <stdint.h>

/* Leaf flags of the page at va in the current address space (PTE_* bits);
 * false if nothing is mapped there. */
typedef bool (*ucopy_lookup_fn)(void *ctx, uint64_t va, uint64_t *flags_out);

/* The arithmetic part only: no wrap-around, end at most VM_USER_TOP. */
bool ucopy_range_ok(uint64_t addr, uint64_t len);

/* The whole check, page by page; write = the kernel will write the range. */
bool ucopy_check(uint64_t addr, uint64_t len, bool write, ucopy_lookup_fn lookup, void *ctx);

#endif
