/*
 * The toolkit's text field and the name rules (1.5 stage 10); see
 * ui_textfield.h.
 */
#include "kernel/display/ui_textfield.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_trace.h"

void ui_textfield_set(struct ui_textfield *t, const char *text)
{
    uint16_t n = 0;
    while (text[n] && n < UI_TEXTFIELD_MAX) {
        t->buf[n] = text[n];
        n++;
    }
    t->buf[n] = '\0';
    t->len = n;
    t->pos = n;
    t->first = 0;
    t->sel = false;
}

void ui_textfield_select_all(struct ui_textfield *t)
{
    t->sel = t->len > 0;
    t->pos = t->len;
}

/* 1.5 stage 13: the selected text removed (the field empty). */
static void tf_drop_selection(struct ui_textfield *t)
{
    t->len = 0;
    t->pos = 0;
    t->first = 0;
    t->buf[0] = '\0';
    t->sel = false;
}

static enum ui_tf_result tf_insert(struct ui_textfield *t, char ch)
{
    if (t->sel)
        tf_drop_selection(t);
    if (t->len >= UI_TEXTFIELD_MAX)
        return UI_TF_REFUSED;
    for (uint16_t i = t->len; i > t->pos; i--)
        t->buf[i] = t->buf[i - 1];
    t->buf[t->pos] = ch;
    t->len++;
    t->pos++;
    t->buf[t->len] = '\0';
    return UI_TF_CHANGED;
}

/* Removes the byte at i (i < len). */
static void tf_remove(struct ui_textfield *t, uint16_t i)
{
    for (; i + 1 < t->len; i++)
        t->buf[i] = t->buf[i + 1];
    t->len--;
    t->buf[t->len] = '\0';
}

static enum ui_tf_result tf_move(struct ui_textfield *t, uint16_t pos)
{
    if (pos == t->pos)
        return UI_TF_NONE;
    t->pos = pos;
    return UI_TF_CHANGED;
}

enum ui_tf_result ui_textfield_key(struct ui_textfield *t, const struct key_event *e)
{
    if (!(e->flags & KEY_PRESSED) || (e->flags & (KEY_CTRL | KEY_ALT)))
        return UI_TF_NONE;
    switch (e->keycode) {
    case KEY_ENTER:
    case KEY_KP_ENTER:
        return UI_TF_SUBMIT;
    case KEY_ESC:
        return UI_TF_CANCEL;
    case KEY_TAB:
        return UI_TF_PASS;
    default:
        break;
    }
    if (t->sel) {                       /* 1.5 stage 13 (D1): the whole text selected */
        switch (e->keycode) {
        case KEY_BACKSPACE:
        case KEY_DELETE:
            tf_drop_selection(t);
            return UI_TF_CHANGED;
        case KEY_LEFT:
        case KEY_HOME:
            t->sel = false;
            t->pos = 0;
            return UI_TF_CHANGED;
        case KEY_RIGHT:
        case KEY_END:
            t->sel = false;
            t->pos = t->len;
            return UI_TF_CHANGED;
        default:
            break;
        }
    }
    switch (e->keycode) {
    case KEY_BACKSPACE:
        if (t->pos == 0)
            return UI_TF_NONE;
        t->pos--;
        tf_remove(t, t->pos);
        return UI_TF_CHANGED;
    case KEY_DELETE:
        if (t->pos >= t->len)
            return UI_TF_NONE;
        tf_remove(t, t->pos);
        return UI_TF_CHANGED;
    case KEY_LEFT:
        return t->pos ? tf_move(t, (uint16_t)(t->pos - 1)) : UI_TF_NONE;
    case KEY_RIGHT:
        return t->pos < t->len ? tf_move(t, (uint16_t)(t->pos + 1)) : UI_TF_NONE;
    case KEY_HOME:
        return tf_move(t, 0);
    case KEY_END:
        return tf_move(t, t->len);
    case KEY_SPACE:
    case KEY_CHAR:
        if (e->ascii >= 0x20 && e->ascii <= 0x7E)
            return tf_insert(t, e->ascii);
        return UI_TF_NONE;
    default:
        return UI_TF_NONE;
    }
}

/* floor(a / 2) for any sign (as ui_widget.c's labels). */
static int64_t tf_half_down(int64_t a)
{
    return a >= 0 ? a / 2 : -((-a + 1) / 2);
}

int32_t ui_textfield_cols(struct rect r, uint32_t s, const struct font *f)
{
    int64_t room = (int64_t)r.w - 2 * (int64_t)ui_px(8, s);
    if (room <= 0 || f->width == 0)
        return 0;
    return (int32_t)(room / f->width);
}

void ui_textfield_fit(struct ui_textfield *t, int32_t cols)
{
    int32_t k = cols < 1 ? 1 : cols;
    if (t->pos < t->first)
        t->first = t->pos;
    else if ((int32_t)t->pos - (int32_t)t->first > k - 1)
        t->first = (uint16_t)((int32_t)t->pos - (k - 1));
    if (t->first > 0 && (int32_t)t->len - (int32_t)t->first < k - 1) {
        int32_t back = (int32_t)t->len - (k - 1);
        t->first = (uint16_t)(back < 0 ? 0 : back);
    }
}

void ui_textfield_click(struct ui_textfield *t, struct rect r, uint32_t s, const struct font *f, int32_t x)
{
    int64_t rel = (int64_t)x - r.x - ui_px(8, s), w = f->width;
    int64_t col = rel <= 0 || w == 0 ? 0 : (rel + w / 2) / w;
    int64_t pos = (int64_t)t->first + col;
    t->pos = (uint16_t)(pos > t->len ? t->len : pos);
    t->sel = false;
}

void ui_textfield_draw(const struct ui_canvas *c, const struct ui_textfield *t, struct rect r, uint32_t s,
                       const struct ui_fonts *f, bool focused)
{
    if (ui_rect_empty(r) || ui_rect_empty(ui_rect_intersect(c->clip, r)))
        return;
    const struct font *fr = f->regular;
    int32_t R = ui_px(4, s), edge = focused ? ui_px(2, s) : ui_px(1, s);
    uint32_t edge_rgb = focused ? UI_ACCENT : UI_TEXT_MUTED;
#ifdef UI_TRACE
    /* The edge is a state mark against what lies outside the field
     * (ui_trace.h): the colour under the middle of its top side. */
    int64_t ex = (int64_t)r.x + r.w / 2, ey = r.y;
    if (ex >= 0 && ey >= 0 && ex < c->s->width && ey < c->s->height)
        UI_TRACE_PAIR(UI_TRACE_STATE, edge_rgb, surface_get(c->s, (uint32_t)ex, (uint32_t)ey));
#endif
    ui_fill_round(c, r, R, UI_CORNERS_ALL, UI_SURFACE);
    ui_frame_round(c, r, R, edge, UI_CORNERS_ALL, edge_rgb);
    int32_t cols = ui_textfield_cols(r, s, fr);
    int32_t tx = r.x + ui_px(8, s), ty = (int32_t)((int64_t)r.y + tf_half_down((int64_t)r.h - fr->height));
    char shown[UI_TEXTFIELD_MAX + 1];
    int32_t n = 0;
    for (int32_t i = t->first; i < t->len && n < cols; i++) {
        char ch = t->buf[i];
        shown[n++] = ch >= 0x20 && ch <= 0x7E ? ch : '?';
    }
    shown[n] = '\0';
    if (t->sel && n > 0) {
        /* 1.5 stage 13: the selected bytes on `accent`, in text.on.accent. */
        struct rect box = { tx, ty, (int32_t)((int64_t)n * fr->width), (int32_t)fr->height };
        struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, ui_rect_inflate(r, -ui_px(2, s))) };
        UI_TRACE_PAIR(UI_TRACE_STATE, UI_ACCENT, UI_SURFACE);
        UI_TRACE_PAIR(UI_TRACE_TEXT, UI_TEXT_ON_ACCENT, UI_ACCENT);
        if (!ui_rect_empty(in.clip))
            ui_fill(&in, box, UI_ACCENT);
        ui_text(c, fr, tx, ty, shown, UI_TEXT_ON_ACCENT);
        return;
    }
    ui_text(c, fr, tx, ty, shown, UI_TEXT);
    if (focused) {
        struct rect caret = { (int32_t)((int64_t)tx + ((int64_t)t->pos - t->first) * fr->width), ty, ui_px(2, s),
                              (int32_t)fr->height };
        struct ui_canvas in = { c->s, ui_rect_intersect(c->clip, ui_rect_inflate(r, -ui_px(2, s))) };
        /* The cursor against the field's fill (a state mark). */
        UI_TRACE_PAIR(UI_TRACE_STATE, UI_TEXT, UI_SURFACE);
        if (!ui_rect_empty(in.clip))
            ui_fill(&in, caret, UI_TEXT);
    }
}

/* ---- The name rules ---- */

enum ui_name_check ui_name_check(const char *name, ui_name_exists_fn exists, void *ctx)
{
    size_t n = 0;
    bool blank = true, slash = false, other = false;
    while (name[n] && n <= UI_TEXTFIELD_MAX) {
        char ch = name[n];
        if (ch != ' ')
            blank = false;
        if (ch == '/')
            slash = true;
        if (ch < 0x20 || ch > 0x7E)
            other = true;
        n++;
    }
    if (blank && n <= UI_TEXTFIELD_MAX)
        return UI_NAME_EMPTY;
    if (slash)
        return UI_NAME_SLASH;
    if ((n == 1 && name[0] == '.') || (n == 2 && name[0] == '.' && name[1] == '.'))
        return UI_NAME_RESERVED;
    if (n > UI_TEXTFIELD_MAX)
        return UI_NAME_TOO_LONG;
    if (other)
        return UI_NAME_NOT_ASCII;
    if (exists && exists(ctx, name))
        return UI_NAME_TAKEN;
    return UI_NAME_OK;
}

const char *ui_name_message(enum ui_name_check c)
{
    switch (c) {
    case UI_NAME_SLASH:
        return "A name can't contain '/'.";
    case UI_NAME_RESERVED:
        return "This name is reserved.";
    case UI_NAME_TOO_LONG:
        return "Names can be at most 255 characters.";
    case UI_NAME_NOT_ASCII:
        return "Use printable ASCII characters only.";
    case UI_NAME_TAKEN:
        return "That name is already used here.";
    default:
        return 0;
    }
}

/* Appends text at out[*at] (room checked by the caller). */
static void name_put(char *out, size_t *at, const char *text)
{
    for (size_t i = 0; text[i]; i++)
        out[(*at)++] = text[i];
}

bool ui_name_default(bool folder, ui_name_exists_fn exists, void *ctx, char *out, size_t size)
{
    if (size < 32) {
        if (size)
            out[0] = '\0';
        return false;
    }
    for (uint32_t k = 1; k <= 1000; k++) {
        size_t at = 0;
        name_put(out, &at, folder ? "New Folder" : "New File");
        if (k > 1) {
            char digits[8];
            int nd = 0;
            for (uint32_t v = k; v; v /= 10)
                digits[nd++] = (char)('0' + v % 10);
            out[at++] = ' ';
            while (nd)
                out[at++] = digits[--nd];
        }
        if (!folder)
            name_put(out, &at, ".txt");
        out[at] = '\0';
        if (!exists || !exists(ctx, out))
            return true;
    }
    out[0] = '\0';
    return false;
}

void ui_name_shown(const char *name, char *out, size_t size)
{
    size_t n = 0;
    if (!size)
        return;
    while (name[n] && n < size - 1) {
        char ch = name[n];
        out[n] = ch >= 0x20 && ch <= 0x7E ? ch : '?';
        n++;
    }
    out[n] = '\0';
}
