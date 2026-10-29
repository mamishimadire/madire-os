/* Unit tests for kernel/display/desktop_core.c (the desktop's pictures and
 * its memory plan, 1.3 stage 3): the background cache's plan (its pages,
 * pitch and tables at both pixel formats; fitting exactly at the quarter
 * share and over it by one page; the pages already taken counted); the
 * layout in the five static modes (the taskbar along the bottom, the M
 * cell, the clock 16 dp from the right edge, the logo exactly at the
 * splash's place and size); the background drawn in tiles equal, byte
 * for byte, to the background drawn at once (the per-damaged-rectangle
 * path against the cached one), at 4 and 3 bytes per pixel; the clock's
 * box redrawn alone equal to the taskbar drawn at once with the new time.
 * Fonts of the right sizes are made here (a fixed bit
 * pattern), so the suite needs no generated font. The pixel-exact
 * comparison with tests/desktop_ref.py is part static's. The toolkit's
 * modules are included directly; surface_core.c, wallpaper_core.c and
 * coverage_core.c are linked as separate files (the Makefile). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/desktop_core.c"

static uint8_t font_bits[4][256 * 32 * 2];
static struct font fonts[4];

static const struct font *font_for(uint32_t s)
{
    static const uint8_t w[4] = { 8, 10, 12, 16 }, h[4] = { 16, 20, 24, 32 };
    int i = s == 4 ? 0 : s == 5 ? 1 : s == 6 ? 2 : 3;
    if (!fonts[i].bits) {
        uint8_t rb = (uint8_t)((w[i] + 7) / 8);
        for (uint32_t k = 0; k < 256u * h[i] * rb; k++)
            font_bits[i][k] = (uint8_t)((k * 37u + (k >> 3)) & 0xA5u);
        fonts[i] = (struct font){ w[i], h[i], rb, font_bits[i] };
    }
    return &fonts[i];
}

static void test_plan(void)
{
    struct desktop_cache_plan p;
    /* 1024 x 768 x 32: 768 pages, 1 directory + 2 page tables. */
    CHECK(desktop_plan_cache(1024, 768, 4, 771, 4 * (771 + 768 + 3), &p));
    CHECK_EQ(p.pages, 768);
    CHECK_EQ(p.pitch, 4096);
    CHECK_EQ(p.tables, 3);
    CHECK_EQ(p.display, 771 + 768 + 3);
    CHECK_EQ(p.share, 771 + 768 + 3);
    CHECK_EQ(p.over, 0);
    /* One free page less: the share (rounded down) is one page short; four less, still one; five, two. */
    CHECK(desktop_plan_cache(1024, 768, 4, 771, 4 * (771 + 768 + 3) - 1, &p) == false);
    CHECK_EQ(p.over, 1);
    CHECK(!desktop_plan_cache(1024, 768, 4, 771, 4 * (771 + 768 + 3) - 4, &p));
    CHECK_EQ(p.over, 1);
    CHECK(!desktop_plan_cache(1024, 768, 4, 771, 4 * (771 + 768 + 3) - 5, &p));
    CHECK_EQ(p.over, 2);
    /* At 24 bpp: 1024 x 3 x 768 bytes = 576 pages. */
    CHECK(desktop_plan_cache(1024, 768, 3, 0, 100000, &p));
    CHECK_EQ(p.pages, 576);
    CHECK_EQ(p.pitch, 3072);
    CHECK_EQ(p.tables, 3);
    /* 800 x 600 x 24: 1,440,000 bytes, rounded up to 352 pages. */
    desktop_plan_cache(800, 600, 3, 0, 100000, &p);
    CHECK_EQ(p.pages, 352);
    CHECK_EQ(p.tables, 2);
    /* 2560 x 1440 x 32 at 200 %: 3600 pages, 1 + 8 tables; a small machine refuses it. */
    CHECK(!desktop_plan_cache(2560, 1440, 4, 3600 + 10, 28000, &p));
    CHECK_EQ(p.pages, 3600);
    CHECK_EQ(p.tables, 9);
    CHECK_EQ(p.share, 7000);
    CHECK_EQ(p.over, 3610 + 3609 - 7000);
    CHECK(desktop_plan_cache(2560, 1440, 4, 3610, 4 * 7219, &p));
}

static const uint32_t modes[5][3] = {
    { 800, 600, 4 }, { 1024, 768, 4 }, { 1280, 800, 4 }, { 1920, 1080, 6 }, { 2560, 1440, 8 },
};

static void test_layout(void)
{
    for (int i = 0; i < 5; i++) {
        uint32_t w = modes[i][0], h = modes[i][1], s = modes[i][2];
        struct desktop_layout l;
        CHECK(desktop_layout(&l, w, h, s, font_for(s)));
        int32_t bar = ui_px(40, s);
        CHECK(l.bar.x == 0 && l.bar.y == (int32_t)h - bar && l.bar.w == (int32_t)w && l.bar.h == bar);
        CHECK(l.bar_edge.y == l.bar.y && l.bar_edge.h == ui_px(1, s));
        CHECK(l.m_cell.w == bar && l.m_cell.h == bar && l.m_px == ui_px(24, s));
        CHECK_EQ(l.clock_box.x + l.clock_box.w, (int32_t)w - ui_px(16, s));
        CHECK_EQ(l.clock_box.w, 5 * font_for(s)->width);
        struct splash_layout sl;
        CHECK(splash_layout(&sl, w, h, s, font_for(s)->width, font_for(s)->height));
        CHECK(l.ls == sl.ls && l.lox == sl.lox && l.loy == sl.loy);       /* the splash's M, unmoved */
    }
    struct desktop_layout l;
    CHECK(!desktop_layout(&l, 800, 140, 4, font_for(4)));       /* no room above the taskbar */
}

/* The background and the taskbar drawn at once, against tiles of odd
 * sizes (the no-cache path: each damaged rectangle drawn again). */
static void test_tiles(uint32_t bpp)
{
    uint32_t w = 800, h = 600;
    struct desktop_layout l;
    struct wallpaper wp;
    desktop_layout(&l, w, h, 4, font_for(4));
    wallpaper_setup(&wp, w, h, 4);
    uint8_t *a = calloc((size_t)w * h, bpp), *b = calloc((size_t)w * h, bpp);
    struct surface sa, sb;
    surface_init(&sa, a, w, h, w * bpp, bpp);
    surface_init(&sb, b, w, h, w * bpp, bpp);
    struct rect all = { 0, 0, (int32_t)w, (int32_t)h };
    desktop_draw_background(&sa, &l, &wp, all);
    desktop_draw_taskbar(&sa, &l, all, "12:34");
    for (int32_t y = -13; y < (int32_t)h; y += 61)
        for (int32_t x = -7; x < (int32_t)w; x += 97) {
            struct rect r = { x, y, 97, 61 };
            desktop_draw_background(&sb, &l, &wp, r);
            desktop_draw_taskbar(&sb, &l, r, "12:34");
        }
    CHECK(memcmp(a, b, (size_t)w * h * bpp) == 0);

    /* The clock: drawn with another time, then its box alone again. */
    desktop_draw_taskbar(&sb, &l, all, "88:88");
    CHECK(memcmp(a, b, (size_t)w * h * bpp) != 0);
    desktop_draw_background(&sb, &l, &wp, l.clock_box);
    desktop_draw_taskbar(&sb, &l, l.clock_box, "12:34");
    CHECK(memcmp(a, b, (size_t)w * h * bpp) == 0);
    free(a);
    free(b);
}

static void test_tiles_32(void)
{
    test_tiles(4);
}

static void test_tiles_24(void)
{
    test_tiles(3);
}

int main(void)
{
    printf("test_ui_desktop\n");
    RUN(test_plan);
    RUN(test_layout);
    RUN(test_tiles_32);
    RUN(test_tiles_24);
    return unit_report("test_ui_desktop");
}
