/*
 * Boot screen layout (80x25 text):
 *
 *   rows  0-11  logo, centred
 *   row  13     M A D I R E   O S
 *   row  14     Madire OS v<version>
 *   rows 16-21  left: start-up checks        right: memory figures
 *   row  22     message (e.g. diagnostics result)
 *   row  23     System ready.
 *
 * The screen and the serial log are written separately: the screen is laid
 * out in two columns, the serial log stays one line per item (with the
 * detail notes that don't fit on screen) so tests can match it.
 */
#include <stddef.h>
#include "drivers/serial/serial.h"
#include "drivers/vga/vga_text.h"
#include "kernel/bootscreen.h"
#include "kernel/kernel.h"
#include "kernel/kprintf.h"
#include "kernel/logo.h"
#include "kernel/memory/pmm.h"

#define LOGO_ROW          0
#define TITLE_ROW         13
#define PANEL_ROW         16
#define LEFT_COL          2
#define LEFT_LABEL_WIDTH  26
#define RIGHT_COL         40
#define RIGHT_LABEL_WIDTH 23
#define MESSAGE_ROW       22
#define READY_ROW         23

static size_t status_row = PANEL_ROW;

static size_t str_len(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void screen_write(size_t row, size_t col, const char *text, enum vga_color fg)
{
    vga_set_cursor(row, col);
    vga_set_color(fg, VGA_BLACK);
    while (*text)
        vga_putc(*text++);
}

/* "label" padded to width, then value, as one serial line. */
static void serial_line(const char *label, size_t width, const char *value, const char *note)
{
    serial_write(label);
    for (size_t n = str_len(label); n < width; n++)
        serial_putc(' ');
    serial_write(value);
    if (note)
        klogf("   (%s)", note);
    serial_putc('\n');
}

static void put_centered(size_t row, const char *text, enum vga_color fg)
{
    vga_set_cursor(row, (VGA_WIDTH - str_len(text)) / 2);
    vga_set_color(fg, VGA_BLACK);
    kprintf("%s\n", text);
}

void bootscreen_draw_header(void)
{
    unsigned lit = logo_render();
    logo_draw(LOGO_ROW, (VGA_WIDTH - LOGO_WIDTH) / 2);
    logo_print_serial(lit);

    put_centered(TITLE_ROW, "M A D I R E   O S", VGA_WHITE);
    put_centered(TITLE_ROW + 1, MADIRE_OS_NAME " v" MADIRE_OS_VERSION, VGA_LIGHT_CYAN);
}

void bootscreen_status(const char *label, enum boot_status status, const char *note)
{
    const char *word = "OK";
    enum vga_color color = VGA_LIGHT_GREEN;
    if (status == BOOT_FAILED) {
        word = "FAILED";
        color = VGA_LIGHT_RED;
    } else if (status == BOOT_PENDING) {
        word = "PENDING";
        color = VGA_YELLOW;
    }

    screen_write(status_row, LEFT_COL, label, VGA_LIGHT_GREY);
    screen_write(status_row, LEFT_COL + LEFT_LABEL_WIDTH, word, color);
    status_row++;
    serial_line(label, LEFT_LABEL_WIDTH, word, note);
}

static void memory_row(size_t row, const char *label, const char *value)
{
    screen_write(row, RIGHT_COL, label, VGA_LIGHT_GREY);
    screen_write(row, RIGHT_COL + RIGHT_LABEL_WIDTH, value, VGA_WHITE);
    serial_line(label, RIGHT_LABEL_WIDTH, value, NULL);
}

void bootscreen_memory(const struct memory_stats *st)
{
    char v[24];
    size_t row = PANEL_ROW;

    ksnprintf(v, sizeof(v), "%6u MB", memory_bytes_to_mb(st->ram_bytes));
    memory_row(row++, "Physical RAM detected:", v);
    ksnprintf(v, sizeof(v), "%6u MB", memory_bytes_to_mb(st->usable_bytes));
    memory_row(row++, "Usable RAM (<4 GiB):", v);
    ksnprintf(v, sizeof(v), "%6u MB", memory_bytes_to_mb(st->above_4g_bytes));
    memory_row(row++, "RAM above 4 GiB:", v);
    ksnprintf(v, sizeof(v), "%6u bytes", PAGE_SIZE);
    memory_row(row++, "Page size:", v);
    ksnprintf(v, sizeof(v), "%6u", st->usable_pages);
    memory_row(row++, "Usable pages:", v);
    ksnprintf(v, sizeof(v), "%6u", st->free_pages);
    memory_row(row++, "Free pages:", v);
}

void bootscreen_message(const char *text)
{
    /* Clear the row first so a shorter message doesn't leave old text. */
    for (size_t col = 0; col < VGA_WIDTH; col++)
        vga_put_cell(MESSAGE_ROW, col, ' ', VGA_LIGHT_GREY, VGA_BLACK);
    screen_write(MESSAGE_ROW, LEFT_COL, text, VGA_YELLOW);
    serial_write(text);
    serial_putc('\n');
}

void bootscreen_ready(void)
{
    screen_write(READY_ROW, LEFT_COL, "System ready.", VGA_LIGHT_GREEN);
    serial_write("System ready.\n");
}
