/* Unit tests for kernel/display/ui_widget.c (the toolkit's label and
 * button, 1.3 stage 2): a label's three alignments and its vertical
 * centring (rounded down, also for a box lower than the font), clipped to
 * its box; the buttons' styles equal the Design Guide's tokens, and a state
 * the guide gives no colour is never drawn; the close button's three states
 * (no fill when normal, `danger` on hover, `danger.pressed` pressed); a
 * pressed key; the focus ring 2 dp outside with its 2 dp gap untouched;
 * bold labels from the bold font; refusals. Made-up fonts. The
 * pixel-exact comparison with tests/desktop_ref.py is part static's. The
 * modules are included directly, except surface_core.c,
 * wallpaper_core.c and coverage_core.c (since 1.3 stage 3), linked as
 * separate files (the Makefile), so they never share a translation unit
 * with the toolkit (their statics). */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/ui_widget.c"

#define SW 200
#define SH 120
#define BG UI_SURFACE_WINDOW

static uint8_t buf[SH * SW * 4];
static struct surface surf;
static struct ui_canvas cv;

/* Made-up fonts: in `corner` each glyph has only its top-left pixel set;
 * `block` (the "bold" one) every pixel. */
static uint8_t corner_bits[256 * 16];
static uint8_t block_bits[256 * 16];
static const struct font corner = { 8, 16, 1, corner_bits };
static const struct font block = { 8, 16, 1, block_bits };
static const struct ui_fonts fonts = { &corner, &block };

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

static int count(uint32_t rgb, struct rect r)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            n += at(x, y) == rgb;
    return n;
}

static const struct rect all = { 0, 0, SW, SH };

static void test_label(void)
{
    for (int c = 0; c < 256; c++)
        corner_bits[c * 16] = 0x80;
    memset(block_bits, 0xFF, sizeof(block_bits));
    /* "abc" is 24 pixels wide, 16 high, in a 101 x 41 box at (10, 20). */
    struct rect r = { 10, 20, 101, 41 };
    setup();
    ui_label_draw(&cv, r, &corner, "abc", UI_TEXT, UI_ALIGN_LEFT);
    CHECK_EQ(at(10, 32), UI_TEXT);                  /* y = 20 + (41 - 16) / 2 = 32 */
    CHECK_EQ(at(26, 32), UI_TEXT);
    CHECK_EQ(count(UI_TEXT, all), 3);
    setup();
    ui_label_draw(&cv, r, &corner, "abc", UI_TEXT, UI_ALIGN_CENTRE);
    CHECK_EQ(at(48, 32), UI_TEXT);                  /* x = 10 + (101 - 24) / 2 = 48 */
    setup();
    ui_label_draw(&cv, r, &corner, "abc", UI_TEXT, UI_ALIGN_RIGHT);
    CHECK_EQ(at(87, 32), UI_TEXT);                  /* x = 10 + 101 - 24 = 87 */
    CHECK_EQ(at(103, 32), UI_TEXT);
    /* A box lower than the font: y = 20 + floor((5 - 16) / 2) = 14,
     * outside the box: nothing drawn (clipped to the box). */
    setup();
    ui_label_draw(&cv, (struct rect){ 10, 20, 101, 5 }, &block, "abc", UI_TEXT, UI_ALIGN_CENTRE);
    CHECK_EQ(count(UI_TEXT, all), 24 * 5);          /* the box's 5 rows of the 24 columns */
    CHECK_EQ(at(48, 20), UI_TEXT);
    CHECK(at(48, 19) != UI_TEXT && at(48, 25) != UI_TEXT);
    /* Wider than the box (centred: x = 10 + floor((4 - 24) / 2) = 0): clipped to it. */
    setup();
    ui_label_draw(&cv, (struct rect){ 10, 20, 4, 16 }, &block, "abc", UI_TEXT, UI_ALIGN_CENTRE);
    CHECK_EQ(count(UI_TEXT, all), 4 * 16);
    CHECK_EQ(at(10, 20), UI_TEXT);
    CHECK(at(9, 20) != UI_TEXT);
}

static void test_styles(void)
{
    CHECK_EQ(ui_style_close.fill[UI_BUTTON_NORMAL], UI_COLOUR_NONE);
    CHECK_EQ(ui_style_close.fill[UI_BUTTON_HOVER], UI_DANGER);
    CHECK_EQ(ui_style_close.fill[UI_BUTTON_PRESSED], UI_DANGER_PRESSED);
    CHECK_EQ(ui_style_close.fg[UI_BUTTON_PRESSED], UI_TEXT);
    CHECK_EQ(ui_style_secondary.fill[UI_BUTTON_NORMAL], UI_SURFACE_ACTIVE);
    CHECK_EQ(ui_style_primary.fill[UI_BUTTON_NORMAL], UI_ACCENT);
    CHECK_EQ(ui_style_primary.fg[UI_BUTTON_NORMAL], UI_TEXT_ON_ACCENT);
    CHECK(ui_style_primary.bold && !ui_style_secondary.bold);
    CHECK_EQ(ui_style_key.fill[UI_BUTTON_PRESSED], UI_ACCENT);
    CHECK_EQ(ui_style_key.fg[UI_BUTTON_PRESSED], UI_TEXT_ON_ACCENT);
    CHECK_EQ(ui_style_key_equals.fill[UI_BUTTON_NORMAL], UI_ACCENT);
    CHECK_EQ(ui_style_secondary.radius_dp, 4);
    /* The states the guide leaves open. */
    CHECK_EQ(ui_style_secondary.fill[UI_BUTTON_HOVER], UI_COLOUR_UNSET);
    CHECK_EQ(ui_style_secondary.fill[UI_BUTTON_PRESSED], UI_COLOUR_UNSET);
    CHECK_EQ(ui_style_primary.fill[UI_BUTTON_HOVER], UI_COLOUR_UNSET);
    CHECK_EQ(ui_style_primary.fill[UI_BUTTON_PRESSED], UI_COLOUR_UNSET);
    CHECK_EQ(ui_style_key.fill[UI_BUTTON_HOVER], UI_COLOUR_UNSET);
    CHECK_EQ(ui_style_key_equals.fill[UI_BUTTON_HOVER], UI_COLOUR_UNSET);
}

static void test_refused(void)
{
    struct ui_button b = { { 20, 20, 96, 32 }, "Cancel", UI_ICON_NONE, 0 };
    setup();
    CHECK(!ui_button_draw(&cv, &b, &ui_style_secondary, UI_BUTTON_HOVER, false, 4, &fonts));
    CHECK(!ui_button_draw(&cv, &b, &ui_style_primary, UI_BUTTON_PRESSED, true, 4, &fonts));
    CHECK(!ui_button_draw(&cv, &b, &ui_style_key, UI_BUTTON_HOVER, false, 4, &fonts));
    CHECK(!ui_button_draw(&cv, &b, &ui_style_key, (enum ui_button_state)3, false, 4, &fonts));
    b.icon = UI_ICON_COUNT;
    CHECK(!ui_button_draw(&cv, &b, &ui_style_key, UI_BUTTON_NORMAL, false, 4, &fonts));
    b.icon = -2;
    CHECK(!ui_button_draw(&cv, &b, &ui_style_key, UI_BUTTON_NORMAL, false, 4, &fonts));
    CHECK_EQ(count(BG, all), SW * SH);              /* nothing drawn */
}

static void test_close(void)
{
    struct ui_button b = { { 40, 30, 32, 32 }, NULL, UI_ICON_CLOSE, 16 };
    setup();
    CHECK(ui_button_draw(&cv, &b, &ui_style_close, UI_BUTTON_NORMAL, false, 4, &fonts));
    CHECK_EQ(at(40, 30), BG);                       /* no fill */
    CHECK_EQ(at(71, 61), BG);
    CHECK(count(BG, b.r) < 32 * 32);                /* the icon */
    CHECK_EQ(count(BG, (struct rect){ 40, 30, 8, 32 }), 8 * 32);   /* the 16 dp icon is centred */
    setup();
    CHECK(ui_button_draw(&cv, &b, &ui_style_close, UI_BUTTON_HOVER, false, 4, &fonts));
    CHECK_EQ(at(40, 30), UI_DANGER);                /* square: the corner filled */
    CHECK_EQ(at(71, 61), UI_DANGER);
    CHECK_EQ(at(39, 30), BG);
    CHECK_EQ(at(56, 46), UI_TEXT);                  /* the strokes' crossing */
    setup();
    CHECK(ui_button_draw(&cv, &b, &ui_style_close, UI_BUTTON_PRESSED, false, 8, &fonts));
    CHECK_EQ(at(40, 30), UI_DANGER_PRESSED);
    CHECK_EQ(count(UI_DANGER, all), 0);
}

static void test_key_and_ring(void)
{
    struct ui_button b = { { 50, 40, 52, 40 }, "7", UI_ICON_NONE, 0 };
    setup();
    CHECK(ui_button_draw(&cv, &b, &ui_style_key, UI_BUTTON_PRESSED, false, 4, &fonts));
    CHECK_EQ(at(50, 40), BG);                       /* the 4 dp corner left out */
    CHECK_EQ(at(53, 40), UI_ACCENT);
    CHECK_EQ(at(50 + 22, 40 + 12), UI_TEXT_ON_ACCENT);    /* "7"'s pixel: (52 - 8) / 2, (40 - 16) / 2 */
    CHECK_EQ(count(UI_TEXT_ON_ACCENT, b.r), 1);
    /* Focused at 100 %: the ring 2 px wide, 2 px outside; the gap untouched. */
    setup();
    CHECK(ui_button_draw(&cv, &b, &ui_style_key, UI_BUTTON_NORMAL, true, 4, &fonts));
    CHECK_EQ(at(50 + 26, 40 - 1), BG);              /* the gap */
    CHECK_EQ(at(50 + 26, 40 - 2), BG);
    CHECK_EQ(at(50 + 26, 40 - 3), UI_ACCENT);       /* the ring */
    CHECK_EQ(at(50 + 26, 40 - 4), UI_ACCENT);
    CHECK_EQ(at(50 + 26, 40 - 5), BG);
    CHECK_EQ(at(50 + 52 + 2, 60), UI_ACCENT);
    CHECK_EQ(at(50 + 52 + 1, 60), BG);
    CHECK_EQ(at(50 + 26, 40 + 40 + 3), UI_ACCENT);
    CHECK_EQ(count(UI_ACCENT, b.r), 0);             /* normal: not filled with accent */
    CHECK(count(UI_ACCENT, all) > 2 * 2 * (52 + 40));
    /* At 125 %: g = w = ui_px(2, 5) = 3. */
    setup();
    CHECK(ui_button_draw(&cv, &b, &ui_style_key, UI_BUTTON_NORMAL, true, 5, &fonts));
    CHECK_EQ(at(76, 37), BG);
    CHECK_EQ(at(76, 36), UI_ACCENT);
    CHECK_EQ(at(76, 34), UI_ACCENT);
    CHECK_EQ(at(76, 33), BG);
    /* A bold label comes from the bold font (here every pixel). */
    struct ui_button p = { { 20, 20, 96, 32 }, "Restart", UI_ICON_NONE, 0 };
    setup();
    CHECK(ui_button_draw(&cv, &p, &ui_style_primary, UI_BUTTON_NORMAL, false, 4, &fonts));
    CHECK_EQ(count(UI_TEXT_ON_ACCENT, p.r), 7 * 8 * 16);
}

int main(void)
{
    printf("test_ui_widget\n");
    RUN(test_label);
    RUN(test_styles);
    RUN(test_refused);
    RUN(test_close);
    RUN(test_key_and_ring);
    return unit_report("test_ui_widget");
}
