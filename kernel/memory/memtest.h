#ifndef MADIRE_MEMORY_MEMTEST_H
#define MADIRE_MEMORY_MEMTEST_H

#include <stdint.h>

/*
 * Memory self-tests on the real allocator and real RAM. They write to the
 * pages they allocate through their physical addresses, which is valid only
 * while paging is off (until milestone 0.5).
 */

/* Runs on every boot, after memory_init(). Returns the number of checks
 * passed; any failure is fatal (crash screen), so reaching the return means
 * the allocator was verified. */
uint32_t memtest_boot(void);

/* Diagnostics (test=memexhaust): allocates every free page, verifies each,
 * then frees them all. Returns the number of pages; failure is fatal. */
uint32_t memtest_exhaust(void);

/* Diagnostics that must end on the crash screen. */
void memtest_double_free(void) __attribute__((noreturn));
void memtest_free_reserved(void) __attribute__((noreturn));

#endif
