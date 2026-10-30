#ifndef MADIRE_DISPLAY_UI_MENU_H
#define MADIRE_DISPLAY_UI_MENU_H

/*
 * Menus, the pure part (1.3 stage 5; the Design Guide, sections 4, 5 and
 * 9; host unit tests in tests/unit/test_ui_menu.c). tests/desktop_ref.py
 * draws the same pictures from these rules.
 *
 * A menu is a panel at r, at scale s. Places in dp are made pixels as
 * ui_px(dp, s) from the panel's top-left corner: "at a dp" down is
 * r.y + ui_px(a, s); "from a to b dp" down is [r.y + ui_px(a, s),
 * r.y + ui_px(b, s)); likewise across from r.x. Two menus:
 *   - the M menu: 288 x 182 dp. Its header from 4 to 52 dp (hh = ui_px(52)
 *     - ui_px(4) pixels): the M (24 dp, ui_icon.h's, in `accent`) at 16 dp
 *     across, at r.y + ui_px(4) + (hh - ui_px(24)) / 2; the wordmark
 *     "MADIRE OS" in `text` (wallpaper_core.h's wp_line_draw, the splash's
 *     capsule letters) with a cap height of 10 dp: ts = fx(10 s, 56)
 *     pixels per unit (14 units), radius fx(500 s, 2576) for MADIRE and
 *     fx(250 s, 2576) for OS (the About window's weights at 10/16),
 *     tracking 6 units, pen = (r.x + ui_px(52)) x 256 (12 dp after the M),
 *     its cap top at (r.y + ui_px(4) + (hh - ui_px(10)) / 2) x 256. Its
 *     dividers at 56 and 137 dp; its rows About, Console and Restart from
 *     61, 97 and 142 dp;
 *   - the desktop menu (the right-click menu): 224 x 80 dp, no header, no
 *     divider; its rows "Open Console" and "About Madire OS" from 4 and
 *     40 dp.
 * The panel: r filled in `surface` as a rounded rectangle of radius 6 dp
 * (all corners); last, its 1 dp edge (e = ui_px(1) pixels) in
 * `border.panel` as a frame of the same radius. A divider at d dp: in
 * `border`, from d to d + 1 dp down, from 16 to (width - 16) dp across. A
 * row from t dp: across [r.x + e, r.x + r.w - e), down from t to t + 36
 * dp; its icon (16 dp, in `text`) at 16 dp across, at row.y + (row.h -
 * ui_px(16)) / 2; its label in the console's (regular) font in `text` at
 * 44 dp across (12 dp after the icon), at row.y + (row.h - font height) /
 * 2. The selected row (hovered, or chosen by the keyboard), before its
 * icon and label: its rectangle filled in `surface.active`, only inside
 * the panel's inner rounded shape (r shrunk by e, radius 6 dp - e); the
 * `accent` bar across [row.x, row.x + ui_px(2)), down [row.y + ui_px(4),
 * row.y + row.h - ui_px(4)); with the focus ring, an `accent` frame
 * ui_px(2) wide of radius 4 dp (all corners) around the rectangle across
 * [row.x + ui_px(2), row.x + row.w - ui_px(2)), down the row's: 2 dp
 * inside the panel's edge, beside the bar.
 *
 * Placement: the M menu with its left edge 8 dp from the screen's and its
 * bottom 8 dp above the taskbar (y = bar - ui_px(8) - height, at least 0);
 * the desktop menu with its top-left corner at the pointer's hot spot,
 * moved left to end at the screen's right edge and up to end at the
 * taskbar's top where it would cross them, and never past 0.
 *
 * The keyboard: Down selects the next row (the first when none is
 * selected), Up the one before (the last when none is); both wrap around,
 * and turn the focus ring on.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"

#define UI_MENU_ROWS_MAX 3

enum ui_menu_kind { UI_MENU_M, UI_MENU_DESKTOP };
enum ui_menu_action { UI_MENU_NONE, UI_MENU_ABOUT, UI_MENU_CONSOLE, UI_MENU_RESTART };

/* The M button's states (the Design Guide, section 10; desktop_core.h draws them). */
enum ui_m_state { UI_M_NORMAL, UI_M_HOVER, UI_M_PRESSED, UI_M_OPEN };

struct ui_menu {
    enum ui_menu_kind kind;
    uint32_t scale;
    struct rect r;
    int sel;                    /* the selected row, -1 for none */
    bool ring;                  /* the focus ring on the selected row */
};

/* The menu's rows, and each row's label, icon and action. */
int ui_menu_rows(enum ui_menu_kind k);
const char *ui_menu_label(enum ui_menu_kind k, int row);
int ui_menu_icon(enum ui_menu_kind k, int row);
enum ui_menu_action ui_menu_action(enum ui_menu_kind k, int row);

/* The menu's size at scale s. */
void ui_menu_size(enum ui_menu_kind k, uint32_t s, int32_t *w, int32_t *h);

/* Where the M menu opens, the taskbar's top at bar_y. */
struct rect ui_menu_place_m(uint32_t s, int32_t bar_y);

/* Where the desktop menu opens for the hot spot (x, y) on a screen
 * `width` wide, the taskbar's top at bar_y. */
struct rect ui_menu_place_at(uint32_t s, int32_t width, int32_t bar_y, int32_t x, int32_t y);

/* A menu of kind k at r, no row selected, the ring off. */
void ui_menu_init(struct ui_menu *m, enum ui_menu_kind k, uint32_t s, struct rect r);

/* Row `row`'s rectangle (empty for no such row). */
struct rect ui_menu_row_rect(const struct ui_menu *m, int row);

/* The row at (x, y), or -1. */
int ui_menu_row_at(const struct ui_menu *m, int32_t x, int32_t y);

/* The row the keyboard selects from the selection now: dir > 0 Down,
 * else Up (the rules above). */
int ui_menu_next(const struct ui_menu *m, int dir);

/* The 16-bit entries the M menu's wordmark needs at scale s on a screen of
 * width x height. */
size_t ui_menu_scratch(uint32_t s, uint32_t width, uint32_t height);

/* Draws the menu (the rules above) with fonts f; the wordmark's coverage
 * in scratch. False if the wordmark was not drawn whole (scratch too small). */
bool ui_menu_draw(const struct ui_canvas *c, const struct ui_menu *m, const struct ui_fonts *f, uint16_t *scratch,
                  size_t entries);

#endif
