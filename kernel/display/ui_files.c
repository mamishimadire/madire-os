/*
 * The Files window's picture and hit testing (1.5 stage 12); see
 * ui_files.h.
 */
#include "kernel/display/ui_files.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_about.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_trace.h"

#define FILES_TEXT_MAX 320          /* a status or panel text as drawn, its NUL included */

const char *const ui_files_place_names[UI_FILES_PLACES] = {
    "Home", "Desktop", "Documents", "Downloads", "Pictures", "Music", "Videos", "Trash", "This PC",
};

const int ui_files_place_icons[UI_FILES_PLACES] = {
    UI_ICON_PLACE_HOME, UI_ICON_PLACE_DESKTOP, UI_ICON_PLACE_DOCUMENTS, UI_ICON_PLACE_DOWNLOADS,
    UI_ICON_PLACE_PICTURES, UI_ICON_PLACE_MUSIC, UI_ICON_PLACE_VIDEOS, UI_ICON_PLACE_TRASH, UI_ICON_PLACE_THISPC,
};

const char *const ui_files_button_labels[UF_BUTTONS] = {
    [UF_NONE] = "", [UF_BACK] = "Back", [UF_FORWARD] = "Forward", [UF_UP] = "Up", [UF_LIST] = "List",
    [UF_GRID] = "Grid", [UF_NEWFOLDER] = "New Folder", [UF_NEWFILE] = "New File", [UF_OPEN] = "Open",
    [UF_RENAME] = "Rename", [UF_HIDDEN] = "Show hidden files", [UF_CLOSE] = "Close",
    /* 1.5 stage 13 (the squares' names are their tooltips' first words) */
    [UF_CUT] = "Cut", [UF_COPY] = "Copy", [UF_PASTE] = "Paste", [UF_TRASH] = "Move to Trash",
    [UF_RESTORE] = "Restore", [UF_RESTORE_ALL] = "Restore all", [UF_EMPTY] = "Empty Trash",
    [UF_TDELETE] = "Delete permanently", [UF_ASK_CANCEL] = "Cancel", [UF_ASK_OK] = "Ask OK",
};

const int ui_files_button_icons[UF_BUTTONS] = {
    [UF_NONE] = UI_ICON_NONE, [UF_BACK] = UI_ICON_BACK, [UF_FORWARD] = UI_ICON_FORWARD, [UF_UP] = UI_ICON_UP,
    [UF_LIST] = UI_ICON_LIST, [UF_GRID] = UI_ICON_GRID, [UF_NEWFOLDER] = UI_ICON_NEW_FOLDER,
    [UF_NEWFILE] = UI_ICON_NEW_FILE, [UF_OPEN] = UI_ICON_OPEN, [UF_RENAME] = UI_ICON_RENAME,
    [UF_HIDDEN] = UI_ICON_NONE, [UF_CLOSE] = UI_ICON_NONE, [UF_CUT] = UI_ICON_CUT, [UF_COPY] = UI_ICON_COPY,
    [UF_PASTE] = UI_ICON_PASTE, [UF_TRASH] = UI_ICON_PLACE_TRASH, [UF_RESTORE] = UI_ICON_RESTORE,
    [UF_RESTORE_ALL] = UI_ICON_RESTORE, [UF_EMPTY] = UI_ICON_PLACE_TRASH, [UF_TDELETE] = UI_ICON_PLACE_TRASH,
    [UF_ASK_CANCEL] = UI_ICON_NONE, [UF_ASK_OK] = UI_ICON_NONE,
};

const char *const ui_files_panel_labels[4] = { "Name", "Size", "Type", "Modified" };

const char *const ui_files_tip_texts[UF_BUTTONS] = {
    [UF_NONE] = "", [UF_BACK] = "Back (Alt+Left)", [UF_FORWARD] = "Forward (Alt+Right)", [UF_UP] = "Up (Alt+Up)",
    [UF_LIST] = "", [UF_GRID] = "", [UF_NEWFOLDER] = "", [UF_NEWFILE] = "", [UF_OPEN] = "Open (Enter)",
    [UF_RENAME] = "Rename (F2)", [UF_HIDDEN] = "", [UF_CLOSE] = "", [UF_CUT] = "Cut (Ctrl+X)",
    [UF_COPY] = "Copy (Ctrl+C)", [UF_PASTE] = "Paste (Ctrl+V)", [UF_TRASH] = "Move to Trash (Delete)",
    [UF_RESTORE] = "", [UF_RESTORE_ALL] = "", [UF_EMPTY] = "", [UF_TDELETE] = "Delete permanently (Delete)",
    [UF_ASK_CANCEL] = "", [UF_ASK_OK] = "",
};

bool ui_files_square(enum ui_files_part p)
{
    return (p >= UF_BACK && p <= UF_UP) || p == UF_OPEN || p == UF_RENAME || (p >= UF_CUT && p <= UF_TRASH) ||
           p == UF_TDELETE;
}

/* 1.5 stage 13: the buttons a view shows and hits (the toolbar's, its
 * command row's: the folder's or the Trash's; the question's). 1.5 stage
 * 14b: the This PC view's row holds only Open. */
static bool files_shown(const struct ui_files_view *v, int part)
{
    switch (part) {
    case UF_OPEN:
        return !v->trash;
    case UF_HIDDEN:
        return !v->thispc;
    case UF_NEWFOLDER:
    case UF_NEWFILE:
        return !v->trash && !v->thispc;
    case UF_RENAME:
    case UF_CUT:
    case UF_COPY:
    case UF_PASTE:
    case UF_TRASH:
        return !v->trash && !v->thispc;
    case UF_RESTORE:
    case UF_RESTORE_ALL:
    case UF_EMPTY:
    case UF_TDELETE:
        return v->trash;
    case UF_CLOSE:
    case UF_ASK_CANCEL:
    case UF_ASK_OK:
        return false;                   /* the content's own (panel, question) */
    default:
        return part > UF_NONE && part < UF_BUTTONS;
    }
}

/* 1.5 stage 14b: where a button is in this view (Open moves to the row's
 * start in the This PC view). */
static struct rect files_brect(const struct ui_files_layout *lay, const struct ui_files_view *v, int part)
{
    return v->thispc && part == UF_OPEN ? lay->pc_open : lay->button[part];
}

/* The colour under (x, y) before a mark is drawn there, for the trace
 * (ui_trace.h); nothing outside the surface. */
static void files_trace(const struct ui_canvas *c, int kind, uint32_t rgb, int64_t x, int64_t y)
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

enum ui_files_esc ui_files_esc(const struct ui_files_view *v)
{
    if (v->mode == UI_FILES_LIST && v->renaming)
        return UF_ESC_CANCEL_RENAME;
    if (v->mode == UI_FILES_PANEL)
        return UF_ESC_CLOSE_PANEL;
    if (v->mode == UI_FILES_ASK)
        return UF_ESC_CANCEL_ASK;       /* 1.5 stage 13 */
    return UF_ESC_CLOSE_WINDOW;
}

void ui_files_count_text(uint32_t n, const char *free_text, char *out, uint32_t size)
{
    char digits[12];
    uint32_t nd = 0, k = 0;
    if (size == 0)
        return;
    do {
        digits[nd++] = (char)('0' + n % 10u);
        n /= 10u;
    } while (n && nd < sizeof(digits));
    bool one = nd == 1 && digits[0] == '1';
    while (nd > 0 && k + 1 < size)
        out[k++] = digits[--nd];
    const char *mid = one ? " item, " : " items, ";
    for (uint32_t i = 0; mid[i] && k + 1 < size; i++)
        out[k++] = mid[i];
    for (uint32_t i = 0; free_text && free_text[i] && k + 1 < size; i++)
        out[k++] = free_text[i];
    static const char tail[] = " free";
    for (uint32_t i = 0; tail[i] && k + 1 < size; i++)
        out[k++] = tail[i];
    out[k] = '\0';
}

/* A button with an icon and a label: d(36) + the label + d(12). */
static int32_t files_button_w(const struct font *f, int part, uint32_t s)
{
    return ui_px(36, s) + ui_text_width(f, ui_files_button_labels[part]) + ui_px(12, s);
}

void ui_files_layout(struct rect r, uint32_t s, const struct font *f, bool bar, struct ui_files_layout *out)
{
    int32_t E = ui_px(1, s), d4 = ui_px(4, s), d8 = ui_px(8, s), d32 = ui_px(32, s), d40 = ui_px(40, s);
    int32_t x0 = r.x + E, x1 = r.x + r.w - E, iw = x1 - x0, y = r.y + ui_px(32, s);
    for (int i = 0; i < UF_BUTTONS; i++)
        out->button[i] = (struct rect){ 0, 0, 0, 0 };
    out->x0 = x0;
    out->iw = iw;
    out->bar = (struct rect){ x0, y, bar ? iw : 0, bar ? ui_px(28, s) : 0 };
    y += out->bar.h;
    out->toolbar = (struct rect){ x0, y, iw, d40 };
    for (int i = 0; i < 3; i++)
        out->button[UF_BACK + i] = (struct rect){ x0 + d8 + i * (d32 + d4), y + d4, d32, d32 };
    int32_t gw = files_button_w(f, UF_GRID, s), lw = files_button_w(f, UF_LIST, s);
    out->button[UF_GRID] = (struct rect){ x1 - d8 - gw, y + d4, gw, d32 };
    out->button[UF_LIST] = (struct rect){ out->button[UF_GRID].x - d4 - lw, y + d4, lw, d32 };
    int32_t px0 = out->button[UF_UP].x + d32 + d8, px1 = out->button[UF_LIST].x - d8;
    out->path = (struct rect){ px0, y + d4, px1 > px0 ? px1 - px0 : 0, d32 };
    y += d40;
    out->cmd = (struct rect){ x0, y, iw, d40 };
    out->pc_open = (struct rect){ x0 + d8, y + d4, d32, d32 };     /* 1.5 stage 14b: This PC's Open */
    /* 1.5 stage 13: New Folder and New File, then six squares d(12) on. */
    int32_t bx = x0 + d8;
    for (int i = UF_NEWFOLDER; i <= UF_NEWFILE; i++) {
        int32_t w = files_button_w(f, i, s);
        out->button[i] = (struct rect){ bx, y + d4, w, d32 };
        bx += w + d4;
    }
    static const int files_squares[6] = { UF_OPEN, UF_RENAME, UF_CUT, UF_COPY, UF_PASTE, UF_TRASH };
    bx += d8;
    for (int k = 0; k < 6; k++) {
        out->button[files_squares[k]] = (struct rect){ bx, y + d4, d32, d32 };
        bx += d32 + d4;
    }
    /* The Trash view's row: Restore, Restore all, Empty Trash, then the square. */
    bx = x0 + d8;
    for (int i = UF_RESTORE; i <= UF_EMPTY; i++) {
        int32_t w = files_button_w(f, i, s);
        out->button[i] = (struct rect){ bx, y + d4, w, d32 };
        bx += w + d4;
    }
    out->button[UF_TDELETE] = (struct rect){ bx + d8, y + d4, d32, d32 };
    int32_t hw = files_button_w(f, UF_HIDDEN, s);
    out->button[UF_HIDDEN] = (struct rect){ x1 - d8 - hw, y + d4, hw, d32 };
    y += d40;
    out->status = (struct rect){ x0, r.y + r.h - E - ui_px(28, s), iw, ui_px(28, s) };
    int32_t by = y + E, bb = out->status.y - E, bh = bb > by ? bb - by : 0;
    out->body = (struct rect){ x0, by, iw, bh };
    int32_t sw = ui_px(168, s);
    out->side = (struct rect){ x0, by, sw < iw ? sw : iw, bh };
    int32_t cx = x0 + sw + E;
    out->content = (struct rect){ cx, by, x1 > cx ? x1 - cx : 0, bh };
    struct rect c = out->content;
    int32_t d28 = ui_px(28, s), msg_y = c.y + ui_px(24, s) + 4 * d28 + d8;
    out->panel_close = (struct rect){ c.x + ui_px(96, s), msg_y + d28 + d8,
                                      ui_text_width(f, ui_files_button_labels[UF_CLOSE]) + 2 * ui_px(12, s), d32 };
    out->button[UF_CLOSE] = out->panel_close;
    /* 1.5 stage 13: the question panel's buttons. */
    int32_t ay = c.y + ui_px(88, s), ax = c.x + ui_px(24, s);
    out->button[UF_ASK_CANCEL] = (struct rect){ ax, ay, ui_px(96, s), d32 };
    out->button[UF_ASK_OK] = (struct rect){ ax + ui_px(96, s) + d8, ay, ui_px(128, s), d32 };
}

uint32_t ui_files_path_cells(const struct ui_files_layout *lay, uint32_t s, const struct font *f)
{
    int32_t room = lay->path.w - ui_px(32, s) - ui_px(8, s);
    return room > 0 && f->width > 0 ? (uint32_t)room / f->width : 0;
}

struct rect ui_files_place_rect(const struct ui_files_layout *lay, uint32_t s, int32_t k)
{
    int32_t h = ui_px(32, s);
    return (struct rect){ lay->side.x, lay->side.y + ui_px(8, s) + k * h + (k >= 7 ? ui_px(9, s) : 0), lay->side.w,
                          h };
}

struct rect ui_files_field_rect(const struct ui_list *l, uint32_t i)
{
    uint32_t s = l->scale;
    struct rect it = ui_list_item_rect(l, i);
    if (l->view == UI_LIST_GRID)
        return (struct rect){ it.x + ui_px(2, s), it.y + ui_px(58, s), it.w - ui_px(4, s), ui_px(26, s) };
    struct rect b = ui_list_body(l);
    int32_t xs = b.x + b.w - ui_px(144, s) - ui_px(112, s) - ui_px(80, s);
    int32_t x = it.x + ui_px(32, s), w = xs - l->r.x - ui_px(36, s);
    return (struct rect){ x, it.y, w > 0 ? w : 0, it.h };
}

/* The badge's text as drawn: shown ('?' for other bytes), cut to d(160). */
static void files_badge_text(const char *text, uint32_t s, const struct font *f, char *cut, uint32_t size)
{
    char shown[FILES_NAME_MAX + 1];
    ui_name_shown(text ? text : "", shown, sizeof(shown));
    ui_text_fit(f, shown, ui_px(160, s), cut, size);
}

struct rect ui_files_badge_rect(int32_t x, int32_t y, uint32_t s, const struct font *f, const char *text)
{
    char cut[FILES_NAME_MAX + 1];
    files_badge_text(text, s, f, cut, sizeof(cut));
    return (struct rect){ x + ui_px(16, s), y + ui_px(20, s), ui_px(40, s) + ui_text_width(f, cut), ui_px(28, s) };
}

void ui_files_badge_draw(const struct ui_canvas *c, struct rect r, uint32_t s, const struct font *f, int icon,
                         const char *text)
{
    char cut[FILES_NAME_MAX + 1];
    int32_t d4 = ui_px(4, s), ip = ui_px(16, s);
    if (ui_rect_empty(r))
        return;
    files_badge_text(text, s, f, cut, sizeof(cut));
    ui_fill_round(c, r, d4, UI_CORNERS_ALL, UI_SURFACE);
    ui_frame_round(c, r, d4, ui_px(1, s), UI_CORNERS_ALL, UI_BORDER_PANEL);
    if (icon != UI_ICON_NONE)
        ui_icon_draw(c, icon, r.x + ui_px(8, s), r.y + (r.h - ip) / 2, ip, UI_TEXT);
    ui_text(c, f, r.x + ui_px(32, s), r.y + (r.h - (int32_t)f->height) / 2, cut, UI_TEXT);
}

struct rect ui_files_card_rect(const struct ui_files_layout *lay, uint32_t s, int32_t k)
{
    struct rect c = lay->content;
    int32_t w = c.w - ui_px(32, s), mw = ui_px(480, s);
    return (struct rect){ c.x + ui_px(16, s), c.y + ui_px(16, s) + k * (ui_px(64, s) + ui_px(8, s)),
                          w < mw ? (w > 0 ? w : 0) : mw, ui_px(64, s) };
}

enum ui_files_part ui_files_hit(const struct ui_files_layout *lay, const struct ui_files_view *v, uint32_t s,
                                const struct font *f, int32_t x, int32_t y, int32_t *index)
{
    *index = -1;
    if (v->mode == UI_FILES_NOMEM)
        return UF_NONE;
    for (int i = UF_BACK; i < UF_BUTTONS; i++)            /* 1.5 stage 13: the view's own buttons */
        if (files_shown(v, i) && ui_rect_contains(files_brect(lay, v, i), x, y))
            return (enum ui_files_part)i;
    if (ui_rect_contains(lay->path, x, y)) {
        int32_t tx = lay->path.x + ui_px(32, s);
        if (v->pb && x >= tx && f->width > 0) {
            uint32_t cell = (uint32_t)(x - tx) / f->width;
            for (uint32_t k = 0; k < v->pb->n && k < FILES_PB_MAX; k++)
                if (cell >= v->pb->part[k].x && cell < (uint32_t)v->pb->part[k].x + v->pb->part[k].len) {
                    *index = (int32_t)k;
                    return UF_PATH;
                }
        }
        return UF_NONE;
    }
    if (ui_rect_contains(lay->side, x, y)) {
        for (int32_t k = 0; k < UI_FILES_PLACES; k++)
            if (ui_rect_contains(ui_files_place_rect(lay, s, k), x, y)) {
                *index = k;
                return UF_PLACE;
            }
        return UF_NONE;
    }
    if (ui_rect_contains(lay->content, x, y)) {
        if (v->mode == UI_FILES_PANEL)
            return ui_rect_contains(lay->panel_close, x, y) ? UF_CLOSE : UF_PANEL;
        if (v->mode == UI_FILES_ASK) {      /* 1.5 stage 13: the question panel */
            if (ui_rect_contains(lay->button[UF_ASK_CANCEL], x, y))
                return UF_ASK_CANCEL;
            return ui_rect_contains(lay->button[UF_ASK_OK], x, y) ? UF_ASK_OK : UF_PANEL;
        }
        if (v->thispc)                      /* 1.5 stage 14b: This PC's cards */
            for (int32_t k = 0; k < (int32_t)v->ncards && k < UI_FILES_CARDS; k++)
                if (ui_rect_contains(ui_files_card_rect(lay, s, k), x, y)) {
                    *index = k;
                    break;
                }
        return UF_CONTENT;
    }
    if (ui_rect_contains(lay->bar, x, y))
        return UF_BAR;
    if (ui_rect_contains(lay->status, x, y))
        return UF_STATUS;
    return UF_NONE;
}

/* A button (the rules in ui_files.h): its fill and edge, then its icon
 * and label, or the checkbox (Show hidden files). */
static void files_button(const struct ui_canvas *c, const struct ui_files_view *v, struct rect b, int part,
                         uint32_t s, const struct font *f)
{
    if (part <= UF_NONE || part >= UF_BUTTONS)
        return;
    bool on = v->enabled[part], chosen = v->chosen[part], check = part == UF_HIDDEN;
    if ((part == UF_LIST || part == UF_GRID) && v->list)        /* the mark follows the view drawn */
        chosen = (v->list->view == UI_LIST_GRID) == (part == UF_GRID);
    bool pressed = on && v->pressed == (enum ui_files_part)part;
    bool hover = on && !pressed && v->hover == (enum ui_files_part)part;
    int32_t R = ui_px(4, s), E = ui_px(1, s);
    int64_t mx = (int64_t)b.x + b.w / 2, my = (int64_t)b.y + b.h / 2;
    if (pressed) {
        files_trace(c, UI_TRACE_STATE, UI_ACCENT_PRESSED, mx, my);
        ui_fill_round(c, b, R, UI_CORNERS_ALL, UI_ACCENT_PRESSED);
    } else if (hover) {
        files_trace(c, UI_TRACE_CARRIED_POINTER, UI_SURFACE_HOVER, mx, my);
        ui_fill_round(c, b, R, UI_CORNERS_ALL, UI_SURFACE_HOVER);
    } else if (chosen && !check) {
        files_trace(c, UI_TRACE_CARRIED_BAR, UI_SURFACE_ACTIVE, mx, my);
        ui_fill_round(c, b, R, UI_CORNERS_ALL, UI_SURFACE_ACTIVE);
    } else if (!check) {
        ui_fill_round(c, b, R, UI_CORNERS_ALL, UI_SURFACE);
    }
    if (!check) {
        bool accent_edge = chosen && !pressed && !hover;
        if (accent_edge)
            files_trace(c, UI_TRACE_STATE, UI_ACCENT, mx, b.y);
        ui_frame_round(c, b, R, E, UI_CORNERS_ALL, accent_edge ? UI_ACCENT : UI_BORDER);
    }
    uint32_t fg = on ? UI_TEXT : UI_TEXT_MUTED;
    int32_t ip = ui_px(16, s), iy = b.y + (b.h - ip) / 2, ty = b.y + (b.h - (int32_t)f->height) / 2;
    if (ui_files_square((enum ui_files_part)part)) {        /* 1.5 stage 13: every square */
        ui_icon_draw(c, ui_files_button_icons[part], b.x + (b.w - ip) / 2, iy, ip, fg);
        return;
    }
    const char *label = ui_files_button_labels[part];
    if (part == UF_CLOSE) {
        ui_text(c, f, b.x + (b.w - ui_text_width(f, label)) / 2, ty, label, fg);
        return;
    }
    int32_t ix = b.x + ui_px(12, s);
    if (check) {
        struct rect box = { ix, iy, ip, ip };
        if (chosen) {
            files_trace(c, UI_TRACE_STATE, UI_ACCENT, (int64_t)box.x + box.w / 2, (int64_t)box.y + box.h / 2);
            ui_fill_round(c, box, ui_px(3, s), UI_CORNERS_ALL, UI_ACCENT);
            ui_icon_draw(c, UI_ICON_CHECK, box.x, box.y, ip, UI_TEXT_ON_ACCENT);
        } else {
            files_trace(c, UI_TRACE_STATE, UI_TEXT_MUTED, box.x, (int64_t)box.y + box.h / 2);
            ui_frame_round(c, box, ui_px(3, s), ui_px(2, s), UI_CORNERS_ALL, UI_TEXT_MUTED);
        }
    } else {
        ui_icon_draw(c, ui_files_button_icons[part], ix, iy, ip, fg);
    }
    ui_text(c, f, b.x + ui_px(36, s), ty, label, fg);
}

/* Text cut with "..." to room pixels (bytes outside printable ASCII as '?'). */
static void files_text(const struct ui_canvas *c, const struct font *f, int32_t x, int32_t y, int32_t room,
                       const char *text, uint32_t rgb)
{
    char shown[FILES_TEXT_MAX], cut[FILES_TEXT_MAX];
    if (!text)
        return;
    ui_name_shown(text, shown, sizeof(shown));
    ui_text_fit(f, shown, room < 0 ? 0 : room, cut, sizeof(cut));
    ui_text(c, f, x, y, cut, rgb);
}

static void files_path(const struct ui_canvas *c, const struct ui_files_layout *lay,
                       const struct files_pathbar *pb, uint32_t s, const struct font *f, int icon)
{
    struct rect p = lay->path;
    if (ui_rect_empty(p))
        return;
    ui_fill_round(c, p, ui_px(4, s), UI_CORNERS_ALL, UI_SURFACE);
    struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, p) };
    int32_t ip = ui_px(16, s);
    /* 1.5 stage 13: the Trash view shows the trash place's icon; 1.5 stage
     * 14b: the This PC view its colour icon. */
    ui_icon_draw(&in, icon, p.x + ui_px(8, s), p.y + (p.h - ip) / 2, ip, UI_ACCENT);
    if (!pb)
        return;
    int32_t tx = p.x + ui_px(32, s), ty = p.y + (p.h - (int32_t)f->height) / 2, fw = (int32_t)f->width;
    if (pb->clipped)
        ui_text(&in, f, tx, ty, "...", UI_TEXT_MUTED);
    char part[FILES_NAME_MAX + 1], shown[FILES_NAME_MAX + 1];
    for (uint32_t k = 0; k < pb->n && k < FILES_PB_MAX; k++) {
        const struct files_pb_part *q = &pb->part[k];
        if (k > 0 || (pb->clipped && q->x >= 6))
            ui_text(&in, f, tx + ((int32_t)q->x - 3) * fw, ty, " > ", UI_TEXT_MUTED);
        uint32_t n = q->len < FILES_NAME_MAX ? q->len : FILES_NAME_MAX;
        for (uint32_t i = 0; i < n; i++)
            part[i] = q->text[i];
        part[n] = '\0';
        ui_name_shown(part, shown, sizeof(shown));
        ui_text(&in, f, tx + (int32_t)q->x * fw, ty, shown, UI_TEXT);
    }
}

static void files_side(const struct ui_canvas *c, const struct ui_files_layout *lay, const struct ui_files_view *v,
                       uint32_t s, const struct font *f)
{
    struct rect sd = lay->side;
    if (ui_rect_empty(sd))
        return;
    ui_fill(c, sd, UI_SURFACE);
    struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, sd) };
    int32_t ip = ui_px(16, s), d4 = ui_px(4, s), d12 = ui_px(12, s);
    ui_fill(&in, (struct rect){ sd.x + d12, sd.y + ui_px(8, s) + 7 * ui_px(32, s) + d4, sd.w - 2 * d12, ui_px(1, s) },
            UI_BORDER);
    for (int32_t k = 0; k < UI_FILES_PLACES; k++) {
        struct rect it = ui_files_place_rect(lay, s, k);
        if (it.y >= (int64_t)sd.y + sd.h)
            break;
        if (v->place == k) {
            files_trace(&in, UI_TRACE_CARRIED_BAR, UI_SURFACE_ACTIVE, (int64_t)it.x + it.w / 2,
                        (int64_t)it.y + it.h / 2);
            ui_fill(&in, it, UI_SURFACE_ACTIVE);
            struct rect bar = { it.x, it.y + d4, ui_px(2, s), it.h - 2 * d4 };
            files_trace(&in, UI_TRACE_STATE, UI_ACCENT, (int64_t)bar.x, (int64_t)bar.y + bar.h / 2);
            ui_fill(&in, bar, UI_ACCENT);
        }
        if (v->drop_on && v->drop_place == k) {     /* 1.5 stage 14b: the drop target */
            files_trace(&in, UI_TRACE_CARRIED_POINTER, UI_SURFACE_HOVER, (int64_t)it.x + it.w / 2,
                        (int64_t)it.y + it.h / 2);
            ui_fill_round(&in, it, d4, UI_CORNERS_ALL, UI_SURFACE_HOVER);
            files_trace(&in, UI_TRACE_STATE, UI_ACCENT, (int64_t)it.x + it.w / 2, it.y);
            ui_frame_round(&in, it, d4, ui_px(1, s), UI_CORNERS_ALL, UI_ACCENT);
        }
        /* 1.5 stage 14b: every place is a place now (Trash since S13). */
        ui_icon_draw(&in, ui_files_place_icons[k], it.x + d12, it.y + (it.h - ip) / 2, ip, UI_ACCENT);
        ui_text(&in, f, it.x + ui_px(36, s), it.y + (it.h - (int32_t)f->height) / 2, ui_files_place_names[k],
                UI_TEXT);
    }
}

/* 1.5 stage 14b: the This PC view's cards (the rules in ui_files.h). */
static void files_cards(const struct ui_canvas *c, const struct ui_files_layout *lay, const struct ui_files_view *v,
                        uint32_t s, const struct font *f)
{
    struct rect cr = lay->content;
    struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, cr) };
    if (ui_rect_empty(in.clip))
        return;
    ui_fill(&in, cr, UI_SURFACE_WINDOW);
    int32_t fh = (int32_t)f->height, d8 = ui_px(8, s), x68 = ui_px(68, s), bw = ui_px(200, s);
    for (int32_t k = 0; k < (int32_t)v->ncards && k < UI_FILES_CARDS && v->cards; k++) {
        const struct ui_files_card *cd = &v->cards[k];
        struct rect r = ui_files_card_rect(lay, s, k);
        bool sel = v->card_sel == k, hover = !sel && v->card_hover == k;
        uint32_t fill = sel ? UI_SURFACE_ACTIVE : hover ? UI_SURFACE_HOVER : UI_SURFACE;
        int64_t mx = (int64_t)r.x + r.w / 2, my = (int64_t)r.y + r.h / 2;
        if (sel || hover)
            files_trace(&in, sel ? UI_TRACE_CARRIED_BAR : UI_TRACE_CARRIED_POINTER, fill, mx, my);
        ui_fill_round(&in, r, ui_px(6, s), UI_CORNERS_ALL, fill);
        if (sel)
            files_trace(&in, UI_TRACE_STATE, UI_ACCENT, mx, r.y);
        ui_frame_round(&in, r, ui_px(6, s), ui_px(1, s), UI_CORNERS_ALL, sel ? UI_ACCENT : UI_BORDER);
        ui_icon_draw(&in, cd->icon, r.x + d8, r.y + d8, ui_px(48, s), UI_TEXT);
        int32_t room = r.w - ui_px(76, s);
        if (!cd->bar) {
            files_text(&in, f, r.x + x68, r.y + (r.h - fh) / 2, room, cd->name, UI_TEXT);
            continue;
        }
        files_text(&in, f, r.x + x68, r.y + d8, room, cd->name, UI_TEXT);
        struct rect bar = { r.x + x68, r.y + ui_px(30, s), bw, d8 };
        ui_fill_round(&in, bar, ui_px(4, s), UI_CORNERS_ALL, UI_BORDER);
        int32_t used = files_bar_fill(cd->used, cd->total, bw);
        if (used > 0) {
            uint32_t uc = cd->ro ? UI_TEXT_MUTED : UI_ACCENT;
            files_trace(&in, UI_TRACE_STATE, uc, bar.x, (int64_t)bar.y + bar.h / 2);
            ui_fill_round(&in, (struct rect){ bar.x, bar.y, used, bar.h }, ui_px(4, s), UI_CORNERS_ALL, uc);
        }
        /* text.muted on surface.hover is below the text rule (3.87:1): on hover in text */
        files_text(&in, f, r.x + x68, r.y + ui_px(44, s), room, cd->figures, hover ? UI_TEXT : UI_TEXT_MUTED);
    }
}

static void files_panel(const struct ui_canvas *c, const struct ui_files_layout *lay, const struct ui_files_view *v,
                        uint32_t s, const struct font *f)
{
    struct rect cr = lay->content;
    struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, cr) };
    if (ui_rect_empty(in.clip))
        return;
    int32_t d24 = ui_px(24, s), d28 = ui_px(28, s), fh = (int32_t)f->height;
    if (v->panel_icon != UI_ICON_NONE)
        ui_icon_draw(&in, v->panel_icon, cr.x + d24, cr.y + d24, ui_px(48, s), UI_TEXT);
    int32_t lx = cr.x + ui_px(96, s), vx = cr.x + ui_px(192, s), room = cr.x + cr.w - ui_px(16, s) - vx;
    for (int k = 0; k < 4; k++) {
        int32_t ty = cr.y + d24 + k * d28 + (d28 - fh) / 2;
        ui_text(&in, f, lx, ty, ui_files_panel_labels[k], UI_TEXT_MUTED);
        files_text(&in, f, vx, ty, room, v->panel_values[k], UI_TEXT);
    }
    int32_t my = cr.y + d24 + 4 * d28 + ui_px(8, s);
    if (!v->panel_has_app)                  /* 1.5 stage 13: Properties of a folder or a text file */
        ui_text(&in, f, lx, my + (d28 - fh) / 2, UI_FILES_NOAPP_TEXT, UI_TEXT);
    files_button(&in, v, lay->panel_close, UF_CLOSE, s, f);
}

/* 1.5 stage 13: the question panel (the rules in ui_files.h). */
static void files_ask(const struct ui_canvas *c, const struct ui_files_layout *lay, const struct ui_files_view *v,
                      uint32_t s, const struct ui_fonts *fs)
{
    struct rect cr = lay->content;
    struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, cr) };
    if (ui_rect_empty(in.clip))
        return;
    const struct font *f = fs->regular;
    int32_t room = cr.w - ui_px(48, s);
    files_text(&in, f, cr.x + ui_px(24, s), cr.y + ui_px(24, s), room, v->ask_line1, UI_TEXT);
    files_text(&in, f, cr.x + ui_px(24, s), cr.y + ui_px(44, s), room, v->ask_line2, UI_TEXT);
    struct ui_button cancel = { lay->button[UF_ASK_CANCEL], "Cancel", UI_ICON_NONE, 0 };
    struct ui_button ok = { lay->button[UF_ASK_OK], v->ask_label ? v->ask_label : "", UI_ICON_NONE, 0 };
    ui_button_draw(&in, &cancel, &ui_style_cancel, v->ask_state[0], v->ask_ring && v->ask_focus == 0, s, fs);
    ui_button_draw(&in, &ok, &ui_style_restart, v->ask_state[1], v->ask_ring && v->ask_focus == 1, s, fs);
}

void ui_files_draw(const struct ui_canvas *c, struct rect r, uint32_t s, const struct ui_fonts *f,
                   const struct ui_files_view *v)
{
    const struct font *fr = f->regular;
    int32_t E = ui_px(1, s), tb = ui_px(32, s), fh = (int32_t)fr->height;
    struct rect inner = { r.x + E, r.y + tb, r.w - 2 * E, r.h - tb - E };
    struct ui_canvas cv = { c->s, ui_rect_intersect(c->clip, inner) };
    if (ui_rect_empty(cv.clip))
        return;
    if (v->mode == UI_FILES_NOMEM) {
        ui_text(&cv, fr, inner.x + ui_px(24, s), inner.y + ui_px(24, s), UI_FILES_NOMEM_TEXT, UI_TEXT);
        return;
    }
    struct ui_files_layout lay;
    ui_files_layout(r, s, fr, v->bar != UI_FILES_BAR_NONE, &lay);
    if (!ui_rect_empty(lay.bar)) {
        struct ui_canvas bc = { c->s, ui_rect_intersect(cv.clip, lay.bar) };
        ui_fill(&bc, lay.bar, UI_SURFACE);
        ui_text(&bc, fr, lay.bar.x + ui_px(12, s), lay.bar.y + (lay.bar.h - fh) / 2,
                v->bar == UI_FILES_BAR_LIVE ? UI_FILES_LIVE_TEXT : UI_FILES_RO_TEXT, UI_TEXT);
        ui_fill(&bc, (struct rect){ lay.bar.x, lay.bar.y + lay.bar.h - E, lay.bar.w, E }, UI_BORDER);
    }
    for (int i = UF_BACK; i <= UF_UP; i++)
        files_button(&cv, v, lay.button[i], i, s, fr);
    files_path(&cv, &lay, v->pb, s, fr,
               v->thispc ? UI_ICON_THISPC : v->trash ? UI_ICON_PLACE_TRASH : UI_ICON_PLACE_HOME);
    for (int i = UF_LIST; i < UF_BUTTONS; i++)            /* 1.5 stage 13: the view's command row */
        if (files_shown(v, i) && i != UF_BACK && i != UF_FORWARD && i != UF_UP)
            files_button(&cv, v, files_brect(&lay, v, i), i, s, fr);
    ui_fill(&cv, (struct rect){ lay.x0, lay.cmd.y + lay.cmd.h, lay.iw, E }, UI_BORDER);
    ui_fill(&cv, (struct rect){ lay.side.x + lay.side.w, lay.body.y, E, lay.body.h }, UI_BORDER);
    ui_fill(&cv, (struct rect){ lay.x0, lay.status.y - E, lay.iw, E }, UI_BORDER);
    files_side(&cv, &lay, v, s, fr);
    if (v->mode == UI_FILES_PANEL) {
        files_panel(&cv, &lay, v, s, fr);
    } else if (v->mode == UI_FILES_ASK) {
        files_ask(&cv, &lay, v, s, f);
    } else if (v->thispc) {                 /* 1.5 stage 14b */
        files_cards(&cv, &lay, v, s, fr);
    } else if (v->list) {
        struct ui_canvas lc = { c->s, ui_rect_intersect(cv.clip, lay.content) };
        if (!ui_rect_empty(lc.clip)) {
            ui_list_draw(&lc, v->list, f);
            if (v->renaming && v->field && v->list->cursor < v->list->n) {
                struct ui_canvas fc = { c->s, ui_rect_intersect(lc.clip, ui_list_body(v->list)) };
                if (!ui_rect_empty(fc.clip))
                    ui_textfield_draw(&fc, v->field, ui_files_field_rect(v->list, v->list->cursor), s, f, true);
            }
            if (v->band_on) {               /* 1.5 stage 14b: box selection's band */
                struct ui_canvas bc = { c->s, ui_rect_intersect(lc.clip, ui_list_body(v->list)) };
                if (!ui_rect_empty(bc.clip)) {
                    files_trace(&bc, UI_TRACE_STATE, UI_ACCENT, v->band.x, (int64_t)v->band.y + v->band.h / 2);
                    ui_frame_round(&bc, v->band, 0, E, UI_CORNERS_ALL, UI_ACCENT);
                }
            }
        }
    }
    struct ui_canvas sc = { c->s, ui_rect_intersect(cv.clip, lay.status) };
    files_text(&sc, fr, lay.status.x + ui_px(12, s), lay.status.y + (lay.status.h - fh) / 2,
               lay.status.w - ui_px(24, s), v->status, v->status_error ? UI_DANGER : UI_TEXT_MUTED);
    if (ui_files_square(v->tip) && files_shown(v, v->tip)) {     /* 1.5 stage 13: every square */
        struct rect b = files_brect(&lay, v, v->tip);
        const char *name = ui_files_tip_texts[v->tip];
        struct rect box = { b.x, b.y + b.h + ui_px(4, s), ui_text_width(fr, name) + 2 * ui_px(8, s), ui_px(24, s) };
        ui_fill_round(&cv, box, ui_px(4, s), UI_CORNERS_ALL, UI_SURFACE);
        ui_frame_round(&cv, box, ui_px(4, s), E, UI_CORNERS_ALL, UI_BORDER_PANEL);
        ui_text(&cv, fr, box.x + ui_px(8, s), box.y + (box.h - fh) / 2, name, UI_TEXT);
    }
}
