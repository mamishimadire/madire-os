#ifndef MADIRE_DISPLAY_UI_LIST_H
#define MADIRE_DISPLAY_UI_LIST_H

/*
 * The toolkit's list and grid views and the scroll bar, the pure part
 * (1.5 stage 10; host unit tests in tests/unit/test_ui_list.c;
 * docs/plans/1.5.md, C5; the Design Guide, section 13).
 *
 * The items belong to the caller (Files allocates them when its window
 * opens, S12); each holds its icon, its texts and its selected flag. The
 * view keeps the first line shown (top), the focused item (cursor), the
 * start of a Shift range (anchor), whether the keyboard moved the focus
 * (ring) and the scroll bar's state. Sorting is the caller's.
 *
 * Input (the caller routes it):
 *   - a click on an item selects it alone; Ctrl+click toggles it; Shift+
 *     click selects the range from the anchor to it; a plain click on no
 *     item clears the selection; the focus moves to the item clicked, the
 *     ring goes off;
 *   - a double click (the second press within 500 ms of the first, within
 *     4 dp, on the same item) opens it;
 *   - keys (presses; none with Ctrl or Alt): Up, Down, Home, End, Page Up,
 *     Page Down (and Left, Right in the grid) move the focus and select the
 *     focused item alone, or with Shift the range from the anchor; Enter
 *     opens; the ring comes on and the focused item is scrolled into view;
 *   - the wheel scrolls 3 rows (list) or one line of cells (grid) a notch.
 *
 * The rules of the picture (tests/desktop_ref.py draws the same), in r at
 * scale s, d(n) = ui_px(n, s), the regular font f (width fw, height fh):
 *   - r is filled with `surface.window`; the scroll bar's column is the
 *     d(24) at r's right, the body the rest (below the header in the
 *     list);
 *   - the list: a header d(28) high with the four titles in `text.muted`
 *     at y = r.y + (d(28) - fh) / 2: the first at r.x + d(36), the second
 *     right-aligned d(8) before the Size column's right end, the third and
 *     fourth d(8) into the Type and Modified columns; a d(1) divider in
 *     `border` along the header's bottom across the body's width; the
 *     columns from the body's right: Modified d(144), Type d(112), Size
 *     d(80), Name the rest; rows d(28) high from the header down, clipped
 *     to the body;
 *   - a row: if selected, filled with `surface.active` and marked by a
 *     d(2) `accent` bar along its left edge, d(4) in from its top and
 *     bottom; the focused row with the ring on: a d(2) `accent` frame of
 *     radius d(4) from d(2) after the row's left end to d(2) before its
 *     right end, the row's height; its icon (16 dp) in `text` at r.x +
 *     d(12), centred vertically; the name (each byte outside printable
 *     ASCII as '?') in `text` at r.x + d(36), the other three in
 *     `text.muted` (Size right-aligned d(8) before its column's end, the
 *     others d(8) into theirs), each cut with "..." to its room (the
 *     name's: the Name column less d(44); the others': their column less
 *     d(16)), all at y = row.y + (d(28) - fh) / 2;
 *   - the grid: cells d(96) x d(88) from the body's top left, as many per
 *     line as fit the body (at least one); a selected cell: the cell
 *     shrunk by d(2) filled as a rounded rectangle of radius d(4) with
 *     `surface.active`, and a d(2) `accent` bar d(4) above that shape's
 *     bottom, d(4) in from its sides; the focused cell with the ring on: a
 *     d(2) `accent` frame of radius d(4) on that shape; the icon (48 dp)
 *     in `text`, centred across, d(8) from the cell's top; the name in
 *     `text`, cut with "..." to the cell's width less 4 x d(2) (1.5 stage
 *     11, the owner's refinement: clear of the focus ring at every scale),
 *     centred across, d(64) from the cell's top;
 *   - the scroll bar in its column (sb): when the lines are more than the
 *     page, a thumb d(8) wide, centred across sb, a rounded rectangle of
 *     radius d(4) in `text.muted` (`text` on hover and while dragged). Its
 *     track is sb less d(4) at the top and the bottom, length L; the thumb
 *     is max(d(24), floor(L x page / lines)) long (at most L), at
 *     floor((L - T) x top / (lines - page)) down the track.
 * The page is the full rows (lines of cells) the body holds.
 * 1.5 stage 13 (the Trash view's columns, Q4): a list with `wide` set has
 * three columns: no Size column (its title and texts not drawn) and the
 * third column (the item's type text: the Trash's original location) from
 * the Size column's left end, d(8) into it, its room d(192) less d(16);
 * every other rule as above. ui_list_init leaves it off.
 */

#include <stdbool.h>
#include <stdint.h>
#include "drivers/keyboard/scancode.h"
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"

/* ---- The scroll bar ---- */

enum ui_sb_state { UI_SB_NORMAL, UI_SB_HOVER, UI_SB_DRAG };
enum ui_sb_hit { UI_SB_HIT_NONE, UI_SB_HIT_THUMB, UI_SB_HIT_ABOVE, UI_SB_HIT_BELOW };

/* The thumb of a scroll bar in sb for `lines` lines, `page` shown from
 * line `top` (clamped); false (no thumb) if they all fit. */
bool ui_scrollbar_thumb(struct rect sb, uint32_t lines, uint32_t page, uint32_t top, uint32_t s, struct rect *thumb);

/* Where (x, y) lies: on the thumb, above or below it in sb's column, or
 * outside sb (or no thumb). */
enum ui_sb_hit ui_scrollbar_hit(struct rect sb, uint32_t lines, uint32_t page, uint32_t top, uint32_t s, int32_t x,
                                int32_t y);

/* The top line for a thumb dragged so its top edge is at y (clamped to
 * the track; rounded to the nearest line). */
uint32_t ui_scrollbar_drag(struct rect sb, uint32_t lines, uint32_t page, uint32_t s, int32_t y);

/* Draws the scroll bar's thumb (the rules above), if any. */
void ui_scrollbar_draw(const struct ui_canvas *c, struct rect sb, uint32_t lines, uint32_t page, uint32_t top,
                       enum ui_sb_state st, uint32_t s);

/* ---- The list and the grid ---- */

enum ui_list_view { UI_LIST_DETAILS, UI_LIST_GRID };

struct ui_list_item {
    const char *name;
    const char *size, *type, *modified;     /* the list's other columns; may be null */
    int icon;                               /* ui_icon.h, or UI_ICON_NONE */
    bool selected;
};

struct ui_list {
    enum ui_list_view view;
    struct ui_list_item *items;
    uint32_t n;
    const char *const *titles;              /* the list's four column titles */
    struct rect r;
    uint32_t scale;
    uint32_t top;                           /* the first line shown */
    uint32_t cursor;                        /* the focused item */
    uint32_t anchor;                        /* a Shift range's start */
    bool ring;                              /* the keyboard moved the focus */
    enum ui_sb_state sb;
    bool wide;                              /* 1.5 stage 13: three columns (the Trash view) */
};

enum ui_list_result { UI_LIST_NONE, UI_LIST_MOVED, UI_LIST_OPEN };

struct ui_list_clicks {                     /* the double click's memory */
    uint64_t ms;
    int32_t x, y, item;
    bool armed;
};

/* A view of n items in r at scale s, nothing scrolled, the focus on the
 * first item, the ring off. */
void ui_list_init(struct ui_list *l, enum ui_list_view v, struct ui_list_item *items, uint32_t n,
                  const char *const *titles, struct rect r, uint32_t s);

uint32_t ui_list_columns(const struct ui_list *l);      /* items per line (1 in the list) */
uint32_t ui_list_lines(const struct ui_list *l);        /* lines of items */
uint32_t ui_list_page(const struct ui_list *l);         /* full lines the body holds */
uint32_t ui_list_max_top(const struct ui_list *l);
struct rect ui_list_body(const struct ui_list *l);
struct rect ui_list_sb_rect(const struct ui_list *l);
struct rect ui_list_item_rect(const struct ui_list *l, uint32_t i);

/* The item at (x, y) in the body, or -1. */
int32_t ui_list_item_at(const struct ui_list *l, int32_t x, int32_t y);

/* Scrolls by `lines` (down if positive), within 0 and the last top. */
void ui_list_scroll(struct ui_list *l, int32_t lines);

/* The wheel: `notches` down (positive) or up. */
void ui_list_wheel(struct ui_list *l, int32_t notches);

/* Scrolls the focused item into view. */
void ui_list_reveal(struct ui_list *l);

/* A click on item i (-1: no item) with the modifiers mods (KEY_SHIFT,
 * KEY_CTRL); true if the selection or the focus changed. */
bool ui_list_click(struct ui_list *l, int32_t i, uint8_t mods);

/* Every item selected. */
void ui_list_select_all(struct ui_list *l);

/* One key (the rules above). */
enum ui_list_result ui_list_key(struct ui_list *l, const struct key_event *e);

/* A press at (x, y) on item `item` at time ms: true if it completes a
 * double click (the memory is then cleared), else it is remembered. */
bool ui_list_double(struct ui_list_clicks *k, uint64_t ms, int32_t x, int32_t y, int32_t item, uint32_t s);

/* Draws the view (the rules above). */
void ui_list_draw(const struct ui_canvas *c, const struct ui_list *l, const struct ui_fonts *f);

#endif
