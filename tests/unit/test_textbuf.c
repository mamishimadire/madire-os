/* Unit tests for kernel/console/textbuf.c (console character grid). */
#include <stdint.h>
#include <string.h>
#include "kernel/console/textbuf.h"
#include "tests/unit/unit.h"

static struct textbuf tb;

/* Row text with trailing spaces removed. */
static const char *row_text(uint8_t row)
{
    static char s[TB_COLS + 1];
    for (uint8_t c = 0; c < TB_COLS; c++)
        s[c] = textbuf_char_at(&tb, row, c);
    int n = TB_COLS;
    while (n > 0 && s[n - 1] == ' ')
        n--;
    s[n] = '\0';
    return s;
}

static void test_write_and_position(void)
{
    textbuf_init(&tb, 0x07);
    textbuf_puts(&tb, "madire> ");
    CHECK_EQ(tb.row, 0);
    CHECK_EQ(tb.col, 8);
    CHECK(strcmp(row_text(0), "madire>") == 0);
    CHECK_EQ(textbuf_attr_at(&tb, 0, 0), 0x07);
    textbuf_set_attr(&tb, 0x0B);
    textbuf_putc(&tb, 'x');
    CHECK_EQ(textbuf_attr_at(&tb, 0, 8), 0x0B);
}

static void test_newline(void)
{
    textbuf_init(&tb, 0x07);
    textbuf_puts(&tb, "one\ntwo\n");
    CHECK(strcmp(row_text(0), "one") == 0);
    CHECK(strcmp(row_text(1), "two") == 0);
    CHECK_EQ(tb.row, 2);
    CHECK_EQ(tb.col, 0);
}

static void test_wrap_at_last_column(void)
{
    textbuf_init(&tb, 0x07);
    for (int i = 0; i < TB_COLS; i++)
        textbuf_putc(&tb, 'a');
    CHECK_EQ(tb.row, 0);                    /* wrap pending, not yet moved */
    CHECK_EQ(tb.col, TB_COLS);
    textbuf_putc(&tb, '\n');                /* exactly one line, no blank line */
    CHECK_EQ(tb.row, 1);
    textbuf_init(&tb, 0x07);
    for (int i = 0; i < TB_COLS + 5; i++)
        textbuf_putc(&tb, 'b');
    CHECK_EQ(tb.row, 1);
    CHECK_EQ(tb.col, 5);
    CHECK(strcmp(row_text(1), "bbbbb") == 0);
}

static void test_scroll_at_bottom(void)
{
    char line[8];
    textbuf_init(&tb, 0x07);
    for (int i = 0; i < TB_ROWS; i++) {
        line[0] = (char)('A' + i);
        line[1] = '\n';
        line[2] = '\0';
        textbuf_puts(&tb, line);
    }
    /* 25 lines each ending in '\n': the first has scrolled off. */
    CHECK_EQ(tb.scrolls, 1);
    CHECK(strcmp(row_text(0), "B") == 0);
    CHECK(strcmp(row_text(TB_ROWS - 2), "Y") == 0);
    CHECK(strcmp(row_text(TB_ROWS - 1), "") == 0);     /* new blank bottom row */
    CHECK_EQ(tb.row, TB_ROWS - 1);
}

static void test_many_scrolls(void)
{
    textbuf_init(&tb, 0x07);
    for (int i = 0; i < 1000; i++)
        textbuf_puts(&tb, "line\n");
    CHECK_EQ(tb.scrolls, 1000 - (TB_ROWS - 1));
    CHECK_EQ(tb.row, TB_ROWS - 1);
    CHECK(strcmp(row_text(0), "line") == 0);
}

static void test_clear(void)
{
    textbuf_init(&tb, 0x07);
    textbuf_puts(&tb, "hello\nworld");
    textbuf_clear(&tb);
    CHECK_EQ(tb.row, 0);
    CHECK_EQ(tb.col, 0);
    for (uint8_t r = 0; r < TB_ROWS; r++)
        CHECK(strcmp(row_text(r), "") == 0);
}

static void test_backspace(void)
{
    textbuf_init(&tb, 0x07);
    textbuf_puts(&tb, "ab\b");
    CHECK(strcmp(row_text(0), "a") == 0);
    CHECK_EQ(tb.col, 1);
    textbuf_puts(&tb, "\b\b\b");            /* never past column 0 */
    CHECK_EQ(tb.col, 0);
    CHECK_EQ(tb.row, 0);
}

static void test_tab_and_non_printable(void)
{
    textbuf_init(&tb, 0x07);
    textbuf_puts(&tb, "a\tb");
    CHECK(strcmp(row_text(0), "a    b") == 0);
    textbuf_init(&tb, 0x07);
    textbuf_putc(&tb, (char)0x01);
    textbuf_putc(&tb, (char)0x7F);
    textbuf_putc(&tb, (char)0xC8);
    CHECK(strcmp(row_text(0), "???") == 0);
}

static void test_cursor_and_direct_cells(void)
{
    textbuf_init(&tb, 0x07);
    textbuf_set_cursor(&tb, 200, 200);      /* clamped */
    CHECK_EQ(tb.row, TB_ROWS - 1);
    CHECK_EQ(tb.col, TB_COLS - 1);
    textbuf_put_at(&tb, 3, 4, 'Z', 0x0F);
    CHECK_EQ(textbuf_char_at(&tb, 3, 4), 'Z');
    CHECK_EQ(textbuf_attr_at(&tb, 3, 4), 0x0F);
    textbuf_put_at(&tb, TB_ROWS, 0, 'X', 0x0F);   /* outside: ignored */
    CHECK_EQ(textbuf_char_at(&tb, TB_ROWS, 0), 0);
    CHECK_EQ(tb.row, TB_ROWS - 1);          /* direct writes don't move the cursor */
}

int main(void)
{
    printf("textbuf unit tests\n");
    RUN(test_write_and_position);
    RUN(test_newline);
    RUN(test_wrap_at_last_column);
    RUN(test_scroll_at_bottom);
    RUN(test_many_scrolls);
    RUN(test_clear);
    RUN(test_backspace);
    RUN(test_tab_and_non_printable);
    RUN(test_cursor_and_direct_cells);
    return unit_report("textbuf");
}
