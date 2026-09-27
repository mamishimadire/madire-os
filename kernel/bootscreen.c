/*
 * Boot screen layout (80x25 text):
 *
 *   rows  0-11  logo, centred
 *   row  13     M A D I R E   O S
 *   row  14     Madire OS v<version>
 *   rows 16-21  start-up status lines
 *   row  23     System ready. (or a message such as a crash-test notice)
 */
#include <stddef.h>
#include "drivers/vga/vga_text.h"
#include "kernel/bootscreen.h"
#include "kernel/kernel.h"
#include "kernel/kprintf.h"
#include "kernel/logo.h"

#define LOGO_ROW      0
#define TITLE_ROW     13
#define STATUS_ROW    16
#define STATUS_COL    14
#define LABEL_WIDTH   28

static size_t next_row = STATUS_ROW;

static size_t str_len(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
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
    vga_set_cursor(next_row++, STATUS_COL);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kputs(label);
    for (size_t n = str_len(label); n < LABEL_WIDTH; n++)
        kputc(' ');

    switch (status) {
    case BOOT_OK:
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        kputs("OK");
        break;
    case BOOT_FAILED:
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        kputs("FAILED");
        break;
    case BOOT_PENDING:
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        kputs("PENDING");
        break;
    }

    if (note) {
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        kprintf("   (%s)", note);
    }
    kputc('\n');
}

void bootscreen_message(const char *text)
{
    vga_set_cursor(next_row + 1, STATUS_COL);
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("%s\n", text);
}

void bootscreen_ready(void)
{
    vga_set_cursor(next_row + 1, STATUS_COL);
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kputs("System ready.\n");
}
