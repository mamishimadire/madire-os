#ifndef MADIRE_CONSOLE_TEXTBUF_H
#define MADIRE_CONSOLE_TEXTBUF_H

/*
 * The console's own 80x25 character grid (character + VGA attribute per
 * cell) with a cursor, line wrapping and scrolling.
 *
 * Pure module: no hardware access, printing or panics, so it is unit-tested
 * on the host (tests/unit/test_textbuf.c). console.c copies it to the VGA
 * text buffer.
 */

#include <stdint.h>

#define TB_COLS 80
#define TB_ROWS 25
#define TB_TAB_WIDTH 4

struct textbuf {
    uint16_t cells[TB_ROWS][TB_COLS];   /* character | attribute << 8 */
    uint8_t row, col;                   /* col == TB_COLS: wrap pending */
    uint8_t attr;                       /* attribute for new characters */
    uint32_t scrolls;                   /* lines scrolled off the top so far */
};

void textbuf_init(struct textbuf *t, uint8_t attr);

/* Blank the whole grid (current attribute), cursor to the top left. */
void textbuf_clear(struct textbuf *t);

/*
 * Writes one character at the cursor:
 *   '\n'  next line (scrolls at the bottom)
 *   '\b'  move left one column and blank it (not past column 0)
 *   '\t'  TB_TAB_WIDTH spaces
 *   other non-printable characters are shown as '?'
 * A character written in the last column leaves the cursor "wrap pending":
 * the next printable character starts a new line, so an 80-character line
 * followed by '\n' does not leave an empty line.
 */
void textbuf_putc(struct textbuf *t, char c);
void textbuf_puts(struct textbuf *t, const char *s);

void textbuf_set_attr(struct textbuf *t, uint8_t attr);

/* Moves the cursor (clamped to the grid). */
void textbuf_set_cursor(struct textbuf *t, uint8_t row, uint8_t col);

/* Writes a cell directly, without moving the cursor (ignored if outside). */
void textbuf_put_at(struct textbuf *t, uint8_t row, uint8_t col, char c, uint8_t attr);

char textbuf_char_at(const struct textbuf *t, uint8_t row, uint8_t col);
uint8_t textbuf_attr_at(const struct textbuf *t, uint8_t row, uint8_t col);

#endif
