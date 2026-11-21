#ifndef MADIRE_DISPLAY_ICONS_H
#define MADIRE_DISPLAY_ICONS_H

/*
 * Madire's stroke icons for 1.3 (Design Guide, section 5; 1.3 stage 2):
 * their stroke lists, drawn by ui_icon.c. Each icon is a set of polylines
 * on the 24 dp grid, in units of 1/8 dp (0 to 192), stroked 2 dp wide
 * with round caps (a radius of 8 units); a one-point polyline is a dot.
 * The arcs (the about icon's circle, the restart icon's open circle) are
 * polylines of 15-degree steps, their points rounded to the unit.
 * The M is not here: it is kernel/logo.c's four strokes, as copied into
 * the splash's generated data (splash_data.h), drawn by ui_icon.c.
 */

#include <stdint.h>

#define ICON_GRID   192         /* the 24 dp grid in units */
#define ICON_RADIUS 8           /* the stroke's radius in units (1 dp) */

/* The icons with stroke lists, in ui_icon.h's order after the M. */
enum {
    ICON_CONSOLE, ICON_ABOUT, ICON_RESTART, ICON_CALCULATOR, ICON_CLOSE, ICON_PLUS, ICON_MINUS,
    ICON_TIMES, ICON_DIVIDE, ICON_BACKSPACE, ICON_CHEVRON,
    ICON_MINIMISE, ICON_MAXIMISE, ICON_UNMAXIMISE, ICON_POWER,      /* 1.5 stage 8 */
    ICON_BACK, ICON_FORWARD, ICON_UP,                               /* 1.5 stage 12 */
    ICON_STROKED
};

struct icon_poly {
    uint16_t first;             /* its first point in icon_points */
    uint16_t count;
};

struct icon_def {
    uint16_t first_poly;        /* its first polyline in icon_polys */
    uint16_t polys;
};

extern const int16_t icon_points[][2];
extern const struct icon_poly icon_polys[];
extern const struct icon_def icon_defs[ICON_STROKED];

/* 1.5 stage 12 (the owner's mockup decisions): the Files window's own
 * stroke icons, a second table in the same format (ui_icon.h's
 * UI_SICON_FIRST on): the buttons New Folder, New File, Open, Rename, List
 * and Grid, the checkbox's tick, and the sidebar's places (drawn in
 * `accent`; the path bar's home icon is the Home place's). Kept apart from
 * the 1.3 list above, so the 1.3 icon scene and its unit totals stay as
 * they are (tests/unit/test_ui_cicon.c checks this table). */
enum {
    ICON_NEW_FOLDER, ICON_NEW_FILE, ICON_OPEN, ICON_RENAME, ICON_LIST_VIEW, ICON_GRID_VIEW, ICON_CHECK, ICON_PLACE_HOME,
    ICON_PLACE_DESKTOP, ICON_PLACE_DOCUMENTS, ICON_PLACE_DOWNLOADS, ICON_PLACE_PICTURES, ICON_PLACE_MUSIC,
    ICON_PLACE_VIDEOS, ICON_PLACE_TRASH, ICON_PLACE_THISPC,
    ICON_CUT, ICON_COPY, ICON_PASTE, ICON_RESTORE,                  /* 1.5 stage 13 */
    ICON_S12_COUNT
};

extern const int16_t icon_points2[][2];
extern const struct icon_poly icon_polys2[];
extern const struct icon_def icon_defs2[ICON_S12_COUNT];
extern const uint32_t icon_points2_count, icon_polys2_count;

#endif
