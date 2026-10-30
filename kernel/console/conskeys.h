#ifndef MADIRE_CONSOLE_CONSKEYS_H
#define MADIRE_CONSOLE_CONSKEYS_H

/*
 * The console keys (1.3 stage 7b, decision 16): Ctrl+L, Ctrl+A, Ctrl+E,
 * Ctrl+K, Ctrl+U and Ctrl+W at the prompt.
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_conskeys13.c). The console (console.c) asks
 * which key a key event is, what to do with it in its present state, and
 * has the edit done here on the public struct lineedit; the line editor
 * itself (lineedit.c) is unchanged and still ignores every key with Ctrl.
 *
 * Keys (presses only, Ctrl held, Alt not held; Shift and Caps Lock do not
 * matter):
 *   Ctrl+L  clears the screen and keeps the typed line and its cursor
 *           (the console does it, as "clear": no edit here)
 *   Ctrl+A  the cursor to the start of the line
 *   Ctrl+E  the cursor to the end of the line
 *   Ctrl+K  deletes from the cursor to the end of the line
 *   Ctrl+U  deletes from the start of the line to the cursor
 *   Ctrl+W  deletes the spaces before the cursor, then the word before
 *           them (a word ends at a space)
 * While a program runs (also sleep, presskey, cat and cp) and at a yes/no
 * question they are ignored; scrolled back, the key first returns to the
 * live view (as every key does), then acts, or, at a question, does
 * nothing more. No Ctrl+Y; Ctrl+D stays deferred.
 */

#include <stdbool.h>
#include "kernel/console/lineedit.h"

enum conskey {
    CONSKEY_NONE,       /* not one of these keys */
    CONSKEY_CLEAR,      /* Ctrl+L */
    CONSKEY_START,      /* Ctrl+A */
    CONSKEY_END,        /* Ctrl+E */
    CONSKEY_KILL_END,   /* Ctrl+K */
    CONSKEY_KILL_START, /* Ctrl+U */
    CONSKEY_WORD,       /* Ctrl+W */
};

enum conskeys_do {
    CONSKEYS_IGNORE,        /* nothing at all */
    CONSKEYS_LEAVE,         /* back to the live view only (the key ignored) */
    CONSKEYS_ACT,           /* the key acts */
    CONSKEYS_LEAVE_ACT,     /* back to the live view, then the key acts */
};

/* Which console key e is. */
enum conskey conskeys_classify(const struct key_event *e);

/* What to do with key k: scrolled (the scrollback shown), asking (a yes/no
 * question waits for its answer), busy (a program, sleep, presskey, cat or
 * cp runs). */
enum conskeys_do conskeys_route(enum conskey k, bool scrolled, bool asking, bool busy);

/* The edit of key k on le (none for CONSKEY_CLEAR and CONSKEY_NONE);
 * true if the text or the cursor changed. */
bool conskeys_edit(struct lineedit *le, enum conskey k);

#endif
