#ifndef MADIRE_ARCH_X86_GDT_H
#define MADIRE_ARCH_X86_GDT_H

#include <stdbool.h>

/* Segment selectors (index * 8). Flat 0-4 GiB segments, ring 0 only. */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10

/* Loads Madire OS's own GDT and reloads every segment register.
 * Returns true if the CPU reports the new table and selectors in use. */
bool gdt_init(void);

#endif
