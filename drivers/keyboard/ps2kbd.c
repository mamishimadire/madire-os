/* PS/2 keyboard driver (see ps2kbd.h). */
#include <stddef.h>
#include "drivers/keyboard/ps2kbd.h"
#include "drivers/keyboard/ps2leds.h"
#include "drivers/ps2/i8042.h"
#include "kernel/arch/x86/io.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/input/keyqueue.h"
#include "kernel/timer.h"

#define CMD_READ_CONFIG     0x20
#define CMD_WRITE_CONFIG    0x60
#define CMD_ENABLE_PORT1    0xAE

#define CFG_PORT1_IRQ       0x01
#define CFG_PORT2_IRQ       0x02
#define CFG_PORT1_CLOCK_OFF 0x10
#define CFG_TRANSLATE       0x40

#define DEV_ECHO            0xEE

#define REPLY_LIMIT         64u        /* bytes read for one polled reply */
#define ECHO_WAIT_TICKS     20u        /* 200 ms at 100 Hz */
#define LIGHTS_WAIT_TICKS   3u         /* per reply at run time: 30 ms at 100 Hz */

static struct keyqueue queue;
static struct scancode_state decoder;
static volatile uint32_t bytes_read;
static volatile uint32_t responses;
static volatile uint8_t last_response;

static struct kbd_lights boot_lights;
static bool lights_tried;       /* keyboard_init reached the boot exchange */
static bool lights_off;         /* a failure: no more exchanges until restart */
static bool kbd_ready;          /* keyboard_init returned KBD_OK (IRQ1 handler live) */
static uint8_t lights_set;      /* the state last acknowledged */

/* ---- IRQ1 context: read one byte, decode, queue. Nothing else. ---- */

static void keyboard_irq(void)
{
    enum i8042_irq1_action act = i8042_irq1_action(inb(I8042_STATUS));
    if (act == I8042_IRQ1_NONE)
        return;                     /* edge latched while masked, data already gone */
    if (act == I8042_IRQ1_LEAVE)
        return;                     /* 1.2: port 2 owned: its byte is left for IRQ12 */
    uint8_t byte = inb(I8042_DATA);
    bytes_read++;
    if (act == I8042_IRQ1_DISCARD)
        return;                     /* port 2 byte, no owner: not ours */
    if (scancode_is_response(byte)) {
        last_response = byte;
        responses++;
        return;
    }
    struct key_event e;
    if (scancode_decode(&decoder, byte, &e))
        keyqueue_push(&queue, &e);  /* counts a drop when full */
}

/* ---- Polled controller access (drivers/ps2/i8042.h; reads only while
 * IRQ1 is masked). 1.2: every wait is bounded by the timer when it is
 * verified and interrupts are on, and always by the spin limit. ---- */

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

static bool send_command(uint8_t cmd)
{
    return i8042_send_command(&hw, cmd);
}

static bool send_data(uint8_t data)
{
    return i8042_send_data(&hw, data);
}

static bool read_data(uint8_t *out)
{
    return i8042_read_data(&hw, out);
}

static void flush_output(void)
{
    i8042_flush(&hw);
}

/* ---- The keyboard lights (1.1, see ps2leds.h) ---- */

static void lights_result(struct kbd_lights *out, const struct leds_exchange *x)
{
    out->state = x->state;
    out->ok = x->outcome == LEDS_DONE;
    out->failed_byte = x->failed_byte;
    out->reply = x->reply;
    out->timed_out = x->timed_out;
}

/* Polled (IRQ1 masked): the next reply byte; a byte that is not a reply is
 * discarded, as flush_output discards bytes before IRQ1 is unmasked. 1.2:
 * one wait for the whole reply, the discarded bytes included. */
static int read_reply_polled(void)
{
    struct i8042_wait w;
    i8042_wait_begin(&hw, &w);
    uint8_t byte;
    for (uint32_t i = 0; i < REPLY_LIMIT; i++) {
        if (!i8042_read_data_within(&hw, &w, &byte))
            return LEDS_TIMEOUT;
        if (scancode_is_response(byte))
            return byte;
    }
    return LEDS_TIMEOUT;
}

static void lights_boot(void)
{
    struct leds_exchange x;
    uint8_t byte = leds_begin(&x, scancode_lights(&decoder));
    enum leds_step step;
    do {
        int reply = send_data(byte) ? read_reply_polled() : LEDS_TIMEOUT;
        step = leds_reply(&x, reply, &byte);
    } while (step == LEDS_SEND);
    lights_result(&boot_lights, &x);
    lights_tried = true;
    lights_off = !boot_lights.ok;
    lights_set = x.state;
}

/* Run time (IRQ1 live): the reply arrives through keyboard_irq, which moves
 * <responses>. cli / check / "sti; hlt": sti takes effect after hlt starts,
 * so a reply that arrives after the check still wakes the hlt. */
static int wait_reply_irq(uint32_t before)
{
    uint64_t deadline = timer_ticks() + LIGHTS_WAIT_TICKS;
    for (;;) {
        __asm__ volatile ("cli" : : : "memory");
        if (responses != before) {
            uint8_t reply = last_response;
            __asm__ volatile ("sti" : : : "memory");
            return reply;
        }
        if (timer_ticks() >= deadline) {
            __asm__ volatile ("sti" : : : "memory");
            return LEDS_TIMEOUT;
        }
        __asm__ volatile ("sti; hlt" : : : "memory");
    }
}

const struct kbd_lights *keyboard_lights(void)
{
    return lights_tried ? &boot_lights : NULL;
}

enum kbd_sync keyboard_lights_sync(struct kbd_lights *out)
{
    if (!kbd_ready || lights_off || !timer_verified() || !irq_enabled())
        return KBD_SYNC_NONE;
    uint32_t flags = irq_save();
    uint8_t state = scancode_lights(&decoder);
    irq_restore(flags);
    if (state == lights_set)
        return KBD_SYNC_NONE;

    struct leds_exchange x;
    uint8_t byte = leds_begin(&x, state);
    enum leds_step step;
    do {
        uint32_t before = responses;
        int reply = send_data(byte) ? wait_reply_irq(before) : LEDS_TIMEOUT;
        step = leds_reply(&x, reply, &byte);
    } while (step == LEDS_SEND);
    lights_result(out, &x);
    if (step != LEDS_DONE) {
        lights_off = true;
        return KBD_SYNC_FAILED;
    }
    lights_set = x.state;
    return KBD_SYNC_DONE;
}

enum kbd_status keyboard_init(void)
{
    if (inb(I8042_STATUS) == 0xFF)
        return KBD_NO_CONTROLLER;

    keyqueue_init(&queue);
    scancode_init(&decoder);
    flush_output();

    /* Configuration: port 1 IRQ on, translation (set 1) on, port 1 clock
     * on, port 2 IRQ off. Everything else is left as the BIOS set it. */
    uint8_t cfg;
    if (!send_command(CMD_READ_CONFIG) || !read_data(&cfg))
        return KBD_TIMEOUT;
    cfg = (uint8_t)((cfg | CFG_PORT1_IRQ | CFG_TRANSLATE) & ~(CFG_PORT2_IRQ | CFG_PORT1_CLOCK_OFF));
    uint8_t check;
    if (!send_command(CMD_WRITE_CONFIG) || !send_data(cfg))
        return KBD_TIMEOUT;
    if (!send_command(CMD_READ_CONFIG) || !read_data(&check))
        return KBD_TIMEOUT;
    if (check != cfg)
        return KBD_CONFIG;
    if (!send_command(CMD_ENABLE_PORT1))
        return KBD_TIMEOUT;

    /* A keyboard is present if it echoes. Polled: IRQ1 is still masked. */
    flush_output();
    uint8_t reply = 0;
    if (!send_data(DEV_ECHO) || !read_data(&reply) || reply != DEV_ECHO)
        return KBD_NO_KEYBOARD;

    /* 1.1: the lights, polled (IRQ1 still masked); a failure only turns
     * them off, the keyboard's status does not depend on them. */
    lights_boot();

    /* register -> device ready -> clear pending -> unmask (verified) */
    if (irq_register(IRQ_KEYBOARD, keyboard_irq) != IRQ_OK)
        return KBD_IRQ;
    flush_output();
    if (irq_enable_line(IRQ_KEYBOARD) != IRQ_OK)
        return KBD_IRQ;
    kbd_ready = true;
    return KBD_OK;
}

/* ---- Self-test ---- */

static bool decoder_selftest(void)
{
    /* Shift+A, release both, then E0 48 (Up) and its release. */
    static const uint8_t bytes[] = { 0x2A, 0x1E, 0x9E, 0xAA, 0xE0, 0x48, 0xE0, 0xC8 };
    struct scancode_state s;
    struct key_event e[8];
    unsigned n = 0;
    scancode_init(&s);
    for (unsigned i = 0; i < sizeof(bytes); i++)
        if (n < 8 && scancode_decode(&s, bytes[i], &e[n]))
            n++;
    return n == 6
        && e[0].keycode == KEY_LSHIFT && (e[0].flags & KEY_PRESSED)
        && e[1].keycode == KEY_CHAR && e[1].ascii == 'A' && (e[1].flags & KEY_SHIFT)
        && e[2].keycode == KEY_CHAR && !(e[2].flags & KEY_PRESSED)
        && e[3].keycode == KEY_LSHIFT && !(e[3].flags & KEY_PRESSED)
        && e[4].keycode == KEY_UP && (e[4].flags & (KEY_PRESSED | KEY_EXTENDED)) == (KEY_PRESSED | KEY_EXTENDED)
        && e[5].keycode == KEY_UP && !(e[5].flags & KEY_PRESSED);
}

static bool queue_selftest(void)
{
    static struct keyqueue q;       /* private: not the live keyboard queue */
    struct key_event e = { KEY_CHAR, 'x', KEY_PRESSED, 0x2D }, out;
    keyqueue_init(&q);
    for (uint32_t i = 0; i < KEYQUEUE_SIZE; i++) {
        e.scancode = (uint8_t)i;
        if (!keyqueue_push(&q, &e))
            return false;
    }
    if (keyqueue_push(&q, &e) || keyqueue_dropped(&q) != 1)
        return false;               /* 65th must be dropped and counted */
    for (uint32_t i = 0; i < KEYQUEUE_SIZE; i++)
        if (!keyqueue_pop(&q, &out) || out.scancode != (uint8_t)i)
            return false;           /* FIFO order kept */
    return !keyqueue_pop(&q, &out);
}

/* ECHO sent with IRQ1 unmasked: the reply must arrive through the handler. */
static bool irq_selftest(void)
{
    uint32_t before_responses = responses;
    uint32_t before_irqs = irq_count(IRQ_KEYBOARD);
    last_response = 0;
    if (!send_data(DEV_ECHO))
        return false;

    if (timer_verified()) {
        uint64_t deadline = timer_ticks() + ECHO_WAIT_TICKS;
        while (responses == before_responses && timer_ticks() < deadline)
            __asm__ volatile ("hlt");
    } else {
        for (uint32_t i = 0; i < 50000000u && responses == before_responses; i++)
            __asm__ volatile ("pause");
    }
    return responses != before_responses && last_response == DEV_ECHO
        && irq_count(IRQ_KEYBOARD) != before_irqs;
}

bool keyboard_selftest(struct kbd_selftest *out)
{
    out->decoder_ok = decoder_selftest();
    out->queue_ok = queue_selftest();
    out->irq_delivered = irq_enabled() && irq_selftest();
    return out->decoder_ok && out->queue_ok && out->irq_delivered;
}

/* ---- Consumer side ---- */

bool keyboard_get_event(struct key_event *out)
{
    uint32_t flags = irq_save();
    bool ok = keyqueue_pop(&queue, out);
    irq_restore(flags);
    return ok;
}

uint32_t keyboard_queued(void)
{
    uint32_t flags = irq_save();
    uint32_t n = keyqueue_count(&queue);
    irq_restore(flags);
    return n;
}

uint32_t keyboard_dropped(void)
{
    return keyqueue_dropped(&queue);
}

uint32_t keyboard_bytes(void)
{
    return bytes_read;
}

const char *kbd_status_str(enum kbd_status status)
{
    switch (status) {
    case KBD_OK:            return "ok";
    case KBD_NO_CONTROLLER: return "no i8042 controller";
    case KBD_TIMEOUT:       return "controller timeout";
    case KBD_CONFIG:        return "configuration did not stick";
    case KBD_NO_KEYBOARD:   return "no keyboard answered";
    case KBD_IRQ:           return "IRQ1 could not be enabled";
    }
    return "unknown keyboard error";
}
