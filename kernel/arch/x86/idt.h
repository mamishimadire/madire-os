#ifndef MADIRE_ARCH_X86_IDT_H
#define MADIRE_ARCH_X86_IDT_H

#include <stdbool.h>

#define IDT_ENTRIES          256
#define IDT_HANDLED_VECTORS  48    /* 0-31 CPU exceptions, 32-47 PIC IRQs */

/* Fills the IDT with the entry stubs from isr.asm and loads it.
 * Returns true if the CPU reports the new table in use. */
bool idt_init(void);

#endif
