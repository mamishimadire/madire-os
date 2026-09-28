#ifndef MADIRE_MEMORY_MEMTEST_H
#define MADIRE_MEMORY_MEMTEST_H

#include <stdint.h>

/*
 * Memory self-tests on the real allocator and real RAM. They reach the
 * pages they allocate directly while paging is off, and through the
 * physical-access window (paging.h) once it is on.
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
