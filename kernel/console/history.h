#ifndef MADIRE_CONSOLE_HISTORY_H
#define MADIRE_CONSOLE_HISTORY_H

/*
 * Command history (0.9.1): the last HISTORY_MAX submitted lines, numbered
 * from 1 since boot, and the Up/Down browsing state of the console.
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_history.c). The console adds each submitted
 * line and asks it for older or newer lines when Up or Down is pressed; the
 * line editor itself (lineedit.c) is unchanged and still ignores those keys.
 *
 * Rules:
 *   - empty lines and lines of only spaces and tabs are not kept;
 *   - every other line is kept as submitted, repeats included, at most
 *     HISTORY_LINE_MAX characters (the line editor's limit);
 *   - when full, the oldest line is dropped; numbers keep counting.
 * Browsing: Up starts at the newest line and goes back to the oldest (and
 * stays there); Down goes forward again, and past the newest line returns
 * an empty line and ends browsing. Adding a line ends browsing.
 */

#include <stdbool.h>
#include <stdint.h>

#define HISTORY_MAX       16
#define HISTORY_LINE_MAX  71    /* = LINE_MAX in lineedit.h */

struct history {
    char lines[HISTORY_MAX][HISTORY_LINE_MAX + 1];  /* slot = (number - 1) % MAX */
    uint32_t count;         /* lines added since boot = number of the newest */
    uint32_t browse;        /* number shown while browsing; 0 = not browsing */
};

void history_init(struct history *h);

/* Adds a submitted line. Returns false (nothing changes, browsing still
 * ends) for an empty or blank line. A longer line is cut at
 * HISTORY_LINE_MAX characters. */
bool history_add(struct history *h, const char *line);

/* Number of lines kept (0..HISTORY_MAX). */
uint32_t history_size(const struct history *h);

/* Number of the oldest line kept (count - size + 1), or 0 when empty. */
uint32_t history_first(const struct history *h);

/* The line with this number, or NULL if it is not (or no longer) kept. */
const char *history_get(const struct history *h, uint32_t number);

/* Up: the next older line, or NULL (nothing to show) when there is no line
 * or browsing is already at the oldest. */
const char *history_up(struct history *h);

/* Down: the next newer line; "" after the newest (browsing ends); NULL when
 * not browsing. */
const char *history_down(struct history *h);

/* Ends browsing (the edited line is then a new line). */
void history_end_browse(struct history *h);

#endif
