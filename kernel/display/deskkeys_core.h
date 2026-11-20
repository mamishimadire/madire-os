#ifndef MADIRE_DISPLAY_DESKKEYS_CORE_H
#define MADIRE_DISPLAY_DESKKEYS_CORE_H

/*
 * The desktop's window shortcuts and the Super key (1.5 stage 9b;
 * docs/plans/1.5.md, item (6) and its S9b rules; host unit tests in
 * tests/unit/test_deskkeys.c; desktop.c carries out the actions). Pure: no
 * hardware access, printing or panics.
 *
 * One call per key event (desktop_key), before Alt+Tab, the menus and the
 * window manager see it; the answer is what to do with it:
 *   - the Super key (left or right, each kept apart): its first press marks
 *     it held and clears the "another key" mark; a press of a Super key
 *     already held (the keyboard's repeat) changes nothing; the release of
 *     the last Super key held toggles the M menu (DK_MENU) if no other key
 *     was pressed and no mouse button (deskkeys_pointer) while it was held
 *     and the power dialog is not open; every Super event is taken;
 *   - any other key pressed while a Super key is held sets the mark and is
 *     taken: Up DK_UP, Down DK_DOWN, Left DK_LEFT, Right DK_RIGHT, Pause
 *     DK_ABOUT, D DK_SHOW_DESKTOP, M DK_MINIMISE_ALL (with Shift
 *     DK_RESTORE_ALL); any other key does nothing (DK_TAKEN); with the
 *     power dialog open, nothing (DK_TAKEN). Letters are matched whatever
 *     their case (Caps Lock), Shift by its flag. Releases pass (DK_PASS),
 *     so a modifier's release still reaches Alt+Tab;
 *   - with no Super key held: Alt+F4 pressed (Alt held, Ctrl not) is
 *     DK_CLOSE when a window is focused, no menu or dialog is open and
 *     Alt+Tab is not switching; otherwise it is taken and does nothing.
 *     Every other event passes (DK_PASS);
 *   - safety: each call gives the key queue's dropped-event count
 *     (keyboard_dropped(): kernel/input/keyqueue.c counts an event lost to
 *     a full queue); when it differs from the last one seen, the Super
 *     state and the mark are cleared first (deskkeys_take_reset then
 *     reports it once, if a Super key was held or the mark was set), so a
 *     lost Super release can never leave the desktop taking every key.
 */

#include <stdbool.h>
#include <stdint.h>

enum deskkeys_action {
    DK_PASS,            /* on to Alt+Tab, the menus and the window manager, as before */
    DK_TAKEN,           /* taken; nothing more */
    DK_MENU,            /* toggle the M menu (the Super key released alone) */
    DK_UP, DK_DOWN, DK_LEFT, DK_RIGHT,
    DK_SHOW_DESKTOP, DK_MINIMISE_ALL, DK_RESTORE_ALL,
    DK_ABOUT,           /* Super+Pause */
    DK_CLOSE            /* Alt+F4 */
};

#define DESKKEYS_SUPER_LEFT  0x1u
#define DESKKEYS_SUPER_RIGHT 0x2u

struct deskkeys {
    uint32_t held;              /* DESKKEYS_SUPER_* held now */
    bool other;                 /* another key or a mouse button pressed while held */
    uint32_t dropped;           /* the key queue's dropped count last seen */
    bool reset;                 /* the state was cleared for a drop (deskkeys_take_reset) */
};

/* What the desktop shows when the key comes. */
struct deskkeys_ctx {
    bool menu_open, dlg_open, switching, focused;
};

/* An empty state; `dropped` the key queue's count now. */
void deskkeys_init(struct deskkeys *k, uint32_t dropped);

/* One key event: its keycode (scancode.h), its character, pressed or
 * released, its flags (KEY_SHIFT, KEY_CTRL, KEY_ALT), the desktop's state
 * and the key queue's dropped count now. */
enum deskkeys_action deskkeys_key(struct deskkeys *k, uint8_t keycode, char ascii, bool pressed, uint8_t flags,
                                  const struct deskkeys_ctx *c, uint32_t dropped);

/* A mouse button pressed: with a Super key held, it counts as another key. */
void deskkeys_pointer(struct deskkeys *k);

/* True once after the state was cleared for a dropped key event. */
bool deskkeys_take_reset(struct deskkeys *k);

#endif
