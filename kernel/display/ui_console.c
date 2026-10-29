/*
 * The Console window's client (1.3 stage 4b); see ui_console.h.
 */
#include "kernel/display/ui_console.h"
#include "kernel/display/fbcon_core.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_window.h"

struct rect ui_console_client(struct rect r, uint32_t s, const struct font *f)
{
    return (struct rect){ r.x + ui_window_edge(s), r.y + ui_window_title_h(s),
                          UI_CONSOLE_COLS * (int32_t)f->width, UI_CONSOLE_ROWS * (int32_t)f->height };
}

void ui_console_size(uint32_t s, const struct font *f, int32_t *w, int32_t *h)
{
    ui_window_size_for_client(s, UI_CONSOLE_COLS * (int32_t)f->width, UI_CONSOLE_ROWS * (int32_t)f->height, w, h);
}

void ui_console_cell(const struct ui_canvas *c, struct rect r, uint32_t s, const struct font *f, uint32_t row,
                     uint32_t col, uint8_t ch, uint32_t fg, uint32_t bg, bool cursor)
{
    if (row >= UI_CONSOLE_ROWS || col >= UI_CONSOLE_COLS)
        return;
    struct rect g = ui_console_client(r, s, f);
    struct rect cell = { g.x + (int32_t)(col * f->width), g.y + (int32_t)(row * f->height), (int32_t)f->width,
                         (int32_t)f->height };
    struct rect k = ui_rect_intersect(c->clip, cell);
    if (ui_rect_empty(k))
        return;
    int32_t e = ui_window_edge(s);
    struct rect in = ui_rect_inflate(r, -e);
    int32_t rin = ui_window_radius(s) - e;
    /* Row by row only where the inner shape's bottom corners can cut. */
    if ((int64_t)k.y + k.h <= (int64_t)in.y + in.h - rin) {
        fbcon_draw_cell_at(c->s, f, cell.x, cell.y, ch, fg, bg, cursor, k);
        return;
    }
    for (int64_t y = k.y; y < (int64_t)k.y + k.h; y++) {
        int64_t x0, x1;
        if (!ui_round_span(in, rin, UI_CORNERS_ALL, y, &x0, &x1))
            continue;
        if (x0 < k.x)
            x0 = k.x;
        if (x1 > (int64_t)k.x + k.w)
            x1 = (int64_t)k.x + k.w;
        if (x1 > x0)
            fbcon_draw_cell_at(c->s, f, cell.x, cell.y, ch, fg, bg, cursor,
                               (struct rect){ (int32_t)x0, (int32_t)y, (int32_t)(x1 - x0), 1 });
    }
}

void ui_console_paint(const struct ui_canvas *c, struct rect r, uint32_t s, const struct font *f,
                      const struct ui_console_grid *g)
{
    struct rect k = ui_rect_intersect(c->clip, ui_console_client(r, s, f));
    if (ui_rect_empty(k))
        return;
    struct rect cl = ui_console_client(r, s, f);
    uint32_t c0 = (uint32_t)(k.x - cl.x) / f->width, c1 = (uint32_t)(k.x + k.w - 1 - cl.x) / f->width;
    uint32_t r0 = (uint32_t)(k.y - cl.y) / f->height, r1 = (uint32_t)(k.y + k.h - 1 - cl.y) / f->height;
    for (uint32_t row = r0; row <= r1; row++)
        for (uint32_t col = c0; col <= c1; col++) {
            uint16_t e = g->cells[row * UI_CONSOLE_COLS + col];
            uint8_t attr = (uint8_t)(e >> 8);
            ui_console_cell(c, r, s, f, row, col, (uint8_t)(e & 0xFF), fbcon_rgb6(g->palette[attr & 0x0F]),
                            fbcon_rgb6(g->palette[attr >> 4]),
                            g->cursor && g->cursor_row == row && g->cursor_col == col);
        }
}
