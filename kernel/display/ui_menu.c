/*
 * Menus (1.3 stage 5); see ui_menu.h.
 */
#include "kernel/display/ui_menu.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_trace.h"
#include "kernel/display/wallpaper_core.h"

#define FB 8
#define MENU_ROW_DP  36
#define MENU_TRACK   6                  /* the wordmark's tracking in units */
#define MENU_HEAVY   6                  /* "MADIRE" heavier than " OS" */

struct menu_def {
    int32_t w_dp, h_dp;
    bool header;
    int rows, dividers;
    int32_t row_dp[UI_MENU_ROWS_MAX];
    int32_t div_dp[3];
    const char *label[UI_MENU_ROWS_MAX];
    int icon[UI_MENU_ROWS_MAX];
    enum ui_menu_action act[UI_MENU_ROWS_MAX];
    const char *hint[UI_MENU_ROWS_MAX];     /* 1.5 stage 13 */
};

static const struct menu_def menu_defs[UI_MENU_KINDS] = {
    { 288, 182, true, 3, 2, { 61, 97, 142 }, { 56, 137 }, { "About", "Console", "Power..." },
      { UI_ICON_ABOUT, UI_ICON_CONSOLE, UI_ICON_POWER }, { UI_MENU_ABOUT, UI_MENU_CONSOLE, UI_MENU_POWER }, { 0 } },
    { 224, 80, false, 2, 0, { 4, 40, 0 }, { 0, 0 }, { "Open Console", "About Madire OS", 0 },
      { UI_ICON_CONSOLE, UI_ICON_ABOUT, UI_ICON_NONE }, { UI_MENU_CONSOLE, UI_MENU_ABOUT, UI_MENU_NONE }, { 0 } },
    /* 1.5 stage 13 (note (13)): the Files menus. */
    { 264, 287, false, 7, 3, { 4, 49, 85, 121, 166, 202, 247 }, { 44, 161, 242 },
      { "Open", "Cut", "Copy", "Paste", "Rename", "Move to Trash", "Properties" },
      { UI_ICON_OPEN, UI_ICON_CUT, UI_ICON_COPY, UI_ICON_PASTE, UI_ICON_RENAME, UI_ICON_PLACE_TRASH, UI_ICON_ABOUT },
      { UI_MENU_F_OPEN, UI_MENU_F_CUT, UI_MENU_F_COPY, UI_MENU_F_PASTE, UI_MENU_F_RENAME, UI_MENU_F_TRASH,
        UI_MENU_F_PROPERTIES },
      { "Enter", "Ctrl+X", "Ctrl+C", "Ctrl+V", "F2", "Delete", "Alt+Enter" } },
    { 264, 170, false, 4, 2, { 4, 40, 85, 130 }, { 80, 125 },
      { "New Folder", "New File", "Paste", "Show hidden files" },
      { UI_ICON_NEW_FOLDER, UI_ICON_NEW_FILE, UI_ICON_PASTE, UI_ICON_NONE },
      { UI_MENU_F_NEW_FOLDER, UI_MENU_F_NEW_FILE, UI_MENU_F_PASTE, UI_MENU_F_HIDDEN }, { 0, 0, "Ctrl+V", 0 } },
    { 264, 134, false, 3, 2, { 4, 49, 94 }, { 44, 89 }, { "Restore", "Delete permanently", "Properties" },
      { UI_ICON_RESTORE, UI_ICON_PLACE_TRASH, UI_ICON_ABOUT },
      { UI_MENU_F_RESTORE, UI_MENU_F_DELETE, UI_MENU_F_PROPERTIES }, { 0, "Delete", "Alt+Enter" } },
    { 264, 125, false, 3, 1, { 4, 40, 85 }, { 80 }, { "Restore all", "Empty Trash", "Show hidden files" },
      { UI_ICON_RESTORE, UI_ICON_PLACE_TRASH, UI_ICON_NONE },
      { UI_MENU_F_RESTORE_ALL, UI_MENU_F_EMPTY, UI_MENU_F_HIDDEN }, { 0 } },
};

static const struct menu_def *menu_def(enum ui_menu_kind k)
{
    return &menu_defs[(uint32_t)k < UI_MENU_KINDS ? (uint32_t)k : 0u];
}

const char *ui_menu_hint(enum ui_menu_kind k, int row)
{
    const struct menu_def *d = menu_def(k);
    return row >= 0 && row < d->rows ? d->hint[row] : 0;
}

bool ui_menu_row_live(const struct ui_menu *m, int row)
{
    return row >= 0 && row < menu_def(m->kind)->rows && row < 8 && !(m->greyed & (1u << row));
}

int ui_menu_rows(enum ui_menu_kind k)
{
    return menu_def(k)->rows;
}

const char *ui_menu_label(enum ui_menu_kind k, int row)
{
    const struct menu_def *d = menu_def(k);
    return row >= 0 && row < d->rows ? d->label[row] : "";
}

int ui_menu_icon(enum ui_menu_kind k, int row)
{
    const struct menu_def *d = menu_def(k);
    return row >= 0 && row < d->rows ? d->icon[row] : UI_ICON_NONE;
}

enum ui_menu_action ui_menu_action(enum ui_menu_kind k, int row)
{
    const struct menu_def *d = menu_def(k);
    return row >= 0 && row < d->rows ? d->act[row] : UI_MENU_NONE;
}

void ui_menu_size(enum ui_menu_kind k, uint32_t s, int32_t *w, int32_t *h)
{
    *w = ui_px(menu_def(k)->w_dp, s);
    *h = ui_px(menu_def(k)->h_dp, s);
}

struct rect ui_menu_place_m(uint32_t s, int32_t bar_y)
{
    int32_t w, h;
    ui_menu_size(UI_MENU_M, s, &w, &h);
    int32_t y = bar_y - ui_px(8, s) - h;
    return (struct rect){ ui_px(8, s), y < 0 ? 0 : y, w, h };
}

struct rect ui_menu_place_at(uint32_t s, int32_t width, int32_t bar_y, int32_t x, int32_t y)
{
    return ui_menu_place_kind(UI_MENU_DESKTOP, s, width, bar_y, x, y);
}

struct rect ui_menu_place_kind(enum ui_menu_kind k, uint32_t s, int32_t width, int32_t bar_y, int32_t x, int32_t y)
{
    int32_t w, h;
    ui_menu_size(k, s, &w, &h);
    if (x > width - w)
        x = width - w;
    if (y > bar_y - h)
        y = bar_y - h;
    return (struct rect){ x < 0 ? 0 : x, y < 0 ? 0 : y, w, h };
}

void ui_menu_init(struct ui_menu *m, enum ui_menu_kind k, uint32_t s, struct rect r)
{
    m->kind = k;
    m->scale = s;
    m->r = r;
    m->sel = -1;
    m->ring = false;
    m->greyed = 0;
    m->ticked = 0;
}

struct rect ui_menu_row_rect(const struct ui_menu *m, int row)
{
    const struct menu_def *d = menu_def(m->kind);
    if (row < 0 || row >= d->rows)
        return (struct rect){ 0, 0, 0, 0 };
    uint32_t s = m->scale;
    int32_t e = ui_px(1, s), y0 = m->r.y + ui_px(d->row_dp[row], s);
    int32_t y1 = m->r.y + ui_px(d->row_dp[row] + MENU_ROW_DP, s);
    return (struct rect){ m->r.x + e, y0, m->r.w - 2 * e, y1 - y0 };
}

int ui_menu_row_at(const struct ui_menu *m, int32_t x, int32_t y)
{
    for (int i = 0; i < menu_def(m->kind)->rows; i++)
        if (ui_rect_contains(ui_menu_row_rect(m, i), x, y))
            return i;
    return -1;
}

int ui_menu_next(const struct ui_menu *m, int dir)
{
    int n = menu_def(m->kind)->rows;
    int at = m->sel < 0 || m->sel >= n ? (dir > 0 ? n - 1 : 0) : m->sel;
    /* 1.5 stage 13: the next row that is not greyed (none: -1). With no
     * row greyed this is the row after (or before) the selection, wrapping
     * around, and the first (or last) when none is selected, as before. */
    for (int k = 0; k < n; k++) {
        at = (at + (dir > 0 ? 1 : n - 1)) % n;
        if (ui_menu_row_live(m, at))
            return at;
    }
    return -1;
}

/* The M menu's wordmark as a line, the menu at r. */
static struct wp_line menu_wordmark(uint32_t s, struct rect r)
{
    int32_t hy = r.y + ui_px(4, s), hh = ui_px(52, s) - ui_px(4, s);
    return (struct wp_line){ &wp_wordmark, ((int64_t)r.x + ui_px(52, s)) << FB,
                             ((int64_t)hy + (hh - ui_px(10, s)) / 2) << FB, wp_fx(10 * (int64_t)s, 56),
                             wp_fx(MENU_TRACK, 1), wp_fx(500 * (int64_t)s, 2576), wp_fx(250 * (int64_t)s, 2576),
                             MENU_HEAVY };
}

size_t ui_menu_scratch(uint32_t s, uint32_t width, uint32_t height)
{
    int32_t w, h;
    ui_menu_size(UI_MENU_M, s, &w, &h);
    struct wp_line line = menu_wordmark(s, (struct rect){ 0, 0, w, h });
    return wp_line_scratch(&line, width, height);
}

/* The colour under (x, y) before a mark is drawn there, for the trace
 * (ui_trace.h); nothing outside the surface. */
static void menu_trace(const struct ui_canvas *c, int kind, uint32_t rgb, int64_t x, int64_t y)
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

bool ui_menu_draw(const struct ui_canvas *c, const struct ui_menu *m, const struct ui_fonts *f, uint16_t *scratch,
                  size_t entries)
{
    const struct menu_def *d = menu_def(m->kind);
    uint32_t s = m->scale;
    struct rect r = m->r;
    int32_t R = ui_px(6, s), e = ui_px(1, s);
    bool whole = true;
    if (ui_rect_empty(ui_rect_intersect(c->clip, r)))
        return true;
    ui_fill_round(c, r, R, UI_CORNERS_ALL, UI_SURFACE);
    if (d->header) {
        int32_t P = ui_px(24, s), hy = r.y + ui_px(4, s), hh = ui_px(52, s) - ui_px(4, s);
        ui_icon_draw(c, UI_ICON_M, r.x + ui_px(16, s), hy + (hh - P) / 2, P, UI_ACCENT);
        struct wp_line line = menu_wordmark(s, r);
        if (!wp_line_draw(c->s, c->clip, &line, UI_TEXT, scratch, entries))
            whole = false;
    }
    for (int i = 0; i < d->dividers; i++) {
        int32_t y0 = r.y + ui_px(d->div_dp[i], s), y1 = r.y + ui_px(d->div_dp[i] + 1, s);
        int32_t x0 = r.x + ui_px(16, s), x1 = r.x + ui_px(d->w_dp - 16, s);
        ui_fill(c, (struct rect){ x0, y0, x1 - x0, y1 - y0 }, UI_BORDER);
    }
    for (int i = 0; i < d->rows; i++) {
        struct rect row = ui_menu_row_rect(m, i);
        if (i == m->sel) {
            struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, row) };
            menu_trace(c, UI_TRACE_CARRIED_BAR, UI_SURFACE_ACTIVE, (int64_t)row.x + row.w / 2,
                       (int64_t)row.y + row.h / 2);
            if (!ui_rect_empty(in.clip))
                ui_fill_round(&in, ui_rect_inflate(r, -e), R - e > 0 ? R - e : 0, UI_CORNERS_ALL, UI_SURFACE_ACTIVE);
            int32_t g = ui_px(2, s), inset = ui_px(4, s);
            struct rect bar = { row.x, row.y + inset, g, row.h - 2 * inset };
            menu_trace(c, UI_TRACE_STATE, UI_ACCENT, (int64_t)bar.x + bar.w / 2, (int64_t)bar.y + bar.h / 2);
            ui_fill(c, bar, UI_ACCENT);
            if (m->ring) {
                struct rect ring = { row.x + g, row.y, row.w - 2 * g, row.h };
                menu_trace(c, UI_TRACE_STATE, UI_ACCENT, (int64_t)ring.x + ring.w / 2, ring.y);
                ui_frame_round(c, ring, ui_px(4, s), g, UI_CORNERS_ALL, UI_ACCENT);
            }
        }
        int32_t ip = ui_px(16, s), ty = row.y + (row.h - (int32_t)f->regular->height) / 2;
        bool grey = (m->greyed >> i) & 1u, tick = (m->ticked >> i) & 1u;      /* 1.5 stage 13 */
        uint32_t fg = grey ? UI_TEXT_MUTED : UI_TEXT;
        ui_icon_draw(c, tick ? UI_ICON_CHECK : d->icon[i], r.x + ui_px(16, s), row.y + (row.h - ip) / 2, ip, fg);
        ui_text(c, f->regular, r.x + ui_px(44, s), ty, d->label[i], fg);
        if (d->hint[i])
            ui_text(c, f->regular, r.x + ui_px(d->w_dp - 16, s) - ui_text_width(f->regular, d->hint[i]), ty,
                    d->hint[i], UI_TEXT_MUTED);
    }
    ui_frame_round(c, r, R, e, UI_CORNERS_ALL, UI_BORDER_PANEL);
    return whole;
}
