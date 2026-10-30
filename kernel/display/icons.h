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
    ICON_TIMES, ICON_DIVIDE, ICON_BACKSPACE, ICON_CHEVRON, ICON_STROKED
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

#endif
