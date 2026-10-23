#ifndef MADIRE_ARCH_X86_PIC_H
#define MADIRE_ARCH_X86_PIC_H

#include <stdbool.h>
#include <stdint.h>

/* Remaps the two 8259 PICs to vectors 32-47 and masks every IRQ.
 * Returns true if both mask registers read back as fully masked. */
bool pic_init(void);

/* True if IRQ 7 or 15 was spurious (not really in service). A spurious
 * IRQ 15 still gets EOI on the master's cascade line; the caller must not
 * send another EOI for a spurious IRQ. Always false for other lines. */
bool pic_is_spurious(uint8_t irq);

/* End of interrupt for a real IRQ (slave and master for IRQ 8-15). */
void pic_send_eoi(uint8_t irq);

/* 1.2: unmasking a slave line (8-15) also unmasks the cascade line, IRQ 2;
 * masking one leaves IRQ 2 as it is. */
void pic_unmask(uint8_t irq);
void pic_mask(uint8_t irq);

/* Current mask: bit n set = IRQ n masked. */
uint16_t pic_get_mask(void);

#endif
