#ifndef MADIRE_MEMORY_USERCOPY_H
#define MADIRE_MEMORY_USERCOPY_H

/*
 * Copying from and to user memory (0.10; copy_to_user 1.0 stage 4), the
 * kernel side of usercopy_core.h: the range is checked against the
 * address space loaded in CR3 (every page present + user, and writable
 * for copy_to_user), then copied with RFLAGS.AC set only around the copy
 * when SMAP is on. A range that fails the check is not touched, so the
 * copy can never fault in the kernel.
 *
 * Check, then copy (AGENTS.md section 10): 1.0 adds no call that unmaps
 * user memory and no second thread in a process, so a range checked here
 * stays mapped for the copy that follows.
 *
 * Always-on self-check (1.0 stage 4): user memory is never touched while
 * the calling thread holds the VFS lock (kernel/fs/vfs.h): both copies
 * panic, naming the thread, if it does. The filesystem calls copy through
 * a kernel buffer outside the lock, so no user access can ever happen
 * inside a hold.
 */

#include <stdbool.h>
#include <stdint.h>

/* false: the range is not entirely readable user memory (nothing copied). */
bool copy_from_user(void *dst, uint64_t src, uint64_t len);

/* false: the range is not entirely writable user memory (nothing copied). */
bool copy_to_user(uint64_t dst, const void *src, uint64_t len);

/* True if the whole range is readable user memory (nothing is read): lets
 * a caller refuse a buffer before using any part of it. */
bool user_readable(uint64_t addr, uint64_t len);

/* The same for writable user memory (nothing is written). */
bool user_writable(uint64_t addr, uint64_t len);

/* The self-check above, on demand: copies checked since boot. */
uint64_t usercopy_lock_checks(void);

#endif
