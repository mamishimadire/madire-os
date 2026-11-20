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
 *
 * 1.5 stage 8 (docs/plans/1.5.md, C4; the Design Guide, sections 8 and 10):
 *   - a window's title-bar buttons, from the right: close, then maximise
 *     (restore when maximised) if its `buttons` has UI_WIN_MAXIMISE, then
 *     minimise if it has UI_WIN_MINIMISE; each a tb x tb target with its
 *     16 dp icon in `text`, no fill when normal, `surface.hover` on hover,
 *     `accent.pressed` while pressed (square corners); the title clipped to
 *     end 8 dp before the leftmost button. A window with no `buttons` (the
 *     dialog, the 1.3 scenes) looks as before;
 *   - minimise hides the window (no hit, not drawn) until it is restored;
 *     the focus passes to the top window that is not minimised (or none);
 *   - maximise: the window fills the area above the taskbar, (0, 0, W,
 *     bar); restore (unmaximise) puts back the rectangle it had. A
 *     maximised window is not dragged. Its client is painted in its own
 *     size (w x h), centred across the window, from its top;
 *   - a double click on the title bar of a window that can maximise
 *     toggles maximise: a press there within UI_WM_DCLICK_TICKS of the
 *     release that ended a click on the same title bar (a press and a
 *     release with no drag), within 4 dp of that click's press; no drag
 *     starts from it;
 *   - the taskbar's window buttons (ui_wm_task_rect): one per open window,
 *     minimised ones included, in the order they opened, from task_x0
 *     (ui_wm_set_taskbar) to at most task_x1, 4 dp apart, each 160 dp wide
 *     or less (the space shared evenly, never under 40 dp), from below the
 *     taskbar's 1 dp top line to its bottom. Drawn: the window's icon (16
 *     dp) 8 dp from the button's left, centred down; its title in the
 *     regular font at 32 dp, centred down, clipped to end 8 dp before the
 *     button's right; `text` for both. States as the M button's: hover
 *     fills `surface.active`; the focused window's button and a pressed one
 *     also get a 2 dp `accent` bar along the bottom; normal (and minimised)
 *     draws no fill. A left press on a button presses it; the release over
 *     it: a minimised window is restored (raised and focused), the focused
 *     one minimised, any other raised and focused. A middle press on a
 *     button closes its window (ui_wm_middle);
 *   - Alt+Tab (ui_wm_switch_*): the windows in most-recently-focused order
 *     (minimised ones included); the first step marks the one after the
 *     focused window (back: the last), each further step the next
 *     (back: the previous), with a 2 dp `accent` ring of radius 4 dp, 2 dp
 *     inside its taskbar button; the commit restores, raises and focuses
 *     the marked window; cancel just removes the ring.
 *
 * 1.5 stage 9 (docs/plans/1.5.md, C4): resizing by the edges and corners.
 *   - a window whose owner sets `resizable` (with its minimum size min_w x
 *     min_h, in pixels) has a grab band of ui_window_band(s) = 8 dp
 *     (ui_px(8, s): 8 px at 100 %, 10 at 125 %, 12 at 150 %, 16 at 200 %)
 *     inside each edge of its rectangle (ui_window_edges): left, right,
 *     top, bottom, and where two meet, a corner. The title-bar buttons come
 *     first (the top-right corner is the close button); no band while the
 *     window is maximised or minimised;
 *   - a left press in the band raises and focuses the window (as on its
 *     body) and starts a resize, which keeps the pointer (capture) until the
 *     button is released: the band's edges follow the pointer 1:1 from the
 *     press, the opposite edges stay (ui_wm_resize_rect, below); the
 *     release logs UI_EV_RESIZE if the rectangle changed;
 *   - ui_wm_resize_rect: the size stays between the minimum and the
 *     largest, the screen's width by the area above the taskbar (or the
 *     size it started with, if larger); a moving edge never goes further off
 *     the screen than it started (left >= min(0, its start), right <=
 *     max(W, its start), top >= 0, bottom <= the taskbar's top), and 48 dp
 *     stay on the screen (right >= min(48 dp, its start), left <= max(W -
 *     48 dp, its start)). A rectangle the rules allowed at the start is
 *     always allowed, so the window never jumps;
 *   - a resized window keeps its size when maximised and restored, and
 *     opens again at its own size (w x h; sizes are not saved). Its client
 *     is painted in its own size, centred across the window, from its top
 *     (as a maximised window's).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_widget.h"

#define UI_WINDOWS_MAX   6      /* 1.5 stage 8: 4 -> 6 (C4) */
#define UI_WM_EVENTS     32     /* 1.5 stage 8: 16 -> 32 (a restore logs more) */
#define UI_WM_DCLICK_TICKS 50u  /* 1.5 stage 8: 500 ms at 100 Hz */

/* 1.5 stage 8: a window's title-bar buttons beside close. */
#define UI_WIN_MINIMISE 0x1u
#define UI_WIN_MAXIMISE 0x2u

/* 1.5 stage 9: the edges a point of the grab band lies on (ui_window_edges). */
#define UI_EDGE_LEFT   0x1u
#define UI_EDGE_RIGHT  0x2u
#define UI_EDGE_TOP    0x4u
#define UI_EDGE_BOTTOM 0x8u

struct ui_window {
    /* Set by the window's owner. */
    const char *name;           /* in the log: "About" */
    const char *title;
    int icon;                   /* ui_icon.h */
    int32_t w, h;               /* pixels */
    bool esc_closes;
    uint32_t buttons;           /* 1.5 stage 8: UI_WIN_MINIMISE | UI_WIN_MAXIMISE (0: close only) */
    void (*paint)(const struct ui_canvas *c, struct rect r, void *ctx);     /* the client, inside r */
    void *ctx;
    bool resizable;             /* 1.5 stage 9: the grab band (min_w x min_h the smallest size) */
    int32_t min_w, min_h;
    /* Kept by the window manager. */
    bool open;
    struct rect r;
    enum ui_button_state close;     /* the close button's state as last drawn */
    bool dirty;                 /* its look (focus or a title-bar button) changed since ui_wm_clean */
    /* 1.5 stage 8. */
    bool minimised, maximised;
    struct rect normal_r;       /* the rectangle before maximise */
    enum ui_button_state min_st, max_st;    /* the minimise and maximise buttons' states */
    enum ui_button_state task_st;           /* its taskbar button's state */
};

enum ui_hit {
    UI_HIT_DESKTOP, UI_HIT_TASKBAR, UI_HIT_BODY, UI_HIT_TITLE, UI_HIT_CLOSE,
    UI_HIT_MINIMISE, UI_HIT_MAXIMISE, UI_HIT_TASKBUTTON,    /* 1.5 stage 8 */
    UI_HIT_RESIZE                                           /* 1.5 stage 9: the grab band */
};

enum ui_wm_event_kind {
    UI_EV_OPEN, UI_EV_RAISE, UI_EV_FOCUS, UI_EV_UNFOCUS, UI_EV_DRAG, UI_EV_CLOSE_BUTTON, UI_EV_CLOSE_ESC,
    UI_EV_CLOSE_STATE,
    /* 1.5 stage 8 */
    UI_EV_MINIMISE, UI_EV_RESTORE, UI_EV_MAXIMISE, UI_EV_UNMAXIMISE, UI_EV_CLOSE_MIDDLE, UI_EV_MIN_STATE,
    UI_EV_MAX_STATE, UI_EV_TASK_STATE, UI_EV_SWITCH_SELECT, UI_EV_SWITCH_CHOSEN, UI_EV_SWITCH_CANCEL,
    UI_EV_RESIZE                /* 1.5 stage 9 */
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
    struct ui_window *pressed;  /* the window whose title-bar button (pressed_part) was pressed */
    struct ui_wm_event ev[UI_WM_EVENTS];    /* what the last call changed, in order */
    uint32_t n_ev;
    uint32_t lost_ev;           /* events not kept (the list full) */
    /* 1.5 stage 8. */
    enum ui_hit pressed_part;   /* UI_HIT_CLOSE, UI_HIT_MINIMISE or UI_HIT_MAXIMISE */
    struct ui_window *order[UI_WINDOWS_MAX];    /* the taskbar's order: as they opened (n of them) */
    struct ui_window *mru[UI_WINDOWS_MAX];      /* most recently focused first (n of them) */
    struct ui_window *task_pressed;             /* the window whose taskbar button is pressed */
    int32_t task_x0, task_x1;   /* the taskbar buttons' span (ui_wm_set_taskbar) */
    bool switching;             /* Alt+Tab: a window marked */
    uint32_t switch_i;          /* its place in mru */
    struct ui_window *target;   /* the marked window */
    uint64_t now;               /* the time in ticks (ui_wm_set_now) */
    struct ui_window *click_w;  /* the window whose title bar the last click was on (or none) */
    uint64_t click_at;          /* that click's release */
    int32_t click_x, click_y;   /* that click's press */
    int32_t press_x, press_y;   /* the last left press */
    /* 1.5 stage 9: the window being resized (it has the pointer) and the
     * edges that move; the grab in grab_x, grab_y, grab_r. */
    struct ui_window *resize;
    uint32_t resize_edges;
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

/* ---- 1.5 stage 8 ---- */

/* The target of title-bar button `part` (UI_HIT_CLOSE, UI_HIT_MAXIMISE or
 * UI_HIT_MINIMISE) of w at r; an empty rectangle if w has no such button. */
struct rect ui_window_part_rect(const struct ui_window *w, struct rect r, uint32_t s, enum ui_hit part);

/* The state of title-bar button `part` of w now (as the close button's). */
enum ui_button_state ui_wm_part_state(const struct ui_wm *m, const struct ui_window *w, enum ui_hit part);

/* The taskbar buttons' span: from x0 to at most x1. */
void ui_wm_set_taskbar(struct ui_wm *m, int32_t x0, int32_t x1);

/* Taskbar button k (0 to n - 1, in m->order). */
struct rect ui_wm_task_rect(const struct ui_wm *m, uint32_t k);

/* The taskbar buttons' strip (every button's place, damaged as a whole). */
struct rect ui_wm_task_strip(const struct ui_wm *m);

/* Draws the taskbar buttons (the rules above) with the fonts of m's scale. */
void ui_wm_draw_taskbar(const struct ui_canvas *c, const struct ui_wm *m, const struct ui_fonts *f);

/* The time in ticks, before a pointer call (for the double click). */
void ui_wm_set_now(struct ui_wm *m, uint64_t now);

/* The middle button pressed at (x, y): on a taskbar button, its window
 * closes (UI_EV_CLOSE_MIDDLE). */
void ui_wm_middle(struct ui_wm *m, int32_t x, int32_t y);

/* Restores (if minimised), raises and focuses w (an open window). */
void ui_wm_activate(struct ui_wm *m, struct ui_window *w);

/* Minimise w; maximise or restore w (toggle). */
void ui_wm_minimise(struct ui_wm *m, struct ui_window *w);
void ui_wm_toggle_maximise(struct ui_wm *m, struct ui_window *w);

/* Alt+Tab: one step (back for Shift+Alt+Tab; false with no open window),
 * the commit (Alt released) and cancel (Esc). */
bool ui_wm_switch_step(struct ui_wm *m, bool back);
void ui_wm_switch_commit(struct ui_wm *m);
void ui_wm_switch_cancel(struct ui_wm *m);

/* ---- 1.5 stage 9 ---- */

/* The grab band's width in pixels: 8 dp. */
int32_t ui_window_band(uint32_t s);

/* The edges (UI_EDGE_*) of w at r whose band holds (x, y); 0 outside the
 * band, outside r, or if w is not resizable, maximised or minimised. */
uint32_t ui_window_edges(const struct ui_window *w, struct rect r, uint32_t s, int32_t x, int32_t y);

/* Where a resize of w that started at g puts it after the pointer moved
 * (dx, dy) since the press, moving `edges` (the rules above). */
struct rect ui_wm_resize_rect(const struct ui_wm *m, const struct ui_window *w, struct rect g, uint32_t edges,
                              int64_t dx, int64_t dy);

#endif
