/* Single-line input editor (see lineedit.h). */
#include "kernel/console/lineedit.h"

void lineedit_reset(struct lineedit *le)
{
    le->len = 0;
    le->pos = 0;
    le->buf[0] = '\0';
}

static enum le_result insert(struct lineedit *le, char c)
{
    if (le->len >= LINE_MAX)
        return LE_REFUSED;
    for (uint8_t i = le->len; i > le->pos; i--)
        le->buf[i] = le->buf[i - 1];
    le->buf[le->pos] = c;
    le->len++;
    le->pos++;
    le->buf[le->len] = '\0';
    return LE_CHANGED;
}

/* Removes the character at index i (i < len). */
static void remove_at(struct lineedit *le, uint8_t i)
{
    for (; i + 1 < le->len; i++)
        le->buf[i] = le->buf[i + 1];
    le->len--;
    le->buf[le->len] = '\0';
}

static enum le_result move_to(struct lineedit *le, uint8_t pos)
{
    if (pos == le->pos)
        return LE_NONE;
    le->pos = pos;
    return LE_CHANGED;
}

enum le_result lineedit_key(struct lineedit *le, const struct key_event *e)
{
    if (!(e->flags & KEY_PRESSED) || (e->flags & (KEY_CTRL | KEY_ALT)))
        return LE_NONE;

    switch (e->keycode) {
    case KEY_ENTER:
    case KEY_KP_ENTER:
        return LE_SUBMIT;
    case KEY_BACKSPACE:
        if (le->pos == 0)
            return LE_NONE;
        le->pos--;
        remove_at(le, le->pos);
        return LE_CHANGED;
    case KEY_DELETE:
        if (le->pos >= le->len)
            return LE_NONE;
        remove_at(le, le->pos);
        return LE_CHANGED;
    case KEY_LEFT:
        return le->pos ? move_to(le, (uint8_t)(le->pos - 1)) : LE_NONE;
    case KEY_RIGHT:
        return le->pos < le->len ? move_to(le, (uint8_t)(le->pos + 1)) : LE_NONE;
    case KEY_HOME:
        return move_to(le, 0);
    case KEY_END:
        return move_to(le, le->len);
    case KEY_TAB:
        return insert(le, '\t');
    case KEY_SPACE:
    case KEY_CHAR:
        if (e->ascii >= 0x20 && e->ascii <= 0x7E)
            return insert(le, e->ascii);
        return LE_NONE;
    default:
        return LE_NONE;
    }
}
