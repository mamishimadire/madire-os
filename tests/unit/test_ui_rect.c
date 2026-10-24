/* Unit tests for kernel/display/ui_rect.c (the toolkit's rectangles and
 * damage list, 1.3 stage 2): overlap, bounding rectangle, containment and
 * growth, with empty rectangles and values near the 32-bit limits; the
 * damage list's clipping to the screen, its contained and empty
 * rectangles, its 32 entries and the full list becoming its bounding
 * rectangle. The module is included directly. */
#include <stdint.h>
#include "tests/unit/unit.h"
#include "kernel/display/ui_rect.c"

static int same(struct rect a, struct rect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

#define R(x, y, w, h) ((struct rect){ (x), (y), (w), (h) })

static void test_basics(void)
{
    CHECK(ui_rect_empty(R(0, 0, 0, 5)));
    CHECK(ui_rect_empty(R(0, 0, 5, -1)));
    CHECK(!ui_rect_empty(R(-3, -3, 1, 1)));
    CHECK(same(ui_rect_intersect(R(0, 0, 10, 10), R(5, 5, 10, 10)), R(5, 5, 5, 5)));
    CHECK(same(ui_rect_intersect(R(0, 0, 10, 10), R(10, 0, 5, 5)), R(0, 0, 0, 0)));   /* touching */
    CHECK(same(ui_rect_intersect(R(0, 0, 10, 10), R(2, 2, 0, 3)), R(0, 0, 0, 0)));
    CHECK(same(ui_rect_intersect(R(-5, -5, 10, 10), R(-20, 0, 18, 3)), R(-5, 0, 3, 3)));
    CHECK(same(ui_rect_union(R(0, 0, 2, 2), R(10, 20, 1, 1)), R(0, 0, 11, 21)));
    CHECK(same(ui_rect_union(R(0, 0, 0, 0), R(3, 4, 5, 6)), R(3, 4, 5, 6)));
    CHECK(same(ui_rect_union(R(3, 4, 5, 6), R(9, 9, -1, 2)), R(3, 4, 5, 6)));
    CHECK(same(ui_rect_union(R(0, 0, 0, 0), R(1, 1, 0, 0)), R(0, 0, 0, 0)));
    CHECK(ui_rect_contains(R(2, 3, 4, 5), 2, 3));
    CHECK(ui_rect_contains(R(2, 3, 4, 5), 5, 7));
    CHECK(!ui_rect_contains(R(2, 3, 4, 5), 6, 7));
    CHECK(!ui_rect_contains(R(2, 3, 4, 5), 5, 8));
    CHECK(!ui_rect_contains(R(2, 3, 0, 5), 2, 3));
    CHECK(ui_rect_inside(R(2, 2, 3, 3), R(2, 2, 3, 3)));
    CHECK(ui_rect_inside(R(3, 3, 1, 1), R(2, 2, 3, 3)));
    CHECK(!ui_rect_inside(R(3, 3, 3, 1), R(2, 2, 3, 3)));
    CHECK(!ui_rect_inside(R(3, 3, 0, 0), R(2, 2, 3, 3)));
    CHECK(same(ui_rect_inflate(R(10, 10, 4, 4), 2), R(8, 8, 8, 8)));
    CHECK(same(ui_rect_inflate(R(10, 10, 4, 4), -1), R(11, 11, 2, 2)));
    CHECK(same(ui_rect_inflate(R(10, 10, 4, 4), -2), R(0, 0, 0, 0)));
    CHECK(same(ui_rect_inflate(R(10, 10, 0, 4), 3), R(0, 0, 0, 0)));
}

static void test_limits(void)
{
    /* No overflow: the 64-bit arithmetic clamps to 32 bits. */
    struct rect big = R(INT32_MAX - 10, INT32_MAX - 10, INT32_MAX, INT32_MAX);
    struct rect r = ui_rect_intersect(big, R(INT32_MAX - 5, INT32_MAX - 5, 100, 100));
    CHECK(same(r, R(INT32_MAX - 5, INT32_MAX - 5, 100, 100)));
    r = ui_rect_union(R(INT32_MIN, INT32_MIN, 1, 1), R(INT32_MAX - 1, INT32_MAX - 1, 1, 1));
    CHECK(r.x == INT32_MIN && r.y == INT32_MIN);
    CHECK(r.w == INT32_MAX && r.h == INT32_MAX);    /* 2^32 - 1, clamped */
    CHECK(ui_rect_contains(big, INT32_MAX - 1, INT32_MAX - 1));
    CHECK(!ui_rect_empty(ui_rect_inflate(R(INT32_MIN, 0, 1, 1), 5)));
}

static void test_damage_basic(void)
{
    struct ui_damage d;
    ui_damage_init(&d, 100, 80);
    CHECK_EQ(d.n, 0);
    ui_damage_add(&d, R(0, 0, 0, 10));             /* empty */
    ui_damage_add(&d, R(100, 0, 10, 10));          /* off the screen */
    ui_damage_add(&d, R(-20, -20, 10, 10));
    CHECK_EQ(d.n, 0);
    ui_damage_add(&d, R(-5, 70, 20, 20));          /* clipped */
    CHECK_EQ(d.n, 1);
    CHECK(same(d.r[0], R(0, 70, 15, 10)));
    ui_damage_add(&d, R(2, 72, 3, 3));             /* inside it: nothing */
    ui_damage_add(&d, R(0, 70, 15, 10));           /* equal: nothing */
    CHECK_EQ(d.n, 1);
    ui_damage_add(&d, R(10, 75, 10, 3));           /* overlapping, not inside: appended */
    CHECK_EQ(d.n, 2);
    CHECK(same(d.r[1], R(10, 75, 10, 3)));
    ui_damage_add(&d, R(0, 0, 1000, 1000));        /* the whole screen, appended (not merged) */
    CHECK_EQ(d.n, 3);
    CHECK(same(d.r[2], R(0, 0, 100, 80)));
    ui_damage_add(&d, R(50, 50, 5, 5));            /* inside the screen's entry */
    CHECK_EQ(d.n, 3);
    ui_damage_clear(&d);
    CHECK_EQ(d.n, 0);
    CHECK_EQ(d.width, 100);
}

static void test_damage_full(void)
{
    struct ui_damage d;
    ui_damage_init(&d, 640, 480);
    for (int i = 0; i < UI_DAMAGE_MAX; i++)
        ui_damage_add(&d, R(10 + i * 10, 100 + (i % 4) * 7, 4, 4));
    CHECK_EQ(d.n, UI_DAMAGE_MAX);
    CHECK(same(d.r[31], R(320, 121, 4, 4)));
    ui_damage_add(&d, R(12, 101, 1, 1));           /* inside entry 0: still 32 */
    CHECK_EQ(d.n, UI_DAMAGE_MAX);
    ui_damage_add(&d, R(600, 400, 8, 9));          /* the 33rd: one bounding rectangle */
    CHECK_EQ(d.n, 1);
    CHECK(same(d.r[0], R(10, 100, 598, 309)));
    ui_damage_add(&d, R(20, 110, 5, 5));           /* inside it */
    CHECK_EQ(d.n, 1);
    ui_damage_add(&d, R(0, 0, 3, 3));              /* appended again */
    CHECK_EQ(d.n, 2);
    /* 33 entries, each clipped at an edge: the bound is the screen. */
    ui_damage_init(&d, 64, 48);
    for (int i = 0; i < UI_DAMAGE_MAX; i++)
        ui_damage_add(&d, R(i * 2 - 1, i % 2 ? -5 : 40, 3, 20));
    CHECK_EQ(d.n, UI_DAMAGE_MAX);
    CHECK(same(d.r[0], R(0, 40, 2, 8)));
    CHECK(same(d.r[1], R(1, 0, 3, 15)));
    ui_damage_add(&d, R(63, 40, 3, 20));
    CHECK_EQ(d.n, 1);
    CHECK(same(d.r[0], R(0, 0, 64, 48)));
}

int main(void)
{
    printf("test_ui_rect\n");
    RUN(test_basics);
    RUN(test_limits);
    RUN(test_damage_basic);
    RUN(test_damage_full);
    return unit_report("test_ui_rect");
}
