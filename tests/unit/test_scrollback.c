/* Unit tests for kernel/console/scrollback.c (console scrollback, 0.9.2),
 * plus a check that the (unchanged) scancode decoder delivers Shift+PgUp
 * and Shift+PgDn as the console expects. */
#include <stdio.h>
#include <string.h>
#include "drivers/keyboard/scancode.h"
#include "kernel/console/scrollback.h"
#include "tests/unit/unit.h"

static struct scrollback sb;

/* A row "L<n>" in colour `attr`, the rest blank. */
static void make_row(uint16_t row[TB_COLS], uint32_t n, uint8_t attr)
{
    char text[16];
    snprintf(text, sizeof(text), "L%u", n);
    for (uint32_t c = 0; c < TB_COLS; c++)
        row[c] = (uint16_t)(' ' | (attr << 8));
    for (uint32_t c = 0; text[c]; c++)
        row[c] = (uint16_t)((uint8_t)text[c] | (attr << 8));
}

static void push_lines(uint32_t from, uint32_t to)
{
    uint16_t row[TB_COLS];
    for (uint32_t n = from; n <= to; n++) {
        make_row(row, n, (uint8_t)(n % 2 ? 0x0C : 0x07));
        sb_push(&sb, row);
    }
}

static int line_is(uint32_t i, uint32_t n)
{
    char text[SB_TEXT_MAX + 1], want[16];
    const uint16_t *l = sb_line(&sb, i);
    if (!l)
        return 0;
    sb_row_text(l, text, SB_TEXT_MAX);
    snprintf(want, sizeof(want), "L%u", n);
    return strcmp(text, want) == 0;
}

static void test_empty(void)
{
    sb_init(&sb);
    CHECK_EQ(sb_count(&sb), 0);
    CHECK(!sb_scrolled(&sb));
    CHECK(sb_line(&sb, 0) == NULL);
    CHECK_EQ(sb_page_up(&sb), SB_EMPTY);
    CHECK(!sb_scrolled(&sb));
    CHECK_EQ(sb_page_down(&sb), SB_UNCHANGED);
    CHECK(!sb_to_bottom(&sb));
    CHECK_EQ(sb_total(&sb), TB_ROWS);
}

static void test_push_order_and_colours(void)
{
    sb_init(&sb);
    push_lines(1, 5);
    CHECK_EQ(sb_count(&sb), 5);
    for (uint32_t i = 0; i < 5; i++)
        CHECK(line_is(i, i + 1));
    CHECK(sb_line(&sb, 5) == NULL);
    CHECK_EQ(sb_line(&sb, 0)[0] >> 8, 0x0C);       /* odd lines red */
    CHECK_EQ(sb_line(&sb, 1)[0] >> 8, 0x07);
    CHECK_EQ(sb_line(&sb, 1)[79], ' ' | (0x07 << 8));
}

static void test_wrap_at_1000(void)
{
    sb_init(&sb);
    push_lines(1, 2500);
    CHECK_EQ(sb_count(&sb), SB_LINES);
    CHECK(line_is(0, 1501));                       /* the oldest kept */
    CHECK(line_is(SB_LINES - 1, 2500));            /* the newest */
    int bad = 0;
    for (uint32_t i = 0; i < SB_LINES; i++)
        if (!line_is(i, 1501 + i))
            bad++;
    CHECK_EQ(bad, 0);
    CHECK_EQ(sb_total(&sb), SB_LINES + TB_ROWS);   /* 1025 */
}

static void test_paging(void)
{
    sb_init(&sb);
    push_lines(1, 100);
    CHECK_EQ(sb_first_shown(&sb), 101);            /* at the bottom */
    CHECK_EQ(sb_page_up(&sb), SB_MOVED);           /* the 24 lines above the screen */
    CHECK(sb_scrolled(&sb));
    CHECK_EQ(sb_first_shown(&sb), 77);
    CHECK_EQ(sb_first_shown(&sb), sb_total(&sb) - 48);
    CHECK_EQ(sb_page_up(&sb), SB_MOVED);
    CHECK_EQ(sb_first_shown(&sb), 53);
    CHECK_EQ(sb_page_up(&sb), SB_MOVED);           /* 29 */
    CHECK_EQ(sb_page_up(&sb), SB_MOVED);           /* 5 */
    CHECK_EQ(sb_first_shown(&sb), 5);
    CHECK_EQ(sb_page_up(&sb), SB_MOVED);           /* stops at line 1 */
    CHECK_EQ(sb_first_shown(&sb), 1);
    CHECK_EQ(sb_page_up(&sb), SB_UNCHANGED);
    CHECK_EQ(sb_first_shown(&sb), 1);
    CHECK_EQ(sb_page_down(&sb), SB_MOVED);
    CHECK_EQ(sb_first_shown(&sb), 25);
    CHECK_EQ(sb_page_down(&sb), SB_MOVED);         /* 49 */
    CHECK_EQ(sb_page_down(&sb), SB_MOVED);         /* 73 */
    CHECK(sb_scrolled(&sb));
    CHECK_EQ(sb_page_down(&sb), SB_MOVED);         /* 97: still a kept line */
    CHECK(sb_scrolled(&sb));
    CHECK_EQ(sb_page_down(&sb), SB_MOVED);         /* past line 100: the live screen */
    CHECK(!sb_scrolled(&sb));
    CHECK_EQ(sb_page_down(&sb), SB_UNCHANGED);
    CHECK_EQ(sb_page_up(&sb), SB_MOVED);
    CHECK(sb_to_bottom(&sb));
    CHECK(!sb_scrolled(&sb));
    CHECK(!sb_to_bottom(&sb));
}

static void test_few_lines(void)
{
    sb_init(&sb);
    push_lines(1, 3);                              /* fewer than a page */
    CHECK_EQ(sb_page_up(&sb), SB_MOVED);
    CHECK_EQ(sb_first_shown(&sb), 1);
    CHECK_EQ(sb_page_up(&sb), SB_UNCHANGED);
    CHECK_EQ(sb_page_down(&sb), SB_MOVED);         /* 24 >= 3: back at the bottom */
    CHECK(!sb_scrolled(&sb));
}

static void test_view_rows(void)
{
    struct sb_source src;
    sb_init(&sb);
    push_lines(1, 30);
    sb_page_up(&sb);                               /* top = 6: lines 7..30 */
    for (uint32_t r = 0; r < SB_VIEW_ROWS; r++) {
        src = sb_view_row(&sb, r);
        CHECK(!src.from_screen);
        CHECK_EQ(src.index, 6 + r);
    }
    sb_page_up(&sb);                               /* top = 0 */
    src = sb_view_row(&sb, 0);
    CHECK(!src.from_screen && src.index == 0);
    sb_page_down(&sb);                             /* top = 24: lines 25..30, then screen */
    src = sb_view_row(&sb, 5);
    CHECK(!src.from_screen && src.index == 29);
    src = sb_view_row(&sb, 6);
    CHECK(src.from_screen && src.index == 0);
    src = sb_view_row(&sb, SB_VIEW_ROWS - 1);
    CHECK(src.from_screen && src.index == 17);
    /* Exactly one page kept: the first page up shows only kept lines. */
    sb_init(&sb);
    push_lines(1, 24);
    sb_page_up(&sb);
    CHECK_EQ(sb_first_shown(&sb), 1);
    src = sb_view_row(&sb, 23);
    CHECK(!src.from_screen && src.index == 23);
}

static void test_push_while_scrolled_when_full(void)
{
    sb_init(&sb);
    push_lines(1, SB_LINES);
    sb_page_up(&sb);                               /* top = 976: line L977 */
    CHECK(line_is(sb_first_shown(&sb) - 1, 977));
    push_lines(SB_LINES + 1, SB_LINES + 1);        /* the oldest is dropped */
    CHECK(line_is(sb_first_shown(&sb) - 1, 977));  /* the view keeps its lines */
    CHECK_EQ(sb_first_shown(&sb), 976);
}

static void test_indicator(void)
{
    char out[TB_COLS + 1];
    sb_init(&sb);
    push_lines(1, 999);
    sb_page_up(&sb);
    CHECK_EQ(sb_indicator(&sb, out), strlen(out));
    CHECK(strcmp(out, "-- scrolled back: line 976 of 1024 (Shift+PgUp/PgDn; any key returns) --")
          == 0);
    /* Every possible position fits 79 columns. */
    unsigned longest = 0;
    sb_init(&sb);
    for (uint32_t n = 0; n <= SB_LINES; n++) {
        if (n)
            push_lines(n, n);
        sb.scrolled = true;
        for (uint32_t top = 0; top < (n ? n : 1); top += (n > 50 ? 37 : 1)) {
            sb.top = top;
            unsigned len = sb_indicator(&sb, out);
            if (len > longest)
                longest = len;
        }
    }
    CHECK(longest <= 79);
    CHECK(longest >= 72);                          /* "line 1000 of 1025" */
}

static void test_row_text_and_used_rows(void)
{
    uint16_t row[TB_COLS];
    char out[SB_TEXT_MAX + 1];
    for (uint32_t c = 0; c < TB_COLS; c++)
        row[c] = (uint16_t)(' ' | (0x07 << 8));
    CHECK_EQ(sb_row_text(row, out, SB_TEXT_MAX), 0);
    CHECK(strcmp(out, "") == 0);
    const char *t = "madire> echo hello";
    for (uint32_t c = 0; t[c]; c++)
        row[c] = (uint16_t)((uint8_t)t[c] | (0x0F << 8));
    row[30] = 0x0700 | 0x01;                       /* a control character */
    CHECK_EQ(sb_row_text(row, out, SB_TEXT_MAX), 31);
    CHECK(strncmp(out, "madire> echo hello", 18) == 0 && out[30] == '?');
    for (uint32_t c = 0; c < TB_COLS; c++)
        row[c] = (uint16_t)('x' | (0x07 << 8));
    CHECK_EQ(sb_row_text(row, out, SB_TEXT_MAX), SB_TEXT_MAX);    /* cut at 40 */

    static uint16_t grid[TB_ROWS][TB_COLS];
    for (uint32_t r = 0; r < TB_ROWS; r++)
        for (uint32_t c = 0; c < TB_COLS; c++)
            grid[r][c] = (uint16_t)(' ' | (0x07 << 8));
    CHECK_EQ(sb_last_used_row(grid), -1);
    grid[0][0] = 0x0700 | 'M';
    CHECK_EQ(sb_last_used_row(grid), 0);
    grid[7][79] = 0x0700 | '>';
    CHECK_EQ(sb_last_used_row(grid), 7);
    grid[24][3] = 0x0700 | 'z';
    CHECK_EQ(sb_last_used_row(grid), 24);
}

static struct key_event ev(uint8_t key, uint8_t flags)
{
    struct key_event e = { key, 0, flags, 0 };
    return e;
}

static void test_classify_keys(void)
{
    struct key_event e;
    e = ev(KEY_PGUP, KEY_PRESSED | KEY_SHIFT | KEY_EXTENDED);
    CHECK_EQ(sb_classify_key(&e), SB_KEY_PAGE_UP);
    e = ev(KEY_PGDN, KEY_PRESSED | KEY_SHIFT);
    CHECK_EQ(sb_classify_key(&e), SB_KEY_PAGE_DOWN);
    e = ev(KEY_PGUP, KEY_PRESSED);                 /* plain PgUp: another key */
    CHECK_EQ(sb_classify_key(&e), SB_KEY_OTHER);
    e = ev(KEY_PGUP, KEY_PRESSED | KEY_SHIFT | KEY_CTRL);
    CHECK_EQ(sb_classify_key(&e), SB_KEY_OTHER);
    e = ev(KEY_PGDN, KEY_PRESSED | KEY_SHIFT | KEY_ALT);
    CHECK_EQ(sb_classify_key(&e), SB_KEY_OTHER);
    e = ev(KEY_PGUP, KEY_SHIFT);                   /* release */
    CHECK_EQ(sb_classify_key(&e), SB_KEY_NONE);
    e = ev(KEY_CHAR, 0);
    CHECK_EQ(sb_classify_key(&e), SB_KEY_NONE);
    const uint8_t mods[] = { KEY_LSHIFT, KEY_RSHIFT, KEY_LCTRL, KEY_RCTRL, KEY_LALT, KEY_RALT,
                             KEY_LGUI, KEY_RGUI, KEY_CAPSLOCK, KEY_NUMLOCK, KEY_SCROLLLOCK };
    for (unsigned i = 0; i < sizeof(mods); i++) {
        e = ev(mods[i], KEY_PRESSED | KEY_SHIFT);
        CHECK_EQ(sb_classify_key(&e), SB_KEY_NONE);
    }
    const uint8_t others[] = { KEY_CHAR, KEY_SPACE, KEY_ENTER, KEY_BACKSPACE, KEY_ESC, KEY_UP,
                               KEY_DOWN, KEY_HOME, KEY_END, KEY_F1, KEY_DELETE, KEY_KP_ENTER };
    for (unsigned i = 0; i < sizeof(others); i++) {
        e = ev(others[i], KEY_PRESSED);
        CHECK_EQ(sb_classify_key(&e), SB_KEY_OTHER);
    }
}

/* Feeds bytes to the real decoder; returns the last decoded event. */
static int decode(const uint8_t *bytes, unsigned n, struct key_event *last)
{
    struct scancode_state st;
    struct key_event e;
    int got = 0;
    scancode_init(&st);
    for (unsigned i = 0; i < n; i++)
        if (scancode_decode(&st, bytes[i], &e)) {
            *last = e;
            got++;
        }
    return got;
}

static void test_decoder_delivers_shift_pgup_pgdn(void)
{
    struct key_event e;
    /* Shift held, then grey PgUp. */
    const uint8_t a[] = { 0x2A, 0xE0, 0x49 };
    CHECK(decode(a, sizeof(a), &e) > 0);
    CHECK_EQ(e.keycode, KEY_PGUP);
    CHECK((e.flags & KEY_SHIFT) && (e.flags & KEY_PRESSED));
    CHECK_EQ(sb_classify_key(&e), SB_KEY_PAGE_UP);
    /* What a real keyboard sends: a fake Shift release before the grey key. */
    const uint8_t b[] = { 0x2A, 0xE0, 0xAA, 0xE0, 0x49 };
    CHECK(decode(b, sizeof(b), &e) > 0);
    CHECK_EQ(e.keycode, KEY_PGUP);
    CHECK(e.flags & KEY_SHIFT);
    CHECK_EQ(sb_classify_key(&e), SB_KEY_PAGE_UP);
    /* Shift + grey PgDn. */
    const uint8_t c[] = { 0x2A, 0xE0, 0x51 };
    CHECK(decode(c, sizeof(c), &e) > 0);
    CHECK_EQ(e.keycode, KEY_PGDN);
    CHECK_EQ(sb_classify_key(&e), SB_KEY_PAGE_DOWN);
    /* Grey PgUp without Shift: no Shift flag, so not a scrollback key. */
    const uint8_t d[] = { 0xE0, 0x49 };
    CHECK(decode(d, sizeof(d), &e) > 0);
    CHECK_EQ(e.keycode, KEY_PGUP);
    CHECK(!(e.flags & KEY_SHIFT));
    CHECK_EQ(sb_classify_key(&e), SB_KEY_OTHER);
    /* The release of Shift+PgUp does nothing. */
    const uint8_t f[] = { 0x2A, 0xE0, 0x49, 0xE0, 0xC9 };
    CHECK(decode(f, sizeof(f), &e) > 0);
    CHECK_EQ(e.keycode, KEY_PGUP);
    CHECK_EQ(sb_classify_key(&e), SB_KEY_NONE);
}

int main(void)
{
    printf("scrollback unit tests\n");
    RUN(test_empty);
    RUN(test_push_order_and_colours);
    RUN(test_wrap_at_1000);
    RUN(test_paging);
    RUN(test_few_lines);
    RUN(test_view_rows);
    RUN(test_push_while_scrolled_when_full);
    RUN(test_indicator);
    RUN(test_row_text_and_used_rows);
    RUN(test_classify_keys);
    RUN(test_decoder_delivers_shift_pgup_pgdn);
    return unit_report("scrollback");
}
