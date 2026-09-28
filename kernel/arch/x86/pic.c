/*
 * 8259 Programmable Interrupt Controller (master + slave).
 *
 * After BIOS boot the master PIC delivers IRQ 0-7 on vectors 8-15, which are
 * also CPU exception vectors (8 = Double Fault). They are moved to 32-47
 * before interrupts are enabled.
 */
#include "kernel/arch/x86/io.h"
#include "kernel/arch/x86/pic.h"

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define ICW1_INIT_ICW4  0x11
#define ICW4_8086       0x01
#define PIC_EOI         0x20
#define OCW3_READ_ISR   0x0B

bool pic_init(void)
{
    outb(PIC1_CMD, ICW1_INIT_ICW4);  io_wait();
    outb(PIC2_CMD, ICW1_INIT_ICW4);  io_wait();
    outb(PIC1_DATA, 32);             io_wait();   /* master: vectors 32-39 */
    outb(PIC2_DATA, 40);             io_wait();   /* slave: vectors 40-47 */
    outb(PIC1_DATA, 0x04);           io_wait();   /* slave on master IRQ 2 */
    outb(PIC2_DATA, 0x02);           io_wait();   /* slave cascade identity */
    outb(PIC1_DATA, ICW4_8086);      io_wait();
    outb(PIC2_DATA, ICW4_8086);      io_wait();

    outb(PIC1_DATA, 0xFF);                        /* mask every IRQ */
    outb(PIC2_DATA, 0xFF);

    return inb(PIC1_DATA) == 0xFF && inb(PIC2_DATA) == 0xFF;
}

static uint16_t pic_in_service(void)
{
    outb(PIC1_CMD, OCW3_READ_ISR);
    outb(PIC2_CMD, OCW3_READ_ISR);
    return (uint16_t)(inb(PIC2_CMD) << 8 | inb(PIC1_CMD));
}

bool pic_is_spurious(uint8_t irq)
{
    /* IRQ 7 / 15 can be spurious: not really in service, so no EOI for that
     * PIC (a spurious IRQ 15 still needs EOI on the master's cascade line). */
    if ((irq == 7 || irq == 15) && !(pic_in_service() & (1u << irq))) {
        if (irq == 15)
            outb(PIC1_CMD, PIC_EOI);
        return true;
    }
    return false;
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_unmask(uint8_t irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) & (uint8_t)~(1u << (irq & 7)));
}

void pic_mask(uint8_t irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) | (uint8_t)(1u << (irq & 7)));
}

uint16_t pic_get_mask(void)
{
    return (uint16_t)(inb(PIC2_DATA) << 8 | inb(PIC1_DATA));
}
