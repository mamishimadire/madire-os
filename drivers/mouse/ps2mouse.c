/* PS/2 mouse driver (see ps2mouse.h). */
#include <stddef.h>
#include "drivers/mouse/ps2mouse.h"
#include "kernel/arch/x86/io.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/kprintf.h"
#include "kernel/panic.h"
#include "kernel/timer.h"

static struct mouse_queue queue;
static struct mouse_decoder decoder;
static bool present;
static uint16_t mask_before;            /* the PIC mask when mouse_init began */
static volatile uint32_t irq_bytes, irq_empty, irq_keyboard;

/* ---- IRQ12 context: read one port 2 byte, queue it. Nothing else. ---- */

static void mouse_irq(void)
{
    switch (mouse_irq_action(inb(I8042_STATUS))) {
    case MOUSE_IRQ_NONE:
        irq_empty++;                /* edge latched while masked, nothing waiting */
        return;
    case MOUSE_IRQ_KEYBOARD:
        irq_keyboard++;             /* a port 1 byte: IRQ1's */
        return;
    case MOUSE_IRQ_AUX:
        break;
    }
    uint8_t byte = inb(I8042_DATA);
    irq_bytes++;
    mouse_queue_push(&queue, byte); /* counts a drop when full */
}

/* ---- Polled controller access (drivers/ps2/i8042.h), as the keyboard
 * driver's: bounded by the timer when it is verified and interrupts are
 * on, and always by the spin limit. ---- */

static uint8_t hw_read_status(void)      { return inb(I8042_STATUS); }
static uint8_t hw_read_data(void)        { return inb(I8042_DATA); }
static void hw_write_command(uint8_t c)  { outb(I8042_COMMAND, c); }
static void hw_write_data(uint8_t b)     { outb(I8042_DATA, b); }

static bool hw_ticks(uint64_t *now)
{
    if (!timer_verified() || !irq_enabled())
        return false;
    *now = timer_ticks();
    return true;
}

static const struct i8042_bus hw = {
    hw_read_status, hw_read_data, hw_write_command, hw_write_data, hw_ticks,
};

static const char *step_str(const struct mouse_start *r, char *buf, size_t size)
{
    const char *what = "";
    switch (r->step) {
    case MOUSE_START_OK:         what = "started"; break;
    case MOUSE_START_CONFIG:     what = "controller timeout"; break;
    case MOUSE_START_NO_PORT2:   what = "no second PS/2 port"; break;
    case MOUSE_START_TEST:       what = "aux port interface test"; break;
    case MOUSE_START_DEFAULTS:   what = "set defaults"; break;
    case MOUSE_START_REPORTING:  what = "enable reporting"; break;
    case MOUSE_START_IRQ_CONFIG: what = "the configuration with IRQ12 on"; break;
    case MOUSE_START_PORT1:      what = "port 1 back on"; break;
    }
    char test[24] = "";
    if (r->injected)
        ksnprintf(test, sizeof(test), " (test=mousefail=%u)", mouse_fail_step());
    if (r->step == MOUSE_START_CONFIG)
        ksnprintf(buf, size, "%s%s", what, test);
    else if (r->step == MOUSE_START_NO_PORT2)
        ksnprintf(buf, size, "%s: configuration 0x%02x after enabling it", what, r->reply);
    else if (r->timed_out)
        ksnprintf(buf, size, "%s: no reply%s", what, test);
    else
        ksnprintf(buf, size, "%s: reply 0x%02x", what, r->reply);
    return buf;
}

/* The undo, then what it left: the configuration read back (IRQ1 is
 * still masked, so the reply is ours), port 2's ownership, and the PIC
 * mask once IRQ1 is unmasked again. */
static bool give_up(struct mouse_start *r, const char *why)
{
    i8042_set_aux_owned(false);
    mouse_undo(&hw, r);
    if (r->have_before)
        klogf("Mouse: none (%s); configuration 0x%02x restored, port 2 not used; boot continues without a mouse\n",
              why, r->config_before);
    else
        klogf("Mouse: none (%s); configuration not read, port 2 not used; boot continues without a mouse\n", why);
    uint8_t cfg;
    char now[40];
    if (mouse_read_config(&hw, &cfg))
        ksnprintf(now, sizeof(now), "configuration 0x%02x", cfg);
    else
        ksnprintf(now, sizeof(now), "configuration not read back");
    if (!(mask_before & (1u << IRQ_KEYBOARD)))
        irq_enable_line(IRQ_KEYBOARD);
    klogf("Mouse: after the undo: %s, port 2 %s, IRQ mask 0x%04x (before the start: 0x%04x)\n", now,
          i8042_aux_owned() ? "STILL OWNED" : "not owned", pic_get_mask(), mask_before);
    return false;
}

bool mouse_init(void)
{
    struct mouse_start r;
    char why[80];
    mouse_queue_init(&queue);
    mouse_decoder_init(&decoder);

    mask_before = pic_get_mask();
    pic_mask(IRQ_KEYBOARD);             /* the replies are read here, not by IRQ1 */
    if (!mouse_start(&hw, &r))
        return give_up(&r, step_str(&r, why, sizeof(why)));
    enum irq_result reg = irq_register(IRQ_MOUSE, mouse_irq);
    if (reg != IRQ_OK)
        return give_up(&r, irq_result_str(reg));
    i8042_set_aux_owned(true);
    if (!mouse_enable_irq(&hw, &r) || !mouse_finish(&hw, &r))
        return give_up(&r, step_str(&r, why, sizeof(why)));
    klogf("Mouse: PS/2 aux port enabled and tested, defaults set, reporting on; configuration 0x%02x -> 0x%02x "
          "(port 2 clock on, IRQ12 on)\n", r.config_before, r.config_after);
    if (r.dropped || r.skipped)
        klogf("Mouse: %u port 1 bytes dropped during the start\n", r.dropped + r.skipped);

    /* IRQ12, then the cascade line (pic_unmask), then IRQ1 again; the mask
     * must be the one before with IRQ12 and IRQ2 unmasked. */
    enum irq_result en = irq_enable_line(IRQ_MOUSE);
    enum irq_result kb = (mask_before & (1u << IRQ_KEYBOARD)) ? IRQ_OK : irq_enable_line(IRQ_KEYBOARD);
    uint16_t expected = (uint16_t)(mask_before & ~((1u << IRQ_MOUSE) | (1u << IRQ_CASCADE)));
    uint16_t mask = pic_get_mask();
    klogf("Mouse: IRQ12 and the cascade IRQ2 unmasked: IRQ mask 0x%04x (expected 0x%04x)\n", mask, expected);
    if (en != IRQ_OK || kb != IRQ_OK || mask != expected)
        panicf("PIC mask is 0x%04x after the mouse started, expected 0x%04x (IRQ12: %s, IRQ1: %s)",
               mask, expected, irq_result_str(en), irq_result_str(kb));
    present = true;
    return true;
}

bool mouse_present(void)
{
    return present;
}

/* ---- Consumer side (thread 0) ---- */

bool mouse_next_event(struct mouse_event *out)
{
    for (;;) {
        uint32_t flags = irq_save();
        uint8_t byte;
        bool got = mouse_queue_pop(&queue, &byte);
        irq_restore(flags);
        if (!got)
            return false;
        if (mouse_decode(&decoder, byte, out))
            return true;
    }
}

void mouse_counts(struct mouse_counts *out)
{
    uint32_t flags = irq_save();
    out->packets = decoder.packets;
    out->bad_bytes = decoder.bad_bytes;
    out->overflows = decoder.overflows;
    out->dropped = queue.dropped;
    out->irq_bytes = irq_bytes;
    out->queued = irq_bytes - queue.dropped;
    out->irq_empty = irq_empty;
    out->irq_keyboard = irq_keyboard;
    out->irqs = irq_count(IRQ_MOUSE);
    irq_restore(flags);
}
