/*
 * The desktop's window shortcuts and the Super key (1.5 stage 9b); see
 * deskkeys_core.h.
 */
#include "kernel/display/deskkeys_core.h"
#include "drivers/keyboard/scancode.h"

void deskkeys_init(struct deskkeys *k, uint32_t dropped)
{
    k->held = 0;
    k->other = false;
    k->dropped = dropped;
    k->reset = false;
}

void deskkeys_pointer(struct deskkeys *k)
{
    if (k->held)
        k->other = true;
}

bool deskkeys_take_reset(struct deskkeys *k)
{
    bool r = k->reset;
    k->reset = false;
    return r;
}

/* The letter of a character key, lower case (0 if none). */
static char dk_letter(uint8_t keycode, char ascii)
{
    if (keycode != KEY_CHAR)
        return 0;
    if (ascii >= 'A' && ascii <= 'Z')
        return (char)(ascii - 'A' + 'a');
    return ascii;
}

/* A key pressed with a Super key held. */
static enum deskkeys_action dk_with_super(uint8_t keycode, char ascii, uint8_t flags)
{
    switch (keycode) {
    case KEY_UP:
        return DK_UP;
    case KEY_DOWN:
        return DK_DOWN;
    case KEY_LEFT:
        return DK_LEFT;
    case KEY_RIGHT:
        return DK_RIGHT;
    case KEY_PAUSE:
        return DK_ABOUT;
    default:
        break;
    }
    char l = dk_letter(keycode, ascii);
    if (l == 'd')
        return DK_SHOW_DESKTOP;
    if (l == 'm')
        return (flags & KEY_SHIFT) ? DK_RESTORE_ALL : DK_MINIMISE_ALL;
    if (l == 'e')
        return DK_FILES;            /* 1.5 stage 12 */
    return DK_TAKEN;
}

enum deskkeys_action deskkeys_key(struct deskkeys *k, uint8_t keycode, char ascii, bool pressed, uint8_t flags,
                                  const struct deskkeys_ctx *c, uint32_t dropped)
{
    if (dropped != k->dropped) {            /* a key event was lost: start clean */
        k->dropped = dropped;
        if (k->held || k->other)
            k->reset = true;
        k->held = 0;
        k->other = false;
    }
    if (keycode == KEY_LGUI || keycode == KEY_RGUI) {
        uint32_t bit = keycode == KEY_LGUI ? DESKKEYS_SUPER_LEFT : DESKKEYS_SUPER_RIGHT;
        if (pressed) {
            if (k->held & bit)
                return DK_TAKEN;            /* the keyboard's repeat */
            if (!k->held)
                k->other = false;
            k->held |= bit;
            return DK_TAKEN;
        }
        if (!(k->held & bit))
            return DK_TAKEN;                /* a release with no press seen */
        k->held &= ~bit;
        if (k->held)
            return DK_TAKEN;
        bool alone = !k->other;
        k->other = false;
        return alone && !c->dlg_open ? DK_MENU : DK_TAKEN;
    }
    if (k->held) {
        if (!pressed)
            return DK_PASS;
        k->other = true;
        if (c->dlg_open)
            return DK_TAKEN;
        return dk_with_super(keycode, ascii, flags);
    }
    if (keycode == KEY_F4 && pressed && (flags & KEY_ALT) && !(flags & KEY_CTRL)) {
        if (c->switching || c->menu_open || c->dlg_open || !c->focused)
            return DK_TAKEN;
        return DK_CLOSE;
    }
    return DK_PASS;
}
