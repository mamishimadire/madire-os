#ifndef MADIRE_DISPLAY_POINTER_CORE_H
#define MADIRE_DISPLAY_POINTER_CORE_H

/*
 * The mouse pointer, the pure part (1.2 stage 7; host unit tests in
 * tests/unit/test_pointer.c). The kernel side is pointer.c.
 *
 * The sprite is Madire's own: a dart (its tip at the top-left pixel, the
 * hot spot), filled in the Design Guide's `text`, edged in `bg.deep`, with
 * an `accent` spine one pixel inside its left edge. It is drawn on a
 * 12 x 18 dp grid (pointer_shape: '.' none, 'o' edge, 'w' fill, 'a'
 * spine) and scaled nearest-neighbour (the Design Guide, section 6): at
 * scale s (quarter steps: 4, 5, 6, 8 for 100, 125, 150, 200 %) it is
 * (12 s + 2) / 4 x (18 s + 2) / 4 pixels, and pixel (x, y) shows the grid
 * cell (x 12 / w, y 18 / h). tests/pointer_ref.py renders it from these
 * rules and the rows below, with the colours read from the Design Guide.
 *
 * The pointer's position is its hot spot, clamped to the screen
 * ([0, width - 1] x [0, height - 1]); the sprite is clipped at the right
 * and bottom edges. Save-under: before the sprite is drawn, the screen's
 * pixels under it are copied to a buffer; hiding it copies them back.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/surface_core.h"

#define POINTER_DP_W   12
#define POINTER_DP_H   18
#define POINTER_MAX_W  24       /* at scale 8 (200 %) */
#define POINTER_MAX_H  36

/* The Design Guide's tokens (section 2). */
#define POINTER_EDGE   0x011239u    /* bg.deep */
#define POINTER_FILL   0xFAF9FEu    /* text */
#define POINTER_SPINE  0x2987F3u    /* accent */

extern const char *const pointer_shape[POINTER_DP_H];

enum { POINTER_NONE, POINTER_PX_EDGE, POINTER_PX_FILL, POINTER_PX_SPINE };

struct pointer_sprite {
    uint32_t scale;
    uint32_t w, h;
    uint8_t px[POINTER_MAX_W * POINTER_MAX_H];  /* row by row, w per row */
};

/* The sprite at scale 4, 5, 6 or 8; false (and *s unchanged) for any other. */
bool pointer_sprite_make(struct pointer_sprite *s, uint32_t scale);

/* The colour 0xRRGGBB of a sprite pixel (POINTER_PX_*), 0 for none. */
uint32_t pointer_colour(uint8_t px);

/* Clamps a hot spot to a width x height screen. */
void pointer_clamp(int32_t *x, int32_t *y, uint32_t width, uint32_t height);

/* The sprite's rectangle with its hot spot at (x, y), clipped to the
 * screen (empty if nothing is left). */
struct rect pointer_area(const struct pointer_sprite *s, int32_t x, int32_t y, uint32_t width,
                         uint32_t height);

/* The pixels under the sprite. */
struct pointer_under {
    struct rect r;              /* where they came from; empty if none saved */
    uint8_t pixels[POINTER_MAX_W * POINTER_MAX_H * 4];
};

/* Copies the screen's pixels in r (inside the screen, at most the largest
 * sprite) to u; restore copies them back to the same place and forgets them. */
void pointer_save(struct pointer_under *u, const struct surface *screen, struct rect r);
void pointer_restore(struct pointer_under *u, struct surface *screen);

/* Draws the sprite's pixels (not its transparent ones) with the hot spot
 * at (x, y), clipped to the screen. */
void pointer_paint(struct surface *screen, const struct pointer_sprite *s, int32_t x, int32_t y);

#endif
