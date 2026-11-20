/* Unit tests for kernel/display/deskkeys_core.c (1.5 stage 9b;
 * docs/plans/1.5.md, item (6) and its S9b rules): the Super key opens the
 * M menu on its release, alone (not after another key or a mouse button,
 * not on the keyboard's repeat, left and right kept apart, never with the
 * power dialog open); the shortcuts with Super held (Up, Down, Left, Right,
 * Pause, D, M, Shift+M; letters whatever their case; any other key taken;
 * releases passed); Alt+F4 (only with a window focused, no menu or dialog
 * open and Alt+Tab not switching; Ctrl+Alt+F4 passes); and the safety rule:
 * a dropped key event clears the Super state, so a lost Super release never
 * leaves every key taken. The module is included directly. */
#include <stdint.h>
#include "tests/unit/unit.h"
#include "kernel/display/deskkeys_core.c"

static const struct deskkeys_ctx plain = { false, false, false, true };

static enum deskkeys_action key(struct deskkeys *k, uint8_t code, char ascii, bool pressed, uint8_t flags,
                                const struct deskkeys_ctx *c, uint32_t dropped)
{
    return deskkeys_key(k, code, ascii, pressed, flags, c, dropped);
}

/* A press and a release of the left Super key alone. */
static enum deskkeys_action super_tap(struct deskkeys *k, uint8_t code, const struct deskkeys_ctx *c)
{
    CHECK_EQ(key(k, code, 0, true, 0, c, 0), DK_TAKEN);
    return key(k, code, 0, false, 0, c, 0);
}

static void test_super_alone(void)
{
    struct deskkeys k;
    deskkeys_init(&k, 0);
    CHECK_EQ(super_tap(&k, KEY_LGUI, &plain), DK_MENU);
    CHECK_EQ(k.held, 0);
    CHECK_EQ(super_tap(&k, KEY_RGUI, &plain), DK_MENU);
    /* The keyboard's repeat: more presses while held change nothing; one menu. */
    CHECK_EQ(key(&k, KEY_LGUI, 0, true, 0, &plain, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_LGUI, 0, true, 0, &plain, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_LGUI, 0, true, 0, &plain, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 0), DK_MENU);
    /* Left and right held together: the menu on the last release only. */
    CHECK_EQ(key(&k, KEY_LGUI, 0, true, 0, &plain, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_RGUI, 0, true, 0, &plain, 0), DK_TAKEN);
    CHECK_EQ(k.held, DESKKEYS_SUPER_LEFT | DESKKEYS_SUPER_RIGHT);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_RGUI, 0, false, 0, &plain, 0), DK_MENU);
    /* A release with no press seen: nothing. */
    CHECK_EQ(key(&k, KEY_RGUI, 0, false, 0, &plain, 0), DK_TAKEN);
    /* With a menu open, a tap still toggles it (DK_MENU: the menus close it). */
    struct deskkeys_ctx menu = { true, false, false, true };
    CHECK_EQ(super_tap(&k, KEY_LGUI, &menu), DK_MENU);
}

static void test_another_key(void)
{
    struct deskkeys k;
    deskkeys_init(&k, 0);
    /* A key with no meaning while Super is held: taken, and no menu after. */
    key(&k, KEY_LGUI, 0, true, 0, &plain, 0);
    CHECK_EQ(key(&k, KEY_CHAR, 'x', true, 0, &plain, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_CHAR, 'x', false, 0, &plain, 0), DK_PASS);    /* releases pass */
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 0), DK_TAKEN);
    /* The next tap opens the menu again (the mark cleared). */
    CHECK_EQ(super_tap(&k, KEY_LGUI, &plain), DK_MENU);
    /* A modifier counts as another key (Super, Shift, release both: no menu). */
    key(&k, KEY_LGUI, 0, true, 0, &plain, 0);
    CHECK_EQ(key(&k, KEY_LSHIFT, 0, true, KEY_SHIFT, &plain, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_LSHIFT, 0, false, 0, &plain, 0), DK_PASS);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 0), DK_TAKEN);
    /* Super released before the other key: the other key's release passes. */
    key(&k, KEY_LGUI, 0, true, 0, &plain, 0);
    CHECK_EQ(key(&k, KEY_CHAR, 'd', true, 0, &plain, 0), DK_SHOW_DESKTOP);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_CHAR, 'd', false, 0, &plain, 0), DK_PASS);
    /* A mouse button while Super is held: no menu; with no Super held: nothing. */
    key(&k, KEY_LGUI, 0, true, 0, &plain, 0);
    deskkeys_pointer(&k);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 0), DK_TAKEN);
    deskkeys_pointer(&k);
    CHECK_EQ(super_tap(&k, KEY_LGUI, &plain), DK_MENU);
    /* Without Super, keys pass (Alt+Tab, letters, Esc). */
    CHECK_EQ(key(&k, KEY_TAB, '\t', true, KEY_ALT, &plain, 0), DK_PASS);
    CHECK_EQ(key(&k, KEY_CHAR, 'a', true, 0, &plain, 0), DK_PASS);
    CHECK_EQ(key(&k, KEY_ESC, 0, true, 0, &plain, 0), DK_PASS);
    CHECK_EQ(key(&k, KEY_PAUSE, 0, true, 0, &plain, 0), DK_PASS);
}

static void test_shortcuts(void)
{
    static const struct { uint8_t code; char ascii; uint8_t flags; enum deskkeys_action want; } cases[] = {
        { KEY_UP, 0, 0, DK_UP }, { KEY_DOWN, 0, 0, DK_DOWN }, { KEY_LEFT, 0, 0, DK_LEFT },
        { KEY_RIGHT, 0, 0, DK_RIGHT }, { KEY_PAUSE, 0, 0, DK_ABOUT },
        { KEY_CHAR, 'd', 0, DK_SHOW_DESKTOP }, { KEY_CHAR, 'D', KEY_CAPS, DK_SHOW_DESKTOP },
        { KEY_CHAR, 'D', KEY_SHIFT, DK_SHOW_DESKTOP },
        { KEY_CHAR, 'm', 0, DK_MINIMISE_ALL }, { KEY_CHAR, 'M', KEY_CAPS, DK_MINIMISE_ALL },
        { KEY_CHAR, 'M', KEY_SHIFT, DK_RESTORE_ALL }, { KEY_CHAR, 'm', KEY_SHIFT | KEY_CAPS, DK_RESTORE_ALL },
        { KEY_CHAR, 'e', 0, DK_FILES }, { KEY_CHAR, 'E', KEY_CAPS, DK_FILES }, { KEY_CHAR, 'q', 0, DK_TAKEN },
        { KEY_F4, 0, KEY_ALT, DK_TAKEN }, { KEY_ENTER, '\n', 0, DK_TAKEN },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct deskkeys k;
        deskkeys_init(&k, 0);
        CHECK_EQ(key(&k, KEY_RGUI, 0, true, 0, &plain, 0), DK_TAKEN);
        CHECK_EQ(key(&k, cases[i].code, cases[i].ascii, true, cases[i].flags, &plain, 0), cases[i].want);
        CHECK_EQ(key(&k, KEY_RGUI, 0, false, 0, &plain, 0), DK_TAKEN);    /* no menu after a shortcut */
    }
    /* Two shortcuts in one hold. */
    struct deskkeys k;
    deskkeys_init(&k, 0);
    key(&k, KEY_LGUI, 0, true, 0, &plain, 0);
    CHECK_EQ(key(&k, KEY_UP, 0, true, 0, &plain, 0), DK_UP);
    CHECK_EQ(key(&k, KEY_UP, 0, false, 0, &plain, 0), DK_PASS);
    CHECK_EQ(key(&k, KEY_DOWN, 0, true, 0, &plain, 0), DK_DOWN);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 0), DK_TAKEN);
}

static void test_dialog(void)
{
    struct deskkeys k;
    struct deskkeys_ctx dlg = { false, true, false, false };
    deskkeys_init(&k, 0);
    CHECK_EQ(super_tap(&k, KEY_LGUI, &dlg), DK_TAKEN);              /* no menu over the dialog */
    key(&k, KEY_LGUI, 0, true, 0, &dlg, 0);
    CHECK_EQ(key(&k, KEY_UP, 0, true, 0, &dlg, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_CHAR, 'd', true, 0, &dlg, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &dlg, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_F4, 0, true, KEY_ALT, &dlg, 0), DK_TAKEN);
    /* Pressed over the dialog, released after it closed, nothing else in
     * between: the release decides, so the menu. */
    key(&k, KEY_LGUI, 0, true, 0, &dlg, 0);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 0), DK_MENU);
}

static void test_alt_f4(void)
{
    struct deskkeys k;
    deskkeys_init(&k, 0);
    CHECK_EQ(key(&k, KEY_F4, 0, true, KEY_ALT, &plain, 0), DK_CLOSE);
    CHECK_EQ(key(&k, KEY_F4, 0, false, KEY_ALT, &plain, 0), DK_PASS);
    CHECK_EQ(key(&k, KEY_F4, 0, true, KEY_ALT | KEY_SHIFT, &plain, 0), DK_CLOSE);
    CHECK_EQ(key(&k, KEY_F4, 0, true, KEY_ALT | KEY_CTRL, &plain, 0), DK_PASS);
    CHECK_EQ(key(&k, KEY_F4, 0, true, 0, &plain, 0), DK_PASS);
    struct deskkeys_ctx sw = { false, false, true, true }, menu = { true, false, false, true };
    struct deskkeys_ctx dlg = { false, true, false, true }, none = { false, false, false, false };
    CHECK_EQ(key(&k, KEY_F4, 0, true, KEY_ALT, &sw, 0), DK_TAKEN);     /* Alt+Tab switching: ignored */
    CHECK_EQ(key(&k, KEY_F4, 0, true, KEY_ALT, &menu, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_F4, 0, true, KEY_ALT, &dlg, 0), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_F4, 0, true, KEY_ALT, &none, 0), DK_TAKEN);   /* no window focused: nothing */
}

/* A dropped key event clears the Super state first (the owner's safety
 * rule): a lost Super release never leaves every key taken. */
static void test_dropped_event_resets(void)
{
    struct deskkeys k;
    deskkeys_init(&k, 7);
    key(&k, KEY_LGUI, 0, true, 0, &plain, 7);
    CHECK_EQ(key(&k, KEY_CHAR, 'q', true, 0, &plain, 7), DK_TAKEN);   /* held: taken */
    CHECK(!deskkeys_take_reset(&k));
    /* The Super release was lost (the count moved): the next key passes. */
    CHECK_EQ(key(&k, KEY_CHAR, 'q', true, 0, &plain, 8), DK_PASS);
    CHECK_EQ(k.held, 0);
    CHECK(!k.other);
    CHECK(deskkeys_take_reset(&k));
    CHECK(!deskkeys_take_reset(&k));                                  /* once */
    /* A later Super tap works again. */
    CHECK_EQ(key(&k, KEY_LGUI, 0, true, 0, &plain, 8), DK_TAKEN);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 8), DK_MENU);
    /* The count moving with nothing held: cleared quietly (no reset reported). */
    CHECK_EQ(key(&k, KEY_CHAR, 'a', true, 0, &plain, 9), DK_PASS);
    CHECK(!deskkeys_take_reset(&k));
    /* A drop between the press and the release: the release is one with no
     * press seen (no menu); the mark set before the drop is cleared too. */
    key(&k, KEY_LGUI, 0, true, 0, &plain, 9);
    deskkeys_pointer(&k);
    CHECK_EQ(key(&k, KEY_LGUI, 0, false, 0, &plain, 10), DK_TAKEN);
    CHECK(deskkeys_take_reset(&k));
    CHECK(!k.other);
    /* The counter wrapping round is a change too. */
    deskkeys_init(&k, 0xFFFFFFFFu);
    key(&k, KEY_RGUI, 0, true, 0, &plain, 0xFFFFFFFFu);
    CHECK_EQ(key(&k, KEY_UP, 0, true, 0, &plain, 0), DK_PASS);
    CHECK(deskkeys_take_reset(&k));
}

int main(void)
{
    printf("test_deskkeys\n");
    RUN(test_super_alone);
    RUN(test_another_key);
    RUN(test_shortcuts);
    RUN(test_dialog);
    RUN(test_alt_f4);
    RUN(test_dropped_event_resets);
    return unit_report("test_deskkeys");
}
