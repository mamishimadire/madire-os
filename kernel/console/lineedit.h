#ifndef MADIRE_CONSOLE_LINEEDIT_H
#define MADIRE_CONSOLE_LINEEDIT_H

/*
 * Single-line input editor for the console prompt.
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_lineedit.c). It consumes the key events the
 * 0.4 keyboard driver already produces (characters already include Shift
 * and Caps Lock).
 *
 * Keys (presses only; releases are ignored):
 *   printable characters, Space, Tab  insert at the cursor
 *   Backspace / Delete                 delete left of / at the cursor
 *   Left / Right / Home / End          move the cursor
 *   Enter / keypad Enter               submit the line
 * Any key with Ctrl or Alt held is ignored. No history.
 */

#include <stdint.h>
#include "drivers/keyboard/scancode.h"

#define LINE_MAX 71     /* 8-character prompt + 71 = 79 columns: one row */

enum le_result {
    LE_NONE,        /* key had no effect */
    LE_CHANGED,     /* text or cursor changed: redraw the input line */
    LE_SUBMIT,      /* Enter: buf holds the finished line */
    LE_REFUSED,     /* printable key refused: line is full */
};

struct lineedit {
    char buf[LINE_MAX + 1];     /* always NUL-terminated */
    uint8_t len;
    uint8_t pos;                /* cursor, 0..len */
};

void lineedit_reset(struct lineedit *le);
enum le_result lineedit_key(struct lineedit *le, const struct key_event *e);

#endif
