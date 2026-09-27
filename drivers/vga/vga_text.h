#ifndef MADIRE_DRIVERS_VGA_TEXT_H
#define MADIRE_DRIVERS_VGA_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* Code page 437 "upper half block": foreground paints the top half of the
 * cell, background the bottom half. */
#define VGA_CHAR_UPPER_HALF 0xDF

/* Default VGA text colours. Slots 1, 5, 6, 8, 9 and 13 are reprogrammed by
 * the boot logo (kernel/logo.c), so the rest of the kernel avoids them. */
enum vga_color {
    VGA_BLACK = 0, VGA_BLUE = 1, VGA_GREEN = 2, VGA_CYAN = 3,
    VGA_RED = 4, VGA_MAGENTA = 5, VGA_BROWN = 6, VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8, VGA_LIGHT_BLUE = 9, VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11, VGA_LIGHT_RED = 12, VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW = 14, VGA_WHITE = 15,
};

/* Hides the cursor, turns off blinking (so all 16 background colours are
 * usable) and clears the screen. Returns true if video memory reads back. */
bool vga_init(void);

void vga_set_color(enum vga_color fg, enum vga_color bg);
void vga_clear(void);                       /* fill with the current colour */
void vga_set_cursor(size_t row, size_t col);
void vga_putc(char c);                      /* at cursor, current colour */
void vga_put_cell(size_t row, size_t col, uint8_t ch, uint8_t fg, uint8_t bg);

/* Sets colour slot 0-15 to an RGB value (6 bits per channel, 0-63). */
void vga_set_palette(uint8_t slot, uint8_t r, uint8_t g, uint8_t b);

#endif
