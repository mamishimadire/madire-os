#ifndef MADIRE_DISPLAY_WALLPAPER_CORE_H
#define MADIRE_DISPLAY_WALLPAPER_CORE_H

/*
 * The procedural wallpaper and the splash, the pure part (1.2 stage 8;
 * host unit tests in tests/unit/test_wallpaper.c). The kernel side is
 * splash.c. scripts/make-wallpaper.py is the reference: it draws the same
 * pictures from the same rules, step for step, and the [1.2] splash checks
 * compare the screen with its rendering pixel for pixel.
 *
 * Integer maths only (the kernel has no floating point): every quantity is
 * fixed point with 8 fraction bits (24.8, the Design Guide, section 6).
 * Rounding: wp_fx, wp_mul and wp_div to nearest, halves away from zero; a
 * colour channel to a byte, halves to even, clamped to 0-255. Square
 * roots: wp_isqrt, the floor. Sizes in dp become pixels as
 * (dp x s + 2) / 4, s the scale step (4, 5, 6 or 8; the console's,
 * fbcon_core.h).
 *
 * The wallpaper, pixel (x, y) of a W x H screen:
 *   - row colour: bg.deep mixed towards bg.low by y / (H - 1);
 *   - brightened towards bg.base around (W / 2, 3 H / 10): w = 1 -
 *     dx^2 / rx^2 - dy^2 / ry^2 (rx = 3 W / 5, ry = H / 2), mixed by w^2
 *     where w > 0;
 *   - three glowing curves, circles through (0, H yl / 1000) and (W, H yr /
 *     1000) with centres at x = W cx / 1000 (wallpaper_core.c), in
 *     whole pixels: at distance d from a circle (from the pixel's centre),
 *     closer than the halo's reach h (40 dp): accent.glow mixed by
 *     0.30 (h - d)^2 / h^2, then accent.light by 0.70 x the core's coverage
 *     (radius s / 8 px, plus half a pixel).
 * The splash, on the wallpaper: kernel/logo.c's M (the copy in the
 * generated splash_data.h) with its strokes' capsules from accent.deep to
 * accent.light (left to right; the two back strokes 35 % towards bg.deep),
 * the wordmark "MADIRE OS" in `text` (the 1.1 capsule letterforms), the
 * line "Booting Madire OS" in the console's font in `text.muted`, and
 * three dots (radius 1.5 dp): the first `phase` in `text`, the others in
 * `surface.active`. The block (logo, 28 dp, the wordmark's cap height,
 * 40 dp, the text line) is centred on the screen. Every shape is painted
 * by coverage: the pixel's bytes mixed towards the shape's colour, then
 * rounded back (a wordmark glyph's coverage is the largest over its
 * strokes).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/display/font.h"
#include "kernel/display/surface_core.h"

/* The Design Guide's tokens (section 2). */
#define WP_BG_DEEP        0x011239u
#define WP_BG_BASE        0x012060u
#define WP_BG_LOW         0x011A4Cu
#define WP_SURFACE_ACTIVE 0x112447u
#define WP_ACCENT_GLOW    0x029FF8u
#define WP_ACCENT_LIGHT   0x64F2F9u
#define WP_ACCENT_DEEP    0x023CA1u
#define WP_TEXT           0xFAF9FEu
#define WP_TEXT_MUTED     0x8B9BB4u

#define WP_CURVES   3
#define SPLASH_DOTS 3
#define SPLASH_TEXT "Booting Madire OS"

/* The generated data's types (splash_data.h, scripts/make-wallpaper.py
 * --emit-c). */
struct wp_poly {
    uint16_t first;             /* its first point in splash_wm_points */
    uint16_t count;
};

struct wp_glyph {
    int16_t advance;            /* 24.8 units of the 14-unit cap height */
    uint16_t first_poly;
    uint16_t polys;
};

struct wp_rgb {
    int64_t c[3];               /* red, green, blue, 24.8 */
};

int64_t wp_fx(int64_t num, int64_t den);
int64_t wp_mul(int64_t a, int64_t b);
int64_t wp_div(int64_t a, int64_t b);
uint64_t wp_isqrt(uint64_t n);
uint8_t wp_byte(int64_t v);

struct wallpaper {
    uint32_t width, height;
    struct wp_rgb deep, low, base, glow, light;
    int64_t halo_w, core_r;     /* 24.8 */
    int64_t ccx, ccy, rx, ry;   /* whole pixels */
    int64_t cx[WP_CURVES], cy[WP_CURVES], r[WP_CURVES];   /* 24.8 */
};

/* The wallpaper of a width x height screen at scale step `scale`. */
void wallpaper_setup(struct wallpaper *wp, uint32_t width, uint32_t height, uint32_t scale);

/* Draws the part of the wallpaper inside `r` on `s` (a screen of the
 * wallpaper's size; r clipped to it). */
void wallpaper_render(struct surface *s, const struct wallpaper *wp, struct rect r);

struct splash_layout {
    uint32_t scale, cell_w, cell_h;
    int64_t ls, ts;             /* px per unit: the logo's (1/8 px), the wordmark's */
    int64_t lox, loy;           /* the logo's origin */
    int64_t title_x, title_y;   /* the wordmark's pen and cap top */
    int64_t r_title, r_os, track;
    int32_t text_x, text_y;     /* "Booting Madire OS", whole pixels */
    int64_t dot_x[SPLASH_DOTS], dot_y, rd;
    struct rect dots_box;       /* what a dots update redraws */
};

/* The splash's places on a width x height screen with the console's
 * scale step and cell size. False if the screen is too small for it. */
bool splash_layout(struct splash_layout *l, uint32_t width, uint32_t height, uint32_t scale,
                   uint32_t cell_w, uint32_t cell_h);

/* The 16-bit entries the wordmark's coverage needs (its largest glyph's
 * box), for splash_draw's scratch. */
size_t splash_scratch_entries(const struct splash_layout *l, uint32_t width, uint32_t height);

/* Draws the logo, the wordmark and the text line on `s` (the wallpaper
 * already there), with the console's font `f`. False (some glyph not
 * drawn) if `entries` is below splash_scratch_entries(). */
bool splash_draw(struct surface *s, const struct splash_layout *l, const struct font *f,
                 uint16_t *scratch, size_t entries);

/* Draws the three dots, the first `phase` lit, on what is under them. */
void splash_draw_dots(struct surface *s, const struct splash_layout *l, uint32_t phase);

#endif
