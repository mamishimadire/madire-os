/*
 * The toolkit's label and button (1.3 stage 2); see ui_widget.h.
 */
#include "kernel/display/ui_widget.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_trace.h"

/* Hover and pressed colours are set only where the Design Guide gives
 * them (section 8: the close button; the Calculator's pressed key). */
const struct ui_button_style ui_style_close = {
    { UI_COLOUR_NONE, UI_DANGER, UI_DANGER_PRESSED }, { UI_TEXT, UI_TEXT, UI_TEXT }, false, 0, 0,
};
const struct ui_button_style ui_style_secondary = {
    { UI_SURFACE_ACTIVE, UI_COLOUR_UNSET, UI_COLOUR_UNSET }, { UI_TEXT, UI_COLOUR_UNSET, UI_COLOUR_UNSET },
    false, 4, UI_CORNERS_ALL,
};
const struct ui_button_style ui_style_primary = {
    { UI_ACCENT, UI_COLOUR_UNSET, UI_COLOUR_UNSET }, { UI_TEXT_ON_ACCENT, UI_COLOUR_UNSET, UI_COLOUR_UNSET },
    true, 4, UI_CORNERS_ALL,
};
const struct ui_button_style ui_style_key = {
    { UI_SURFACE_ACTIVE, UI_COLOUR_UNSET, UI_ACCENT }, { UI_TEXT, UI_COLOUR_UNSET, UI_TEXT_ON_ACCENT },
    false, 4, UI_CORNERS_ALL,
};
const struct ui_button_style ui_style_key_equals = {
    { UI_ACCENT, UI_COLOUR_UNSET, UI_ACCENT }, { UI_TEXT_ON_ACCENT, UI_COLOUR_UNSET, UI_TEXT_ON_ACCENT },
    false, 4, UI_CORNERS_ALL,
};

/* floor(a / 2) for any sign. */
static int32_t half_down(int64_t a)
{
    return (int32_t)(a >= 0 ? a / 2 : -((-a + 1) / 2));
}

void ui_label_draw(const struct ui_canvas *c, struct rect r, const struct font *f, const char *text,
                   uint32_t rgb, enum ui_align align)
{
    struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, r) };
    if (ui_rect_empty(in.clip))
        return;
    int64_t tw = ui_text_width(f, text), x = r.x;
    if (align == UI_ALIGN_CENTRE)
        x = (int64_t)r.x + half_down((int64_t)r.w - tw);
    else if (align == UI_ALIGN_RIGHT)
        x = (int64_t)r.x + r.w - tw;
    ui_text(&in, f, (int32_t)x, (int32_t)((int64_t)r.y + half_down((int64_t)r.h - f->height)), text, rgb);
}

bool ui_button_draw(const struct ui_canvas *c, const struct ui_button *b, const struct ui_button_style *style,
                    enum ui_button_state st, bool focused, uint32_t s, const struct ui_fonts *f)
{
    if ((unsigned)st >= UI_BUTTON_STATES || style->fill[st] == UI_COLOUR_UNSET ||
        style->fg[st] == UI_COLOUR_UNSET || b->icon < UI_ICON_NONE || b->icon >= UI_ICON_COUNT)
        return false;
    struct rect r = b->r;
    int32_t radius = ui_px(style->radius_dp, s);
    if (radius > r.w / 2)
        radius = r.w / 2;
    if (radius > r.h / 2)
        radius = r.h / 2;
    if (radius < 0)
        radius = 0;
#ifdef UI_TRACE
    /* 1.3 stage 4a: a hover or pressed fill is a state mark (ui_trace.h). */
    int64_t cx = (int64_t)r.x + r.w / 2, cy = (int64_t)r.y + r.h / 2;
    if (st != UI_BUTTON_NORMAL && style->fill[st] != UI_COLOUR_NONE && cx >= 0 && cy >= 0 &&
        cx < c->s->width && cy < c->s->height)
        UI_TRACE_PAIR(UI_TRACE_STATE, style->fill[st], surface_get(c->s, (uint32_t)cx, (uint32_t)cy));
#endif
    if (style->fill[st] != UI_COLOUR_NONE)
        ui_fill_round(c, r, radius, style->corners, style->fill[st]);
    if (b->icon != UI_ICON_NONE) {
        int32_t p = ui_px(b->icon_dp, s);
        struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, r) };
        if (!ui_rect_empty(in.clip))
            ui_icon_draw(&in, b->icon, (int32_t)((int64_t)r.x + half_down((int64_t)r.w - p)),
                         (int32_t)((int64_t)r.y + half_down((int64_t)r.h - p)), p, style->fg[st]);
    } else if (b->label) {
        ui_label_draw(c, r, style->bold ? f->bold : f->regular, b->label, style->fg[st], UI_ALIGN_CENTRE);
    }
    if (focused) {
        int32_t g = ui_px(2, s), w = ui_px(2, s);
        ui_frame_round(c, ui_rect_inflate(r, g + w), radius + g + w, w, style->corners, UI_ACCENT);
    }
    return true;
}
