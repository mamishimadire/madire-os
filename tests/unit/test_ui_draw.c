/* Unit tests for kernel/display/ui_draw.c (the toolkit's drawing, 1.3
 * stage 2): dp to pixels; fills and clipping (the surface's edges and the
 * canvas's clip, nothing written outside, at 4 and 3 bytes per pixel);
 * rounded rectangles (the quarter-circles' insets worked out by hand for
 * radii 1 to 4, the radius clamped, each corner alone and the corners'
 * mirror symmetry); frames (equal to the rounded rectangle less its inner
 * one, a width past the middle a fill); blending's roundings; text with
 * made-up fonts (each glyph's pixels where they belong, clipped). The
 * pixel-exact comparison with tests/desktop_ref.py is part static's
 * (tests/desktopdump.c). The modules are included directly, except
 * surface_core.c and wallpaper_core.c, linked as separate files (the
 * Makefile), so they never share a translation unit with the toolkit
 * (their statics, and splash_data.h, which has no include guard). */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"

#define W     40
#define H     30
#define PAD   16                /* guard bytes after each row and around the buffer */
#define GUARD 0xA5
#define BG    0x102030u

static uint8_t buf[2 * PAD + H * (W * 4 + PAD)];
static struct surface surf;
static struct ui_canvas cv;

static void setup(uint32_t bpp)
{
    memset(buf, GUARD, sizeof(buf));
    surface_init(&surf, buf + PAD, W, H, W * bpp + PAD, bpp);
    ui_canvas_init(&cv, &surf);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *p = surf.pixels + y * surf.pitch + x * bpp;
            p[0] = BG & 0xFF;
            p[1] = BG >> 8 & 0xFF;
            p[2] = BG >> 16;
            if (bpp == 4)
                p[3] = 0;
        }
}

/* The guard bytes are untouched. */
static int guards_ok(void)
{
    for (int i = 0; i < PAD; i++)
        if (buf[i] != GUARD || buf[sizeof(buf) - 1 - i] != GUARD)
            return 0;
    for (int y = 0; y < H; y++)
        for (uint32_t i = W * surf.bytes_per_pixel; i < surf.pitch; i++)
            if (surf.pixels[y * surf.pitch + i] != GUARD)
                return 0;
    return 1;
}

static uint32_t at(int x, int y)
{
    return surface_get(&surf, (uint32_t)x, (uint32_t)y);
}

static int count(uint32_t rgb)
{
    int n = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            n += at(x, y) == rgb;
    return n;
}

static void test_px(void)
{
    CHECK_EQ(ui_px(1, 4), 1);
    CHECK_EQ(ui_px(1, 5), 1);       /* 1.75 -> 1 */
    CHECK_EQ(ui_px(2, 5), 3);       /* 3 */
    CHECK_EQ(ui_px(6, 5), 8);       /* 7.5 + 0.5 -> 8 */
    CHECK_EQ(ui_px(2, 6), 3);
    CHECK_EQ(ui_px(6, 6), 9);
    CHECK_EQ(ui_px(24, 8), 48);
    CHECK_EQ(ui_px(40, 5), 50);
    CHECK_EQ(ui_px(0, 8), 0);
}

static void test_fill_clip(void)
{
    for (uint32_t bpp = 3; bpp <= 4; bpp++) {
        setup(bpp);
        ui_fill(&cv, (struct rect){ -5, -5, 10, 10 }, 0xFF0000);
        ui_fill(&cv, (struct rect){ W - 3, H - 2, 50, 50 }, 0x00FF00);
        ui_fill(&cv, (struct rect){ -100, 10, 1000, 1 }, 0x0000FF);
        ui_fill(&cv, (struct rect){ 3, 3, 0, 10 }, 0xFFFFFF);
        CHECK_EQ(count(0xFF0000), 25);
        CHECK_EQ(count(0x00FF00), 6);
        CHECK_EQ(count(0x0000FF), W);
        CHECK_EQ(count(0xFFFFFF), 0);
        CHECK(guards_ok());
        /* The canvas's clip. */
        ui_canvas_clip(&cv, (struct rect){ 10, 12, 5, 4 });
        CHECK_EQ(cv.clip.w, 5);
        ui_fill(&cv, (struct rect){ 0, 0, W, H }, 0x123456);
        CHECK_EQ(count(0x123456), 20);
        CHECK_EQ(at(10, 12), 0x123456);
        CHECK_EQ(at(14, 15), 0x123456);
        CHECK(at(15, 15) != 0x123456);
        ui_canvas_clip(&cv, (struct rect){ W + 5, 0, 5, 5 });   /* outside: nothing drawn */
        CHECK(ui_rect_empty(cv.clip));
        ui_fill(&cv, (struct rect){ 0, 0, W, H }, 0x654321);
        ui_fill_round(&cv, (struct rect){ 0, 0, W, H }, 4, UI_CORNERS_ALL, 0x654321);
        ui_blend(&cv, 1, 1, 0x654321, 256);
        CHECK_EQ(count(0x654321), 0);
        ui_canvas_clip(&cv, (struct rect){ -10, -10, 1000, 1000 });
        CHECK(cv.clip.x == 0 && cv.clip.y == 0 && cv.clip.w == W && cv.clip.h == H);
        CHECK(guards_ok());
    }
}

/* The pixels left out of one corner of a 20 x 20 rectangle at (5, 5). */
static int corner_cut(int32_t radius, uint32_t corner)
{
    setup(4);
    ui_fill_round(&cv, (struct rect){ 5, 5, 20, 20 }, radius, corner, 0xFFFFFF);
    return 400 - count(0xFFFFFF);
}

static void test_round(void)
{
    /* By hand: R = 1 and 3 leave out the corner pixel only for 3; 2 one
     * pixel; 4 three (rows 0 and 1 of the corner square: 2 and 1). */
    CHECK_EQ(corner_cut(0, UI_CORNER_TL), 0);
    CHECK_EQ(corner_cut(1, UI_CORNER_TL), 0);
    CHECK_EQ(corner_cut(2, UI_CORNER_TL), 1);
    CHECK_EQ(corner_cut(3, UI_CORNER_TL), 1);
    CHECK_EQ(corner_cut(4, UI_CORNER_TL), 3);
    setup(4);
    ui_fill_round(&cv, (struct rect){ 5, 5, 20, 20 }, 4, UI_CORNER_TL, 0xFFFFFF);
    CHECK(at(5, 5) != 0xFFFFFF && at(6, 5) != 0xFFFFFF && at(7, 5) == 0xFFFFFF);
    CHECK(at(5, 6) != 0xFFFFFF && at(6, 6) == 0xFFFFFF && at(5, 7) == 0xFFFFFF);
    /* Each corner alone cuts the same; all four cut four times as much. */
    int one = corner_cut(6, UI_CORNER_TL);
    CHECK(one > 3);
    CHECK_EQ(corner_cut(6, UI_CORNER_TR), one);
    CHECK_EQ(corner_cut(6, UI_CORNER_BL), one);
    CHECK_EQ(corner_cut(6, UI_CORNER_BR), one);
    CHECK_EQ(corner_cut(6, UI_CORNERS_ALL), 4 * one);
    CHECK_EQ(corner_cut(6, 0), 0);
    /* Mirror symmetry of all four corners. */
    setup(4);
    ui_fill_round(&cv, (struct rect){ 3, 2, 31, 25 }, 9, UI_CORNERS_ALL, 0xFFFFFF);
    int asym = 0;
    for (int y = 2; y < 27; y++)
        for (int x = 3; x < 34; x++)
            asym += (at(x, y) == 0xFFFFFF) != (at(3 + 33 - x, y) == 0xFFFFFF) ||
                    (at(x, y) == 0xFFFFFF) != (at(x, 2 + 26 - y) == 0xFFFFFF);
    CHECK_EQ(asym, 0);
    /* The radius is clamped to half the shorter side: 100 acts as 5. */
    int clamped = corner_cut(100, UI_CORNERS_ALL);
    setup(4);
    ui_fill_round(&cv, (struct rect){ 5, 5, 20, 10 }, 100, UI_CORNERS_ALL, 0xFFFFFF);
    int n100 = count(0xFFFFFF);
    setup(4);
    ui_fill_round(&cv, (struct rect){ 5, 5, 20, 10 }, 5, UI_CORNERS_ALL, 0xFFFFFF);
    CHECK_EQ(n100, count(0xFFFFFF));
    CHECK_EQ(clamped, corner_cut(10, UI_CORNERS_ALL));
    CHECK(corner_cut(-3, UI_CORNERS_ALL) == 0);
    /* Clipped by the surface: the visible part is unchanged. */
    setup(4);
    ui_fill_round(&cv, (struct rect){ -10, -10, 30, 30 }, 12, UI_CORNERS_ALL, 0xFFFFFF);
    int clip_n = count(0xFFFFFF);
    setup(4);
    ui_canvas_clip(&cv, (struct rect){ 0, 0, 20, 20 });
    ui_fill_round(&cv, (struct rect){ -10, -10, 30, 30 }, 12, UI_CORNERS_ALL, 0xFFFFFF);
    CHECK_EQ(count(0xFFFFFF), clip_n);
    CHECK(guards_ok());
}

static void test_frame(void)
{
    static const int32_t cases[][2] = { { 0, 1 }, { 4, 1 }, { 6, 2 }, { 9, 3 }, { 2, 4 }, { 12, 5 } };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int32_t R = cases[i][0], w = cases[i][1];
        struct rect r = { 4, 3, 30, 22 };
        /* The frame equals the rounded rectangle less the inner one. */
        setup(4);
        ui_frame_round(&cv, r, R, w, UI_CORNERS_ALL, 0xFFFFFF);
        uint8_t frame[H][W];
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                frame[y][x] = at(x, y) == 0xFFFFFF;
        setup(4);
        ui_fill_round(&cv, r, R, UI_CORNERS_ALL, 0xFFFFFF);
        ui_fill_round(&cv, ui_rect_inflate(r, -w), R - w > 0 ? R - w : 0, UI_CORNERS_ALL, BG);
        int diff = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                diff += frame[y][x] != (at(x, y) == 0xFFFFFF);
        CHECK_EQ(diff, 0);
    }
    setup(4);
    ui_frame_round(&cv, (struct rect){ 2, 2, 10, 6 }, 0, 1, 0, 0xFFFFFF);
    CHECK_EQ(count(0xFFFFFF), 2 * 10 + 2 * 6 - 4);
    setup(4);
    ui_frame_round(&cv, (struct rect){ 2, 2, 10, 6 }, 2, 3, UI_CORNERS_ALL, 0xFFFFFF);  /* past the middle */
    int f = count(0xFFFFFF);
    setup(4);
    ui_fill_round(&cv, (struct rect){ 2, 2, 10, 6 }, 2, UI_CORNERS_ALL, 0xFFFFFF);
    CHECK_EQ(count(0xFFFFFF), f);
    setup(4);
    ui_frame_round(&cv, (struct rect){ 2, 2, 10, 6 }, 2, 0, UI_CORNERS_ALL, 0xFFFFFF);
    CHECK_EQ(count(0xFFFFFF), 0);
}

static void test_blend(void)
{
    setup(4);
    ui_fill(&cv, (struct rect){ 0, 0, 4, 1 }, 0x000000);
    ui_blend(&cv, 0, 0, 0xFFFFFF, 0);
    ui_blend(&cv, 1, 0, 0xFFFFFF, 256);
    ui_blend(&cv, 2, 0, 0xFFFFFF, 128);    /* 127.5 -> 128 (halves to even) */
    ui_blend(&cv, 3, 0, 0x02FE10, 64);     /* 0.5 -> 0, 63.5 -> 64, 4 */
    CHECK_EQ(at(0, 0), 0x000000);
    CHECK_EQ(at(1, 0), 0xFFFFFF);
    CHECK_EQ(at(2, 0), 0x808080);
    CHECK_EQ(at(3, 0), 0x004004);
    ui_blend(&cv, -1, 0, 0xFFFFFF, 256);   /* outside: nothing */
    ui_blend(&cv, W, H, 0xFFFFFF, 256);
    CHECK(guards_ok());
    /* At 3 bytes per pixel the same. */
    setup(3);
    ui_fill(&cv, (struct rect){ 0, 0, 1, 1 }, 0x000000);
    ui_blend(&cv, 0, 0, 0xFFFFFF, 128);
    CHECK_EQ(at(0, 0), 0x808080);
}

/* Made-up fonts: glyph c of `one` has only the pixel (c % 8, c / 8 % 16) set. */
static uint8_t one_bits[256 * 16];
static const struct font one = { 8, 16, 1, one_bits };
static uint8_t wide_bits[256 * 4 * 2];
static const struct font wide = { 10, 4, 2, wide_bits };   /* every pixel set */

static void test_text(void)
{
    for (int c = 0; c < 256; c++)
        one_bits[c * 16 + (c / 8) % 16] = (uint8_t)(0x80u >> (c % 8));
    memset(wide_bits, 0xFF, sizeof(wide_bits));
    setup(4);
    ui_text(&cv, &one, 2, 3, "\x01\x0A\x7F", 0xFFFFFF);
    CHECK_EQ(count(0xFFFFFF), 3);
    CHECK_EQ(at(2 + 1, 3 + 0), 0xFFFFFF);          /* 0x01: (1, 0) */
    CHECK_EQ(at(10 + 2, 3 + 1), 0xFFFFFF);         /* 0x0A: (2, 1) */
    CHECK_EQ(at(18 + 7, 3 + 15), 0xFFFFFF);        /* 0x7F: (7, 15) */
    CHECK_EQ(ui_text_width(&one, "abc"), 24);
    CHECK_EQ(ui_text_width(&wide, ""), 0);
    /* Only the set pixels are written; clipped at the surface and the clip. */
    setup(4);
    ui_text(&cv, &wide, -5, 28, "ABCDE", 0xFFFFFF);
    CHECK_EQ(count(0xFFFFFF), 40 * 2);            /* 50 - 5 columns would be 45, but W is 40 */
    CHECK(guards_ok());
    setup(4);
    ui_canvas_clip(&cv, (struct rect){ 3, 0, 4, 2 });
    ui_text(&cv, &wide, 0, 0, "AB", 0xFFFFFF);
    CHECK_EQ(count(0xFFFFFF), 8);
    CHECK(guards_ok());
}

int main(void)
{
    printf("test_ui_draw\n");
    RUN(test_px);
    RUN(test_fill_clip);
    RUN(test_round);
    RUN(test_frame);
    RUN(test_blend);
    RUN(test_text);
    return unit_report("test_ui_draw");
}
