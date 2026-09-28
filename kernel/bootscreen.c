/*
 * Boot screen layout (80x25 text):
 *
 *   rows  0-11  logo, centred (unchanged since 0.2)
 *   row  12     M A D I R E   O S
 *   row  13     Madire OS v<version>
 *   rows 15-22  left: 8 start-up checks
 *               right: 6 memory figures (15-20), Uptime (21), Last key (22)
 *   row  23     message (e.g. diagnostics result)
 *   row  24     System ready.
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
#define TITLE_ROW         12
#define PANEL_ROW         15
#define PANEL_ROWS        8
#define LEFT_COL          2
#define LEFT_LABEL_WIDTH  26
#define RIGHT_COL         40
#define RIGHT_LABEL_WIDTH 23
#define RIGHT_VALUE_WIDTH 6
#define UPTIME_ROW        21
#define LAST_KEY_ROW      22
#define MESSAGE_ROW       23
#define READY_ROW         24

static size_t status_row = PANEL_ROW;

static size_t str_len(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* Writes text at (row, col), clipped at the end of the row (never wraps). */
static void screen_write(size_t row, size_t col, const char *text, enum vga_color fg)
{
    vga_set_cursor(row, col);
    vga_set_color(fg, VGA_BLACK);
    for (size_t c = col; *text && c < VGA_WIDTH; c++)
        vga_putc(*text++);
}

static void clear_cells(size_t row, size_t from_col, size_t to_col)
{
    for (size_t col = from_col; col < to_col; col++)
        vga_put_cell(row, col, ' ', VGA_LIGHT_GREY, VGA_BLACK);
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

/* Right-column live value: label, then value right-aligned like the
 * memory figures. Screen only. */
static void live_row(size_t row, const char *label, const char *value)
{
    char v[24];
    size_t len = str_len(value), pad = len < RIGHT_VALUE_WIDTH ? RIGHT_VALUE_WIDTH - len : 0;
    size_t i = 0;
    for (; i < pad; i++)
        v[i] = ' ';
    for (size_t j = 0; value[j] && i < sizeof(v) - 1; j++)
        v[i++] = value[j];
    v[i] = '\0';

    clear_cells(row, RIGHT_COL, VGA_WIDTH);
    screen_write(row, RIGHT_COL, label, VGA_LIGHT_GREY);
    screen_write(row, RIGHT_COL + RIGHT_LABEL_WIDTH, v, VGA_WHITE);
}

void bootscreen_uptime(uint32_t seconds)
{
    char v[24];
    ksnprintf(v, sizeof(v), "%u s", seconds);
    live_row(UPTIME_ROW, "Uptime:", v);
}

void bootscreen_last_key(const struct key_event *e)
{
    char v[24];
    if (!e)
        ksnprintf(v, sizeof(v), "none");
    else if (e->keycode == KEY_CHAR)
        ksnprintf(v, sizeof(v), "'%c'", e->ascii);
    else
        ksnprintf(v, sizeof(v), "%s", scancode_key_name(e->keycode));
    live_row(LAST_KEY_ROW, "Last key:", v);
}

void bootscreen_diag_begin(void)
{
    for (size_t r = 0; r < PANEL_ROWS; r++)
        clear_cells(PANEL_ROW + r, 0, VGA_WIDTH);
}

void bootscreen_diag_line(uint32_t index, const char *text)
{
    size_t row = PANEL_ROW + index % PANEL_ROWS;
    size_t prev = PANEL_ROW + (index + PANEL_ROWS - 1) % PANEL_ROWS;
    if (index)
        vga_put_cell(prev, 0, ' ', VGA_LIGHT_GREY, VGA_BLACK);   /* move the marker */
    clear_cells(row, 0, VGA_WIDTH);
    vga_put_cell(row, 0, '>', VGA_YELLOW, VGA_BLACK);             /* newest line */
    screen_write(row, LEFT_COL, text, VGA_WHITE);
}

void bootscreen_message(const char *text)
{
    /* Clear the row first so a shorter message doesn't leave old text. */
    clear_cells(MESSAGE_ROW, 0, VGA_WIDTH);
    screen_write(MESSAGE_ROW, LEFT_COL, text, VGA_YELLOW);
    serial_write(text);
    serial_putc('\n');
}

void bootscreen_ready(void)
{
    screen_write(READY_ROW, LEFT_COL, "System ready.", VGA_LIGHT_GREEN);
    serial_write("System ready.\n");
}
