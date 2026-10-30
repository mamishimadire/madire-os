/*
 * The Restart dialog (1.3 stage 5); see ui_dialog.h.
 */
#include "kernel/display/ui_dialog.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_rect.h"

void ui_dialog_size(uint32_t s, int32_t *w, int32_t *h)
{
    *w = ui_px(360, s);
    *h = ui_px(172, s);
}

void ui_dialog_init(struct ui_dialog *d, uint32_t s, const struct ui_fonts *f, int32_t width, int32_t bar_y,
                    bool ring)
{
    int32_t w, h;
    ui_dialog_size(s, &w, &h);
    int32_t y = (bar_y - h) / 2;
    /* Field by field: no struct copy the compiler could turn into a library call. */
    d->w.name = UI_DIALOG_TITLE;
    d->w.title = UI_DIALOG_TITLE;
    d->w.icon = UI_ICON_RESTART;
    d->w.w = w;
    d->w.h = h;
    d->w.esc_closes = false;
    d->w.paint = ui_dialog_paint;
    d->w.ctx = d;
    d->w.open = true;
    d->w.r = (struct rect){ (width - w) / 2, y < 0 ? 0 : y, w, h };
    d->w.close = UI_BUTTON_NORMAL;
    d->w.dirty = false;
    d->scale = s;
    d->fonts.regular = f->regular;
    d->fonts.bold = f->bold;
    d->focus = UI_DLG_CANCEL;
    d->ring = ring;
    d->btn[UI_DLG_CANCEL] = UI_BUTTON_NORMAL;
    d->btn[UI_DLG_RESTART] = UI_BUTTON_NORMAL;
}

/* A button's rectangle in a dialog at r. */
static struct rect dlg_button_at(struct rect r, uint32_t s, enum ui_dialog_button b)
{
    int32_t x0 = ui_px(b == UI_DLG_CANCEL ? 136 : 240, s), x1 = ui_px(b == UI_DLG_CANCEL ? 232 : 336, s);
    int32_t y0 = ui_px(116, s), y1 = ui_px(148, s);
    return (struct rect){ r.x + x0, r.y + y0, x1 - x0, y1 - y0 };
}

struct rect ui_dialog_button_rect(const struct ui_dialog *d, enum ui_dialog_button b)
{
    return dlg_button_at(d->w.r, d->scale, b);
}

enum ui_dialog_part ui_dialog_hit(const struct ui_dialog *d, int32_t x, int32_t y)
{
    if (!ui_rect_contains(d->w.r, x, y))
        return UI_DLG_NONE;
    if (ui_rect_contains(ui_window_close_rect(d->w.r, d->scale), x, y))
        return UI_DLG_PART_CLOSE;
    if (ui_rect_contains(ui_dialog_button_rect(d, UI_DLG_CANCEL), x, y))
        return UI_DLG_PART_CANCEL;
    if (ui_rect_contains(ui_dialog_button_rect(d, UI_DLG_RESTART), x, y))
        return UI_DLG_PART_RESTART;
    return UI_DLG_BODY;
}

void ui_dialog_paint(const struct ui_canvas *c, struct rect r, void *ctx)
{
    const struct ui_dialog *d = ctx;
    uint32_t s = d->scale;
    ui_text(c, d->fonts.regular, r.x + ui_px(24, s), r.y + ui_px(56, s), UI_DIALOG_LINE1, UI_TEXT);
    ui_text(c, d->fonts.regular, r.x + ui_px(24, s), r.y + ui_px(76, s), UI_DIALOG_LINE2, UI_TEXT);
    for (int b = 0; b < UI_DLG_BUTTONS; b++) {
        struct ui_button btn = { dlg_button_at(r, s, (enum ui_dialog_button)b), b ? "Restart" : "Cancel",
                                 UI_ICON_NONE, 0 };
        ui_button_draw(c, &btn, b ? &ui_style_restart : &ui_style_cancel, d->btn[b], d->ring && (int)d->focus == b, s,
                       &d->fonts);
    }
}

void ui_dialog_draw(const struct ui_canvas *c, const struct ui_dialog *d)
{
    ui_window_draw(c, &d->w, d->w.r, true, d->w.close, d->scale, &d->fonts);
}
