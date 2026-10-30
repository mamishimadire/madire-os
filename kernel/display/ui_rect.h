#ifndef MADIRE_DISPLAY_UI_RECT_H
#define MADIRE_DISPLAY_UI_RECT_H

/*
 * The toolkit's rectangles and damage list, the pure part (1.3 stage 2;
 * host unit tests in tests/unit/test_ui_rect.c). Rectangles are
 * surface_core.h's struct rect (x, y, w, h; w or h <= 0 is empty); the
 * arithmetic is done in 64 bits, results are clamped to 32.
 *
 * The damage list (docs/plans/1.3.md, C1) keeps the screen's damaged
 * rectangles until the compositor redraws them:
 *   - each rectangle added is clipped to the screen first; an empty one,
 *     or one inside a rectangle already listed, adds nothing;
 *   - otherwise it is appended, up to UI_DAMAGE_MAX (32) entries;
 *   - when the list is full, the list becomes one rectangle: the bounding
 *     rectangle of every entry and the new one.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/surface_core.h"

#define UI_DAMAGE_MAX 32

bool ui_rect_empty(struct rect r);

/* The overlap of a and b (the empty rectangle at 0, 0 if none). */
struct rect ui_rect_intersect(struct rect a, struct rect b);

/* The bounding rectangle of a and b; an empty one is ignored. */
struct rect ui_rect_union(struct rect a, struct rect b);

/* True if (x, y) lies inside r. */
bool ui_rect_contains(struct rect r, int32_t x, int32_t y);

/* True if inner (not empty) lies wholly inside outer. */
bool ui_rect_inside(struct rect inner, struct rect outer);

/* r grown by d pixels on every side (shrunk for d < 0; empty if nothing is left). */
struct rect ui_rect_inflate(struct rect r, int32_t d);

struct ui_damage {
    uint32_t width, height;     /* the screen */
    uint32_t n;
    struct rect r[UI_DAMAGE_MAX];
};

/* An empty list for a width x height screen. */
void ui_damage_init(struct ui_damage *d, uint32_t width, uint32_t height);

/* Empties the list. */
void ui_damage_clear(struct ui_damage *d);

/* Adds r (the rules above). */
void ui_damage_add(struct ui_damage *d, struct rect r);

/* 1.3 stage 7b: r's area in pixels (0 if empty). */
uint64_t ui_rect_area(struct rect r);

/* 1.3 stage 7b (the drag damage): empty rectangles are dropped; then two
 * listed rectangles become their bounding rectangle, at the first one's
 * place (the later one removed, the order otherwise kept), whenever its
 * area is no larger than the sum of their areas; repeated until no pair
 * merges. ui_damage_add is unchanged; the compositor calls this before it
 * composes. */
void ui_damage_merge_overlaps(struct ui_damage *d);

#endif
