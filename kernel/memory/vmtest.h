#ifndef MADIRE_MEMORY_VMTEST_H
#define MADIRE_MEMORY_VMTEST_H

#include <stdbool.h>

/*
 * Virtual-memory diagnostics (GRUB "Diagnostics" menu, test=<name>). Each
 * one makes a deliberate page fault, which must end on the crash screen
 * with exception 14 and the decoded "Page fault:" line; if the access does
 * not fault, the diagnostic itself panics with "... was not detected".
 */

/* test=pfnull: read from address 0 (the NULL guard page). */
void vmtest_pf_null(void) __attribute__((noreturn));
/* test=pfunmapped: read from 0x40000000 (never mapped). */
void vmtest_pf_unmapped(void) __attribute__((noreturn));
/* test=pfreadonly: write to the first kernel code page (read-only, CR0.WP). */
void vmtest_pf_readonly(void) __attribute__((noreturn));

/*
 * Kernel heap diagnostics (QEMU only, no GRUB entries). The first three
 * must end on the crash screen with the kfree error message.
 */
/* test=heapdouble: kfree the same block twice. */
void vmtest_heap_double(void) __attribute__((noreturn));
/* test=heapinvalid: kfree a stack address. */
void vmtest_heap_invalid(void) __attribute__((noreturn));
/* test=heapcorrupt: overwrite a block header, then kfree the block. */
void vmtest_heap_corrupt(void) __attribute__((noreturn));
/* test=heapexhaust: allocate until kmalloc returns NULL (16 MiB cap or no
 * physical memory left), check, free everything, check again. true = PASS;
 * the result line is logged. */
bool vmtest_heap_exhaust(void);

#endif
