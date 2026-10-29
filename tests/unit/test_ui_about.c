/* Unit tests for kernel/display/ui_about.c (the About window's content,
 * 1.3 stage 4a): its size at every scale; ui_text_fit, the owner's cut
 * rule (a value that does not fit its column ends with "...", ASCII),
 * with a boot loader's name longer than the column and at the buffer's
 * limit; the values cut to the column at every scale; the 24 dp right
 * padding never crossed (with that long name); the wordmark's coverage
 * scratch within UI_ABOUT_SCRATCH at every scale; the window with its
 * content painted on its own surface and composed through ui_blit_round
 * equal, byte for byte, to the window painted on the screen directly, at
 * every scale and both pixel formats, partly off the screen. Fonts are
 * made here (a fixed bit pattern). The toolkit's modules are included
 * directly; surface_core.c, wallpaper_core.c and coverage_core.c are
 * linked (the Makefile). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/ui_widget.c"
#include "kernel/display/ui_window.c"
#include "kernel/display/ui_about.c"

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

static const uint32_t scales[4] = { 4, 5, 6, 8 };
static const char long_loader[] = "Limine 12.9.3 (a boot loader name much longer than the column)";
static uint16_t scratch[UI_ABOUT_SCRATCH];

static void test_size(void)
{
    int32_t w, h;
    ui_about_size(4, &w, &h);
    CHECK(w == 352 && h == 377);
    ui_about_size(8, &w, &h);
    CHECK(w == 704 && h == 754);
    ui_about_size(5, &w, &h);
    CHECK(w == 440 && h == 471);
    for (int i = 0; i < 4; i++)                 /* 24 characters of the console's font at every scale */
        CHECK_EQ(ui_about_value_px(scales[i]), 24 * fonts_for(scales[i]).regular->width);
}

static void test_fit(void)
{
    const struct font *f = fonts_for(4).regular;    /* 8 px wide */
    char out[UI_ABOUT_VALUE_MAX];
    ui_text_fit(f, "Live (memory only)", 192, out, sizeof(out));
    CHECK(strcmp(out, "Live (memory only)") == 0);
    ui_text_fit(f, "abcdefghijklmnopqrstuvwx", 192, out, sizeof(out));        /* exactly 24: kept */
    CHECK(strcmp(out, "abcdefghijklmnopqrstuvwx") == 0);
    ui_text_fit(f, "abcdefghijklmnopqrstuvwxy", 192, out, sizeof(out));       /* 25: cut */
    CHECK(strcmp(out, "abcdefghijklmnopqrstu...") == 0);
    ui_text_fit(f, long_loader, 192, out, sizeof(out));
    CHECK(strcmp(out, "Limine 12.9.3 (a boot...") == 0);
    CHECK_EQ(ui_text_width(f, out), 192);
    ui_text_fit(f, "", 192, out, sizeof(out));
    CHECK(out[0] == 0);
    ui_text_fit(f, "abcdef", 16, out, sizeof(out));                           /* room for "..." only */
    CHECK(strcmp(out, "...") == 0 || strcmp(out, "") == 0);
    /* At the buffer's limit: at most size - 1 bytes, cut with "..." too. */
    char small[8];
    ui_text_fit(f, "abcdefghij", 192, small, sizeof(small));
    CHECK(strcmp(small, "abcd...") == 0);
    ui_text_fit(f, "abcdefg", 192, small, sizeof(small));
    CHECK(strcmp(small, "abcdefg") == 0);
}

static void test_values(void)
{
    for (int i = 0; i < 4; i++) {
        uint32_t s = scales[i];
        struct ui_fonts f = fonts_for(s);
        struct ui_about a;
        ui_about_setup(&a, s, &f, "1.3", long_loader, "1024x768x32", "127 MB", scratch, UI_ABOUT_SCRATCH);
        CHECK(strcmp(a.values[0], "1.3") == 0);
        CHECK(strcmp(a.values[1], "Limine 12.9.3 (a boot...") == 0);
        CHECK(strcmp(a.values[2], "1024x768x32") == 0);
        CHECK(strcmp(a.values[3], "127 MB") == 0);
        CHECK(strcmp(a.values[4], UI_ABOUT_MODE) == 0);
        CHECK(ui_text_width(f.regular, a.values[1]) <= ui_about_value_px(s));
        size_t need = ui_about_scratch(s, 4096, 4096);
        CHECK(need > 0 && need <= UI_ABOUT_SCRATCH);
    }
}

static struct ui_window about_window(struct ui_about *a, uint32_t s)
{
    int32_t w, h;
    ui_about_size(s, &w, &h);
    return (struct ui_window){ .name = "About", .title = UI_ABOUT_TITLE, .icon = UI_ICON_ABOUT, .w = w, .h = h,
                               .esc_closes = true, .paint = ui_about_paint, .ctx = a };
}

/* With the long name, nothing is drawn in the right padding (from 328 dp
 * to the edge) below the title bar: it stays surface.window. */
static void test_padding(void)
{
    for (int i = 0; i < 4; i++) {
        uint32_t s = scales[i];
        struct ui_fonts f = fonts_for(s);
        struct ui_about a;
        ui_about_setup(&a, s, &f, "1.3", long_loader, "1024x768x32", "127 MB", scratch, UI_ABOUT_SCRATCH);
        struct ui_window w = about_window(&a, s);
        uint8_t *px = malloc((size_t)w.w * w.h * 4);
        struct surface sf;
        surface_init(&sf, px, (uint32_t)w.w, (uint32_t)w.h, (uint32_t)w.w * 4, 4);
        struct ui_canvas c;
        ui_canvas_init(&c, &sf);
        ui_window_draw(&c, &w, (struct rect){ 0, 0, w.w, w.h }, true, UI_BUTTON_NORMAL, s, &f);
        CHECK(!a.short_scratch);
        int bad = 0, set = 0;
        int32_t e = ui_window_edge(s), R = ui_window_radius(s);
        for (int32_t y = ui_window_title_h(s); y < w.h - R; y++)
            for (int32_t x = ui_px(328, s); x < w.w - e; x++)
                bad += surface_get(&sf, (uint32_t)x, (uint32_t)y) != UI_SURFACE_WINDOW;
        /* The value column itself has text in it (the cut name reaches it). */
        int32_t y1 = ui_px(176 + 28, s);
        for (int32_t y = y1; y < y1 + (int32_t)f.regular->height; y++)
            for (int32_t x = ui_px(136, s); x < ui_px(328, s); x++)
                set += surface_get(&sf, (uint32_t)x, (uint32_t)y) == UI_TEXT;
        CHECK_EQ(bad, 0);
        CHECK(set > 0);
        free(px);
    }
}

static void check_surface(uint32_t s, uint32_t bpp, int32_t x, int32_t y, bool active)
{
    uint32_t W = 1200, H = 900;
    struct ui_fonts f = fonts_for(s);
    struct ui_about a;
    ui_about_setup(&a, s, &f, "1.3", "Limine 12.9.3", "2560x1440x32", "4091 MB", scratch, UI_ABOUT_SCRATCH);
    struct ui_window w = about_window(&a, s);
    struct rect r = { x, y, w.w, w.h };
    uint8_t *pa = malloc((size_t)W * H * bpp), *pb = malloc((size_t)W * H * bpp);
    uint8_t *ps = malloc((size_t)w.w * w.h * bpp);
    for (size_t i = 0; i < (size_t)W * H * bpp; i++)
        pa[i] = pb[i] = (uint8_t)(i * 13 + (i >> 10));
    memset(ps, 0xA5, (size_t)w.w * w.h * bpp);
    struct surface sa, sb, ss;
    surface_init(&sa, pa, W, H, W * bpp, bpp);
    surface_init(&sb, pb, W, H, W * bpp, bpp);
    surface_init(&ss, ps, (uint32_t)w.w, (uint32_t)w.h, (uint32_t)w.w * bpp, bpp);
    struct ui_canvas ca, cs, cb;
    ui_canvas_init(&ca, &sa);
    ui_window_draw(&ca, &w, r, active, UI_BUTTON_NORMAL, s, &f);
    ui_canvas_init(&cs, &ss);
    ui_window_draw(&cs, &w, (struct rect){ 0, 0, w.w, w.h }, active, UI_BUTTON_NORMAL, s, &f);
    ui_canvas_init(&cb, &sb);
    ui_blit_round(&cb, r, &ss, ui_window_radius(s), UI_CORNERS_ALL);
    CHECK(memcmp(pa, pb, (size_t)W * H * bpp) == 0);
    /* Painted in tiles (the exposure path): the same. */
    for (size_t i = 0; i < (size_t)W * H * bpp; i++)
        pb[i] = (uint8_t)(i * 13 + (i >> 10));
    for (int32_t ty = -9; ty < (int32_t)H; ty += 47)
        for (int32_t tx = -3; tx < (int32_t)W; tx += 83) {
            ui_canvas_clip(&cb, (struct rect){ tx, ty, 83, 47 });
            ui_window_draw(&cb, &w, r, active, UI_BUTTON_NORMAL, s, &f);
        }
    CHECK(memcmp(pa, pb, (size_t)W * H * bpp) == 0);
    CHECK(!a.short_scratch);
    free(pa);
    free(pb);
    free(ps);
}

static void test_surface_equals_direct(void)
{
    for (int i = 0; i < 4; i++)
        for (uint32_t bpp = 3; bpp <= 4; bpp++) {
            check_surface(scales[i], bpp, 101, 37, true);
            check_surface(scales[i], bpp, -57, 11, false);
        }
}

int main(void)
{
    printf("test_ui_about\n");
    RUN(test_size);
    RUN(test_fit);
    RUN(test_values);
    RUN(test_padding);
    RUN(test_surface_equals_direct);
    return unit_report("test_ui_about");
}
