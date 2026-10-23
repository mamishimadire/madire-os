/* Unit tests for kernel/display/wallpaper_core.c (the procedural wallpaper
 * and the splash, 1.2 stage 8): the fixed-point roundings and the square
 * root; the wallpaper drawn whole, in pieces and clipped (the same pixels,
 * nothing outside the surface), at 4 and 3 bytes per pixel, and against a
 * plain per-pixel recomputation without the curves' bands; the splash's
 * layout at the five test modes; the scratch the wordmark needs; the dots'
 * colours; the generated data (kernel/logo.c's strokes). The pixel-exact
 * comparison with scripts/make-wallpaper.py is part static's (the host tool
 * tests/splashdump.c). The modules are included directly. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/surface_core.c"
#include "kernel/display/wallpaper_core.c"

#define GUARD 0xA5
#define PAD   64

static void test_rounding(void)
{
    CHECK_EQ(wp_fx(1, 3), 85);                  /* 85.33 */
    CHECK_EQ(wp_fx(1, 2), 128);
    CHECK_EQ(wp_fx(-1, 2), (uint64_t)-128);
    CHECK_EQ(wp_fx(3, 512), 2);                 /* 1.5: away from zero */
    CHECK_EQ(wp_fx(-3, 512), (uint64_t)-2);
    CHECK_EQ(wp_mul(3, 128), 2);                /* 1.5 */
    CHECK_EQ(wp_mul(-3, 128), (uint64_t)-2);
    CHECK_EQ(wp_mul(1, 128), 1);                /* 0.5 */
    CHECK_EQ(wp_mul(-1, 128), (uint64_t)-1);
    CHECK_EQ(wp_mul(256, 256), 256);
    CHECK_EQ(wp_div(1, 2), 128);
    CHECK_EQ(wp_div(-1, 2), (uint64_t)-128);
    CHECK_EQ(wp_div(1, 3), 85);
    CHECK_EQ(wp_div(2, 3), 171);                /* 170.67 */
    CHECK_EQ(wp_div(1, -512), (uint64_t)-1);    /* -0.5: away from zero */
    CHECK_EQ(wp_byte(0x180), 2);                /* 1.5: to even */
    CHECK_EQ(wp_byte(0x280), 2);                /* 2.5: to even */
    CHECK_EQ(wp_byte(0x281), 3);
    CHECK_EQ(wp_byte(0x17F), 1);
    CHECK_EQ(wp_byte(-5), 0);
    CHECK_EQ(wp_byte(300 << 8), 255);
}

static void test_isqrt(void)
{
    int ok = 1;
    for (uint64_t n = 0; n < 200000; n++) {
        uint64_t r = wp_isqrt(n);
        if (r * r > n || (r + 1) * (r + 1) <= n)
            ok = 0;
    }
    CHECK(ok);
    CHECK_EQ(wp_isqrt(0xFFFFFFFE00000001ull), 0xFFFFFFFFull);   /* (2^32 - 1)^2 */
    CHECK_EQ(wp_isqrt(0xFFFFFFFFFFFFFFFFull), 0xFFFFFFFFull);
    CHECK_EQ(wp_isqrt(1ull << 62), 1ull << 31);
    CHECK_EQ(wp_isqrt((1ull << 62) - 1), (1ull << 31) - 1);
}

#define WW 200
#define WH 150

struct buf {
    uint8_t mem[PAD + WW * WH * 4 + PAD];
    struct surface s;
};

static void buf_init(struct buf *b, uint32_t w, uint32_t h, uint32_t bpp)
{
    memset(b->mem, GUARD, sizeof(b->mem));
    surface_init(&b->s, b->mem + PAD, w, h, w * bpp, bpp);
}

static bool guards_ok(const struct buf *b)
{
    for (uint32_t i = 0; i < PAD; i++)
        if (b->mem[i] != GUARD || b->mem[sizeof(b->mem) - 1 - i] != GUARD)
            return false;
    return true;
}

static bool same_rgb(const struct surface *a, const struct surface *b)
{
    for (uint32_t y = 0; y < a->height; y++)
        for (uint32_t x = 0; x < a->width; x++)
            if (surface_get(a, x, y) != surface_get(b, x, y))
                return false;
    return true;
}

/* The wallpaper's pixel recomputed plainly: every curve tested at every
 * pixel (no bands), as scripts/make-wallpaper.py does. */
static uint32_t plain_pixel(const struct wallpaper *wp, int64_t x, int64_t y)
{
    struct wp_rgb c = mix(wp->deep, wp->low, wp_fx(y, (int64_t)wp->height - 1));
    int64_t dx = x - wp->ccx, dy = y - wp->ccy;
    int64_t w = ONE - wp_div(dx * dx, wp->rx * wp->rx) - wp_div(dy * dy, wp->ry * wp->ry);
    if (w > 0)
        c = mix(c, wp->base, wp_mul(w, w));
    int64_t px = (x << FB) + HALF, py = (y << FB) + HALF;
    for (int i = 0; i < WP_CURVES; i++) {
        int64_t ex = px - wp->cx[i], ey = py - wp->cy[i];
        int64_t d = abs64((int64_t)wp_isqrt((uint64_t)(ex * ex + ey * ey)) - wp->r[i]);
        if (d >= wp->halo_w)
            continue;
        int64_t t = wp->halo_w - d;
        c = mix(c, wp->glow, wp_mul(wp_fx(30, 100), t * t / wp->halo_w * ONE / wp->halo_w));
        int64_t cov = wp->core_r + HALF - d;
        if (cov > 0)
            c = mix(c, wp->light, wp_mul(wp_fx(70, 100), cov < ONE ? cov : ONE));
    }
    return (uint32_t)wp_byte(c.c[0]) << 16 | (uint32_t)wp_byte(c.c[1]) << 8 | wp_byte(c.c[2]);
}

static void test_wallpaper(void)
{
    static struct buf whole, pieces, three;
    struct wallpaper wp;
    wallpaper_setup(&wp, WW, WH, 4);
    buf_init(&whole, WW, WH, 4);
    wallpaper_render(&whole.s, &wp, (struct rect){ 0, 0, WW, WH });
    CHECK(guards_ok(&whole));

    /* Every pixel as the plain loop gives it: the bands drop nothing. */
    int ok = 1, glow = 0;
    for (int64_t y = 0; y < WH; y++)
        for (int64_t x = 0; x < WW; x++) {
            uint32_t want = plain_pixel(&wp, x, y);
            if (surface_get(&whole.s, (uint32_t)x, (uint32_t)y) != want)
                ok = 0;
            if ((want & 0xFF) > 0x80)           /* a curve's light core */
                glow++;
        }
    CHECK(ok);
    CHECK(glow > 0);

    /* In four pieces, two of them partly outside: the same pixels. */
    buf_init(&pieces, WW, WH, 4);
    wallpaper_render(&pieces.s, &wp, (struct rect){ -20, -10, 120, 90 });
    wallpaper_render(&pieces.s, &wp, (struct rect){ 100, 0, 150, 80 });
    wallpaper_render(&pieces.s, &wp, (struct rect){ 0, 80, 100, 70 });
    wallpaper_render(&pieces.s, &wp, (struct rect){ 100, 80, 500, 500 });
    CHECK(same_rgb(&whole.s, &pieces.s));
    CHECK(guards_ok(&pieces));

    /* At 3 bytes per pixel: the same colours. */
    buf_init(&three, WW, WH, 3);
    wallpaper_render(&three.s, &wp, (struct rect){ 0, 0, WW, WH });
    CHECK(same_rgb(&whole.s, &three.s));
    CHECK(guards_ok(&three));
}

static void test_curves(void)
{
    /* Each circle passes within a pixel of its two edge points. */
    static const uint32_t sizes[3][2] = { { 800, 600 }, { 1024, 768 }, { 2560, 1440 } };
    for (int k = 0; k < 3; k++) {
        struct wallpaper wp;
        wallpaper_setup(&wp, sizes[k][0], sizes[k][1], 4);
        for (int i = 0; i < WP_CURVES; i++) {
            int64_t cx = wp.cx[i] >> FB, cy = wp.cy[i] >> FB, r = wp.r[i] >> FB;
            int64_t yl = (int64_t)sizes[k][1] * curves[i][1] / 1000, yr = (int64_t)sizes[k][1] * curves[i][2] / 1000;
            int64_t dl = (int64_t)wp_isqrt((uint64_t)(cx * cx + (cy - yl) * (cy - yl))) - r;
            int64_t w = sizes[k][0];
            int64_t dr = (int64_t)wp_isqrt((uint64_t)((cx - w) * (cx - w) + (cy - yr) * (cy - yr))) - r;
            CHECK(dl >= -1 && dl <= 1);
            CHECK(dr >= -1 && dr <= 1);
            CHECK(cx > w && cy > (int64_t)sizes[k][1]);   /* the centre below and right of the screen */
        }
    }
}

static void test_layout(void)
{
    static const uint32_t modes[5][5] = {          /* w, h, scale, cell w, cell h */
        { 800, 600, 4, 8, 16 }, { 1024, 768, 4, 8, 16 }, { 1280, 1024, 5, 10, 20 },
        { 1920, 1080, 6, 12, 24 }, { 2560, 1440, 8, 16, 32 },
    };
    for (int i = 0; i < 5; i++) {
        struct splash_layout l;
        uint32_t w = modes[i][0], h = modes[i][1];
        CHECK(splash_layout(&l, w, h, modes[i][2], modes[i][3], modes[i][4]));
        CHECK(l.text_x > 0 && l.text_x + (int32_t)(17 * modes[i][3]) < (int32_t)w);
        CHECK(l.text_y > 0 && l.text_y + (int32_t)modes[i][4] < (int32_t)h);
        struct rect b = l.dots_box;
        CHECK(b.x > l.text_x + (int32_t)(17 * modes[i][3]) - 1);  /* right of the text */
        CHECK(b.x >= 0 && b.y >= 0 && b.x + b.w <= (int32_t)w && b.y + b.h <= (int32_t)h);
        for (int d = 0; d < SPLASH_DOTS; d++) {
            CHECK(((l.dot_x[d] - l.rd) >> FB) >= b.x && ((l.dot_x[d] + l.rd) >> FB) < b.x + b.w);
            CHECK(((l.dot_y - l.rd) >> FB) >= b.y && ((l.dot_y + l.rd) >> FB) < b.y + b.h);
        }
        CHECK(l.loy >= 0 && l.title_y > l.loy && (l.title_y >> FB) < l.text_y);
        CHECK(splash_scratch_entries(&l, w, h) > 0);
    }
    struct splash_layout l;
    CHECK(!splash_layout(&l, 300, 200, 4, 8, 16));             /* too small */
}

/* A font whose every glyph has only its top-left pixel set. */
static uint8_t corner_bits[256 * 16];
static const struct font corner = { 8, 16, 1, corner_bits };

static void test_draw(void)
{
    enum { SW = 800, SH = 600 };
    uint8_t *mem = malloc((size_t)SW * SH * 4 + 2 * PAD);
    struct surface s;
    struct splash_layout l;
    struct wallpaper wp;
    for (int c = 0; c < 256; c++)
        corner_bits[c * 16] = 0x80;
    memset(mem, GUARD, (size_t)SW * SH * 4 + 2 * PAD);
    surface_init(&s, mem + PAD, SW, SH, SW * 4, 4);
    wallpaper_setup(&wp, SW, SH, 4);
    wallpaper_render(&s, &wp, (struct rect){ 0, 0, SW, SH });
    CHECK(splash_layout(&l, SW, SH, 4, 8, 16));
    size_t n = splash_scratch_entries(&l, SW, SH);
    uint16_t *scratch = malloc(n * sizeof(uint16_t));
    CHECK(!splash_draw(&s, &l, &corner, scratch, n - 1));        /* too small: some glyph skipped */
    wallpaper_render(&s, &wp, (struct rect){ 0, 0, SW, SH });
    CHECK(splash_draw(&s, &l, &corner, scratch, n));

    /* The text line: each character's top-left pixel in text.muted. */
    int ok = 1;
    for (uint32_t i = 0; i < 17; i++)
        if (surface_get(&s, (uint32_t)l.text_x + i * 8, (uint32_t)l.text_y) != WP_TEXT_MUTED)
            ok = 0;
    CHECK(ok);

    /* The logo's left foot: deep blue, fully covered. */
    uint32_t foot = surface_get(&s, (uint32_t)((l.lox + 59 * l.ls) >> FB),
                                (uint32_t)((l.loy + wp_mul(wp_fx(123 * 20, 23), l.ls)) >> FB));
    CHECK((foot & 0xFF) > (foot >> 16 & 0xFF));             /* blue over red */

    /* The dots: lit ones in text, the others in surface.active (their
     * centres are fully covered). */
    for (uint32_t phase = 0; phase <= SPLASH_DOTS; phase++) {
        splash_draw_dots(&s, &l, phase);
        for (uint32_t d = 0; d < SPLASH_DOTS; d++)
            CHECK_EQ(surface_get(&s, (uint32_t)(l.dot_x[d] >> FB), (uint32_t)(l.dot_y >> FB)),
                     d < phase ? WP_TEXT : WP_SURFACE_ACTIVE);
    }
    int guards = 1;
    for (uint32_t i = 0; i < PAD; i++)
        if (mem[i] != GUARD || mem[(size_t)SW * SH * 4 + 2 * PAD - 1 - i] != GUARD)
            guards = 0;
    CHECK(guards);
    free(scratch);
    free(mem);
}

static void test_generated(void)
{
    /* kernel/logo.c's strokes (the copy splash_data.h holds). */
    static const int32_t logo[4][4] = {
        { 95, 57, 139, 169 }, { 182, 23, 229, 122 }, { 59, 123, 95, 57 }, { 139, 169, 182, 23 },
    };
    CHECK(memcmp(splash_logo_strokes, logo, sizeof(logo)) == 0);
    CHECK_EQ(SPLASH_LOGO_RX, 20);
    CHECK_EQ(SPLASH_LOGO_RY, 23);
    /* "MADIRE OS": nine glyphs, their advances in 1/256 units. */
    static const int16_t adv[9] = { 12, 10, 11, 0, 9, 9, 6, 14, 10 };
    CHECK_EQ(sizeof(splash_wm_glyphs) / sizeof(splash_wm_glyphs[0]), 9);
    int ok = 1;
    for (int i = 0; i < 9; i++)
        if (splash_wm_glyphs[i].advance != adv[i] * 256)
            ok = 0;
    CHECK(ok);
    CHECK_EQ(splash_wm_glyphs[6].polys, 0);                 /* the space */
}

int main(void)
{
    RUN(test_rounding);
    RUN(test_isqrt);
    RUN(test_wallpaper);
    RUN(test_curves);
    RUN(test_layout);
    RUN(test_draw);
    RUN(test_generated);
    return unit_report("wallpaper");
}
