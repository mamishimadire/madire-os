/* PIT driver (see pit.h). */
#include "drivers/pit/pit.h"
#include "drivers/pit/pit_divisor.h"
#include "kernel/arch/x86/io.h"

#define PIT_CH0            0x40
#define PIT_CMD            0x43
#define PIT_CMD_CH0_MODE2  0x34     /* channel 0, lobyte/hibyte, mode 2, binary */
#define PIT_CMD_LATCH_CH0  0x00     /* latch channel 0 count */

enum irq_result pit_init(uint32_t hz, irq_handler_t on_tick, uint16_t *divisor_out)
{
    uint16_t divisor;
    if (!pit_divisor_for(hz, &divisor))
        return IRQ_ERR_RANGE;

    enum irq_result r = irq_register(IRQ_TIMER, on_tick);
    if (r != IRQ_OK)
        return r;

    uint32_t flags = irq_save();
    outb(PIT_CMD, PIT_CMD_CH0_MODE2);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, divisor >> 8);
    irq_restore(flags);

    if (divisor_out)
        *divisor_out = divisor;
    return IRQ_OK;
}

uint16_t pit_read_count(void)
{
    uint32_t flags = irq_save();
    outb(PIT_CMD, PIT_CMD_LATCH_CH0);
    uint8_t lo = inb(PIT_CH0);
    uint8_t hi = inb(PIT_CH0);
    irq_restore(flags);
    return (uint16_t)(hi << 8 | lo);
}
