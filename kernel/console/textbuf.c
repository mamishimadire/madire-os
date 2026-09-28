/* Console character grid (see textbuf.h). */
#include "kernel/console/textbuf.h"

static uint16_t cell(char c, uint8_t attr)
{
    return (uint16_t)((uint8_t)c | (uint16_t)attr << 8);
}

static void blank_row(struct textbuf *t, uint8_t row)
{
    for (uint8_t c = 0; c < TB_COLS; c++)
        t->cells[row][c] = cell(' ', t->attr);
}

static void newline(struct textbuf *t)
{
    t->col = 0;
    if (t->row + 1 < TB_ROWS) {
        t->row++;
        return;
    }
    for (uint8_t r = 1; r < TB_ROWS; r++)
        for (uint8_t c = 0; c < TB_COLS; c++)
            t->cells[r - 1][c] = t->cells[r][c];
    blank_row(t, TB_ROWS - 1);
    t->scrolls++;
}

void textbuf_init(struct textbuf *t, uint8_t attr)
{
    t->attr = attr;
    t->scrolls = 0;
    textbuf_clear(t);
}

void textbuf_clear(struct textbuf *t)
{
    for (uint8_t r = 0; r < TB_ROWS; r++)
        blank_row(t, r);
    t->row = 0;
    t->col = 0;
}

static void put_printable(struct textbuf *t, char c)
{
    if (t->col >= TB_COLS)
        newline(t);                 /* pending wrap */
    t->cells[t->row][t->col] = cell(c, t->attr);
    t->col++;
}

void textbuf_putc(struct textbuf *t, char c)
{
    switch (c) {
    case '\n':
        newline(t);
        return;
    case '\b':
        if (t->col >= TB_COLS)
            t->col = TB_COLS - 1;
        else if (t->col > 0)
            t->col--;
        t->cells[t->row][t->col] = cell(' ', t->attr);
        return;
    case '\t':
        for (int i = 0; i < TB_TAB_WIDTH; i++)
            put_printable(t, ' ');
        return;
    default:
        put_printable(t, (c >= 0x20 && c <= 0x7E) ? c : '?');
        return;
    }
}

void textbuf_puts(struct textbuf *t, const char *s)
{
    while (*s)
        textbuf_putc(t, *s++);
}

void textbuf_set_attr(struct textbuf *t, uint8_t attr)
{
    t->attr = attr;
}

void textbuf_set_cursor(struct textbuf *t, uint8_t row, uint8_t col)
{
    t->row = row < TB_ROWS ? row : TB_ROWS - 1;
    t->col = col < TB_COLS ? col : TB_COLS - 1;
}

void textbuf_put_at(struct textbuf *t, uint8_t row, uint8_t col, char c, uint8_t attr)
{
    if (row < TB_ROWS && col < TB_COLS)
        t->cells[row][col] = cell(c, attr);
}

char textbuf_char_at(const struct textbuf *t, uint8_t row, uint8_t col)
{
    return (row < TB_ROWS && col < TB_COLS) ? (char)(t->cells[row][col] & 0xFF) : 0;
}

uint8_t textbuf_attr_at(const struct textbuf *t, uint8_t row, uint8_t col)
{
    return (row < TB_ROWS && col < TB_COLS) ? (uint8_t)(t->cells[row][col] >> 8) : 0;
}
