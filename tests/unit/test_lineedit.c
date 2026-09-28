/* Unit tests for kernel/console/lineedit.c (prompt line editor). */
#include <stdint.h>
#include <string.h>
#include "kernel/console/lineedit.h"
#include "tests/unit/unit.h"

static struct lineedit le;

static enum le_result key(uint8_t keycode, char ascii, uint8_t flags)
{
    struct key_event e = { keycode, ascii, flags, 0 };
    return lineedit_key(&le, &e);
}

static enum le_result press(uint8_t keycode)
{
    return key(keycode, 0, KEY_PRESSED);
}

static void type(const char *s)
{
    for (; *s; s++)
        key(*s == ' ' ? KEY_SPACE : KEY_CHAR, *s, KEY_PRESSED);
}

static void test_insert_printable(void)
{
    lineedit_reset(&le);
    type("help");
    CHECK(strcmp(le.buf, "help") == 0);
    CHECK_EQ(le.len, 4);
    CHECK_EQ(le.pos, 4);
    CHECK_EQ(press(KEY_ENTER), LE_SUBMIT);
    CHECK(strcmp(le.buf, "help") == 0);             /* submit keeps the text */
}

static void test_shift_and_caps_characters_pass_through(void)
{
    /* The 0.4 decoder already applies Shift / Caps Lock to ascii. */
    lineedit_reset(&le);
    key(KEY_CHAR, 'A', KEY_PRESSED | KEY_SHIFT);
    key(KEY_CHAR, 'B', KEY_PRESSED | KEY_CAPS);
    key(KEY_CHAR, '!', KEY_PRESSED | KEY_SHIFT);
    CHECK(strcmp(le.buf, "AB!") == 0);
}

static void test_backspace(void)
{
    lineedit_reset(&le);
    CHECK_EQ(press(KEY_BACKSPACE), LE_NONE);       /* at start: no-op */
    type("hellx");
    CHECK_EQ(press(KEY_BACKSPACE), LE_CHANGED);
    type("o");
    CHECK(strcmp(le.buf, "hello") == 0);
    press(KEY_HOME);
    press(KEY_RIGHT);
    press(KEY_RIGHT);
    CHECK_EQ(press(KEY_BACKSPACE), LE_CHANGED);    /* middle: removes 'e' */
    CHECK(strcmp(le.buf, "hllo") == 0);
    CHECK_EQ(le.pos, 1);
}

static void test_delete(void)
{
    lineedit_reset(&le);
    type("abXc");
    CHECK_EQ(press(KEY_DELETE), LE_NONE);          /* at end: no-op */
    press(KEY_LEFT);
    press(KEY_LEFT);
    CHECK_EQ(press(KEY_DELETE), LE_CHANGED);
    CHECK(strcmp(le.buf, "abc") == 0);
    CHECK_EQ(le.pos, 2);
}

static void test_cursor_movement_bounds(void)
{
    lineedit_reset(&le);
    type("abc");
    CHECK_EQ(press(KEY_RIGHT), LE_NONE);           /* already at end */
    CHECK_EQ(press(KEY_END), LE_NONE);
    CHECK_EQ(press(KEY_HOME), LE_CHANGED);
    CHECK_EQ(le.pos, 0);
    CHECK_EQ(press(KEY_LEFT), LE_NONE);            /* already at start */
    CHECK_EQ(press(KEY_HOME), LE_NONE);
    CHECK_EQ(press(KEY_END), LE_CHANGED);
    CHECK_EQ(le.pos, 3);
}

static void test_insert_at_cursor(void)
{
    lineedit_reset(&le);
    type("cho world");
    press(KEY_HOME);
    type("e");
    press(KEY_END);
    type("!");
    CHECK(strcmp(le.buf, "echo world!") == 0);
    lineedit_reset(&le);
    type("echo hllo");
    press(KEY_HOME);
    for (int i = 0; i < 6; i++)
        press(KEY_RIGHT);
    type("e");
    CHECK(strcmp(le.buf, "echo hello") == 0);
    CHECK_EQ(le.pos, 7);
}

static void test_71_character_limit(void)
{
    lineedit_reset(&le);
    for (int i = 0; i < LINE_MAX; i++)
        CHECK_EQ(key(KEY_CHAR, 'a', KEY_PRESSED), LE_CHANGED);
    CHECK_EQ(le.len, 71);
    CHECK_EQ(key(KEY_CHAR, 'b', KEY_PRESSED), LE_REFUSED);   /* 72nd */
    CHECK_EQ(key(KEY_SPACE, ' ', KEY_PRESSED), LE_REFUSED);
    CHECK_EQ(le.len, 71);
    CHECK_EQ(strlen(le.buf), 71);
    press(KEY_HOME);
    CHECK_EQ(key(KEY_CHAR, 'b', KEY_PRESSED), LE_REFUSED);   /* also when inserting */
    CHECK_EQ(le.buf[0], 'a');
    CHECK_EQ(press(KEY_BACKSPACE), LE_NONE);       /* pos 0 */
    press(KEY_END);
    CHECK_EQ(press(KEY_BACKSPACE), LE_CHANGED);    /* room again */
    CHECK_EQ(key(KEY_CHAR, 'z', KEY_PRESSED), LE_CHANGED);
    CHECK_EQ(le.buf[70], 'z');
}

static void test_enter_submits(void)
{
    lineedit_reset(&le);
    CHECK_EQ(press(KEY_ENTER), LE_SUBMIT);         /* empty line submits too */
    CHECK_EQ(le.len, 0);
    CHECK_EQ(press(KEY_KP_ENTER), LE_SUBMIT);
}

static void test_releases_ignored(void)
{
    lineedit_reset(&le);
    CHECK_EQ(key(KEY_CHAR, 'a', 0), LE_NONE);      /* release */
    CHECK_EQ(key(KEY_ENTER, '\n', 0), LE_NONE);
    CHECK_EQ(le.len, 0);
}

static void test_ctrl_and_alt_ignored(void)
{
    lineedit_reset(&le);
    CHECK_EQ(key(KEY_CHAR, 'c', KEY_PRESSED | KEY_CTRL), LE_NONE);
    CHECK_EQ(key(KEY_CHAR, 'x', KEY_PRESSED | KEY_ALT), LE_NONE);
    CHECK_EQ(key(KEY_ENTER, '\n', KEY_PRESSED | KEY_CTRL), LE_NONE);
    CHECK_EQ(key(KEY_BACKSPACE, '\b', KEY_PRESSED | KEY_ALT), LE_NONE);
    CHECK_EQ(le.len, 0);
}

static void test_other_keys_ignored(void)
{
    lineedit_reset(&le);
    type("ab");
    CHECK_EQ(press(KEY_UP), LE_NONE);              /* no history */
    CHECK_EQ(press(KEY_DOWN), LE_NONE);
    CHECK_EQ(press(KEY_F1), LE_NONE);
    CHECK_EQ(press(KEY_ESC), LE_NONE);
    CHECK_EQ(press(KEY_LSHIFT), LE_NONE);
    CHECK_EQ(press(KEY_CAPSLOCK), LE_NONE);
    CHECK_EQ(press(KEY_UNKNOWN), LE_NONE);
    CHECK_EQ(key(KEY_CHAR, (char)0x01, KEY_PRESSED), LE_NONE);   /* non-printable */
    CHECK(strcmp(le.buf, "ab") == 0);
}

static void test_tab_inserted(void)
{
    lineedit_reset(&le);
    type("echo");
    CHECK_EQ(key(KEY_TAB, '\t', KEY_PRESSED), LE_CHANGED);
    type("a");
    CHECK(strcmp(le.buf, "echo\ta") == 0);
}

int main(void)
{
    printf("lineedit unit tests\n");
    RUN(test_insert_printable);
    RUN(test_shift_and_caps_characters_pass_through);
    RUN(test_backspace);
    RUN(test_delete);
    RUN(test_cursor_movement_bounds);
    RUN(test_insert_at_cursor);
    RUN(test_71_character_limit);
    RUN(test_enter_submits);
    RUN(test_releases_ignored);
    RUN(test_ctrl_and_alt_ignored);
    RUN(test_other_keys_ignored);
    RUN(test_tab_inserted);
    return unit_report("lineedit");
}
