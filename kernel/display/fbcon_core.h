#ifndef MADIRE_DISPLAY_FBCON_CORE_H
#define MADIRE_DISPLAY_FBCON_CORE_H

/*
 * The framebuffer console, the pure part (1.2 stage 6; host unit tests in
 * tests/unit/test_fbcon.c). It draws the VGA text driver's 80 x 25 cell
 * grid (drivers/vga/vga_text.h) on a surface: one font cell per character
 * cell, the grid centred on the screen, black around it. The kernel side
 * is fbcon.c.
 *
 * The rules (tests/fbcon_ref.py renders the same picture from them,
 * reading the BDF files itself):
 *   - scale: chosen from the screen height H (decision 19; the Design
 *     Guide, section 6): H < 900 -> 4 (100 %, 8x16), H < 1080 -> 5
 *     (125 %, 10x20), H < 1440 -> 6 (150 %, 12x24), otherwise 8 (200 %,
 *     16x32); a grid wider or taller than the screen steps down until it
 *     fits;
 *   - the grid's top-left corner: ((W - 80 x cell width) / 2,
 *     (H - 25 x cell height) / 2), rounded down;
 *   - a cell: a glyph pixel set -> the foreground colour, clear -> the
 *     background; the cursor (when shown) paints the cell's rows
 *     [height x 13 / 16, height x 15 / 16) in the foreground colour, as
 *     the VGA's underline cursor on scan lines 13-14 of 16;
 *   - colours: a palette slot's 6-bit DAC value v becomes (v << 2) |
 *     (v >> 4) per channel (0x2A -> 0xAA, 0x15 -> 0x55, 0x3F -> 0xFF).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/font.h"
#include "kernel/display/surface_core.h"

#define FBCON_COLS 80
#define FBCON_ROWS 25

struct fbcon_layout {
    const struct font *font;
    uint32_t scale;             /* quarter steps: 4, 5, 6 or 8 */
    uint32_t x, y;              /* the grid's top-left pixel */
    uint32_t cell_w, cell_h;
};

/* The scale step for a screen `height` pixels high (4, 5, 6 or 8). */
uint32_t fbcon_scale_for_height(uint32_t height);

/* Lays the grid out on a width x height screen with `fonts` (the fonts of
 * scales 4, 5, 6 and 8, in that order). False if not even the scale 4
 * grid fits (then *out is unchanged). */
bool fbcon_layout(uint32_t width, uint32_t height, const struct font *const fonts[4],
                  struct fbcon_layout *out);

/* 0xRRGGBB of a 6-bit DAC value (red, green, blue, each 0-63; higher bits
 * ignored). */
uint32_t fbcon_rgb6(const uint8_t rgb6[3]);

/* The cursor's rows in a cell `height` rows high: [*first, *end). */
void fbcon_cursor_rows(uint32_t height, uint32_t *first, uint32_t *end);

/* Draws character `ch` at (row, col) of the grid in colours fg and bg
 * (0xRRGGBB), with the cursor if `cursor`. Draws nothing if the cell does
 * not lie wholly inside the surface or (row, col) is outside the grid. */
void fbcon_draw_cell(struct surface *s, const struct fbcon_layout *l, uint32_t row, uint32_t col,
                     uint8_t ch, uint32_t fg, uint32_t bg, bool cursor);

/* The cell's rectangle on the screen. */
struct rect fbcon_cell_rect(const struct fbcon_layout *l, uint32_t row, uint32_t col);

#endif
