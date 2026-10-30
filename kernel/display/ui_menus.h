#ifndef MADIRE_DISPLAY_UI_MENUS_H
#define MADIRE_DISPLAY_UI_MENUS_H

/*
 * The desktop's menus and the Restart dialog: what the pointer and the keys
 * do to them, the pure part (1.3 stage 5; docs/plans/1.3.md, C5, decisions
 * 9 and 20; the Design Guide, sections 9 and 10; host unit tests in
 * tests/unit/test_ui_menus.c; the kernel side is desktop.c). The window
 * manager (ui_window.h) gets what they leave. tests/desktop_ref.py's Menus
 * follows the same rules.
 *
 * The pointer, each packet (the hot spot, the left button pressed and
 * released, the right button pressed), from the top down:
 *   - the dialog open: it keeps every packet. A left press on Cancel,
 *     Restart or its close button presses it; released on the same one, it
 *     is chosen (Cancel and the close button cancel); a press elsewhere
 *     does nothing. Each of the three is pressed while the press on it is
 *     held and the pointer is over it, on hover while the pointer is over
 *     it and no left press is held, else normal;
 *   - a menu open: it keeps every packet. The pointer on a row selects it
 *     (the ring off); on no row, a selection made by the pointer is cleared
 *     (one made by the keyboard stays). A left press inside the menu,
 *     released on a row, chooses it; a left press on the M button with the
 *     M menu open, released on it, closes the M menu; any other left or
 *     right press closes the menu and does nothing else (its release
 *     neither);
 *   - a left press the menus keep (on the M button, or one that closed a
 *     menu) is held: the packets are kept until it is released; released on
 *     the M button after a press there, the M menu opens, no row selected;
 *   - otherwise: a left press on the M button (no button held in the window
 *     manager) is kept, as above; every other packet goes to the window
 *     manager (ui_wm_pointer), and then a right press on the desktop (no
 *     window there, above the taskbar, no left button held) opens the
 *     desktop menu at the hot spot, no row selected.
 * The M button: open while the M menu is open; pressed while a left press
 * on it is held and the pointer is over it; on hover while the pointer is
 * over it, no left button held, no menu and no dialog open; else normal.
 *
 * The keys, each event pressed or released:
 *   - the dialog open: it keeps every key: Enter pressed presses the
 *     focused button, Tab moves the focus to the other one (the ring on),
 *     Esc cancels; nothing else does anything (the Windows key included);
 *   - the Windows key (decision 20), pressed: closes the M menu if it is
 *     open; else closes the desktop menu if it is open, lets go of a left
 *     press (the menus' and the window manager's: ui_wm_cancel) and opens
 *     the M menu with its first row (About) selected and the focus ring.
 *     Never passed on, released or pressed;
 *   - a menu open: Up and Down move the selection (ui_menu.h), Enter
 *     chooses the selected row (with none, nothing), Esc closes it; every
 *     key is kept;
 *   - otherwise: UI_MENUS_TO_WM (the caller hands it to ui_wm_key).
 * Choosing a row closes its menu first. About opens (or raises and
 * focuses) the About window and Console the Console window (ui_wm_open;
 * the caller logs the About values, or starts the console if it is not
 * running: ui_menus_take_done); Restart opens the dialog (its ring on if
 * chosen by the keyboard). Restart pressed in the dialog: restart is set
 * (the caller restarts the machine); the dialog stays.
 * When a menu or the dialog closes, the window manager gets the pointer's
 * place with no button (ui_wm_pointer), so its close buttons follow it.
 * Each change is an event (for the log, in order) and damage.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_dialog.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_menu.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_window.h"

#define UI_MENUS_EVENTS 16

enum ui_menus_key { UI_MKEY_OTHER, UI_MKEY_UP, UI_MKEY_DOWN, UI_MKEY_ENTER, UI_MKEY_TAB, UI_MKEY_ESC,
                    UI_MKEY_WINDOWS };
enum ui_menus_route { UI_MENUS_TAKEN, UI_MENUS_TO_WM };

/* What a kept left press was on. */
enum ui_menus_press { UI_MPRESS_NONE, UI_MPRESS_M, UI_MPRESS_MENU, UI_MPRESS_OUTSIDE, UI_MPRESS_CANCEL,
                      UI_MPRESS_RESTART, UI_MPRESS_CLOSE };

/* How a menu opened or closed, or the dialog closed. */
enum ui_menus_why { UI_WHY_MOUSE, UI_WHY_KEY, UI_WHY_ESC, UI_WHY_OUTSIDE, UI_WHY_WINDOWS, UI_WHY_M_BUTTON,
                    UI_WHY_CHOSEN, UI_WHY_CANCEL, UI_WHY_CLOSE_BUTTON };

enum ui_menus_ev_kind {
    UI_MEV_MENU_OPEN,           /* menu, why (mouse or key), r; with key: row 0 selected */
    UI_MEV_MENU_SELECT,         /* menu, row (-1 none), ring */
    UI_MEV_MENU_CHOSEN,         /* menu, row */
    UI_MEV_MENU_CLOSE,          /* menu, why */
    UI_MEV_M_STATE,             /* state */
    UI_MEV_DLG_OPEN,            /* r, ring */
    UI_MEV_DLG_FOCUS,           /* row: the button focused */
    UI_MEV_DLG_STATE,           /* row: the part (a button, or 2 the close button), state */
    UI_MEV_DLG_CLOSE,           /* why */
    UI_MEV_RESTART
};

struct ui_menus_event {
    enum ui_menus_ev_kind kind;
    enum ui_menu_kind menu;
    int row;
    bool ring;
    enum ui_menus_why why;
    int state;
    struct rect r;
};

struct ui_menus {
    struct ui_wm *wm;
    struct ui_window *about, *console;
    struct rect m_cell;
    struct ui_fonts fonts;
    bool menu_open;
    struct ui_menu menu;
    bool dlg_open;
    struct ui_dialog dlg;
    int32_t x, y;               /* the pointer */
    bool left;                  /* a left press the menus keep is held */
    enum ui_menus_press press;
    enum ui_m_state m_state;
    enum ui_menu_action done;   /* About or Console opened, not yet taken */
    bool restart;               /* Restart pressed in the dialog */
    struct ui_menus_event ev[UI_MENUS_EVENTS];
    uint32_t n_ev;
    uint32_t lost_ev;
};

/* Menus for the window manager wm (initialised: its screen, scale and
 * damage list), its About and Console windows, the M button's cell and the
 * fonts of wm's scale. The pointer starts where wm's is. */
void ui_menus_init(struct ui_menus *m, struct ui_wm *wm, struct ui_window *about, struct ui_window *console,
                   struct rect m_cell, const struct ui_fonts *f);

/* One packet (the rules above): the hot spot (clamped by the caller), the
 * left button pressed (down) and/or released (up), the right button pressed. */
void ui_menus_pointer(struct ui_menus *m, int32_t x, int32_t y, bool down, bool up, bool right);

/* One key event (the rules above). */
enum ui_menus_route ui_menus_key(struct ui_menus *m, enum ui_menus_key k, bool pressed);

/* The window a row opened since the last call (UI_MENU_ABOUT or
 * UI_MENU_CONSOLE), or UI_MENU_NONE; cleared. */
enum ui_menu_action ui_menus_take_done(struct ui_menus *m);

/* Empties the event list. */
void ui_menus_clear_events(struct ui_menus *m);

#endif
