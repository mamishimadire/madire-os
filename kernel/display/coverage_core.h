#ifndef MADIRE_DISPLAY_COVERAGE_CORE_H
#define MADIRE_DISPLAY_COVERAGE_CORE_H

/*
 * Coverage of capsules, the pure part (1.3 stage 2; host unit tests in
 * tests/unit/test_coverage.c). A capsule is a segment with a radius (a
 * zero-length one is a dot); the toolkit's stroke icons are drawn from
 * them. Positions and the radius are in 24.8 fixed point (the Design
 * Guide, section 6), the pixel (x, y) has its centre at
 * (x * 256 + 128, y * 256 + 128).
 *
 * The rule (wallpaper_core.c's splash follows the same one with its own
 * copy, until stage 3 moves it here; test_coverage.c checks that the two
 * agree): a pixel's coverage, 0 to 256, is the radius plus half a pixel
 * minus the distance from the pixel's centre to the segment, clamped. The
 * nearest point of the segment is at t = d * 256 / l2 (rounded down)
 * along it, where d is the dot product with the segment and l2 its
 * squared length (t is 0 for d <= 0 and 256 for d >= l2); the distance is
 * the integer square root of the squared offset from that point.
 *
 * Integers only; no floating point, no kernel dependency (the fixed-point
 * helpers are wallpaper_core's public ones).
 */

#include <stdbool.h>
#include <stdint.h>

struct cov_capsule {
    int64_t x1, y1, dx, dy, l2, r;
    int64_t bx0, by0, bx1, by1;     /* the pixels it may cover, clipped */
};

/* Sets up the capsule from (x1, y1) to (x2, y2) with radius r, its pixel
 * box (the ends' box widened by r and 2 pixels) clipped to [0, width) x
 * [0, height). False if nothing of the box is left. */
bool cov_capsule_setup(struct cov_capsule *c, int64_t x1, int64_t y1, int64_t x2, int64_t y2,
                       int64_t r, uint32_t width, uint32_t height);

/* The coverage of pixel (x, y), 0 to 256 (any pixel; 0 far away). */
int64_t cov_capsule_at(const struct cov_capsule *c, int64_t x, int64_t y);

#endif
