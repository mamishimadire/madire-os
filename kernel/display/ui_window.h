#ifndef MADIRE_DISPLAY_UI_WINDOW_H
#define MADIRE_DISPLAY_UI_WINDOW_H

/*
 * Windows and the window manager, the pure part (1.3 stage 4a; host unit
 * tests in tests/unit/test_ui_window.c; the kernel side is desktop.c).
 *
 * A window (the Design Guide, section 8), at scale s, dp to pixels as
 * ui_px(dp, s), its rectangle r:
 *   - the frame: r filled in `surface.window` as a rounded rectangle of
 *     radius 6 dp (all corners), then a 1 dp frame of the same radius in
 *     `border.window` (the active window) or `border.inactive`;
 *   - the client (its paint function, e.g. the About window's content);
 *   - the title bar, the top 32 dp (tb): the window's icon (16 dp) at
 *     r.x + 8 dp, r.y + (tb - icon) / 2, in `text`; the title in the bold
 *     font at r.x + 32 dp, r.y + (tb - font height) / 2, in `text` (active)
 *     or `text.muted`, clipped to end 8 dp before the close button;
 *   - the close button: the tb x tb target at the right end of the title
 *     bar, drawn as ui_widget.h's button with the close icon (16 dp) in
 *     `text`, no fill when normal, `danger` on hover, `danger.pressed`
 *     while pressed, filled as a rounded rectangle of radius 6 dp at its
 *     top-right corner only (it follows the window's corner, over the
 *     edge: the fill covers the whole target).
 * The pixels outside the rounded rectangle are never drawn. Each pixel gets
 * the same value whatever the clip, and wherever the window lies (its
 * shapes are placed in whole pixels), so a window painted on its own
 * surface at (0, 0) and composed through ui_blit_round gives the pixels of
 * the window painted on the screen directly (docs/plans/1.3.md, C4, A1).
 *
 * The window manager keeps the open windows in z-order (bottom to top),
 * the focused one (or none) and the pointer's state, and reports what it
 * changed as damage (into the compositor's list) and as events (for the
 * log):
 *   - opening a window places it (below), puts it on top and focuses it;
 *   - placement: centred in the area above the taskbar (x = (W - w) / 2,
 *     y = (bar - h) / 2, 0 if negative); while an open window has that
 *     same top-left corner, 24 dp further right and down; then clamped;
 *   - the clamp: y in [0, bar - h] (0 if bar < h): never under the
 *     taskbar, the title bar always on the screen; x in [48 dp - w,
 *     W - 48 dp]: at least 48 dp of the window on the screen;
 *   - the left button pressed: hit-tested from the top down (the windows by
 *     z-order, then the taskbar, then the desktop). On a window: it is
 *     raised (if not on top) and focused (if not focused); on its close
 *     button the button is pressed; on its title bar a drag starts, which
 *     keeps the pointer (capture) until the button is released: the window
 *     goes to clamp(its start + the pointer's movement since), 1:1. On
 *     the taskbar: nothing. On the desktop: no window is focused;
 *   - the left button released on the close button that was pressed:
 *     the window closes; the focus passes to the top window left, if any;
 *   - the close button's state: pressed while the button pressed on it
 *     is held with the pointer over it (normal when the pointer leaves);
 *     hover while the pointer is over it (no drag, no button pressed
 *     elsewhere); else normal;
 *   - Esc (ui_wm_esc): closes the focused window if it closes on Esc.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_widget.h"

#define UI_WINDOWS_MAX   4
#define UI_WM_EVENTS     16

struct ui_window {
    /* Set by the window's owner. */
    const char *name;           /* in the log: "About" */
    const char *title;
    int icon;                   /* ui_icon.h */
    int32_t w, h;               /* pixels */
    bool esc_closes;
    void (*paint)(const struct ui_canvas *c, struct rect r, void *ctx);     /* the client, inside r */
    void *ctx;
    /* Kept by the window manager. */
    bool open;
    struct rect r;
    enum ui_button_state close;     /* the close button's state as last drawn */
    bool dirty;                 /* its look (focus or close button) changed since ui_wm_clean */
};

enum ui_hit { UI_HIT_DESKTOP, UI_HIT_TASKBAR, UI_HIT_BODY, UI_HIT_TITLE, UI_HIT_CLOSE };

enum ui_wm_event_kind {
    UI_EV_OPEN, UI_EV_RAISE, UI_EV_FOCUS, UI_EV_UNFOCUS, UI_EV_DRAG, UI_EV_CLOSE_BUTTON, UI_EV_CLOSE_ESC,
    UI_EV_CLOSE_STATE
};

struct ui_wm_event {
    enum ui_wm_event_kind kind;
    struct ui_window *w;        /* none for UI_EV_UNFOCUS */
    struct rect r;              /* OPEN, DRAG: the window's rectangle */
    enum ui_button_state state; /* CLOSE_STATE */
};

struct ui_wm {
    int32_t width, height, bar_y;
    uint32_t scale;
    struct ui_damage *damage;
    struct ui_window *z[UI_WINDOWS_MAX];    /* the open windows, bottom to top */
    uint32_t n;
    struct ui_window *focus;
    int32_t x, y;               /* the pointer */
    bool left;                  /* the left button held */
    struct ui_window *drag;     /* the window being dragged (it has the pointer) */
    int32_t grab_x, grab_y;     /* the pointer when the drag started */
    struct rect grab_r;         /* the window then */
    struct ui_window *pressed;  /* the window whose close button was pressed */
    struct ui_wm_event ev[UI_WM_EVENTS];    /* what the last call changed, in order */
    uint32_t n_ev;
    uint32_t lost_ev;           /* events not kept (the list full) */
};

/* Pixels of the frame: the title bar (32 dp), the edge (1 dp), the corner
 * radius (6 dp). */
int32_t ui_window_title_h(uint32_t s);
int32_t ui_window_edge(uint32_t s);
int32_t ui_window_radius(uint32_t s);

/* The window's size for a client of cw x ch pixels below the title bar,
 * inside the edge (the Console window: the 80 x 25 grid). */
void ui_window_size_for_client(uint32_t s, int32_t cw, int32_t ch, int32_t *w, int32_t *h);

/* The close button's target of a window at r. */
struct rect ui_window_close_rect(struct rect r, uint32_t s);

/* Draws window w at r (the rules above) with the fonts of scale s. */
void ui_window_draw(const struct ui_canvas *c, const struct ui_window *w, struct rect r, bool active,
                    enum ui_button_state close, uint32_t s, const struct ui_fonts *f);

/* A manager for a width x height screen whose taskbar starts at row
 * bar_y, at scale s; damage goes to d. */
void ui_wm_init(struct ui_wm *m, int32_t width, int32_t height, int32_t bar_y, uint32_t s, struct ui_damage *d);

/* Where a w x h window opens (placement, above), the open windows as they are. */
struct rect ui_wm_place(const struct ui_wm *m, int32_t w, int32_t h);

/* r moved to the nearest place the clamp allows. */
struct rect ui_wm_clamp(const struct ui_wm *m, struct rect r);

/* What is at (x, y), from the top down; *w the window hit (or none). */
enum ui_hit ui_wm_hit(const struct ui_wm *m, int32_t x, int32_t y, struct ui_window **w);

/* Opens w (or raises and focuses it if it is open). False if
 * UI_WINDOWS_MAX windows are open already. */
bool ui_wm_open(struct ui_wm *m, struct ui_window *w);

/* The pointer at (x, y) (clamped to the screen by the caller), then the
 * left button pressed (down) and/or released (up) there. */
void ui_wm_pointer(struct ui_wm *m, int32_t x, int32_t y, bool down, bool up);

/* Esc: closes the focused window if it closes on Esc; true if it did. */
bool ui_wm_esc(struct ui_wm *m);

/* 1.3 stage 5: the menus take the pointer (the Windows key opened the M
 * menu): a held left button is let go here as if released, except that a
 * pressed close button closes nothing: a drag ends where the window is
 * (its event if it moved), the close button is no longer pressed, and
 * the release that follows is ignored (the button is no longer held). */
void ui_wm_cancel(struct ui_wm *m);

/* 1.3 stage 4b: where a key goes (decision 7, as amended in stage 3b; the
 * Design Guide, section 8), for one key event, pressed or released:
 *   - no window focused: a press opens the Console window `console` (or
 *     raises it) and focuses it, and is not typed: UI_KEY_OPENED; a
 *     release does nothing (UI_KEY_TAKEN);
 *   - `console` focused: the console takes the key, Esc included (Esc
 *     does not close the Console window): UI_KEY_TO_CONSOLE;
 *   - another window focused: Esc pressed closes it if it closes on Esc
 *     (ui_wm_esc); every key: UI_KEY_TAKEN (nothing else happens). */
enum ui_key_route { UI_KEY_TAKEN, UI_KEY_OPENED, UI_KEY_TO_CONSOLE };
enum ui_key_route ui_wm_key(struct ui_wm *m, struct ui_window *console, bool pressed, bool esc);

/* The close button's state for w now (the rules above). */
enum ui_button_state ui_wm_close_state(const struct ui_wm *m, const struct ui_window *w);

/* Empties the event list (each call above adds to it). */
void ui_wm_clear_events(struct ui_wm *m);

#endif
