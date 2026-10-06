#ifndef MADIRE_CONSOLE_SCROLLBACK_H
#define MADIRE_CONSOLE_SCROLLBACK_H

/*
 * Console scrollback (0.9.2): the last SB_LINES lines that scrolled off the
 * top of the console (or were on screen at a "clear"), each with its
 * colours, and the view while the user pages back through them.
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_scrollback.c). console.c pushes each line
 * that leaves the 80x25 grid (textbuf.c is unchanged) and draws the view.
 *
 * Lines are numbered from the oldest kept line: the kept lines first, then
 * the TB_ROWS rows of the live screen. While scrolled back, screen rows
 * 0..SB_VIEW_ROWS-1 show lines top .. top+SB_VIEW_ROWS-1 of that list and
 * the last row shows the indicator:
 *   "-- scrolled back: line X of Y (Shift+PgUp/PgDn; any key returns) --"
 * with X = top + 1 and Y = kept lines + TB_ROWS (at most 1025, so it
 * always fits 79 columns).
 *
 * Keys: Shift+PgUp moves back SB_VIEW_ROWS lines (from the bottom: to the
 * SB_VIEW_ROWS lines just above the screen), stopping at line 1;
 * Shift+PgDn moves forward and returns to the bottom (the live screen) when
 * it reaches it. Any other key press returns to the bottom; key releases
 * and modifier keys on their own (Shift, Ctrl, Alt, GUI, the locks) don't.
 */

#include <stdbool.h>
#include <stdint.h>
#include "drivers/keyboard/scancode.h"
#include "kernel/console/textbuf.h"

#define SB_LINES          1000
#define SB_VIEW_ROWS      (TB_ROWS - 1)     /* 24: the last row is the indicator */
#define SB_INDICATOR_ATTR 0x70              /* black on light grey (reverse) */
#define SB_TEXT_MAX       40                /* "top" text in the serial log */

struct scrollback {
    uint16_t lines[SB_LINES][TB_COLS];      /* cells: character | attribute << 8 */
    uint32_t head;                          /* next slot to write */
    uint32_t count;                         /* lines kept, at most SB_LINES */
    uint32_t top;                           /* first line shown while scrolled back */
    bool scrolled;                          /* false: the live screen is shown */
};

void sb_init(struct scrollback *s);

/* Keeps one row (TB_COLS cells). When full, the oldest line is dropped; a
 * scrolled-back view keeps showing the same lines while it can. */
void sb_push(struct scrollback *s, const uint16_t row[TB_COLS]);

uint32_t sb_count(const struct scrollback *s);

/* Kept line i (0 = the oldest), or NULL if there is no such line. */
const uint16_t *sb_line(const struct scrollback *s, uint32_t i);

bool sb_scrolled(const struct scrollback *s);

enum sb_move {
    SB_MOVED,           /* the view changed */
    SB_UNCHANGED,       /* already at line 1 (up) or not scrolled back (down) */
    SB_EMPTY,           /* nothing kept yet: there is nothing to scroll back to */
};

enum sb_move sb_page_up(struct scrollback *s);
enum sb_move sb_page_down(struct scrollback *s);

/* Back to the live screen. True if the view was scrolled back. */
bool sb_to_bottom(struct scrollback *s);

/* While scrolled back: where screen row r (0..SB_VIEW_ROWS-1) comes from,
 * a kept line (from_screen false) or a row of the live screen. */
struct sb_source {
    bool from_screen;
    uint32_t index;
};
struct sb_source sb_view_row(const struct scrollback *s, uint32_t r);

/* X (the first line shown, 1-based) and Y (all lines: kept + TB_ROWS). */
uint32_t sb_first_shown(const struct scrollback *s);
uint32_t sb_total(const struct scrollback *s);

/* The indicator text (NUL-terminated, at most TB_COLS - 1 characters).
 * Returns its length. */
uint32_t sb_indicator(const struct scrollback *s, char out[TB_COLS + 1]);

/* The characters of a row of cells, trailing spaces removed, at most `max`
 * characters (out needs max + 1 bytes). Returns the length. */
uint32_t sb_row_text(const uint16_t row[TB_COLS], char *out, uint32_t max);

/* The last row of a grid that holds anything but spaces, or -1 if all are
 * blank ("clear" keeps rows 0..that row). */
int32_t sb_last_used_row(const uint16_t cells[TB_ROWS][TB_COLS]);

enum sb_key {
    SB_KEY_NONE,        /* release or a modifier on its own: no effect */
    SB_KEY_PAGE_UP,     /* Shift+PgUp (no Ctrl or Alt) */
    SB_KEY_PAGE_DOWN,   /* Shift+PgDn (no Ctrl or Alt) */
    SB_KEY_OTHER,       /* any other key press: returns to the bottom */
};

enum sb_key sb_classify_key(const struct key_event *e);

#endif
