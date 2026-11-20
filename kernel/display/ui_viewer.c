/*
 * The Text Viewer's lines and picture (1.5 stage 12); see ui_viewer.h.
 */
#include "kernel/display/ui_viewer.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_rect.h"

char ui_viewer_shown(uint8_t b)
{
    return b >= 0x20 && b <= 0x7E ? (char)b : '?';
}

uint32_t ui_viewer_next(const uint8_t *buf, uint32_t len, uint32_t off, uint32_t cols, uint32_t *next)
{
    uint32_t col = 0, drawn = 0, i = off;
    if (cols < 1)
        cols = 1;
    while (i < len) {
        uint8_t b = buf[i];
        if (b == '\n') {
            i++;
            break;
        }
        uint32_t w = b == '\t' ? 8u - col % 8u : 1u;
        if (col + w > cols) {
            if (col > 0)
                break;
            w = cols;                       /* a tab at column 0 wider than the line */
        }
        col += w;
        drawn++;
        i++;
        if (col >= cols) {
            if (i < len && buf[i] == '\n')
                i++;
            break;
        }
    }
    *next = i;
    return drawn;
}

uint32_t ui_viewer_lines(const uint8_t *buf, uint32_t len, uint32_t cols)
{
    uint32_t n = 0, off = 0, next;
    while (off < len) {
        (void)ui_viewer_next(buf, len, off, cols, &next);
        off = next;
        n++;
    }
    return n;
}

uint32_t ui_viewer_line_start(const uint8_t *buf, uint32_t len, uint32_t cols, uint32_t n)
{
    uint32_t off = 0, next;
    for (uint32_t k = 0; k < n && off < len; k++) {
        (void)ui_viewer_next(buf, len, off, cols, &next);
        off = next;
    }
    return off;
}

/* The area below the bar (if any) to the window's bottom edge. */
static struct rect viewer_body(const struct ui_viewer *v, struct rect r, uint32_t s)
{
    int32_t E = ui_px(1, s), y = r.y + ui_px(32, s) + (v->cut ? ui_px(28, s) : 0), b = r.y + r.h - E;
    return (struct rect){ r.x + E, y, r.w - 2 * E, b > y ? b - y : 0 };
}

struct rect ui_viewer_area(const struct ui_viewer *v, struct rect r, uint32_t s)
{
    struct rect b = viewer_body(v, r, s);
    int32_t w = b.w - ui_px(24, s);
    return (struct rect){ b.x, b.y, w > 0 ? w : 0, b.h };
}

struct rect ui_viewer_sb(const struct ui_viewer *v, struct rect r, uint32_t s)
{
    struct rect b = viewer_body(v, r, s), a = ui_viewer_area(v, r, s);
    return (struct rect){ a.x + a.w, b.y, b.w - a.w, b.h };
}

uint32_t ui_viewer_cols(const struct ui_viewer *v, struct rect r, uint32_t s, const struct font *f)
{
    int32_t room = ui_viewer_area(v, r, s).w - 2 * ui_px(8, s);
    uint32_t c = room > 0 && f->width > 0 ? (uint32_t)room / f->width : 0;
    return c < 1 ? 1 : c;
}

uint32_t ui_viewer_page(const struct ui_viewer *v, struct rect r, uint32_t s, const struct font *f)
{
    int32_t room = ui_viewer_area(v, r, s).h - 2 * ui_px(8, s);
    return room > 0 && f->height > 0 ? (uint32_t)room / f->height : 0;
}

void ui_viewer_scroll(struct ui_viewer *v, struct rect r, uint32_t s, const struct font *f, int32_t lines)
{
    uint32_t n = ui_viewer_lines(v->buf, v->len, ui_viewer_cols(v, r, s, f)), page = ui_viewer_page(v, r, s, f);
    uint32_t max = n > page ? n - page : 0;
    int64_t t = (int64_t)v->top + lines;
    if (t < 0)
        t = 0;
    if (t > max)
        t = max;
    v->top = (uint32_t)t;
}

void ui_viewer_draw(const struct ui_canvas *c, const struct ui_viewer *v, struct rect r, uint32_t s,
                    const struct ui_fonts *f)
{
    const struct font *fr = f->regular;
    int32_t E = ui_px(1, s), fh = (int32_t)fr->height, fw = (int32_t)fr->width, d8 = ui_px(8, s);
    struct rect inner = { r.x + E, r.y + ui_px(32, s), r.w - 2 * E, r.h - ui_px(32, s) - E };
    struct ui_canvas cv = { c->s, ui_rect_intersect(c->clip, inner) };
    if (ui_rect_empty(cv.clip))
        return;
    if (v->cut) {
        struct rect bar = { inner.x, inner.y, inner.w, ui_px(28, s) };
        struct ui_canvas bc = { c->s, ui_rect_intersect(cv.clip, bar) };
        ui_fill(&bc, bar, UI_SURFACE);
        ui_text(&bc, fr, bar.x + ui_px(12, s), bar.y + (bar.h - fh) / 2, UI_VIEWER_CUT_TEXT, UI_TEXT);
        ui_fill(&bc, (struct rect){ bar.x, bar.y + bar.h - E, bar.w, E }, UI_BORDER);
    }
    struct rect a = ui_viewer_area(v, r, s);
    uint32_t cols = ui_viewer_cols(v, r, s, fr), page = ui_viewer_page(v, r, s, fr);
    uint32_t lines = ui_viewer_lines(v->buf, v->len, cols);
    struct ui_canvas tc = { c->s, ui_rect_intersect(cv.clip, a) };
    if (!ui_rect_empty(tc.clip) && v->buf) {
        uint32_t off = ui_viewer_line_start(v->buf, v->len, cols, v->top);
        char one[2] = { 0, 0 };
        for (uint32_t k = 0; off < v->len; k++) {
            int64_t ty = (int64_t)a.y + d8 + (int64_t)k * fh;
            if (ty >= (int64_t)a.y + a.h)
                break;
            uint32_t next, n = ui_viewer_next(v->buf, v->len, off, cols, &next), col = 0;
            for (uint32_t i = 0; i < n && off + i < v->len; i++) {
                uint8_t b = v->buf[off + i];
                if (b == '\t') {
                    col += 8u - col % 8u;
                    continue;
                }
                one[0] = ui_viewer_shown(b);
                if (one[0] != ' ')
                    ui_text(&tc, fr, a.x + d8 + (int32_t)col * fw, (int32_t)ty, one, UI_TEXT);
                col++;
            }
            off = next;
        }
    }
    ui_scrollbar_draw(&cv, ui_viewer_sb(v, r, s), lines, page, v->top, v->sb, s);
}
