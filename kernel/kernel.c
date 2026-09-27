/*
 * Madire OS kernel - milestone 0.1
 *
 * Runs in 32-bit protected mode, entered from boot/boot.asm.
 * Output goes to two places:
 *   - the VGA text buffer at 0xB8000 (what you see in VirtualBox);
 *   - serial port COM1 (so `make test` can check boot output headlessly).
 * Both are deliberately tiny and live here until milestone 0.2 splits
 * them into proper drivers.
 */
#include "kernel.h"

/* ---- Port I/O ---------------------------------------------------------- */

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ---- Serial (COM1) ----------------------------------------------------- */

#define COM1 0x3F8

static void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* disable interrupts */
    outb(COM1 + 3, 0x80);   /* enable DLAB to set baud divisor */
    outb(COM1 + 0, 0x03);   /* divisor 3 -> 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 bits, no parity, 1 stop bit */
    outb(COM1 + 2, 0xC7);   /* enable FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x03);   /* DTR + RTS */
}

static void serial_putc(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0)
        ;
    outb(COM1, (uint8_t)c);
}

/* ---- VGA text mode ----------------------------------------------------- */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static size_t cursor_row;
static size_t cursor_col;
static uint8_t color;

static inline uint8_t vga_attr(enum vga_color fg, enum vga_color bg)
{
    return (uint8_t)(fg | bg << 4);
}

static void vga_clear(void)
{
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = (uint16_t)' ' | (uint16_t)color << 8;
    cursor_row = 0;
    cursor_col = 0;
}

static void vga_disable_cursor(void)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static void kputc(char c)
{
    serial_putc(c);

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else {
        vga[cursor_row * VGA_WIDTH + cursor_col] = (uint16_t)c | (uint16_t)color << 8;
        if (++cursor_col == VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }
    /* No scrolling yet: wrap to the top rather than write past the buffer. */
    if (cursor_row == VGA_HEIGHT)
        cursor_row = 0;
}

static void kputs(const char *s)
{
    while (*s)
        kputc(*s++);
}

static void puts_color(const char *s, enum vga_color fg)
{
    uint8_t saved = color;
    color = vga_attr(fg, VGA_BLACK);
    kputs(s);
    color = saved;
}

static void puts_centered(const char *s, enum vga_color fg)
{
    size_t len = 0;
    while (s[len])
        len++;
    for (size_t i = 0; i < (VGA_WIDTH - len) / 2; i++)
        kputc(' ');
    puts_color(s, fg);
    kputc('\n');
}

static void status(const char *label, int ok)
{
    kputs(label);
    puts_color(ok ? "OK\n" : "FAILED\n", ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED);
}

/* ---- Kernel entry ------------------------------------------------------ */

static const char rule[] =
    "-------------------------------------------------------------------------------";

void kernel_main(uint32_t magic, uint32_t mb_info)
{
    (void)mb_info;  /* memory map is parsed from milestone 0.3 onwards */

    serial_init();
    color = vga_attr(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
    vga_disable_cursor();

    puts_color(rule, VGA_CYAN);
    kputc('\n');
    puts_centered("M A D I R E   O S", VGA_WHITE);
    kputc('\n');
    puts_color(rule, VGA_CYAN);
    kputs("\n\n");

    puts_color(MADIRE_OS_NAME " v" MADIRE_OS_VERSION "\n\n", VGA_YELLOW);

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        puts_color("Error: not loaded by a Multiboot bootloader.\n", VGA_LIGHT_RED);
        return;
    }

    kputs("Kernel started successfully.\n\n");

    /* Milestone 0.1 performs no real subsystem setup; these lines mark
     * where CPU (GDT/IDT), memory and display drivers will report in. */
    status("CPU initialization:     ", 1);
    status("Memory initialization:  ", 1);
    status("Display initialization: ", 1);

    kputc('\n');
    puts_color("System ready.\n", VGA_LIGHT_GREEN);
}
