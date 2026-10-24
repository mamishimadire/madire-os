/* Unit tests for kernel/display/coverage_core.c (capsule coverage, 1.3
 * stage 2): for a fixed-seed set of capsules (every scale's radii,
 * sub-pixel ends, zero-length, very short and very long, near-vertical and
 * near-horizontal, partly off the surface) it gives the same pixel box and
 * the same coverage at every pixel of the box and a 2-pixel margin around
 * it as an independent reference. Until 1.3 stage 3 the reference was
 * wallpaper_core.c's own copy of the rule (the static capsule_setup and
 * capsule_cov); stage 3 moved the splash onto coverage_core.c and removed
 * the copy, and the reference is now tests/desktop_ref.py's capsule
 * coverage (scripts/make-wallpaper.py's capsule rule), as the generated
 * table build/gen/coverage_ref.h: the same capsules, drawn by the same
 * generator in Python, with each one's box and every sample. The suite
 * also checks that its capsules are the table's, one by one. Each capsule's
 * random draws are made in a fixed order (the order gcc evaluated the stage
 * 2 suite's arguments in: right to left), so the set is the stage 2 one.
 * The modules are included directly. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/surface_core.c"
#include "kernel/display/coverage_core.c"
#include "kernel/display/wallpaper_core.c"
#include "coverage_ref.h"           /* generated: build/gen/ (the Makefile) */

static uint64_t seed = 0x13C0FFEE2026ull;
static unsigned long long samples, capsules;

static uint64_t rnd(void)
{
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    return seed >> 17;
}

/* A value in [lo, hi]. */
static int64_t between(int64_t lo, int64_t hi)
{
    return lo + (int64_t)(rnd() % (uint64_t)(hi - lo + 1));
}

static int mismatches;

static void compare(int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t r, uint32_t w, uint32_t h)
{
    struct cov_capsule b;
    bool ok_b = cov_capsule_setup(&b, x1, y1, x2, y2, r, w, h);
    if (capsules >= COV_REF_CAPSULES) {
        capsules++;
        CHECK(0);                   /* more capsules than the table */
        return;
    }
    const struct cov_ref *a = &cov_ref_caps[capsules++];
    if (a->x1 != x1 || a->y1 != y1 || a->x2 != x2 || a->y2 != y2 || a->r != r || a->w != w || a->h != h) {
        if (mismatches++ < 5)
            printf("    capsule %llu is not the table's: (%lld,%lld)-(%lld,%lld) r %lld\n", capsules - 1,
                   (long long)x1, (long long)y1, (long long)x2, (long long)y2, (long long)r);
        CHECK(0);
        return;
    }
    bool ok_a = a->ok != 0;
    CHECK(ok_a == ok_b);
    if (ok_a != ok_b || a->bx0 != b.bx0 || a->by0 != b.by0 || a->bx1 != b.bx1 || a->by1 != b.by1) {
        if (mismatches++ < 5)
            printf("    box differs: (%lld,%lld)-(%lld,%lld) r %lld\n", (long long)x1, (long long)y1,
                   (long long)x2, (long long)y2, (long long)r);
        CHECK(0);
        return;
    }
    if (!ok_a)
        return;
    int bad = 0;
    for (int64_t y = a->by0 - 2; y <= a->by1 + 2; y++)
        for (int64_t x = a->bx0 - 2; x <= a->bx1 + 2; x++) {
            if (samples >= COV_REF_SAMPLES || cov_ref_samples[samples] != cov_capsule_at(&b, x, y))
                bad++;
            samples++;
        }
    CHECK_EQ(bad, 0);
}

/* The radii the toolkit and the splash use, at every scale and size. */
static int64_t radius_for(int k)
{
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    static const uint32_t sizes[4] = { 16, 24, 32, 48 };
    uint32_t s = scales[k % 4], p = (sizes[(k / 4) % 4] * s + 2) / 4;
    switch ((k / 16) % 4) {
    case 0:
        return wp_fx(8 * (int64_t)p, 192);                          /* an icon's stroke */
    case 1:
        return SPLASH_LOGO_RX * wp_fx(22 * (int64_t)p, 24 * 210);   /* the M icon's */
    case 2:
        return wp_fx(25 * (int64_t)s, 40);                          /* the wordmark's */
    default:
        return between(1, 6 * 256);                                 /* anything up to 6 px */
    }
}

static void test_general(void)
{
    for (int i = 0; i < 400; i++) {
        int64_t x1 = between(-2000, 30000), y1 = between(-2000, 30000);
        int64_t r = radius_for(i), dy = between(-10000, 10000), dx = between(-10000, 10000);
        compare(x1, y1, x1 + dx, y1 + dy, r, 120, 120);
    }
}

static void test_dots(void)
{
    for (int i = 0; i < 200; i++) {
        int64_t x = between(-1000, 30000), y = between(-1000, 30000);
        compare(x, y, x, y, radius_for(i), 120, 120);
    }
}

static void test_short(void)
{
    for (int i = 0; i < 300; i++) {
        int64_t x = between(0, 25000), y = between(0, 25000);
        int64_t r = radius_for(i), dy = between(-255, 255), dx = between(-255, 255);
        compare(x, y, x + dx, y + dy, r, 120, 120);
    }
}

static void test_long(void)
{
    /* Near the axes (thin boxes) and a few diagonals, up to 1500 px. */
    for (int i = 0; i < 24; i++) {
        int64_t x = between(0, 2000), y = between(0, 2000), len = between(300 * 256, 1500 * 256);
        int64_t off = between(-3 * 256, 3 * 256);
        if (i % 3 == 0)
            compare(x, y, x + len, y + off, radius_for(i), 2048, 2048);
        else if (i % 3 == 1)
            compare(x, y, x + off, y + len, radius_for(i), 2048, 2048);
        else
            compare(x, y, x + len / 3, y + len / 4, radius_for(i), 2048, 2048);
    }
}

static void test_near_axes(void)
{
    for (int i = 0; i < 300; i++) {
        int64_t x = between(0, 25000), y = between(0, 25000), len = between(256, 60 * 256);
        int64_t off = between(-3, 3);
        if (i % 2)
            compare(x, y, x + off, y + (i % 4 == 1 ? len : -len), radius_for(i), 160, 160);
        else
            compare(x, y, x + (i % 4 == 0 ? len : -len), y + off, radius_for(i), 160, 160);
    }
}

static void test_clipped(void)
{
    /* Small surfaces: boxes clipped on every side, or wholly outside. */
    for (int i = 0; i < 300; i++) {
        int64_t x1 = between(-4000, 20000), y1 = between(-4000, 16000);
        uint32_t h = (uint32_t)between(1, 48), w = (uint32_t)between(1, 64);
        int64_t r = radius_for(i), dy = between(-6000, 6000), dx = between(-6000, 6000);
        compare(x1, y1, x1 + dx, y1 + dy, r, w, h);
    }
}

int main(void)
{
    printf("test_coverage\n");
    RUN(test_general);
    RUN(test_dots);
    RUN(test_short);
    RUN(test_long);
    RUN(test_near_axes);
    RUN(test_clipped);
    CHECK_EQ(capsules, COV_REF_CAPSULES);
    CHECK_EQ(samples, COV_REF_SAMPLES);
    printf("  coverage_core against tests/desktop_ref.py: %llu capsules, %llu samples compared\n", capsules,
           samples);
    return unit_report("test_coverage");
}
