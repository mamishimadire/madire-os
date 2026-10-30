/* Unit tests for kernel/display/ui_menu.c (menus, 1.3 stage 5): the two
 * menus' sizes at every scale; placement (the M menu 8 dp from the left
 * and 8 dp above the taskbar; the desktop menu at the hot spot, moved left
 * and up to stay on the screen and above the taskbar, never past 0); the
 * rows' rectangles and hit testing (the edge, the header, the dividers and
 * the padding hit no row); the keyboard's selection (Down and Up from none,
 * wrapping); each row's label, icon and action; and the picture: the
 * panel, its edge and corners, a divider, the selected row's fill, bar and
 * focus ring, the M menu's wordmark drawn whole, and a menu painted in
 * tiles equal, byte for byte, to the menu painted whole. Fonts are made
 * here (a fixed bit pattern). The toolkit's modules are included directly;
 * surface_core.c, wallpaper_core.c and coverage_core.c are linked (the
 * Makefile). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/ui_widget.c"
#include "kernel/display/ui_menu.c"
#include "kernel/display/ui_about.h"

static uint8_t font_bits[8][256 * 32 * 2];
static struct font font_store[8];

static struct ui_fonts fonts_for(uint32_t s)
{
    static const uint8_t w[4] = { 8, 10, 12, 16 }, h[4] = { 16, 20, 24, 32 };
    int i = s == 4 ? 0 : s == 5 ? 1 : s == 6 ? 2 : 3;
    for (int b = 0; b < 2; b++) {
        struct font *f = &font_store[2 * i + b];
        if (!f->bits) {
            uint8_t rb = (uint8_t)((w[i] + 7) / 8);
            for (uint32_t k = 0; k < 256u * h[i] * rb; k++)
                font_bits[2 * i + b][k] = (uint8_t)((k * (37u + 6u * b) + (k >> 3)) & (b ? 0xB5u : 0xA5u));
            *f = (struct font){ w[i], h[i], rb, font_bits[2 * i + b] };
        }
    }
    return (struct ui_fonts){ &font_store[2 * i], &font_store[2 * i + 1] };
}

static bool same_rect(struct rect a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

static uint16_t scratch[UI_ABOUT_SCRATCH];

static void test_sizes_and_rows(void)
{
    int32_t w = 0, h = 0;
    ui_menu_size(UI_MENU_M, 4, &w, &h);
    CHECK(w == 288 && h == 182);
    ui_menu_size(UI_MENU_M, 5, &w, &h);
    CHECK(w == 360 && h == 228);
    ui_menu_size(UI_MENU_DESKTOP, 8, &w, &h);
    CHECK(w == 448 && h == 160);
    CHECK_EQ(ui_menu_rows(UI_MENU_M), 3);
    CHECK_EQ(ui_menu_rows(UI_MENU_DESKTOP), 2);
    CHECK(strcmp(ui_menu_label(UI_MENU_M, 0), "About") == 0 && strcmp(ui_menu_label(UI_MENU_M, 2), "Restart") == 0);
    CHECK(strcmp(ui_menu_label(UI_MENU_DESKTOP, 0), "Open Console") == 0);
    CHECK(strcmp(ui_menu_label(UI_MENU_DESKTOP, 1), "About Madire OS") == 0);
    CHECK_EQ(ui_menu_action(UI_MENU_M, 1), UI_MENU_CONSOLE);
    CHECK_EQ(ui_menu_action(UI_MENU_M, 2), UI_MENU_RESTART);
    CHECK_EQ(ui_menu_action(UI_MENU_DESKTOP, 0), UI_MENU_CONSOLE);
    CHECK_EQ(ui_menu_action(UI_MENU_DESKTOP, 1), UI_MENU_ABOUT);
    CHECK_EQ(ui_menu_action(UI_MENU_DESKTOP, 2), UI_MENU_NONE);
    CHECK_EQ(ui_menu_icon(UI_MENU_M, 2), UI_ICON_RESTART);
    CHECK_EQ(ui_menu_icon(UI_MENU_DESKTOP, 0), UI_ICON_CONSOLE);
    CHECK_EQ(ui_menu_icon(UI_MENU_M, 5), UI_ICON_NONE);
}

static void test_placement(void)
{
    CHECK(same_rect(ui_menu_place_m(4, 728), 8, 538, 288, 182));
    CHECK(same_rect(ui_menu_place_m(8, 1360), 16, 1360 - 16 - 364, 576, 364));
    CHECK(same_rect(ui_menu_place_m(4, 100), 8, 0, 288, 182));             /* never above the screen */
    CHECK(same_rect(ui_menu_place_at(4, 1024, 728, 10, 20), 10, 20, 224, 80));
    CHECK(same_rect(ui_menu_place_at(4, 1024, 728, 900, 20), 800, 20, 224, 80));   /* moved left */
    CHECK(same_rect(ui_menu_place_at(4, 1024, 728, 10, 700), 10, 648, 224, 80));   /* moved up, above the taskbar */
    CHECK(same_rect(ui_menu_place_at(4, 1024, 728, 1023, 727), 800, 648, 224, 80));
    CHECK(same_rect(ui_menu_place_at(4, 200, 60, 150, 50), 0, 0, 224, 80));       /* never past 0 */
    CHECK(same_rect(ui_menu_place_at(5, 800, 550, 700, 500), 520, 450, 280, 100));
}

static void test_rows_hit(void)
{
    struct ui_menu m;
    ui_menu_init(&m, UI_MENU_M, 4, ui_menu_place_m(4, 728));
    CHECK(m.sel == -1 && !m.ring);
    CHECK(same_rect(ui_menu_row_rect(&m, 0), 9, 599, 286, 36));
    CHECK(same_rect(ui_menu_row_rect(&m, 1), 9, 635, 286, 36));
    CHECK(same_rect(ui_menu_row_rect(&m, 2), 9, 680, 286, 36));
    CHECK(ui_rect_empty(ui_menu_row_rect(&m, 3)));
    CHECK(ui_rect_empty(ui_menu_row_rect(&m, -1)));
    CHECK_EQ(ui_menu_row_at(&m, 152, 617), 0);
    CHECK_EQ(ui_menu_row_at(&m, 152, 634), 0);
    CHECK_EQ(ui_menu_row_at(&m, 152, 635), 1);
    CHECK_EQ(ui_menu_row_at(&m, 152, 698), 2);
    CHECK_EQ(ui_menu_row_at(&m, 152, 560), -1);     /* the header */
    CHECK_EQ(ui_menu_row_at(&m, 152, 675), -1);     /* the divider's gap */
    CHECK_EQ(ui_menu_row_at(&m, 8, 617), -1);       /* the edge */
    CHECK_EQ(ui_menu_row_at(&m, 294, 617), 0);
    CHECK_EQ(ui_menu_row_at(&m, 295, 617), -1);
    CHECK_EQ(ui_menu_row_at(&m, 152, 717), -1);     /* the bottom padding */
    CHECK_EQ(ui_menu_row_at(&m, 152, 760), -1);     /* outside */
    struct ui_menu d;
    ui_menu_init(&d, UI_MENU_DESKTOP, 5, (struct rect){ 100, 100, 280, 100 });
    CHECK(same_rect(ui_menu_row_rect(&d, 0), 101, 105, 278, 45));
    CHECK(same_rect(ui_menu_row_rect(&d, 1), 101, 150, 278, 45));
    CHECK_EQ(ui_menu_row_at(&d, 200, 104), -1);
    CHECK_EQ(ui_menu_row_at(&d, 200, 160), 1);
}

static void test_keyboard(void)
{
    struct ui_menu m;
    ui_menu_init(&m, UI_MENU_M, 4, ui_menu_place_m(4, 728));
    CHECK_EQ(ui_menu_next(&m, 1), 0);
    CHECK_EQ(ui_menu_next(&m, -1), 2);
    m.sel = 0;
    CHECK_EQ(ui_menu_next(&m, 1), 1);
    CHECK_EQ(ui_menu_next(&m, -1), 2);              /* wraps */
    m.sel = 2;
    CHECK_EQ(ui_menu_next(&m, 1), 0);
    CHECK_EQ(ui_menu_next(&m, -1), 1);
    struct ui_menu d;
    ui_menu_init(&d, UI_MENU_DESKTOP, 4, ui_menu_place_at(4, 1024, 728, 0, 0));
    d.sel = 1;
    CHECK_EQ(ui_menu_next(&d, 1), 0);
    CHECK_EQ(ui_menu_next(&d, -1), 0);
}

static struct surface screen(uint32_t w, uint32_t h, uint32_t bpp)
{
    struct surface s;
    uint8_t *px = calloc((size_t)w * h, bpp);
    surface_init(&s, px, w, h, w * bpp, bpp);
    struct ui_canvas c;
    ui_canvas_init(&c, &s);
    ui_fill(&c, (struct rect){ 0, 0, (int32_t)w, (int32_t)h }, UI_BG_DEEP);
    return s;
}

static void test_picture(void)
{
    struct ui_fonts f = fonts_for(4);
    struct surface s = screen(1024, 768, 4);
    struct ui_canvas c;
    ui_canvas_init(&c, &s);
    struct ui_menu m;
    ui_menu_init(&m, UI_MENU_M, 4, ui_menu_place_m(4, 728));
    m.sel = 1;
    m.ring = true;
    CHECK(ui_menu_scratch(4, 1024, 768) <= UI_ABOUT_SCRATCH);
    CHECK(ui_menu_draw(&c, &m, &f, scratch, UI_ABOUT_SCRATCH));
    CHECK_EQ(surface_get(&s, 8, 538), UI_BG_DEEP);             /* outside the rounded corner */
    CHECK_EQ(surface_get(&s, 108, 538), UI_BORDER_PANEL);      /* the edge */
    CHECK_EQ(surface_get(&s, 208, 542), UI_SURFACE);           /* the header, clear of its M and wordmark */
    CHECK_EQ(surface_get(&s, 108, 594), UI_BORDER);            /* the divider at 56 dp */
    CHECK_EQ(surface_get(&s, 18, 594), UI_SURFACE);            /* ... from 16 dp across */
    CHECK_EQ(surface_get(&s, 9, 640), UI_ACCENT);              /* the selected row's bar */
    CHECK_EQ(surface_get(&s, 9, 637), UI_SURFACE_ACTIVE);      /* ... inset 4 dp: the row's fill */
    CHECK_EQ(surface_get(&s, 11, 650), UI_ACCENT);             /* the focus ring's left side */
    CHECK_EQ(surface_get(&s, 150, 635), UI_ACCENT);            /* ... its top */
    CHECK_EQ(surface_get(&s, 150, 637), UI_SURFACE_ACTIVE);
    CHECK_EQ(surface_get(&s, 150, 610), UI_SURFACE);           /* a row not selected */
    CHECK_EQ(surface_get(&s, 150, 719), UI_BORDER_PANEL);      /* the bottom edge */
    /* The same menu painted in tiles of 41 x 23 from (-5, -9). */
    struct surface t = screen(1024, 768, 4);
    for (int32_t y = -9; y < 768; y += 23)
        for (int32_t x = -5; x < 1024; x += 41) {
            struct ui_canvas k;
            ui_canvas_init(&k, &t);
            ui_canvas_clip(&k, (struct rect){ x, y, 41, 23 });
            ui_menu_draw(&k, &m, &f, scratch, UI_ABOUT_SCRATCH);
        }
    CHECK(memcmp(s.pixels, t.pixels, (size_t)1024 * 768 * 4) == 0);
    /* The ring off; the desktop menu at 150 % on a 3-byte screen. */
    m.ring = false;
    ui_menu_draw(&c, &m, &f, scratch, UI_ABOUT_SCRATCH);
    CHECK_EQ(surface_get(&s, 150, 635), UI_SURFACE_ACTIVE);
    struct ui_fonts f6 = fonts_for(6);
    struct surface u = screen(1280, 800, 3);
    struct ui_canvas cu;
    ui_canvas_init(&cu, &u);
    struct ui_menu d;
    ui_menu_init(&d, UI_MENU_DESKTOP, 6, ui_menu_place_at(6, 1280, 740, 1270, 730));
    CHECK(same_rect(d.r, 1280 - 336, 740 - 120, 336, 120));
    CHECK(ui_menu_draw(&cu, &d, &f6, scratch, UI_ABOUT_SCRATCH));
    CHECK_EQ(surface_get(&u, (uint32_t)d.r.x + 300, (uint32_t)d.r.y + 20), UI_SURFACE);
    CHECK_EQ(surface_get(&u, (uint32_t)d.r.x + 300, (uint32_t)d.r.y), UI_BORDER_PANEL);
    free(s.pixels);
    free(t.pixels);
    free(u.pixels);
}

int main(void)
{
    printf("test_ui_menu\n");
    RUN(test_sizes_and_rows);
    RUN(test_placement);
    RUN(test_rows_hit);
    RUN(test_keyboard);
    RUN(test_picture);
    return unit_report("test_ui_menu");
}
