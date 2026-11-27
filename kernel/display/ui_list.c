/*
 * The toolkit's list and grid views and the scroll bar (1.5 stage 10);
 * see ui_list.h.
 */
#include "kernel/display/ui_list.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_about.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_textfield.h"
#include "kernel/display/ui_trace.h"

#define LIST_HEADER_DP  28
#define LIST_ROW_DP     28
#define LIST_SB_DP      24
#define LIST_CELL_W_DP  96
#define LIST_CELL_H_DP  88
#define LIST_TEXT_MAX   (UI_TEXTFIELD_MAX + 1)

/* ---- The scroll bar ---- */

bool ui_scrollbar_thumb(struct rect sb, uint32_t lines, uint32_t page, uint32_t top, uint32_t s, struct rect *thumb)
{
    int64_t pad = ui_px(4, s), L = (int64_t)sb.h - 2 * pad;
    if (page == 0 || lines <= page || L <= 0)
        return false;
    int64_t T = L * page / lines, maxtop = (int64_t)lines - page, t = top > maxtop ? maxtop : top;
    if (T < ui_px(24, s))
        T = ui_px(24, s);
    if (T > L)
        T = L;
    int64_t off = (L - T) * t / maxtop;
    int32_t w = ui_px(8, s);
    thumb->x = (int32_t)((int64_t)sb.x + ((int64_t)sb.w - w) / 2);
    thumb->y = (int32_t)((int64_t)sb.y + pad + off);
    thumb->w = w;
    thumb->h = (int32_t)T;
    return true;
}

enum ui_sb_hit ui_scrollbar_hit(struct rect sb, uint32_t lines, uint32_t page, uint32_t top, uint32_t s, int32_t x,
                                int32_t y)
{
    struct rect th;
    if (!ui_rect_contains(sb, x, y) || !ui_scrollbar_thumb(sb, lines, page, top, s, &th))
        return UI_SB_HIT_NONE;
    if (y < th.y)
        return UI_SB_HIT_ABOVE;
    if (y >= (int64_t)th.y + th.h)
        return UI_SB_HIT_BELOW;
    return UI_SB_HIT_THUMB;
}

uint32_t ui_scrollbar_drag(struct rect sb, uint32_t lines, uint32_t page, uint32_t s, int32_t y)
{
    struct rect th;
    if (!ui_scrollbar_thumb(sb, lines, page, 0, s, &th))
        return 0;
    int64_t pad = ui_px(4, s), room = (int64_t)sb.h - 2 * pad - th.h, maxtop = (int64_t)lines - page;
    int64_t rel = (int64_t)y - sb.y - pad;
    if (room <= 0)
        return 0;
    if (rel < 0)
        rel = 0;
    if (rel > room)
        rel = room;
    return (uint32_t)((rel * maxtop + room / 2) / room);
}

/* The colour under (x, y) before a mark is drawn there, for the trace
 * (ui_trace.h); nothing outside the surface. */
static void list_trace(const struct ui_canvas *c, int kind, uint32_t rgb, int64_t x, int64_t y)
{
#ifdef UI_TRACE
    if (x >= 0 && y >= 0 && x < c->s->width && y < c->s->height)
        UI_TRACE_PAIR(kind, rgb, surface_get(c->s, (uint32_t)x, (uint32_t)y));
#else
    (void)c;
    (void)kind;
    (void)rgb;
    (void)x;
    (void)y;
#endif
}

void ui_scrollbar_draw(const struct ui_canvas *c, struct rect sb, uint32_t lines, uint32_t page, uint32_t top,
                       enum ui_sb_state st, uint32_t s)
{
    struct rect th;
    if (!ui_scrollbar_thumb(sb, lines, page, top, s, &th))
        return;
    uint32_t rgb = st == UI_SB_NORMAL ? UI_TEXT_MUTED : UI_TEXT;
    list_trace(c, UI_TRACE_STATE, rgb, (int64_t)th.x + th.w / 2, (int64_t)th.y + th.h / 2);
    ui_fill_round(c, th, ui_px(4, s), UI_CORNERS_ALL, rgb);
}

/* ---- The list and the grid ---- */

void ui_list_init(struct ui_list *l, enum ui_list_view v, struct ui_list_item *items, uint32_t n,
                  const char *const *titles, struct rect r, uint32_t s)
{
    l->view = v;
    l->items = items;
    l->n = n;
    l->titles = titles;
    l->r = r;
    l->scale = s;
    l->top = 0;
    l->cursor = 0;
    l->anchor = 0;
    l->ring = false;
    l->sb = UI_SB_NORMAL;
    l->wide = false;
    l->drop = -1;
}

struct rect ui_list_body(const struct ui_list *l)
{
    uint32_t s = l->scale;
    int32_t hd = l->view == UI_LIST_DETAILS ? ui_px(LIST_HEADER_DP, s) : 0;
    struct rect b = { l->r.x, l->r.y + hd, l->r.w - ui_px(LIST_SB_DP, s), l->r.h - hd };
    if (b.w < 0)
        b.w = 0;
    if (b.h < 0)
        b.h = 0;
    return b;
}

struct rect ui_list_sb_rect(const struct ui_list *l)
{
    struct rect b = ui_list_body(l);
    int32_t w = ui_px(LIST_SB_DP, l->scale);
    return (struct rect){ l->r.x + l->r.w - w, b.y, w, b.h };
}

uint32_t ui_list_columns(const struct ui_list *l)
{
    if (l->view == UI_LIST_DETAILS)
        return 1;
    int32_t cols = ui_list_body(l).w / ui_px(LIST_CELL_W_DP, l->scale);
    return cols < 1 ? 1 : (uint32_t)cols;
}

uint32_t ui_list_lines(const struct ui_list *l)
{
    uint32_t cols = ui_list_columns(l);
    return (l->n + cols - 1) / cols;
}

uint32_t ui_list_page(const struct ui_list *l)
{
    int32_t step = ui_px(l->view == UI_LIST_DETAILS ? LIST_ROW_DP : LIST_CELL_H_DP, l->scale);
    int32_t h = ui_list_body(l).h;
    return step > 0 && h > 0 ? (uint32_t)(h / step) : 0;
}

uint32_t ui_list_max_top(const struct ui_list *l)
{
    uint32_t lines = ui_list_lines(l), page = ui_list_page(l);
    return lines > page ? lines - page : 0;
}

struct rect ui_list_item_rect(const struct ui_list *l, uint32_t i)
{
    uint32_t s = l->scale, cols = ui_list_columns(l);
    struct rect b = ui_list_body(l);
    int64_t line = (int64_t)(i / cols) - l->top;
    if (l->view == UI_LIST_DETAILS) {
        int32_t rh = ui_px(LIST_ROW_DP, s);
        return (struct rect){ b.x, (int32_t)(b.y + line * rh), b.w, rh };
    }
    int32_t cw = ui_px(LIST_CELL_W_DP, s), ch = ui_px(LIST_CELL_H_DP, s);
    return (struct rect){ (int32_t)(b.x + (int64_t)(i % cols) * cw), (int32_t)(b.y + line * ch), cw, ch };
}

int32_t ui_list_item_at(const struct ui_list *l, int32_t x, int32_t y)
{
    uint32_t s = l->scale, cols = ui_list_columns(l);
    struct rect b = ui_list_body(l);
    if (!ui_rect_contains(b, x, y))
        return -1;
    int64_t step = ui_px(l->view == UI_LIST_DETAILS ? LIST_ROW_DP : LIST_CELL_H_DP, s);
    int64_t line = ((int64_t)y - b.y) / step + l->top, col = 0;
    if (l->view == UI_LIST_GRID) {
        col = ((int64_t)x - b.x) / ui_px(LIST_CELL_W_DP, s);
        if (col >= cols)
            return -1;
    }
    int64_t i = line * cols + col;
    return i < l->n ? (int32_t)i : -1;
}

void ui_list_scroll(struct ui_list *l, int32_t lines)
{
    int64_t t = (int64_t)l->top + lines, max = ui_list_max_top(l);
    l->top = (uint32_t)(t < 0 ? 0 : t > max ? max : t);
}

void ui_list_wheel(struct ui_list *l, int32_t notches)
{
    ui_list_scroll(l, l->view == UI_LIST_DETAILS ? 3 * notches : notches);
}

void ui_list_reveal(struct ui_list *l)
{
    uint32_t line = l->cursor / ui_list_columns(l), page = ui_list_page(l), max = ui_list_max_top(l);
    if (line < l->top || page == 0)
        l->top = line;
    else if (line >= l->top + page)
        l->top = line - page + 1;
    if (l->top > max)
        l->top = max;
}

/* Selects the items from a to b (either order) and no other. */
static void list_select_range(struct ui_list *l, uint32_t a, uint32_t b)
{
    uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
    for (uint32_t i = 0; i < l->n; i++)
        l->items[i].selected = i >= lo && i <= hi;
}

bool ui_list_click(struct ui_list *l, int32_t i, uint8_t mods)
{
    if (i < 0 || (uint32_t)i >= l->n) {
        if (mods & (KEY_SHIFT | KEY_CTRL))
            return false;
        bool any = false;
        for (uint32_t k = 0; k < l->n; k++) {
            any |= l->items[k].selected;
            l->items[k].selected = false;
        }
        bool ring = l->ring;
        l->ring = false;
        return any || ring;
    }
    uint32_t u = (uint32_t)i;
    if (mods & KEY_SHIFT) {
        if (l->anchor >= l->n)
            l->anchor = u;
        list_select_range(l, l->anchor, u);
    } else if (mods & KEY_CTRL) {
        l->items[u].selected = !l->items[u].selected;
        l->anchor = u;
    } else {
        list_select_range(l, u, u);
        l->anchor = u;
    }
    l->cursor = u;
    l->ring = false;
    return true;
}

void ui_list_select_all(struct ui_list *l)
{
    for (uint32_t i = 0; i < l->n; i++)
        l->items[i].selected = true;
}

enum ui_list_result ui_list_key(struct ui_list *l, const struct key_event *e)
{
    if (!(e->flags & KEY_PRESSED) || (e->flags & (KEY_CTRL | KEY_ALT)) || l->n == 0)
        return UI_LIST_NONE;
    uint32_t cols = ui_list_columns(l), page = ui_list_page(l), n = l->n;
    uint32_t cur = l->cursor < n ? l->cursor : n - 1, step = (page ? page : 1) * cols, to;
    bool grid = l->view == UI_LIST_GRID;
    switch (e->keycode) {
    case KEY_ENTER:
    case KEY_KP_ENTER:
        return UI_LIST_OPEN;
    case KEY_UP:
        to = cur >= cols ? cur - cols : cur;
        break;
    case KEY_DOWN:
        to = cur + cols < n ? cur + cols : cur;
        break;
    case KEY_LEFT:
        if (!grid)
            return UI_LIST_NONE;
        to = cur ? cur - 1 : cur;
        break;
    case KEY_RIGHT:
        if (!grid)
            return UI_LIST_NONE;
        to = cur + 1 < n ? cur + 1 : cur;
        break;
    case KEY_HOME:
        to = 0;
        break;
    case KEY_END:
        to = n - 1;
        break;
    case KEY_PGUP:
        to = cur >= step ? cur - step : cur % cols;
        break;
    case KEY_PGDN:
        to = cur + step < n ? cur + step : n - 1;
        break;
    default:
        return UI_LIST_NONE;
    }
    if (e->flags & KEY_SHIFT) {
        if (l->anchor >= n)
            l->anchor = cur;
        list_select_range(l, l->anchor, to);
    } else {
        list_select_range(l, to, to);
        l->anchor = to;
    }
    l->cursor = to;
    l->ring = true;
    ui_list_reveal(l);
    return UI_LIST_MOVED;
}

bool ui_list_double(struct ui_list_clicks *k, uint64_t ms, int32_t x, int32_t y, int32_t item, uint32_t s)
{
    int64_t near = ui_px(4, s), dx = (int64_t)x - k->x, dy = (int64_t)y - k->y;
    if (k->armed && item >= 0 && item == k->item && ms - k->ms <= 500 && ms >= k->ms && dx <= near &&
        dx >= -near && dy <= near && dy >= -near) {
        k->armed = false;
        return true;
    }
    k->ms = ms;
    k->x = x;
    k->y = y;
    k->item = item;
    k->armed = item >= 0;
    return false;
}

/* Draws text cut with "..." to room pixels at (x, y), or right-aligned
 * with its right end at x if right. */
static void list_text(const struct ui_canvas *c, const struct font *f, int32_t x, int32_t y, int32_t room,
                      const char *text, uint32_t rgb, bool right)
{
    char shown[LIST_TEXT_MAX], cut[LIST_TEXT_MAX];
    if (!text)
        return;
    ui_name_shown(text, shown, sizeof(shown));
    ui_text_fit(f, shown, room < 0 ? 0 : room, cut, sizeof(cut));
    ui_text(c, f, right ? x - ui_text_width(f, cut) : x, y, cut, rgb);
}

/* The selection's fill and bar, and the ring, for the row or cell at r. */
static void list_marks(const struct ui_canvas *c, const struct ui_list *l, uint32_t i, struct rect r)
{
    uint32_t s = l->scale;
    int32_t g = ui_px(2, s), in4 = ui_px(4, s);
    bool grid = l->view == UI_LIST_GRID;
    struct rect shape = grid ? ui_rect_inflate(r, -g) : r;
    if (l->items[i].selected) {
        list_trace(c, UI_TRACE_CARRIED_BAR, UI_SURFACE_ACTIVE, (int64_t)r.x + r.w / 2, (int64_t)r.y + r.h / 2);
        struct rect bar;
        if (grid) {
            ui_fill_round(c, shape, in4, UI_CORNERS_ALL, UI_SURFACE_ACTIVE);
            bar = (struct rect){ shape.x + in4, shape.y + shape.h - in4, shape.w - 2 * in4, g };
        } else {
            ui_fill(c, r, UI_SURFACE_ACTIVE);
            bar = (struct rect){ r.x, r.y + in4, g, r.h - 2 * in4 };
        }
        list_trace(c, UI_TRACE_STATE, UI_ACCENT, (int64_t)bar.x + bar.w / 2, (int64_t)bar.y + bar.h / 2);
        ui_fill(c, bar, UI_ACCENT);
    }
    if (l->ring && i == l->cursor) {
        struct rect ring = grid ? shape : (struct rect){ r.x + g, r.y, r.w - 2 * g, r.h };
        list_trace(c, UI_TRACE_STATE, UI_ACCENT, (int64_t)ring.x + ring.w / 2, ring.y);
        ui_frame_round(c, ring, in4, g, UI_CORNERS_ALL, UI_ACCENT);
    }
    if (l->drop >= 0 && (uint32_t)l->drop == i) {      /* 1.5 stage 14b: the drop target */
        list_trace(c, UI_TRACE_CARRIED_POINTER, UI_SURFACE_HOVER, (int64_t)shape.x + shape.w / 2,
                   (int64_t)shape.y + shape.h / 2);
        if (grid)
            ui_fill_round(c, shape, in4, UI_CORNERS_ALL, UI_SURFACE_HOVER);
        else
            ui_fill(c, shape, UI_SURFACE_HOVER);
        list_trace(c, UI_TRACE_STATE, UI_ACCENT, (int64_t)shape.x + shape.w / 2, shape.y);
        ui_frame_round(c, shape, in4, ui_px(1, s), UI_CORNERS_ALL, UI_ACCENT);
    }
}

/* ---- 1.5 stage 14b: box selection ---- */

int64_t ui_list_content_y(const struct ui_list *l, int32_t y)
{
    int64_t step = ui_px(l->view == UI_LIST_DETAILS ? LIST_ROW_DP : LIST_CELL_H_DP, l->scale);
    return (int64_t)y - ui_list_body(l).y + (int64_t)l->top * step;
}

struct rect ui_list_band_rect(const struct ui_list *l, int32_t ax, int64_t ay, int32_t bx, int64_t by)
{
    int64_t step = ui_px(l->view == UI_LIST_DETAILS ? LIST_ROW_DP : LIST_CELL_H_DP, l->scale);
    int64_t base = (int64_t)ui_list_body(l).y - (int64_t)l->top * step;
    int64_t x0 = ax < bx ? ax : bx, x1 = (ax < bx ? bx : ax) + 1;
    int64_t y0 = (ay < by ? ay : by) + base, y1 = (ay < by ? by : ay) + 1 + base;
    if (y0 < INT32_MIN / 2)
        y0 = INT32_MIN / 2;
    if (y1 > INT32_MAX / 2)
        y1 = INT32_MAX / 2;
    return (struct rect){ (int32_t)x0, (int32_t)y0, (int32_t)(x1 - x0), (int32_t)(y1 - y0) };
}

void ui_list_band(struct ui_list *l, int32_t ax, int64_t ay, int32_t bx, int64_t by, const uint8_t *base, bool add)
{
    uint32_t s = l->scale, cols = ui_list_columns(l);
    struct rect b = ui_list_body(l);
    bool grid = l->view == UI_LIST_GRID;
    int64_t step = ui_px(grid ? LIST_CELL_H_DP : LIST_ROW_DP, s), cw = ui_px(LIST_CELL_W_DP, s);
    int64_t x0 = ax < bx ? ax : bx, x1 = (ax < bx ? bx : ax) + 1;
    int64_t y0 = ay < by ? ay : by, y1 = (ay < by ? by : ay) + 1;
    if (x0 < b.x)                       /* x clamped to the body */
        x0 = b.x;
    if (x1 > (int64_t)b.x + b.w)
        x1 = (int64_t)b.x + b.w;
    for (uint32_t i = 0; i < l->n; i++) {
        int64_t line = i / cols, iy0 = line * step, iy1 = iy0 + step;
        int64_t ix0 = grid ? b.x + (int64_t)(i % cols) * cw : b.x, ix1 = grid ? ix0 + cw : (int64_t)b.x + b.w;
        bool hit = x0 < x1 && ix0 < x1 && x0 < ix1 && iy0 < y1 && y0 < iy1;
        l->items[i].selected = hit || (add && base && base[i]);
    }
}

void ui_list_draw(const struct ui_canvas *c, const struct ui_list *l, const struct ui_fonts *f)
{
    uint32_t s = l->scale;
    const struct font *fr = f->regular;
    int32_t fh = (int32_t)fr->height;
    struct rect r = l->r, b = ui_list_body(l);
    if (ui_rect_empty(r) || ui_rect_empty(ui_rect_intersect(c->clip, r)))
        return;
    ui_fill(c, r, UI_SURFACE_WINDOW);
    struct ui_canvas body = { c->s, ui_rect_intersect(c->clip, b) };
    uint32_t cols = ui_list_columns(l);
    if (l->view == UI_LIST_DETAILS) {
        int32_t hd = ui_px(LIST_HEADER_DP, s), rh = ui_px(LIST_ROW_DP, s), d8 = ui_px(8, s);
        int32_t xm = b.x + b.w - ui_px(144, s), xt = xm - ui_px(112, s), xs = xt - ui_px(80, s);
        int32_t hy = r.y + (hd - fh) / 2;
        /* 1.5 stage 13: three columns: the third from the Size column's left. */
        int32_t x3 = l->wide ? xs : xt, room3 = l->wide ? ui_px(192, s) - ui_px(16, s) : ui_px(112, s) - ui_px(16, s);
        if (l->titles) {
            ui_text(c, fr, r.x + ui_px(36, s), hy, l->titles[0], UI_TEXT_MUTED);
            if (!l->wide)
                ui_text(c, fr, xs + ui_px(80, s) - d8 - ui_text_width(fr, l->titles[1]), hy, l->titles[1],
                        UI_TEXT_MUTED);
            ui_text(c, fr, x3 + d8, hy, l->titles[2], UI_TEXT_MUTED);
            ui_text(c, fr, xm + d8, hy, l->titles[3], UI_TEXT_MUTED);
        }
        ui_fill(c, (struct rect){ r.x, r.y + hd - ui_px(1, s), b.w, ui_px(1, s) }, UI_BORDER);
        if (!ui_rect_empty(body.clip)) {
            for (uint32_t i = l->top; i < l->n; i++) {
                struct rect row = ui_list_item_rect(l, i);
                if (row.y >= (int64_t)b.y + b.h)
                    break;
                const struct ui_list_item *it = &l->items[i];
                int32_t ty = row.y + (rh - fh) / 2, ip = ui_px(16, s);
                list_marks(&body, l, i, row);
                if (it->icon != UI_ICON_NONE)
                    ui_icon_draw(&body, it->icon, r.x + ui_px(12, s), row.y + (rh - ip) / 2, ip, UI_TEXT);
                list_text(&body, fr, r.x + ui_px(36, s), ty, xs - r.x - ui_px(44, s), it->name, UI_TEXT, false);
                if (!l->wide)
                    list_text(&body, fr, xs + ui_px(80, s) - d8, ty, ui_px(80, s) - ui_px(16, s), it->size,
                              UI_TEXT_MUTED, true);
                list_text(&body, fr, x3 + d8, ty, room3, it->type, UI_TEXT_MUTED, false);
                list_text(&body, fr, xm + d8, ty, ui_px(144, s) - ui_px(16, s), it->modified, UI_TEXT_MUTED,
                          false);
            }
        }
    } else if (!ui_rect_empty(body.clip)) {
        int32_t cw = ui_px(LIST_CELL_W_DP, s), ip = ui_px(48, s);
        for (uint32_t i = l->top * cols; i < l->n; i++) {
            struct rect cell = ui_list_item_rect(l, i);
            if (cell.y >= (int64_t)b.y + b.h)
                break;
            const struct ui_list_item *it = &l->items[i];
            list_marks(&body, l, i, cell);
            if (it->icon != UI_ICON_NONE)
                ui_icon_draw(&body, it->icon, cell.x + (cw - ip) / 2, cell.y + ui_px(8, s), ip, UI_TEXT);
            if (it->name) {
                char shown[LIST_TEXT_MAX], cut[LIST_TEXT_MAX];
                ui_name_shown(it->name, shown, sizeof(shown));
                ui_text_fit(fr, shown, cw - 4 * ui_px(2, s), cut, sizeof(cut));
                ui_text(&body, fr, cell.x + (cw - ui_text_width(fr, cut)) / 2, cell.y + ui_px(64, s), cut, UI_TEXT);
            }
        }
    }
    ui_scrollbar_draw(c, ui_list_sb_rect(l), ui_list_lines(l), ui_list_page(l), l->top, l->sb, s);
}
