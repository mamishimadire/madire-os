/* The console keys (1.3 stage 7b); see conskeys.h. */
#include "kernel/console/conskeys.h"

enum conskey conskeys_classify(const struct key_event *e)
{
    if (!(e->flags & KEY_PRESSED) || !(e->flags & KEY_CTRL) || (e->flags & KEY_ALT) || e->keycode != KEY_CHAR)
        return CONSKEY_NONE;
    char c = e->ascii;
    if (c >= 1 && c <= 26)
        c = (char)(c + 'a' - 1);            /* a control code, as Ctrl+C's 3 */
    if (c >= 'A' && c <= 'Z')
        c = (char)(c + 'a' - 'A');
    switch (c) {
    case 'l':
        return CONSKEY_CLEAR;
    case 'a':
        return CONSKEY_START;
    case 'e':
        return CONSKEY_END;
    case 'k':
        return CONSKEY_KILL_END;
    case 'u':
        return CONSKEY_KILL_START;
    case 'w':
        return CONSKEY_WORD;
    default:
        return CONSKEY_NONE;
    }
}

enum conskeys_do conskeys_route(enum conskey k, bool scrolled, bool asking, bool busy)
{
    if (k == CONSKEY_NONE || busy)
        return CONSKEYS_IGNORE;
    if (asking)
        return scrolled ? CONSKEYS_LEAVE : CONSKEYS_IGNORE;
    return scrolled ? CONSKEYS_LEAVE_ACT : CONSKEYS_ACT;
}

/* Deletes [from, to) of the line; the cursor goes to `from`. */
static void conskeys_cut(struct lineedit *le, uint8_t from, uint8_t to)
{
    uint8_t n = (uint8_t)(to - from);
    for (uint8_t i = from; i + n <= le->len; i++)
        le->buf[i] = le->buf[i + n];        /* the NUL moves too */
    le->len = (uint8_t)(le->len - n);
    le->pos = from;
}

bool conskeys_edit(struct lineedit *le, enum conskey k)
{
    uint8_t at = le->pos;
    switch (k) {
    case CONSKEY_START:
        le->pos = 0;
        return at != 0;
    case CONSKEY_END:
        le->pos = le->len;
        return at != le->len;
    case CONSKEY_KILL_END:
        if (at == le->len)
            return false;
        le->len = at;
        le->buf[at] = '\0';
        return true;
    case CONSKEY_KILL_START:
        if (at == 0)
            return false;
        conskeys_cut(le, 0, at);
        return true;
    case CONSKEY_WORD:
        while (at > 0 && le->buf[at - 1] == ' ')
            at--;
        while (at > 0 && le->buf[at - 1] != ' ')
            at--;
        if (at == le->pos)
            return false;
        conskeys_cut(le, at, le->pos);
        return true;
    default:
        return false;
    }
}
