#ifndef MADIRE_DISPLAY_UI_TEXTFIELD_H
#define MADIRE_DISPLAY_UI_TEXTFIELD_H

/*
 * The toolkit's text field and the name rules, the pure part (1.5 stage
 * 10; host unit tests in tests/unit/test_ui_textfield.c; docs/plans/1.5.md,
 * C5 and C6b, the change in S10; the Design Guide, section 13).
 *
 * The field keeps its own edit buffer of UI_TEXTFIELD_MAX (255) bytes
 * (struct lineedit holds 71; the protected lineedit.c is unchanged). Its
 * keys (presses only; releases do nothing):
 *   - a printable ASCII character (0x20 to 0x7E) is inserted at the
 *     cursor; refused when the field is full;
 *   - Backspace and Delete delete left of and at the cursor; Left, Right,
 *     Home and End move it;
 *   - Enter (and keypad Enter) submits, Esc cancels;
 *   - Tab (with or without Shift) is the caller's (UI_TF_PASS);
 *   - every key with Ctrl or Alt held, and every other key, is consumed
 *     by the field (UI_TF_NONE): it never reaches the caller (so Ctrl+A
 *     while renaming never selects all items). The desktop's own keys
 *     (Alt+Tab, Alt+F4, the Super key) are taken before the field.
 * No text selection, no clipboard, no blinking. The bytes are kept as
 * they are; a byte outside printable ASCII is shown as '?'.
 *
 * The rules of the picture (tests/desktop_ref.py draws the same):
 *   - the field in r at scale s: a rounded rectangle of radius ui_px(4, s)
 *     filled with `surface`; its edge ui_px(1, s) wide in `text.muted`,
 *     or, focused, ui_px(2, s) wide in `accent`;
 *   - the text in the regular font, in `text`, from x = r.x + ui_px(8, s)
 *     at y = r.y + floor((r.h - font height) / 2): the bytes from `first`
 *     on, at most ui_textfield_cols() of them;
 *   - focused, the cursor: ui_px(2, s) wide, the font's height, in `text`,
 *     at x = r.x + ui_px(8, s) + (pos - first) x font width, drawn after
 *     the text, clipped to r shrunk by ui_px(2, s).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "drivers/keyboard/scancode.h"
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"

#define UI_TEXTFIELD_MAX 255

struct ui_textfield {
    char buf[UI_TEXTFIELD_MAX + 1];     /* always NUL-terminated */
    uint16_t len;
    uint16_t pos;                       /* the cursor, 0..len */
    uint16_t first;                     /* the first byte shown */
};

enum ui_tf_result {
    UI_TF_NONE,         /* consumed, nothing changed */
    UI_TF_CHANGED,      /* the text or the cursor changed: redraw */
    UI_TF_SUBMIT,       /* Enter */
    UI_TF_CANCEL,       /* Esc */
    UI_TF_REFUSED,      /* a character refused: the field is full */
    UI_TF_PASS,         /* Tab: the caller's key */
};

/* The field holds text (its first UI_TEXTFIELD_MAX bytes), the cursor at
 * its end, shown from its start. */
void ui_textfield_set(struct ui_textfield *t, const char *text);

/* One key (the rules above). */
enum ui_tf_result ui_textfield_key(struct ui_textfield *t, const struct key_event *e);

/* The bytes that fit the field r at scale s in font f (0 or more). */
int32_t ui_textfield_cols(struct rect r, uint32_t s, const struct font *f);

/* Scrolls so the cursor is shown with cols bytes visible: first moves to
 * the cursor if it is before it, or so the cursor is the last column if
 * it is beyond; then, if first > 0 and fewer than cols - 1 bytes follow
 * it, back so that cols - 1 bytes follow (cols below 1 counts as 1). */
void ui_textfield_fit(struct ui_textfield *t, int32_t cols);

/* A click at x in the field r: the cursor to the nearest byte boundary
 * shown (never past the end). */
void ui_textfield_click(struct ui_textfield *t, struct rect r, uint32_t s, const struct font *f, int32_t x);

/* Draws the field (the rules above). */
void ui_textfield_draw(const struct ui_canvas *c, const struct ui_textfield *t, struct rect r, uint32_t s,
                       const struct ui_fonts *f, bool focused);

/* ---- The name rules (docs/plans/1.5.md, C5 "Names"; C6b) ----
 * ui_name_check takes a NUL-terminated name and decides, in this order:
 * empty or only spaces (EMPTY: no message, the field stays open); a '/'
 * (SLASH); "." or ".." (RESERVED); more than 255 bytes (TOO_LONG); a byte
 * outside printable ASCII (NOT_ASCII); already used, if exists (may be
 * null) says so (TAKEN); else OK. */
enum ui_name_check {
    UI_NAME_OK,
    UI_NAME_EMPTY,
    UI_NAME_SLASH,
    UI_NAME_RESERVED,
    UI_NAME_TOO_LONG,
    UI_NAME_NOT_ASCII,
    UI_NAME_TAKEN,
};

typedef bool (*ui_name_exists_fn)(void *ctx, const char *name);

enum ui_name_check ui_name_check(const char *name, ui_name_exists_fn exists, void *ctx);

/* The message shown for a refused name; null for OK and EMPTY. */
const char *ui_name_message(enum ui_name_check c);

/* The first default name not used (exists may be null): "New Folder",
 * "New Folder 2" ... "New Folder 1000", or "New File.txt", "New File
 * 2.txt" ... "New File 1000.txt", written to out (size at least 32).
 * False (out empty) if every one is used or out is too small. */
bool ui_name_default(bool folder, ui_name_exists_fn exists, void *ctx, char *out, size_t size);

/* name as shown: each byte outside printable ASCII as '?', at most
 * size - 1 bytes, NUL-terminated (size at least 1). */
void ui_name_shown(const char *name, char *out, size_t size);

#endif
