/* Console scrollback (see scrollback.h). */
#include "kernel/console/scrollback.h"

void sb_init(struct scrollback *s)
{
    s->head = 0;
    s->count = 0;
    s->top = 0;
    s->scrolled = false;
}

void sb_push(struct scrollback *s, const uint16_t row[TB_COLS])
{
    for (uint32_t c = 0; c < TB_COLS; c++)
        s->lines[s->head][c] = row[c];
    s->head = s->head + 1 == SB_LINES ? 0 : s->head + 1;
    if (s->count < SB_LINES) {
        s->count++;
    } else if (s->scrolled && s->top > 0) {
        s->top--;           /* the oldest line went: same lines, one lower number */
    }
}

uint32_t sb_count(const struct scrollback *s)
{
    return s->count;
}

const uint16_t *sb_line(const struct scrollback *s, uint32_t i)
{
    if (i >= s->count)
        return 0;
    uint32_t slot = (s->head + SB_LINES - s->count + i) % SB_LINES;
    return s->lines[slot];
}

bool sb_scrolled(const struct scrollback *s)
{
    return s->scrolled;
}

enum sb_move sb_page_up(struct scrollback *s)
{
    if (s->count == 0)
        return SB_EMPTY;
    if (!s->scrolled) {
        s->top = s->count >= SB_VIEW_ROWS ? s->count - SB_VIEW_ROWS : 0;
        s->scrolled = true;
        return SB_MOVED;
    }
    if (s->top == 0)
        return SB_UNCHANGED;
    s->top = s->top >= SB_VIEW_ROWS ? s->top - SB_VIEW_ROWS : 0;
    return SB_MOVED;
}

enum sb_move sb_page_down(struct scrollback *s)
{
    if (!s->scrolled)
        return SB_UNCHANGED;
    s->top += SB_VIEW_ROWS;
    if (s->top >= s->count) {
        s->top = 0;
        s->scrolled = false;            /* reached the live screen */
    }
    return SB_MOVED;
}

bool sb_to_bottom(struct scrollback *s)
{
    bool was = s->scrolled;
    s->scrolled = false;
    s->top = 0;
    return was;
}

struct sb_source sb_view_row(const struct scrollback *s, uint32_t r)
{
    struct sb_source src;
    uint32_t line = s->top + r;
    if (line < s->count) {
        src.from_screen = false;
        src.index = line;
    } else {
        src.from_screen = true;
        src.index = line - s->count;
    }
    return src;
}

uint32_t sb_first_shown(const struct scrollback *s)
{
    return (s->scrolled ? s->top : s->count) + 1;
}

uint32_t sb_total(const struct scrollback *s)
{
    return s->count + TB_ROWS;
}

static uint32_t put_str(char *out, uint32_t n, const char *s)
{
    while (*s)
        out[n++] = *s++;
    return n;
}

static uint32_t put_uint(char *out, uint32_t n, uint32_t v)
{
    char digits[10];
    uint32_t k = 0;
    do {
        digits[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (k)
        out[n++] = digits[--k];
    return n;
}

uint32_t sb_indicator(const struct scrollback *s, char out[TB_COLS + 1])
{
    uint32_t n = 0;
    n = put_str(out, n, "-- scrolled back: line ");
    n = put_uint(out, n, sb_first_shown(s));
    n = put_str(out, n, " of ");
    n = put_uint(out, n, sb_total(s));
    n = put_str(out, n, " (Shift+PgUp/PgDn; any key returns) --");
    out[n] = '\0';
    return n;
}

uint32_t sb_row_text(const uint16_t row[TB_COLS], char *out, uint32_t max)
{
    uint32_t end = TB_COLS;
    while (end > 0) {
        char c = (char)(row[end - 1] & 0xFF);
        if (c != ' ' && c != '\0')
            break;
        end--;
    }
    uint32_t n = end < max ? end : max;
    for (uint32_t i = 0; i < n; i++) {
        char c = (char)(row[i] & 0xFF);
        out[i] = (c >= 0x20 && c <= 0x7E) ? c : '?';
    }
    out[n] = '\0';
    return n;
}

int32_t sb_last_used_row(const uint16_t cells[TB_ROWS][TB_COLS])
{
    for (int32_t r = TB_ROWS - 1; r >= 0; r--)
        for (uint32_t c = 0; c < TB_COLS; c++) {
            char ch = (char)(cells[r][c] & 0xFF);
            if (ch != ' ' && ch != '\0')
                return r;
        }
    return -1;
}

enum sb_key sb_classify_key(const struct key_event *e)
{
    if (!(e->flags & KEY_PRESSED))
        return SB_KEY_NONE;
    switch (e->keycode) {
    case KEY_LSHIFT: case KEY_RSHIFT: case KEY_LCTRL: case KEY_RCTRL:
    case KEY_LALT: case KEY_RALT: case KEY_LGUI: case KEY_RGUI:
    case KEY_CAPSLOCK: case KEY_NUMLOCK: case KEY_SCROLLLOCK:
        return SB_KEY_NONE;
    default:
        break;
    }
    bool shift_only = (e->flags & KEY_SHIFT) && !(e->flags & (KEY_CTRL | KEY_ALT));
    if (shift_only && e->keycode == KEY_PGUP)
        return SB_KEY_PAGE_UP;
    if (shift_only && e->keycode == KEY_PGDN)
        return SB_KEY_PAGE_DOWN;
    return SB_KEY_OTHER;
}
