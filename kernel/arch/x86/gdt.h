#ifndef MADIRE_ARCH_X86_GDT_H
#define MADIRE_ARCH_X86_GDT_H

#include <stdbool.h>
#include <stdint.h>

/* Segment selectors (index * 8). In long mode code and data segments are
 * flat by definition; the TSS is needed for RSP0 and the IST stacks. The
 * user segments (0.10) are in gdt_desc.h. */
#define GDT_KERNEL_CODE 0x08    /* 64-bit code (L=1) */
#define GDT_KERNEL_DATA 0x10
#define GDT_TSS         0x18    /* 64-bit TSS: 16-byte descriptor, slots 3-4 */

/* Loads Madire OS's own GDT and TSS, reloads CS/DS/ES/SS/FS/GS and TR.
 * Returns true if the CPU reports the new table, selectors and TSS in use
 * and the code descriptor is 64-bit. */
bool gdt_init(void);

/* The 8-byte descriptor for selector in the GDT the CPU is using (sgdt);
 * 0 if the selector is beyond the table's limit. */
uint64_t gdt_descriptor(uint16_t selector);

/* 0.10: the TSS's RSP0 (the kernel stack for entries from ring 3), set on
 * every thread switch, and the IST stack tops (n = 1..7; 0 if unused). */
void gdt_set_rsp0(uint64_t rsp0);
extern void *const gdt_rsp0_slot;          /* where RSP0 is, for the SYSCALL entry */
uint64_t gdt_rsp0(void);
uint64_t gdt_ist(unsigned n);

#endif
