#ifndef MADIRE_ARCH_X86_PIC_H
#define MADIRE_ARCH_X86_PIC_H

#include <stdbool.h>
#include <stdint.h>

/* Remaps the two 8259 PICs to vectors 32-47 and masks every IRQ.
 * Returns true if both mask registers read back as fully masked. */
bool pic_init(void);

/* Sends end-of-interrupt for a real IRQ; ignores spurious IRQ 7/15.
 * Returns false if the IRQ was spurious. */
bool pic_acknowledge(uint8_t irq);

#endif
