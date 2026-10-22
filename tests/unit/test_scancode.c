/* Unit tests for drivers/keyboard/scancode.c (set 1 decoder, US layout). */
#include <stdint.h>
#include "drivers/keyboard/scancode.h"
#include "tests/unit/unit.h"

static struct scancode_state st;
static struct key_event ev[32];

/* Feeds bytes; returns how many events were produced (stored in ev[]). */
static unsigned feed(const uint8_t *bytes, unsigned n)
{
    unsigned count = 0;
    for (unsigned i = 0; i < n; i++)
        if (scancode_decode(&st, bytes[i], &ev[count]) && count < 31)
            count++;
    return count;
}

#define FEED(...) feed((const uint8_t[]){ __VA_ARGS__ }, sizeof((const uint8_t[]){ __VA_ARGS__ }))

static void reset(void)
{
    scancode_init(&st);
}

static void test_letter_make_and_break(void)
{
    reset();
    CHECK_EQ(FEED(0x1E, 0x9E), 2);
    CHECK_EQ(ev[0].keycode, KEY_CHAR);
    CHECK_EQ(ev[0].ascii, 'a');
    CHECK_EQ(ev[0].flags, KEY_PRESSED);
    CHECK_EQ(ev[0].scancode, 0x1E);
    CHECK_EQ(ev[1].keycode, KEY_CHAR);
    CHECK_EQ(ev[1].ascii, 'a');
    CHECK_EQ(ev[1].flags & KEY_PRESSED, 0);
    CHECK_EQ(ev[1].scancode, 0x9E);
}

static void test_left_and_right_shift(void)
{
    reset();
    CHECK_EQ(FEED(0x2A, 0x30, 0xB0, 0xAA), 4);      /* LShift, b */
    CHECK_EQ(ev[0].keycode, KEY_LSHIFT);
    CHECK_EQ(ev[1].ascii, 'B');
    CHECK(ev[1].flags & KEY_SHIFT);
    CHECK_EQ(ev[3].keycode, KEY_LSHIFT);
    CHECK_EQ(ev[3].flags & KEY_SHIFT, 0);           /* released */

    reset();
    CHECK_EQ(FEED(0x36, 0x30), 2);                  /* RShift, b */
    CHECK_EQ(ev[1].ascii, 'B');

    reset();                                         /* both held, one released */
    CHECK_EQ(FEED(0x2A, 0x36, 0xAA, 0x30), 4);
    CHECK_EQ(ev[3].ascii, 'B');                     /* right still held */
    CHECK(ev[3].flags & KEY_SHIFT);
}

static void test_caps_lock_toggles_once_while_held(void)
{
    reset();
    /* press, two auto-repeats, release, then 'a' */
    CHECK_EQ(FEED(0x3A, 0x3A, 0x3A, 0xBA, 0x1E), 5);
    CHECK_EQ(ev[0].keycode, KEY_CAPSLOCK);
    CHECK(ev[0].flags & KEY_CAPS);
    CHECK_EQ(ev[4].ascii, 'A');                     /* on once, not three times */
    CHECK(ev[4].flags & KEY_CAPS);

    CHECK_EQ(FEED(0x3A, 0xBA, 0x1E), 3);           /* second press: off */
    CHECK_EQ(ev[2].ascii, 'a');
    CHECK_EQ(ev[2].flags & KEY_CAPS, 0);
}

static void test_caps_and_shift_combine(void)
{
    reset();
    CHECK_EQ(FEED(0x3A, 0xBA, 0x2A, 0x1E), 4);      /* caps on + shift: lowercase */
    CHECK_EQ(ev[3].ascii, 'a');
    reset();
    CHECK_EQ(FEED(0x3A, 0xBA, 0x02), 3);           /* caps does not shift digits */
    CHECK_EQ(ev[2].ascii, '1');
}

static void test_shifted_symbols(void)
{
    reset();
    CHECK_EQ(FEED(0x2A, 0x02, 0x0C, 0x27, 0x35), 5);
    CHECK_EQ(ev[1].ascii, '!');
    CHECK_EQ(ev[2].ascii, '_');
    CHECK_EQ(ev[3].ascii, ':');
    CHECK_EQ(ev[4].ascii, '?');
    reset();
    CHECK_EQ(FEED(0x02, 0x0C, 0x27, 0x35, 0x29, 0x2B), 6);
    CHECK_EQ(ev[0].ascii, '1');
    CHECK_EQ(ev[1].ascii, '-');
    CHECK_EQ(ev[2].ascii, ';');
    CHECK_EQ(ev[3].ascii, '/');
    CHECK_EQ(ev[4].ascii, '`');
    CHECK_EQ(ev[5].ascii, '\\');
}

static void test_ctrl_and_alt_flags(void)
{
    reset();
    CHECK_EQ(FEED(0x1D, 0x2E, 0x9D, 0x38, 0x2E, 0xB8, 0x2E), 7);
    CHECK(ev[1].flags & KEY_CTRL);                  /* Ctrl+C */
    CHECK_EQ(ev[1].ascii, 'c');                     /* no control characters in 0.4 */
    CHECK_EQ(ev[2].flags & KEY_CTRL, 0);
    CHECK(ev[4].flags & KEY_ALT);                   /* Alt+C */
    CHECK_EQ(ev[6].flags & (KEY_CTRL | KEY_ALT), 0);
}

static void test_extended_keys(void)
{
    reset();
    CHECK_EQ(FEED(0xE0, 0x48, 0xE0, 0xC8), 2);      /* Up */
    CHECK_EQ(ev[0].keycode, KEY_UP);
    CHECK_EQ(ev[0].flags, KEY_PRESSED | KEY_EXTENDED);
    CHECK_EQ(ev[1].keycode, KEY_UP);
    CHECK_EQ(ev[1].flags, KEY_EXTENDED);

    reset();
    CHECK_EQ(FEED(0x48), 1);                        /* same byte, no prefix: keypad 8 */
    CHECK_EQ(ev[0].keycode, KEY_CHAR);
    CHECK_EQ(ev[0].ascii, '8');

    reset();
    CHECK_EQ(FEED(0xE0, 0x1D, 0x2E, 0xE0, 0x9D), 3); /* right Ctrl sets Ctrl */
    CHECK_EQ(ev[0].keycode, KEY_RCTRL);
    CHECK(ev[1].flags & KEY_CTRL);
    CHECK_EQ(ev[2].keycode, KEY_RCTRL);

    reset();
    CHECK_EQ(FEED(0xE0, 0x1C, 0xE0, 0x38, 0xE0, 0x53, 0xE0, 0x4B), 4);
    CHECK_EQ(ev[0].keycode, KEY_KP_ENTER);
    CHECK_EQ(ev[0].ascii, '\n');
    CHECK_EQ(ev[1].keycode, KEY_RALT);
    CHECK_EQ(ev[2].keycode, KEY_DELETE);
    CHECK_EQ(ev[3].keycode, KEY_LEFT);
}

static void test_fake_shifts_ignored(void)
{
    reset();
    /* Print Screen make: E0 2A E0 37, break: E0 B7 E0 AA */
    CHECK_EQ(FEED(0xE0, 0x2A, 0xE0, 0x37, 0xE0, 0xB7, 0xE0, 0xAA), 2);
    CHECK_EQ(ev[0].keycode, KEY_PRINTSCREEN);
    CHECK_EQ(ev[1].keycode, KEY_PRINTSCREEN);
    CHECK_EQ(ev[1].flags & KEY_SHIFT, 0);           /* no phantom shift */
    CHECK_EQ(FEED(0x1E), 1);
    CHECK_EQ(ev[0].ascii, 'a');
}

static void test_pause_sequence(void)
{
    reset();
    CHECK_EQ(FEED(0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5), 1);
    CHECK_EQ(ev[0].keycode, KEY_PAUSE);
    CHECK(ev[0].flags & KEY_PRESSED);
    CHECK_EQ(ev[0].flags & KEY_CTRL, 0);            /* the 1D/9D inside is not Ctrl */
    CHECK_EQ(FEED(0x1E), 1);                        /* decoder back to normal */
    CHECK_EQ(ev[0].ascii, 'a');
    CHECK_EQ(ev[0].flags & KEY_CTRL, 0);
}

static void test_unknown_codes(void)
{
    reset();
    CHECK_EQ(FEED(0x59, 0xE0, 0x10), 2);            /* unused base and E0 codes */
    CHECK_EQ(ev[0].keycode, KEY_UNKNOWN);
    CHECK_EQ(ev[0].scancode, 0x59);
    CHECK_EQ(ev[1].keycode, KEY_UNKNOWN);
    CHECK(ev[1].flags & KEY_EXTENDED);
}

static void test_stray_break(void)
{
    reset();
    CHECK_EQ(FEED(0xAA, 0x1E), 2);                  /* LShift release without press */
    CHECK_EQ(ev[0].keycode, KEY_LSHIFT);
    CHECK_EQ(ev[1].ascii, 'a');                     /* shift state not corrupted */
}

static void test_whitespace_keys(void)
{
    reset();
    CHECK_EQ(FEED(0x1C, 0x0E, 0x0F, 0x39, 0x01), 5);
    CHECK_EQ(ev[0].keycode, KEY_ENTER);
    CHECK_EQ(ev[0].ascii, '\n');
    CHECK_EQ(ev[1].keycode, KEY_BACKSPACE);
    CHECK_EQ(ev[1].ascii, '\b');
    CHECK_EQ(ev[2].keycode, KEY_TAB);
    CHECK_EQ(ev[2].ascii, '\t');
    CHECK_EQ(ev[3].keycode, KEY_SPACE);
    CHECK_EQ(ev[3].ascii, ' ');
    CHECK_EQ(ev[4].keycode, KEY_ESC);
    CHECK_EQ(ev[4].ascii, 0);
}

static void test_function_keys(void)
{
    reset();
    CHECK_EQ(FEED(0x3B, 0x44, 0x57, 0x58), 4);
    CHECK_EQ(ev[0].keycode, KEY_F1);
    CHECK_EQ(ev[1].keycode, KEY_F10);
    CHECK_EQ(ev[2].keycode, KEY_F11);
    CHECK_EQ(ev[3].keycode, KEY_F12);
}

static void test_prefix_then_break(void)
{
    reset();
    CHECK_EQ(FEED(0xE0), 0);                        /* prefix alone: no event */
    CHECK_EQ(FEED(0xD0), 1);                        /* E0 D0 = Down released */
    CHECK_EQ(ev[0].keycode, KEY_DOWN);
    CHECK_EQ(ev[0].flags, KEY_EXTENDED);
}

static void test_responses_are_not_keys(void)
{
    CHECK(scancode_is_response(0xFA));
    CHECK(scancode_is_response(0xEE));
    CHECK(scancode_is_response(0xFE));
    CHECK(scancode_is_response(0x00));
    CHECK(scancode_is_response(0xFF));
    CHECK(!scancode_is_response(0xAA));             /* LShift release, not a response */
    CHECK(!scancode_is_response(0x1E));
    CHECK(!scancode_is_response(0xE0));
}

static void test_every_byte_is_safe(void)
{
    /* All 256 byte values, in every prefix state: never crashes, never
     * produces KEY_NONE, and always leaves the decoder usable. */
    for (unsigned prefix = 0; prefix < 3; prefix++) {
        for (unsigned b = 0; b < 256; b++) {
            struct key_event e;
            reset();
            if (prefix == 1)
                scancode_decode(&st, 0xE0, &e);
            if (prefix == 2)
                scancode_decode(&st, 0xE1, &e);
            if (scancode_decode(&st, (uint8_t)b, &e))
                CHECK(e.keycode != KEY_NONE && e.keycode < KEY_COUNT);
        }
    }
}

/* 1.1: Num Lock, Scroll Lock, the keypad and the ISO key. */
static void test_num_lock_on_after_init(void)
{
    reset();
    CHECK(st.num_on);
    CHECK(!st.caps_on);
    CHECK(!st.scroll_on);
    CHECK_EQ(scancode_lights(&st), 0x02);
    CHECK_EQ(FEED(0x48, 0xC8), 2);                  /* keypad 8 */
    CHECK_EQ(ev[0].keycode, KEY_CHAR);
    CHECK_EQ(ev[0].ascii, '8');
    CHECK_EQ(ev[0].flags, KEY_PRESSED);
}

static void test_num_and_scroll_lock_toggle_once_while_held(void)
{
    reset();
    /* Num Lock: press, two auto-repeats, release */
    CHECK_EQ(FEED(0x45, 0x45, 0x45, 0xC5), 4);
    CHECK_EQ(ev[0].keycode, KEY_NUMLOCK);
    CHECK(!st.num_on);                              /* off once, not three times */
    CHECK_EQ(scancode_lights(&st), 0x00);
    CHECK_EQ(FEED(0x45, 0xC5), 2);                  /* second press: on */
    CHECK(st.num_on);
    CHECK_EQ(scancode_lights(&st), 0x02);

    /* Scroll Lock: the same */
    CHECK_EQ(FEED(0x46, 0x46, 0x46, 0xC6), 4);
    CHECK_EQ(ev[0].keycode, KEY_SCROLLLOCK);
    CHECK(st.scroll_on);
    CHECK_EQ(scancode_lights(&st), 0x03);
    CHECK_EQ(FEED(0x46, 0xC6), 2);
    CHECK(!st.scroll_on);
    CHECK_EQ(scancode_lights(&st), 0x02);
    CHECK(st.num_on);                               /* the locks are separate */
    CHECK(!st.caps_on);
}

static void test_keypad_follows_num_lock(void)
{
    static const struct { uint8_t code; uint8_t key; char digit; } kp[] = {
        { 0x47, KEY_HOME, '7' },   { 0x48, KEY_UP, '8' },    { 0x49, KEY_PGUP, '9' },
        { 0x4B, KEY_LEFT, '4' },   { 0x4D, KEY_RIGHT, '6' }, { 0x4F, KEY_END, '1' },
        { 0x50, KEY_DOWN, '2' },   { 0x51, KEY_PGDN, '3' },  { 0x52, KEY_INSERT, '0' },
        { 0x53, KEY_DELETE, '.' },
    };
    reset();
    CHECK_EQ(FEED(0x45, 0xC5), 2);                  /* Num Lock off */
    for (unsigned i = 0; i < sizeof(kp) / sizeof(kp[0]); i++) {
        CHECK_EQ(FEED(kp[i].code, (uint8_t)(kp[i].code | 0x80)), 2);
        CHECK_EQ(ev[0].keycode, kp[i].key);
        CHECK_EQ(ev[0].ascii, 0);
        CHECK_EQ(ev[0].flags, KEY_PRESSED);         /* no KEY_EXTENDED */
        CHECK_EQ(ev[0].scancode, kp[i].code);
        CHECK_EQ(ev[1].keycode, kp[i].key);
        CHECK_EQ(ev[1].flags, 0);
    }
    CHECK_EQ(FEED(0x37, 0x4A, 0x4E, 0xE0, 0x35, 0xE0, 0x1C), 5); /* * - + / Enter */
    CHECK_EQ(ev[0].ascii, '*');
    CHECK_EQ(ev[1].ascii, '-');
    CHECK_EQ(ev[2].ascii, '+');
    CHECK_EQ(ev[3].ascii, '/');
    CHECK_EQ(ev[4].keycode, KEY_KP_ENTER);
    CHECK_EQ(FEED(0xE0, 0x48), 1);                  /* the grey Up: unchanged */
    CHECK_EQ(ev[0].keycode, KEY_UP);
    CHECK_EQ(ev[0].flags, KEY_PRESSED | KEY_EXTENDED);

    CHECK_EQ(FEED(0x45, 0xC5), 2);                  /* Num Lock on again */
    for (unsigned i = 0; i < sizeof(kp) / sizeof(kp[0]); i++) {
        CHECK_EQ(FEED(kp[i].code), 1);
        CHECK_EQ(ev[0].keycode, KEY_CHAR);
        CHECK_EQ(ev[0].ascii, kp[i].digit);
        CHECK_EQ(ev[0].flags, KEY_PRESSED);
    }
    CHECK_EQ(FEED(0x4C), 1);
    CHECK_EQ(ev[0].ascii, '5');
}

static void test_keypad_5_with_num_lock_off(void)
{
    reset();
    CHECK_EQ(FEED(0x45, 0xC5, 0x4C, 0xCC), 4);
    CHECK_EQ(ev[2].keycode, KEY_UNKNOWN);
    CHECK_EQ(ev[2].ascii, 0);
    CHECK_EQ(ev[2].flags, KEY_PRESSED);
    CHECK_EQ(ev[3].keycode, KEY_UNKNOWN);
}

static void test_shift_keypad_9_with_num_lock_off(void)
{
    reset();
    CHECK_EQ(FEED(0x45, 0xC5, 0x2A, 0x49, 0xC9, 0xAA), 6);
    CHECK_EQ(ev[3].keycode, KEY_PGUP);
    CHECK_EQ(ev[3].ascii, 0);
    CHECK_EQ(ev[3].flags, KEY_PRESSED | KEY_SHIFT);
    CHECK_EQ(ev[3].scancode, 0x49);
}

static void test_pause_leaves_num_lock_on(void)
{
    reset();
    CHECK_EQ(FEED(0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5), 1);
    CHECK_EQ(ev[0].keycode, KEY_PAUSE);
    CHECK(st.num_on);                               /* the inner 45 never toggles */
    CHECK(!st.num_held);
    CHECK_EQ(scancode_lights(&st), 0x02);
    CHECK_EQ(FEED(0x48), 1);
    CHECK_EQ(ev[0].ascii, '8');
}

static void test_iso_key(void)
{
    reset();
    CHECK_EQ(FEED(0x56, 0xD6, 0x2A, 0x56, 0xD6, 0xAA), 6);
    CHECK_EQ(ev[0].keycode, KEY_CHAR);
    CHECK_EQ(ev[0].ascii, '\\');
    CHECK_EQ(ev[0].flags, KEY_PRESSED);
    CHECK_EQ(ev[0].scancode, 0x56);
    CHECK_EQ(ev[3].keycode, KEY_CHAR);
    CHECK_EQ(ev[3].ascii, '|');
    CHECK_EQ(ev[3].flags, KEY_PRESSED | KEY_SHIFT);
    reset();
    CHECK_EQ(FEED(0x3A, 0xBA, 0x56), 3);           /* Caps Lock does not shift it */
    CHECK_EQ(ev[2].ascii, '\\');
}

static void test_lights_after_each_lock_change(void)
{
    reset();
    CHECK_EQ(scancode_lights(&st), 0x02);
    FEED(0x3A, 0xBA);                               /* Caps on */
    CHECK_EQ(scancode_lights(&st), 0x06);
    FEED(0x45, 0xC5);                               /* Num off */
    CHECK_EQ(scancode_lights(&st), 0x04);
    FEED(0x46, 0xC6);                               /* Scroll on */
    CHECK_EQ(scancode_lights(&st), 0x05);
    FEED(0x3A, 0xBA);                               /* Caps off */
    CHECK_EQ(scancode_lights(&st), 0x01);
    FEED(0x45, 0xC5);                               /* Num on */
    CHECK_EQ(scancode_lights(&st), 0x03);
    FEED(0x46, 0xC6);                               /* Scroll off */
    CHECK_EQ(scancode_lights(&st), 0x02);
    FEED(0x1E, 0x9E, 0xE0, 0x46, 0xE0, 0xC6);      /* a key, Ctrl+Break: no change */
    CHECK_EQ(scancode_lights(&st), 0x02);
}

static void test_key_names(void)
{
    CHECK(scancode_key_name(KEY_CHAR)[0] == 'C');
    CHECK(scancode_key_name(KEY_UP)[0] == 'U');
    CHECK(scancode_key_name(KEY_COUNT)[0] == '?');
    for (unsigned k = 0; k < KEY_COUNT; k++)
        CHECK(scancode_key_name((uint8_t)k)[0] != '?');
}

int main(void)
{
    printf("scancode unit tests\n");
    RUN(test_letter_make_and_break);
    RUN(test_left_and_right_shift);
    RUN(test_caps_lock_toggles_once_while_held);
    RUN(test_caps_and_shift_combine);
    RUN(test_shifted_symbols);
    RUN(test_ctrl_and_alt_flags);
    RUN(test_extended_keys);
    RUN(test_fake_shifts_ignored);
    RUN(test_pause_sequence);
    RUN(test_unknown_codes);
    RUN(test_stray_break);
    RUN(test_whitespace_keys);
    RUN(test_function_keys);
    RUN(test_prefix_then_break);
    RUN(test_responses_are_not_keys);
    RUN(test_every_byte_is_safe);
    RUN(test_num_lock_on_after_init);
    RUN(test_num_and_scroll_lock_toggle_once_while_held);
    RUN(test_keypad_follows_num_lock);
    RUN(test_keypad_5_with_num_lock_off);
    RUN(test_shift_keypad_9_with_num_lock_off);
    RUN(test_pause_leaves_num_lock_on);
    RUN(test_iso_key);
    RUN(test_lights_after_each_lock_change);
    RUN(test_key_names);
    return unit_report("scancode");
}
