/*
 * The About window's content (1.3 stage 4a); see ui_about.h.
 */
#include "kernel/display/ui_about.h"
#include "kernel/display/tokens.h"
#include "kernel/display/wallpaper_core.h"

#define FB 8

/* The logo's box in its units (as ui_icon.c): x 39 to 249 (centre 144), height 167. */
#define LOGO_W  210
#define LOGO_CX 144
#define LOGO_H  167
#define WM_CAP  14                      /* the wordmark's cap height in units */
#define WM_TRACK 6
#define WM_HEAVY 6                      /* "MADIRE" heavier than " OS" */

const char *const ui_about_labels[UI_ABOUT_ROWS] = { "Version", "Boot loader", "Display", "Memory", "Mode" };

void ui_about_size(uint32_t s, int32_t *w, int32_t *h)
{
    *w = ui_px(352, s);
    *h = ui_px(377, s);
}

int32_t ui_about_value_px(uint32_t s)
{
    return ui_px(328, s) - ui_px(136, s);
}

void ui_text_fit(const struct font *f, const char *text, int32_t max_px, char *out, size_t size)
{
    size_t n = 0, keep;
    if (!size)
        return;
    while (text[n] && n < size - 1)
        n++;
    if ((int64_t)n * f->width <= max_px && !text[n]) {
        keep = n;
    } else {
        /* The longest beginning that leaves room for "..." (and the NUL). */
        int64_t room = (int64_t)max_px / f->width - 3;
        keep = room < 0 ? 0 : (size_t)room;
        if (keep > n)
            keep = n;
        if (keep + 3 > size - 1)
            keep = size - 1 >= 3 ? size - 1 - 3 : 0;
    }
    for (size_t i = 0; i < keep; i++)
        out[i] = text[i];
    size_t end = keep;
    if (keep < n || text[n]) {
        for (int i = 0; i < 3 && end < size - 1; i++)
            out[end++] = '.';
    }
    out[end] = 0;
}

static void set_value(struct ui_about *a, int row, const char *v)
{
    ui_text_fit(a->fonts.regular, v, ui_about_value_px(a->scale), a->values[row], UI_ABOUT_VALUE_MAX);
}

void ui_about_setup(struct ui_about *a, uint32_t s, const struct ui_fonts *f, const char *version,
                    const char *loader, const char *display, const char *memory, uint16_t *scratch,
                    size_t entries)
{
    a->scale = s;
    a->fonts = *f;
    set_value(a, 0, version);
    set_value(a, 1, loader);
    set_value(a, 2, display);
    set_value(a, 3, memory);
    set_value(a, 4, UI_ABOUT_MODE);
    a->scratch = scratch;
    a->entries = entries;
    a->short_scratch = false;
}

/* The wordmark as a line, the window at r. */
static struct wp_line wordmark(uint32_t s, struct rect r)
{
    int64_t ts = wp_fx(4 * (int64_t)s, WM_CAP), track = wp_fx(WM_TRACK, 1), adv = 0;
    for (size_t i = 0; i < wp_wordmark.count; i++)
        adv += wp_wordmark.glyphs[i].advance + track;
    int64_t pen = ((int64_t)r.x << FB) + wp_fx(r.w, 2) - (wp_mul(adv, ts) - wp_mul(track, ts)) / 2;
    return (struct wp_line){ &wp_wordmark, pen, ((int64_t)r.y + ui_px(136, s)) << FB, ts, track,
                             wp_fx(50 * (int64_t)s, 161), wp_fx(25 * (int64_t)s, 161), WM_HEAVY };
}

size_t ui_about_scratch(uint32_t s, uint32_t width, uint32_t height)
{
    int32_t w, h;
    ui_about_size(s, &w, &h);
    struct wp_line line = wordmark(s, (struct rect){ 0, 0, w, h });
    return wp_line_scratch(&line, width, height);
}

void ui_about_paint(const struct ui_canvas *c, struct rect r, void *ctx)
{
    struct ui_about *a = ctx;
    uint32_t s = a->scale;
    const struct font *f = a->fonts.regular;

    int32_t P = ui_px(64, s), bx = r.x + (r.w - P) / 2, by = r.y + ui_px(56, s);
    int64_t m = wp_fx(56 * (int64_t)P, 64 * LOGO_W);
    int64_t lox = ((int64_t)bx << FB) + wp_fx(P, 2) - LOGO_CX * m;
    int64_t loy = ((int64_t)by << FB) + wp_fx(P, 2) - wp_mul(wp_fx(LOGO_H, 2), m);
    wp_logo_draw(c->s, c->clip, lox, loy, m);

    struct wp_line line = wordmark(s, r);
    if (!wp_line_draw(c->s, c->clip, &line, UI_TEXT, a->scratch, a->entries))
        a->short_scratch = true;

    for (int i = 0; i < UI_ABOUT_ROWS; i++) {
        int32_t y = r.y + ui_px(176 + 28 * i, s);
        ui_text(c, f, r.x + ui_px(24, s), y, ui_about_labels[i], UI_TEXT_MUTED);
        ui_text(c, f, r.x + ui_px(136, s), y, a->values[i], UI_TEXT);
    }
    ui_fill(c, (struct rect){ r.x + ui_px(24, s), r.y + ui_px(320, s), ui_px(328, s) - ui_px(24, s), ui_px(1, s) },
            UI_BORDER);
    ui_text(c, f, r.x + (r.w - ui_text_width(f, UI_ABOUT_FOOTER)) / 2, r.y + ui_px(337, s), UI_ABOUT_FOOTER,
            UI_TEXT_MUTED);
}
