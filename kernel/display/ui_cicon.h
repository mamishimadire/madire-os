#ifndef MADIRE_DISPLAY_UI_CICON_H
#define MADIRE_DISPLAY_UI_CICON_H

/*
 * The toolkit's colour icons, the pure part (1.5 stage 12; docs/plans/
 * 1.5.md, note (12)(a) with the S12 change: the folder's back in
 * `accent.hover`; the Design Guide, section 5; host unit tests in
 * tests/unit/test_ui_cicon.c). Madire's own designs, drawn in-house from
 * the data tables in cicons.c (no bitmaps): the folder, the home folders
 * (Home, Desktop, Documents, Downloads, Pictures, Music, Videos: the folder
 * with its emblem), the file, the text file, the link (the file with the
 * link badge), the badge alone, the Trash empty and full. Existing tokens
 * only (cicon_colours).
 *
 * Each icon has two designs, on a 16 dp grid (128 units of 1/8 dp) and a
 * 48 dp grid (384 units): a box of at most CICON_SMALL_MAX (40) pixels
 * uses the 16 dp design, a larger one the 48 dp design, scaled to the box.
 * A design is a list of shapes, drawn in order, each blended over what is
 * under it with its own colour:
 *   - CICON_RRECT: a filled rectangle from point (x0, y0) to (x1, y1)
 *     (half-open), its corners rounded with `radius` units;
 *   - CICON_POLY: a filled polygon of `count` points (even-odd rule);
 *   - CICON_STROKE: a polyline of `count` points stroked 2 dp wide with
 *     round caps (a one-point polyline a dot): capsules, as the stroke
 *     icons (ui_icon.h), the point u at x * 256 + fx(u * P, G) in 24.8 and
 *     the radius fx(8 * P, G), P the box's pixels, G the grid's units; a
 *     pixel's coverage the largest over the shape's capsules.
 * A filled shape's coverage: 4 x 4 samples per pixel; sample (i, j) of the
 * pixel (x + rx, y + ry) is at X = (8 rx + 2 i + 1) G, Y = (8 ry + 2 j + 1)
 * G and the shape's point u at u * 8 * P (integers only); the coverage is
 * 16 per sample inside (0 to 256). Only pixels inside the box and the
 * canvas's clip are touched. tests/desktop_ref.py draws the same.
 *
 * The trace (UI_TRACE builds, ui_trace.h): each shape whose `trace` is not
 * CI_TRACE_NONE records once, when it covers a pixel, the pair of its
 * colour and either the background (CI_TRACE_BG: the pixel at the box's
 * top-left corner before the icon is drawn) or the colour named (the shape
 * it lies on); part static checks the pairs against the Design Guide's
 * section 12 (outlines and emblems at least 3:1).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_icon.h"

#define CICON_SMALL_MAX 40
#define CICON_GRID_16   128
#define CICON_GRID_48   384
#define CICON_STROKE_MAX 9      /* points in a stroke shape, at most */

enum { CICON_RRECT, CICON_POLY, CICON_STROKE };

/* The colours a shape may have (cicon_colours: tokens.h's values). */
enum {
    CI_ACCENT, CI_ACCENT_HOVER, CI_ACCENT_LIGHT, CI_TEXT, CI_TEXT_MUTED, CI_TEXT_ON_ACCENT, CI_SURFACE,
    CI_BG_DEEP, CI_WARNING, CI_COLOURS
};

#define CI_TRACE_NONE 0xFFu
#define CI_TRACE_BG   0xFEu

struct cicon_shape {
    uint8_t kind;               /* CICON_RRECT, CICON_POLY or CICON_STROKE */
    uint8_t colour;             /* CI_* */
    uint8_t trace;              /* CI_TRACE_NONE, CI_TRACE_BG or a CI_* colour */
    uint8_t count;              /* its points (2 for a rectangle) */
    uint16_t first;             /* its first point in cicon_points */
    int16_t radius;             /* a rectangle's corners, in units */
};

struct cicon_def {
    uint16_t first;             /* its first shape in cicon_shapes */
    uint16_t shapes;
};

extern const uint32_t cicon_colours[CI_COLOURS];
extern const int16_t cicon_points[][2];
extern const struct cicon_shape cicon_shapes[];
extern const struct cicon_def cicon_defs[UI_CICON_COUNT][2];      /* [icon][0: 16 dp, 1: 48 dp] */
extern const uint32_t cicon_point_count, cicon_shape_count;

/* Draws colour icon `icon` (UI_CICON_FIRST to UI_CICON_END - 1) in the
 * px x px box at (x, y). False (nothing drawn) for another number or px
 * outside 1 to UI_ICON_MAX_PX. */
bool ui_cicon_draw(const struct ui_canvas *c, int icon, int32_t x, int32_t y, int32_t px);

#endif
