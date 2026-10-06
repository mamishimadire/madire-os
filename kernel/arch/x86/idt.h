#ifndef MADIRE_ARCH_X86_IDT_H
#define MADIRE_ARCH_X86_IDT_H

#include <stdbool.h>

#define IDT_ENTRIES          256
#define IDT_HANDLED_VECTORS  48    /* 0-31 CPU exceptions, 32-47 PIC IRQs */

/* Fills the IDT with the entry stubs from isr.asm (the double fault on
 * IST1; 0.10: NMI and machine check on IST2) and loads it. Returns true if
 * the CPU reports the new table in use. */
bool idt_init(void);

/* 0.10: the IST index (0-7) of `vector` in the IDT the CPU is using (sidt),
 * or -1 beyond its limit. */
int idt_gate_ist(int vector);

#endif
