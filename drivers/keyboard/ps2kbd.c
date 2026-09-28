/* PS/2 keyboard driver (see ps2kbd.h). */
#include "drivers/keyboard/ps2kbd.h"
#include "kernel/arch/x86/io.h"
#include "kernel/arch/x86/irq.h"
#include "kernel/input/keyqueue.h"
#include "kernel/timer.h"

#define KBD_DATA            0x60
#define KBD_STATUS          0x64    /* read */
#define KBD_COMMAND         0x64    /* write */

#define STATUS_OUTPUT_FULL  0x01    /* a byte is waiting in port 0x60 */
#define STATUS_INPUT_FULL   0x02    /* controller has not taken our last byte */
#define STATUS_AUX_DATA     0x20    /* the waiting byte is from port 2 (mouse) */

#define CMD_READ_CONFIG     0x20
#define CMD_WRITE_CONFIG    0x60
#define CMD_ENABLE_PORT1    0xAE

#define CFG_PORT1_IRQ       0x01
#define CFG_PORT2_IRQ       0x02
#define CFG_PORT1_CLOCK_OFF 0x10
#define CFG_TRANSLATE       0x40

#define DEV_ECHO            0xEE

#define SPIN_LIMIT          1000000u   /* per controller wait */
#define FLUSH_LIMIT         64u
#define ECHO_WAIT_TICKS     20u        /* 200 ms at 100 Hz */

static struct keyqueue queue;
static struct scancode_state decoder;
static volatile uint32_t bytes_read;
static volatile uint32_t responses;
static volatile uint8_t last_response;

/* ---- IRQ1 context: read one byte, decode, queue. Nothing else. ---- */

static void keyboard_irq(void)
{
    uint8_t status = inb(KBD_STATUS);
    if (!(status & STATUS_OUTPUT_FULL))
        return;                     /* edge latched while masked, data already gone */
    uint8_t byte = inb(KBD_DATA);
    bytes_read++;
    if (status & STATUS_AUX_DATA)
        return;                     /* port 2 byte: not ours */
    if (scancode_is_response(byte)) {
        last_response = byte;
        responses++;
        return;
    }
    struct key_event e;
    if (scancode_decode(&decoder, byte, &e))
        keyqueue_push(&queue, &e);  /* counts a drop when full */
}

/* ---- Polled controller access (used only while IRQ1 is masked). ---- */

static bool wait_can_write(void)
{
    for (uint32_t i = 0; i < SPIN_LIMIT; i++)
        if (!(inb(KBD_STATUS) & STATUS_INPUT_FULL))
            return true;
    return false;
}

static bool wait_can_read(void)
{
    for (uint32_t i = 0; i < SPIN_LIMIT; i++)
        if (inb(KBD_STATUS) & STATUS_OUTPUT_FULL)
            return true;
    return false;
}

static bool send_command(uint8_t cmd)
{
    if (!wait_can_write())
        return false;
    outb(KBD_COMMAND, cmd);
    return true;
}

static bool send_data(uint8_t data)
{
    if (!wait_can_write())
        return false;
    outb(KBD_DATA, data);
    return true;
}

static bool read_data(uint8_t *out)
{
    if (!wait_can_read())
        return false;
    *out = inb(KBD_DATA);
    return true;
}

static void flush_output(void)
{
    for (uint32_t i = 0; i < FLUSH_LIMIT && (inb(KBD_STATUS) & STATUS_OUTPUT_FULL); i++)
        (void)inb(KBD_DATA);
}

enum kbd_status keyboard_init(void)
{
    if (inb(KBD_STATUS) == 0xFF)
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

    /* register -> device ready -> clear pending -> unmask (verified) */
    if (irq_register(IRQ_KEYBOARD, keyboard_irq) != IRQ_OK)
        return KBD_IRQ;
    flush_output();
    if (irq_enable_line(IRQ_KEYBOARD) != IRQ_OK)
        return KBD_IRQ;
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
