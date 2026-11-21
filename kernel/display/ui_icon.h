#ifndef MADIRE_DISPLAY_UI_ICON_H
#define MADIRE_DISPLAY_UI_ICON_H

/*
 * The toolkit's stroke icons, the pure part (1.3 stage 2; host unit tests
 * in tests/unit/test_ui_icon.c): the 1.3 icon list of the Design Guide,
 * section 5, drawn by coverage (coverage_core.h) in one colour.
 *
 * The rules (tests/desktop_ref.py draws the same pictures from them):
 *   - an icon is drawn in a P x P pixel box at (x, y), P = ui_px(size in
 *     dp, scale) (16, 24, 32 or 48 dp); only pixels inside the box (and
 *     the canvas's clip) are touched;
 *   - a stroked icon (icons.h): the point v (units of 1/8 dp on the 24 dp
 *     grid) is at (x * 256 + fx(vx * P, 192), y * 256 + fx(vy * P, 192))
 *     in 24.8, the radius fx(8 * P, 192); each polyline's segments (a
 *     one-point polyline a dot) are capsules;
 *   - the M: kernel/logo.c's four strokes (splash_data.h's copy, x and y in
 *     1/8 logo pixels, y corrected by RX / RY as on the splash), scaled so
 *     the logo's 210-unit width (x 39 to 249 with the stroke radius) spans
 *     22 of the 24 dp: m = fx(22 * P, 24 * 210) pixels per unit (24.8),
 *     the logo's centre (144, 83.5) at the box's centre:
 *     X = x * 256 + fx(P, 2) + (sx - 144) * m,
 *     Y = y * 256 + fx(P, 2) + mul(fx(sy * RX, RY) - fx(167, 2), m),
 *     radius RX * m; all four strokes in the one colour (a control's M,
 *     section 5);
 *   - a pixel's coverage is the largest over the icon's capsules; the pixel
 *     is blended towards the colour by it (ui_blend).
 * fx and mul are wallpaper_core's wp_fx and wp_mul (to nearest, halves
 * away from zero).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"

enum ui_icon {
    UI_ICON_M, UI_ICON_CONSOLE, UI_ICON_ABOUT, UI_ICON_RESTART, UI_ICON_CALCULATOR, UI_ICON_CLOSE,
    UI_ICON_PLUS, UI_ICON_MINUS, UI_ICON_TIMES, UI_ICON_DIVIDE, UI_ICON_BACKSPACE, UI_ICON_CHEVRON,
    UI_ICON_MINIMISE, UI_ICON_MAXIMISE, UI_ICON_UNMAXIMISE, UI_ICON_POWER,      /* 1.5 stage 8 */
    UI_ICON_BACK, UI_ICON_FORWARD, UI_ICON_UP,                                  /* 1.5 stage 12 */
    UI_ICON_COUNT
};

/* 1.5 stage 12 (the owner's mockup decisions): the Files window's stroke
 * icons, icons.h's second table, drawn as the stroke icons above; their
 * numbers start at UI_SICON_FIRST. */
#define UI_SICON_FIRST 32
enum ui_sicon_id {
    UI_ICON_NEW_FOLDER = UI_SICON_FIRST, UI_ICON_NEW_FILE, UI_ICON_OPEN, UI_ICON_RENAME, UI_ICON_LIST,
    UI_ICON_GRID, UI_ICON_CHECK, UI_ICON_PLACE_HOME, UI_ICON_PLACE_DESKTOP, UI_ICON_PLACE_DOCUMENTS,
    UI_ICON_PLACE_DOWNLOADS, UI_ICON_PLACE_PICTURES, UI_ICON_PLACE_MUSIC, UI_ICON_PLACE_VIDEOS,
    UI_ICON_PLACE_TRASH, UI_ICON_PLACE_THISPC,
    UI_ICON_CUT, UI_ICON_COPY, UI_ICON_PASTE, UI_ICON_RESTORE,      /* 1.5 stage 13 */
    UI_SICON_END
};

/* 1.5 stage 12 (docs/plans/1.5.md, note (12)(a)): the colour icons, drawn
 * in their own colours by ui_cicon.c (ui_cicon.h's rules; the colour given
 * to ui_icon_draw is not used). Their numbers start at UI_CICON_FIRST, so
 * UI_ICON_COUNT stays the stroke icons' count. */
#define UI_CICON_FIRST 64
enum ui_cicon_id {
    UI_ICON_FOLDER = UI_CICON_FIRST, UI_ICON_HOME, UI_ICON_DESKTOP, UI_ICON_DOCUMENTS, UI_ICON_DOWNLOADS,
    UI_ICON_PICTURES, UI_ICON_MUSIC, UI_ICON_VIDEOS, UI_ICON_FILE, UI_ICON_TEXTFILE, UI_ICON_LINK,
    UI_ICON_BADGE, UI_ICON_TRASH, UI_ICON_TRASH_FULL,
    UI_CICON_END
};
#define UI_CICON_COUNT (UI_CICON_END - UI_CICON_FIRST)

#define UI_ICON_NONE   (-1)
#define UI_ICON_MAX_PX 96       /* 48 dp at 200 % */

/* Draws icon `icon` in the px x px box at (x, y) in colour rgb (1.5 stage
 * 12: a colour icon, UI_CICON_FIRST on, in its own colours). False
 * (nothing drawn) for an unknown icon or px outside 1 to UI_ICON_MAX_PX. */
bool ui_icon_draw(const struct ui_canvas *c, int icon, int32_t x, int32_t y, int32_t px, uint32_t rgb);

#endif
