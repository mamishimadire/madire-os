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
};

const int ui_files_button_icons[UF_BUTTONS] = {
    [UF_NONE] = UI_ICON_NONE, [UF_BACK] = UI_ICON_BACK, [UF_FORWARD] = UI_ICON_FORWARD, [UF_UP] = UI_ICON_UP,
    [UF_LIST] = UI_ICON_LIST, [UF_GRID] = UI_ICON_GRID, [UF_NEWFOLDER] = UI_ICON_NEW_FOLDER,
    [UF_NEWFILE] = UI_ICON_NEW_FILE, [UF_OPEN] = UI_ICON_OPEN, [UF_RENAME] = UI_ICON_RENAME,
    [UF_HIDDEN] = UI_ICON_NONE, [UF_CLOSE] = UI_ICON_NONE,
};

const char *const ui_files_panel_labels[4] = { "Name", "Size", "Type", "Modified" };

const char *const ui_files_tip_texts[UF_BUTTONS] = {
    [UF_NONE] = "", [UF_BACK] = "Back (Alt+Left)", [UF_FORWARD] = "Forward (Alt+Right)", [UF_UP] = "Up (Alt+Up)",
    [UF_LIST] = "", [UF_GRID] = "", [UF_NEWFOLDER] = "", [UF_NEWFILE] = "", [UF_OPEN] = "", [UF_RENAME] = "",
    [UF_HIDDEN] = "", [UF_CLOSE] = "",
};

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
    int32_t bx = x0 + d8;
    for (int i = UF_NEWFOLDER; i <= UF_RENAME; i++) {
        int32_t w = files_button_w(f, i, s);
        out->button[i] = (struct rect){ bx, y + d4, w, d32 };
        bx += w + d4;
    }
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

enum ui_files_part ui_files_hit(const struct ui_files_layout *lay, const struct ui_files_view *v, uint32_t s,
                                const struct font *f, int32_t x, int32_t y, int32_t *index)
{
    *index = -1;
    if (v->mode == UI_FILES_NOMEM)
        return UF_NONE;
    for (int i = UF_BACK; i <= UF_HIDDEN; i++)
        if (ui_rect_contains(lay->button[i], x, y))
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
    if (part >= UF_BACK && part <= UF_UP) {
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
                       const struct files_pathbar *pb, uint32_t s, const struct font *f)
{
    struct rect p = lay->path;
    if (ui_rect_empty(p))
        return;
    ui_fill_round(c, p, ui_px(4, s), UI_CORNERS_ALL, UI_SURFACE);
    struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, p) };
    int32_t ip = ui_px(16, s);
    ui_icon_draw(&in, UI_ICON_PLACE_HOME, p.x + ui_px(8, s), p.y + (p.h - ip) / 2, ip, UI_ACCENT);
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
        bool later = k >= 7;
        ui_icon_draw(&in, ui_files_place_icons[k], it.x + d12, it.y + (it.h - ip) / 2, ip,
                     later ? UI_TEXT_MUTED : UI_ACCENT);
        ui_text(&in, f, it.x + ui_px(36, s), it.y + (it.h - (int32_t)f->height) / 2, ui_files_place_names[k],
                later ? UI_TEXT_MUTED : UI_TEXT);
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
    ui_text(&in, f, lx, my + (d28 - fh) / 2, UI_FILES_NOAPP_TEXT, UI_TEXT);
    files_button(&in, v, lay->panel_close, UF_CLOSE, s, f);
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
    files_path(&cv, &lay, v->pb, s, fr);
    for (int i = UF_LIST; i <= UF_HIDDEN; i++)
        files_button(&cv, v, lay.button[i], i, s, fr);
    ui_fill(&cv, (struct rect){ lay.x0, lay.cmd.y + lay.cmd.h, lay.iw, E }, UI_BORDER);
    ui_fill(&cv, (struct rect){ lay.side.x + lay.side.w, lay.body.y, E, lay.body.h }, UI_BORDER);
    ui_fill(&cv, (struct rect){ lay.x0, lay.status.y - E, lay.iw, E }, UI_BORDER);
    files_side(&cv, &lay, v, s, fr);
    if (v->mode == UI_FILES_PANEL) {
        files_panel(&cv, &lay, v, s, fr);
    } else if (v->list) {
        struct ui_canvas lc = { c->s, ui_rect_intersect(cv.clip, lay.content) };
        if (!ui_rect_empty(lc.clip)) {
            ui_list_draw(&lc, v->list, f);
            if (v->renaming && v->field && v->list->cursor < v->list->n) {
                struct ui_canvas fc = { c->s, ui_rect_intersect(lc.clip, ui_list_body(v->list)) };
                if (!ui_rect_empty(fc.clip))
                    ui_textfield_draw(&fc, v->field, ui_files_field_rect(v->list, v->list->cursor), s, f, true);
            }
        }
    }
    struct ui_canvas sc = { c->s, ui_rect_intersect(cv.clip, lay.status) };
    files_text(&sc, fr, lay.status.x + ui_px(12, s), lay.status.y + (lay.status.h - fh) / 2,
               lay.status.w - ui_px(24, s), v->status, v->status_error ? UI_DANGER : UI_TEXT_MUTED);
    if (v->tip >= UF_BACK && v->tip <= UF_UP) {
        struct rect b = lay.button[v->tip];
        const char *name = ui_files_tip_texts[v->tip];
        struct rect box = { b.x, b.y + b.h + ui_px(4, s), ui_text_width(fr, name) + 2 * ui_px(8, s), ui_px(24, s) };
        ui_fill_round(&cv, box, ui_px(4, s), UI_CORNERS_ALL, UI_SURFACE);
        ui_frame_round(&cv, box, ui_px(4, s), E, UI_CORNERS_ALL, UI_BORDER_PANEL);
        ui_text(&cv, fr, box.x + ui_px(8, s), box.y + (box.h - fh) / 2, name, UI_TEXT);
    }
}
