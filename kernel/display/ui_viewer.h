#ifndef MADIRE_DISPLAY_UI_VIEWER_H
#define MADIRE_DISPLAY_UI_VIEWER_H

/*
 * The Text Viewer's lines and picture, the pure part (1.5 stage 12;
 * docs/plans/1.5.md, C5 and C6b; host unit tests in
 * tests/unit/test_ui_files.c; the kernel side is files_app.c). Read only.
 *
 * The bytes are never interpreted (the owner's security rule, S12): a
 * printable ASCII byte (0x20 to 0x7E) is drawn as itself; a newline (0x0A)
 * ends a line; a tab (0x09) moves to the next multiple of 8 columns; EVERY
 * other byte (NUL, BEL, BS, CR, ESC, DEL, 0x80-0xFF ...) takes one column
 * and is drawn as '?' (ui_viewer_shown), with no other effect.
 *
 * The lines (ui_viewer_next): a line starts at an offset and takes bytes
 * until a newline (consumed, the line ends) or until the next byte would
 * not fit in `cols` columns (it starts the next line; a tab at column 0
 * wider than the line takes the rest of it); a line filled exactly is
 * followed by its newline, if the next byte is one (consumed, so a full
 * line does not leave an empty one). The lines run until the buffer's
 * end; an empty buffer has none, and a final newline adds none.
 *
 * The picture in the window's rectangle r at scale s, d(n) = ui_px(n, s),
 * the window's edge E = d(1), its title bar d(32), the inner span x0 =
 * r.x + E, width iw = r.w - 2 E; the regular font f (width fw, height fh):
 *   - when the file was cut (only its first 64 KiB read): a bar (x0, r.y +
 *     d(32), iw, d(28)) filled `surface`, "Only the first 64 KiB are
 *     shown." in `text` at x0 + d(12), centred down, a d(1) `border` line
 *     along its bottom;
 *   - the text area below it to r.y + r.h - E, less the scroll bar's
 *     column (d(24) at its right, ui_list.h's scroll bar: the lines, the
 *     page, the top line): the lines from line `top`, each in `text` from
 *     x0 + d(8), the first at the area's top + d(8), fh apart; cols =
 *     (area width - 2 d(8)) / fw (at least 1), the page = (area height -
 *     2 d(8)) / fh full lines.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_list.h"

#define UI_VIEWER_W_DP     640
#define UI_VIEWER_H_DP     480
#define UI_VIEWER_MIN_W_DP 320
#define UI_VIEWER_MIN_H_DP 240
#define UI_VIEWER_MAX      65536u          /* bytes read: the first 64 KiB */
#define UI_VIEWER_CUT_TEXT "Only the first 64 KiB are shown."

struct ui_viewer {
    const uint8_t *buf;
    uint32_t len;
    bool cut;                               /* the file is longer than UI_VIEWER_MAX */
    uint32_t top;                           /* the first line shown */
    enum ui_sb_state sb;
};

/* The character a byte is drawn as: itself if printable ASCII, else '?'
 * (a tab and a newline are not drawn). */
char ui_viewer_shown(uint8_t b);

/* The line from offset off (< len) for cols columns: its end in *next
 * (after a consumed newline), the bytes drawn (without a newline) as the
 * return value. */
uint32_t ui_viewer_next(const uint8_t *buf, uint32_t len, uint32_t off, uint32_t cols, uint32_t *next);

/* The number of lines; the offset where line n starts (len if none). */
uint32_t ui_viewer_lines(const uint8_t *buf, uint32_t len, uint32_t cols);
uint32_t ui_viewer_line_start(const uint8_t *buf, uint32_t len, uint32_t cols, uint32_t n);

/* The text area, its scroll bar's column, its columns and its page. */
struct rect ui_viewer_area(const struct ui_viewer *v, struct rect r, uint32_t s);
struct rect ui_viewer_sb(const struct ui_viewer *v, struct rect r, uint32_t s);
uint32_t ui_viewer_cols(const struct ui_viewer *v, struct rect r, uint32_t s, const struct font *f);
uint32_t ui_viewer_page(const struct ui_viewer *v, struct rect r, uint32_t s, const struct font *f);

/* Scrolls by lines (down if positive) within 0 and lines - page. */
void ui_viewer_scroll(struct ui_viewer *v, struct rect r, uint32_t s, const struct font *f, int32_t lines);

/* Draws the client of the window at r (the rules above). */
void ui_viewer_draw(const struct ui_canvas *c, const struct ui_viewer *v, struct rect r, uint32_t s,
                    const struct ui_fonts *f);

#endif
