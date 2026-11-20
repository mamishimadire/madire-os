/* Unit tests for kernel/display/ui_textfield.c (1.5 stage 10): the text
 * field's own 255-byte buffer and its keys (insert, refuse when full,
 * Backspace, Delete, the moves, Enter, Esc, Tab passed on), every key with
 * Ctrl or Alt consumed (never passed to the caller: Ctrl+A while renaming
 * cannot select all items), bytes kept and shown as '?', the scrolling
 * (fit), a click placing the cursor, the picture's edge, text and cursor
 * (made-up fonts); the name rules in their order (empty and only spaces,
 * '/', "." and "..", more than 255 bytes, outside printable ASCII, already
 * used), their messages and the default names ("New File 2.txt"). The
 * pixel-exact comparison with tests/desktop_ref.py is part static's. The
 * modules are included directly, except surface_core.c, wallpaper_core.c
 * and coverage_core.c, linked as separate files (the Makefile). */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/ui_textfield.c"

#define SW 240
#define SH 80
#define BG UI_SURFACE_WINDOW

static uint8_t buf[SH * SW * 4];
static struct surface surf;
static struct ui_canvas cv;

/* A made-up font: each glyph has only its top-left pixel set. */
static uint8_t corner_bits[256 * 16];
static const struct font corner = { 8, 16, 1, corner_bits };
static const struct ui_fonts fonts = { &corner, &corner };

static void setup(void)
{
    surface_init(&surf, buf, SW, SH, SW * 4, 4);
    ui_canvas_init(&cv, &surf);
    ui_fill(&cv, (struct rect){ 0, 0, SW, SH }, BG);
}

static uint32_t at(int x, int y)
{
    return surface_get(&surf, (uint32_t)x, (uint32_t)y);
}

static struct key_event key(uint8_t code, char ascii, uint8_t mods)
{
    return (struct key_event){ code, ascii, (uint8_t)(KEY_PRESSED | mods), 0 };
}

static enum ui_tf_result type(struct ui_textfield *t, const char *s)
{
    enum ui_tf_result r = UI_TF_NONE;
    for (; *s; s++) {
        struct key_event e = key(*s == ' ' ? KEY_SPACE : KEY_CHAR, *s, 0);
        r = ui_textfield_key(t, &e);
    }
    return r;
}

static enum ui_tf_result press(struct ui_textfield *t, uint8_t code, uint8_t mods)
{
    struct key_event e = key(code, 0, mods);
    return ui_textfield_key(t, &e);
}

static void tf_set_and_limit(void)
{
    static char long_text[301];
    struct ui_textfield t;
    for (int i = 0; i < 300; i++)
        long_text[i] = 'a';
    long_text[300] = '\0';
    ui_textfield_set(&t, long_text);
    CHECK_EQ(t.len, 255);
    CHECK_EQ(t.pos, 255);
    CHECK_EQ(t.first, 0);
    CHECK_EQ(t.buf[255], 0);
    CHECK_EQ(type(&t, "b"), UI_TF_REFUSED);
    CHECK_EQ(t.len, 255);
    press(&t, KEY_BACKSPACE, 0);
    CHECK_EQ(type(&t, "b"), UI_TF_CHANGED);
    CHECK_EQ(t.buf[254], 'b');
    ui_textfield_set(&t, "");
    CHECK_EQ(t.len, 0);
    CHECK_EQ(t.buf[0], 0);
}

static void tf_keys(void)
{
    struct ui_textfield t;
    ui_textfield_set(&t, "");
    CHECK_EQ(type(&t, "abc d"), UI_TF_CHANGED);
    CHECK(strcmp(t.buf, "abc d") == 0);
    CHECK_EQ(press(&t, KEY_HOME, 0), UI_TF_CHANGED);
    CHECK_EQ(t.pos, 0);
    CHECK_EQ(press(&t, KEY_HOME, 0), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_LEFT, 0), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_BACKSPACE, 0), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_RIGHT, 0), UI_TF_CHANGED);
    CHECK_EQ(type(&t, "X"), UI_TF_CHANGED);
    CHECK(strcmp(t.buf, "aXbc d") == 0);
    CHECK_EQ(t.pos, 2);
    CHECK_EQ(press(&t, KEY_DELETE, 0), UI_TF_CHANGED);
    CHECK(strcmp(t.buf, "aXc d") == 0);
    CHECK_EQ(press(&t, KEY_BACKSPACE, 0), UI_TF_CHANGED);
    CHECK(strcmp(t.buf, "ac d") == 0);
    CHECK_EQ(t.pos, 1);
    CHECK_EQ(press(&t, KEY_END, 0), UI_TF_CHANGED);
    CHECK_EQ(t.pos, 4);
    CHECK_EQ(press(&t, KEY_RIGHT, 0), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_DELETE, 0), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_ENTER, 0), UI_TF_SUBMIT);
    CHECK_EQ(press(&t, KEY_KP_ENTER, 0), UI_TF_SUBMIT);
    CHECK_EQ(press(&t, KEY_ESC, 0), UI_TF_CANCEL);
    CHECK_EQ(press(&t, KEY_TAB, 0), UI_TF_PASS);
    CHECK_EQ(press(&t, KEY_TAB, KEY_SHIFT), UI_TF_PASS);
    CHECK(strcmp(t.buf, "ac d") == 0);          /* Tab inserts nothing */
    /* Shift is part of the character (the driver's): "A" typed. */
    struct key_event e = key(KEY_CHAR, 'A', KEY_SHIFT);
    CHECK_EQ(ui_textfield_key(&t, &e), UI_TF_CHANGED);
    CHECK(strcmp(t.buf, "ac dA") == 0);
}

/* The owner's D2 rule: keys with Ctrl or Alt are consumed while the field
 * has focus, never passed to the caller; so is any other key. */
static void tf_ctrl_alt_consumed(void)
{
    struct ui_textfield t;
    ui_textfield_set(&t, "name");
    struct key_event e = key(KEY_CHAR, 'a', KEY_CTRL);      /* Ctrl+A */
    CHECK_EQ(ui_textfield_key(&t, &e), UI_TF_NONE);
    e = key(KEY_CHAR, 1, KEY_CTRL);                         /* Ctrl+A as its control code */
    CHECK_EQ(ui_textfield_key(&t, &e), UI_TF_NONE);
    e = key(KEY_CHAR, 'c', KEY_CTRL);
    CHECK_EQ(ui_textfield_key(&t, &e), UI_TF_NONE);
    e = key(KEY_CHAR, 'x', KEY_ALT);
    CHECK_EQ(ui_textfield_key(&t, &e), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_TAB, KEY_CTRL), UI_TF_NONE);     /* Ctrl+Tab: not passed */
    CHECK_EQ(press(&t, KEY_ENTER, KEY_CTRL), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_ESC, KEY_ALT), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_HOME, KEY_CTRL), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_F2, 0), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_UP, 0), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_DOWN, 0), UI_TF_NONE);
    CHECK_EQ(press(&t, KEY_PGDN, 0), UI_TF_NONE);
    e = (struct key_event){ KEY_CHAR, 'z', 0, 0 };          /* a release */
    CHECK_EQ(ui_textfield_key(&t, &e), UI_TF_NONE);
    e = key(KEY_CHAR, '\x7f', 0);
    CHECK_EQ(ui_textfield_key(&t, &e), UI_TF_NONE);
    CHECK(strcmp(t.buf, "name") == 0);
    CHECK_EQ(t.pos, 4);
}

static void tf_bytes_kept_and_shown(void)
{
    struct ui_textfield t;
    char out[16];
    ui_textfield_set(&t, "caf\xe9 \x01x");
    CHECK_EQ(t.len, 7);
    CHECK_EQ((uint8_t)t.buf[3], 0xE9);
    ui_name_shown(t.buf, out, sizeof(out));
    CHECK(strcmp(out, "caf? ?x") == 0);
    ui_name_shown("abcdef", out, 4);
    CHECK(strcmp(out, "abc") == 0);
}

static void tf_fit(void)
{
    struct ui_textfield t;
    ui_textfield_set(&t, "0123456789abcdefghij");      /* 20 bytes, the cursor at 20 */
    ui_textfield_fit(&t, 8);
    CHECK_EQ(t.first, 13);                              /* the cursor in the last column (7) */
    press(&t, KEY_HOME, 0);
    ui_textfield_fit(&t, 8);
    CHECK_EQ(t.first, 0);
    for (int i = 0; i < 10; i++)
        press(&t, KEY_RIGHT, 0);
    ui_textfield_fit(&t, 8);
    CHECK_EQ(t.first, 3);
    press(&t, KEY_END, 0);
    ui_textfield_fit(&t, 8);
    CHECK_EQ(t.first, 13);
    /* Deleting from the end pulls the start back: 7 bytes follow first. */
    for (int i = 0; i < 5; i++)
        press(&t, KEY_BACKSPACE, 0);
    ui_textfield_fit(&t, 8);
    CHECK_EQ(t.len, 15);
    CHECK_EQ(t.first, 8);
    /* All fits: shown from the start. */
    ui_textfield_set(&t, "abc");
    t.first = 2;
    ui_textfield_fit(&t, 8);
    CHECK_EQ(t.first, 0);
    /* No room counts as one column. */
    ui_textfield_set(&t, "abcd");
    ui_textfield_fit(&t, 0);
    CHECK_EQ(t.first, 4);
    /* The columns of a field: (w - 2 x d(8)) / font width. */
    CHECK_EQ(ui_textfield_cols((struct rect){ 0, 0, 240, 32 }, 4, &corner), 28);
    CHECK_EQ(ui_textfield_cols((struct rect){ 0, 0, 40, 32 }, 4, &corner), 3);
    CHECK_EQ(ui_textfield_cols((struct rect){ 0, 0, 16, 32 }, 4, &corner), 0);
    CHECK_EQ(ui_textfield_cols((struct rect){ 0, 0, 300, 40 }, 5, &corner), 35);   /* (300 - 20) / 8 */
}

static void tf_click(void)
{
    struct ui_textfield t;
    struct rect r = { 10, 10, 240, 32 };
    ui_textfield_set(&t, "abcdef");
    ui_textfield_click(&t, r, 4, &corner, 18);          /* the text's left end */
    CHECK_EQ(t.pos, 0);
    ui_textfield_click(&t, r, 4, &corner, 21);          /* 3 px in: still before 'a' */
    CHECK_EQ(t.pos, 0);
    ui_textfield_click(&t, r, 4, &corner, 22);          /* 4 px in: after 'a' (nearest) */
    CHECK_EQ(t.pos, 1);
    ui_textfield_click(&t, r, 4, &corner, 18 + 8 * 3 + 2);
    CHECK_EQ(t.pos, 3);
    ui_textfield_click(&t, r, 4, &corner, 200);         /* past the end */
    CHECK_EQ(t.pos, 6);
    ui_textfield_click(&t, r, 4, &corner, 0);
    CHECK_EQ(t.pos, 0);
    t.first = 2;
    ui_textfield_click(&t, r, 4, &corner, 18);
    CHECK_EQ(t.pos, 2);
}

static void tf_draw(void)
{
    struct ui_textfield t;
    struct rect r = { 20, 20, 200, 32 };
    for (int g = 0; g < 256; g++)
        corner_bits[g * 16] = 0x80;
    ui_textfield_set(&t, "ab");
    setup();
    ui_textfield_draw(&cv, &t, r, 4, &fonts, false);
    CHECK_EQ(at(120, 20), UI_TEXT_MUTED);               /* the edge, 1 px */
    CHECK_EQ(at(120, 21), UI_SURFACE);
    CHECK_EQ(at(120, 51), UI_TEXT_MUTED);
    CHECK_EQ(at(120, 19), BG);
    CHECK_EQ(at(20, 20), BG);                           /* the rounded corner */
    CHECK_EQ(at(28, 28), UI_TEXT);                      /* 'a' at (r.x + 8, r.y + 8) */
    CHECK_EQ(at(36, 28), UI_TEXT);                      /* 'b' */
    CHECK_EQ(at(44, 28), UI_SURFACE);                   /* no cursor unfocused */
    CHECK_EQ(at(44, 35), UI_SURFACE);
    setup();
    ui_textfield_draw(&cv, &t, r, 4, &fonts, true);
    CHECK_EQ(at(120, 20), UI_ACCENT);                   /* the edge, 2 px */
    CHECK_EQ(at(120, 21), UI_ACCENT);
    CHECK_EQ(at(120, 22), UI_SURFACE);
    CHECK_EQ(at(44, 28), UI_TEXT);                      /* the cursor: 2 px, the font's height */
    CHECK_EQ(at(45, 43), UI_TEXT);
    CHECK_EQ(at(46, 35), UI_SURFACE);
    CHECK_EQ(at(44, 44), UI_SURFACE);
    /* Bytes outside printable ASCII drawn as '?': glyph 0x3F's corner. */
    for (int g = 0; g < 256; g++)
        corner_bits[g * 16] = g == '?' ? 0x80 : 0;
    ui_textfield_set(&t, "\x01" "a");
    setup();
    ui_textfield_draw(&cv, &t, r, 4, &fonts, false);
    CHECK_EQ(at(28, 28), UI_TEXT);
    CHECK_EQ(at(36, 28), UI_SURFACE);
    /* Only the columns that fit are drawn: a 40 px field shows 3 bytes. */
    for (int g = 0; g < 256; g++)
        corner_bits[g * 16] = 0x80;
    ui_textfield_set(&t, "abcdef");
    t.pos = 0;
    setup();
    ui_textfield_draw(&cv, &t, (struct rect){ 20, 20, 40, 32 }, 4, &fonts, false);
    CHECK_EQ(at(28 + 16, 28), UI_TEXT);
    CHECK_EQ(at(28 + 24, 28), UI_SURFACE);
}

static int taken_n;
static const char *taken[4];

static bool exists(void *ctx, const char *name)
{
    (void)ctx;
    for (int i = 0; i < taken_n; i++)
        if (strcmp(taken[i], name) == 0)
            return true;
    return false;
}

static void name_rules(void)
{
    static char n255[257], n256[258];
    for (int i = 0; i < 256; i++)
        n256[i] = 'a';
    n256[256] = '\0';
    for (int i = 0; i < 255; i++)
        n255[i] = 'a';
    n255[255] = '\0';
    CHECK_EQ(ui_name_check("", exists, 0), UI_NAME_EMPTY);
    CHECK_EQ(ui_name_check(" ", exists, 0), UI_NAME_EMPTY);       /* the owner's D3 addition */
    CHECK_EQ(ui_name_check("   ", exists, 0), UI_NAME_EMPTY);
    CHECK_EQ(ui_name_check(" a", exists, 0), UI_NAME_OK);
    CHECK_EQ(ui_name_check("a ", exists, 0), UI_NAME_OK);
    CHECK_EQ(ui_name_check("a/b", exists, 0), UI_NAME_SLASH);
    CHECK_EQ(ui_name_check("/", exists, 0), UI_NAME_SLASH);
    CHECK_EQ(ui_name_check(".", exists, 0), UI_NAME_RESERVED);
    CHECK_EQ(ui_name_check("..", exists, 0), UI_NAME_RESERVED);
    CHECK_EQ(ui_name_check("...", exists, 0), UI_NAME_OK);
    CHECK_EQ(ui_name_check(".hidden", exists, 0), UI_NAME_OK);
    CHECK_EQ(ui_name_check(n255, exists, 0), UI_NAME_OK);
    CHECK_EQ(ui_name_check(n256, exists, 0), UI_NAME_TOO_LONG);
    CHECK_EQ(ui_name_check("caf\xe9", exists, 0), UI_NAME_NOT_ASCII);
    CHECK_EQ(ui_name_check("tab\there", exists, 0), UI_NAME_NOT_ASCII);
    CHECK_EQ(ui_name_check("a/\x01", exists, 0), UI_NAME_SLASH);  /* the order: '/' first */
    taken_n = 1;
    taken[0] = "Notes";
    CHECK_EQ(ui_name_check("Notes", exists, 0), UI_NAME_TAKEN);
    CHECK_EQ(ui_name_check("notes", exists, 0), UI_NAME_OK);
    CHECK_EQ(ui_name_check("Notes", 0, 0), UI_NAME_OK);
    taken_n = 0;
    CHECK(ui_name_message(UI_NAME_OK) == 0);
    CHECK(ui_name_message(UI_NAME_EMPTY) == 0);
    CHECK(strcmp(ui_name_message(UI_NAME_SLASH), "A name can't contain '/'.") == 0);
    CHECK(strcmp(ui_name_message(UI_NAME_RESERVED), "This name is reserved.") == 0);
    CHECK(strcmp(ui_name_message(UI_NAME_TOO_LONG), "Names can be at most 255 characters.") == 0);
    CHECK(strcmp(ui_name_message(UI_NAME_NOT_ASCII), "Use printable ASCII characters only.") == 0);
    CHECK(strcmp(ui_name_message(UI_NAME_TAKEN), "That name is already used here.") == 0);
}

static int always_n;

static bool always(void *ctx, const char *name)
{
    (void)ctx;
    (void)name;
    always_n++;
    return true;
}

static void name_defaults(void)
{
    char out[32];
    CHECK(ui_name_default(true, exists, 0, out, sizeof(out)));
    CHECK(strcmp(out, "New Folder") == 0);
    CHECK(ui_name_default(false, 0, 0, out, sizeof(out)));
    CHECK(strcmp(out, "New File.txt") == 0);
    taken_n = 2;
    taken[0] = "New Folder";
    taken[1] = "New Folder 2";
    CHECK(ui_name_default(true, exists, 0, out, sizeof(out)));
    CHECK(strcmp(out, "New Folder 3") == 0);
    taken[0] = "New File.txt";
    taken_n = 1;
    CHECK(ui_name_default(false, exists, 0, out, sizeof(out)));
    CHECK(strcmp(out, "New File 2.txt") == 0);              /* the owner's D3 numbering */
    taken_n = 0;
    always_n = 0;
    CHECK(!ui_name_default(true, always, 0, out, sizeof(out)));
    CHECK_EQ(always_n, 1000);                               /* "New Folder" .. "New Folder 1000" */
    CHECK_EQ(out[0], 0);
    CHECK(!ui_name_default(true, 0, 0, out, 31));
    CHECK_EQ(out[0], 0);
}

int main(void)
{
    printf("test_ui_textfield\n");
    RUN(tf_set_and_limit);
    RUN(tf_keys);
    RUN(tf_ctrl_alt_consumed);
    RUN(tf_bytes_kept_and_shown);
    RUN(tf_fit);
    RUN(tf_click);
    RUN(tf_draw);
    RUN(name_rules);
    RUN(name_defaults);
    return unit_report("test_ui_textfield");
}
