#ifndef MADIRE_DISPLAY_UI_DRAW_H
#define MADIRE_DISPLAY_UI_DRAW_H

/*
 * The toolkit's drawing, the pure part (1.3 stage 2; host unit tests in
 * tests/unit/test_ui_draw.c). Everything draws on a canvas: a surface and
 * a clip rectangle inside it. Only pixels inside the clip are written, and
 * nothing outside the surface is read or written. Colours are 0xRRGGBB.
 * Integers only.
 *
 * The rules (tests/desktop_ref.py draws the same pictures from them):
 *   - a rounded rectangle r with radius R (clamped to w / 2 and h / 2, and
 *     to 0): every pixel of r, except, in each corner named in `corners`,
 *     the pixels of the R x R corner square whose centre lies outside the
 *     quarter-circle of radius R centred R pixels in from both edges. For
 *     the corner pixel i columns and j rows in from the corner (0 to R - 1),
 *     that is (2R - 2i - 1)^2 + (2R - 2j - 1)^2 > 4R^2 (Design Guide,
 *     section 4: integer quarter-circles);
 *   - a frame of width W: the pixels of the rounded rectangle (r, R) that
 *     are not in the rounded rectangle r shrunk by W on every side with
 *     radius max(R - W, 0) (R the clamped radius), with the same corners;
 *   - text: each byte a code page 437 glyph of the font, the glyphs side
 *     by side from (x, y), each set pixel in the colour (opaque), clear
 *     pixels untouched;
 *   - blending by coverage a (0 to 256): each channel v of the pixel becomes
 *     byte(v * 256 + mul((c - v) * 256, a)), with wallpaper_core's rounding
 *     (mul to nearest, halves away from zero; byte halves to even).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/font.h"
#include "kernel/display/surface_core.h"

#define UI_CORNER_TL   1u
#define UI_CORNER_TR   2u
#define UI_CORNER_BL   4u
#define UI_CORNER_BR   8u
#define UI_CORNERS_ALL 15u

struct ui_canvas {
    struct surface *s;
    struct rect clip;           /* always inside the surface */
};

/* Pixels from dp at scale step s (4, 5, 6 or 8): (dp x s + 2) / 4
 * (Design Guide, section 6). */
int32_t ui_px(int32_t dp, uint32_t s);

/* A canvas on the whole of s. */
void ui_canvas_init(struct ui_canvas *c, struct surface *s);

/* Sets the clip to the part of r inside the surface (may be empty). */
void ui_canvas_clip(struct ui_canvas *c, struct rect r);

/* Fills the part of r inside the clip. */
void ui_fill(const struct ui_canvas *c, struct rect r, uint32_t rgb);

/* A filled rounded rectangle (the rules above). */
void ui_fill_round(const struct ui_canvas *c, struct rect r, int32_t radius, uint32_t corners, uint32_t rgb);

/* A rounded frame `width` pixels wide (the rules above). */
void ui_frame_round(const struct ui_canvas *c, struct rect r, int32_t radius, int32_t width,
                    uint32_t corners, uint32_t rgb);

/* 1.3 stage 4a: copies src's pixels onto the canvas where the rounded
 * rectangle (r, radius, corners) lies (the fill's rule above): the pixel
 * at (x, y) of r comes from (x - r.x, y - r.y) of src; pixels outside src
 * are not copied. A window's surface is composed through it, so the
 * pixels outside its corners show what is under the window. src must be
 * another surface than the canvas's, in its format. */
void ui_blit_round(const struct ui_canvas *c, struct rect r, const struct surface *src, int32_t radius,
                   uint32_t corners);

/* Blends pixel (x, y) towards rgb by coverage a (0 to 256), if inside the clip. */
void ui_blend(const struct ui_canvas *c, int64_t x, int64_t y, uint32_t rgb, int64_t a);

/* Draws text (bytes up to the first 0) with font f at (x, y). */
void ui_text(const struct ui_canvas *c, const struct font *f, int32_t x, int32_t y, const char *text,
             uint32_t rgb);

/* The width of text in font f, in pixels. */
int32_t ui_text_width(const struct font *f, const char *text);

#endif
