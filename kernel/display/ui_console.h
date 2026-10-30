#ifndef MADIRE_DISPLAY_UI_CONSOLE_H
#define MADIRE_DISPLAY_UI_CONSOLE_H

/*
 * The Console window's client, the pure part (1.3 stage 4b; host unit tests
 * in tests/unit/test_ui_console.c; the kernel side is fbcon.c and
 * desktop.c). docs/plans/1.3.md, C2 and C4; the Design Guide, section 8.
 *
 * The client is exactly the console's 80 x 25 grid at the screen's scale s
 * (the console's font f), with no padding: for a window at r (ui_window.h)
 * the grid's top-left pixel is (r.x + edge, r.y + title bar), edge =
 * ui_window_edge(s), title bar = ui_window_title_h(s); a cell is drawn as
 * fbcon_core.h says (the glyph in the cell's colours, the cursor's rows).
 * The grid keeps 1.2's VGA palette (the Design Guide's section 12 table
 * does not cover it).
 *
 * The bottom corners: the grid's pixels are drawn only inside the window's
 * inner rounded shape, the rounded rectangle r shrunk by the edge on every
 * side with the radius ui_window_radius(s) - edge (all corners;
 * ui_draw.h's rule), so the window's 1 dp edge stays whole at its rounded
 * bottom corners and nothing is drawn outside the window's own shape.
 * Inside the canvas's clip only. Each pixel gets the same value whatever
 * the clip, so the grid painted on the window's surface and composed
 * through ui_blit_round equals the grid painted on the screen directly
 * (the exposure path, docs/plans/1.3.md, C4, A1).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/font.h"
#include "kernel/display/ui_draw.h"

#define UI_CONSOLE_COLS 80
#define UI_CONSOLE_ROWS 25

/* A grid as drivers/vga/vga_text.h's shadow holds it: each cell's VGA
 * entry (the character in the low byte, the foreground colour slot in
 * bits 8-11, the background slot in bits 12-15), the 16 slots' 6-bit DAC
 * values, the cursor. */
struct ui_console_grid {
    const uint16_t *cells;
    const uint8_t (*palette)[3];
    bool cursor;
    uint32_t cursor_row, cursor_col;
};

/* The client (the grid's rectangle) of a Console window at r, with font f. */
struct rect ui_console_client(struct rect r, uint32_t s, const struct font *f);

/* The size of a Console window with font f at scale s. */
void ui_console_size(uint32_t s, const struct font *f, int32_t *w, int32_t *h);

/* Draws cell (row, col) of the Console window at r: character ch in
 * fg and bg (0xRRGGBB), with the cursor if `cursor` (the rules above). */
void ui_console_cell(const struct ui_canvas *c, struct rect r, uint32_t s, const struct font *f, uint32_t row,
                     uint32_t col, uint8_t ch, uint32_t fg, uint32_t bg, bool cursor);

/* Draws every cell of grid g that meets the canvas's clip. */
void ui_console_paint(const struct ui_canvas *c, struct rect r, uint32_t s, const struct font *f,
                      const struct ui_console_grid *g);

#endif
