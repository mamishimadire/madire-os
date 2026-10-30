/* Unit tests for kernel/display/compositor.c (1.3 stage 3; docs/plans/1.3.md,
 * C1): nothing composed or presented without damage; each damaged
 * rectangle drawn by every layer from the bottom up, clipped to it, then
 * presented, in the list's order; a rectangle off the screen ignored, one
 * inside another not composed twice; 33 rectangles become their bounding
 * rectangle; the list emptied by a pass; the layers' pixels inside the
 * rectangle only; the layer limit. The modules are included directly. */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/surface_core.c"
#include "kernel/display/ui_rect.c"
#include "kernel/display/compositor.c"

#define SW 64
#define SH 48

static uint8_t px[SH * SW * 4];
static struct surface back;
static struct compositor comp;

/* What happened, in order: 'a' or 'b' (a layer) or 'p' (present), with the rectangle. */
static char seq[256];
static struct rect seen[256];
static int events;

static void record(char what, struct rect r)
{
    if (events < 256) {
        seq[events] = what;
        seen[events] = r;
        events++;
    }
}

static int same(struct rect a, struct rect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

#define R(x, y, w, h) ((struct rect){ (x), (y), (w), (h) })

static void layer_a(void *ctx, struct surface *s, struct rect clip)
{
    (void)ctx;
    record('a', clip);
    surface_fill(s, clip, 0x112233u);                       /* the whole clip */
}

static void layer_b(void *ctx, struct surface *s, struct rect clip)
{
    (void)ctx;
    record('b', clip);
    struct rect box = ui_rect_intersect(R(10, 10, 20, 20), clip);   /* a box, clipped */
    surface_fill(s, box, 0xAABBCCu);
}

static void present(void *ctx, struct rect r)
{
    (void)ctx;
    record('p', r);
}

static void setup(void)
{
    memset(px, 0, sizeof(px));
    surface_init(&back, px, SW, SH, SW * 4, 4);
    comp_init(&comp, &back, present, 0);
    CHECK(comp_add_layer(&comp, layer_a, 0));
    CHECK(comp_add_layer(&comp, layer_b, 0));
    events = 0;
}

static void test_nothing(void)
{
    setup();
    CHECK(!comp_pending(&comp));
    CHECK_EQ(comp_compose(&comp), 0);
    CHECK_EQ(events, 0);
    CHECK_EQ(comp.passes, 0);
    comp_damage(&comp, R(-20, -20, 10, 10));                /* off the screen */
    comp_damage(&comp, R(5, 5, 0, 7));                      /* empty */
    CHECK(!comp_pending(&comp));
    CHECK_EQ(comp_compose(&comp), 0);
    CHECK_EQ(events, 0);
}

static void test_order(void)
{
    setup();
    comp_damage(&comp, R(0, 0, 8, 8));
    comp_damage(&comp, R(40, 30, 100, 100));                /* clipped to the screen */
    comp_damage(&comp, R(2, 2, 3, 3));                      /* inside the first: nothing */
    CHECK(comp_pending(&comp));
    CHECK_EQ(comp_compose(&comp), 2);
    CHECK_EQ(events, 6);
    CHECK(memcmp(seq, "abpabp", 6) == 0);
    CHECK(same(seen[0], R(0, 0, 8, 8)) && same(seen[1], R(0, 0, 8, 8)) && same(seen[2], R(0, 0, 8, 8)));
    CHECK(same(seen[3], R(40, 30, 24, 18)) && same(seen[5], R(40, 30, 24, 18)));
    CHECK(!comp_pending(&comp));                            /* the list is emptied */
    CHECK_EQ(comp_compose(&comp), 0);
    CHECK_EQ(events, 6);
    CHECK_EQ(comp.passes, 1);
    CHECK_EQ(comp.rects, 2);
}

static void test_full_list(void)
{
    setup();
    for (int i = 0; i < 33; i++)
        comp_damage(&comp, R(1 + i, 2 + (i % 3), 1, 1));
    CHECK_EQ(comp.damage.n, 1);
    CHECK_EQ(comp_compose(&comp), 1);
    CHECK_EQ(events, 3);
    CHECK(same(seen[2], R(1, 2, 33, 3)));
}

static void test_pixels(void)
{
    setup();
    comp_damage(&comp, R(5, 5, 10, 10));
    comp_compose(&comp);
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            uint32_t want = 0;
            if (x >= 5 && x < 15 && y >= 5 && y < 15)
                want = (x >= 10 && y >= 10) ? 0xAABBCCu : 0x112233u;
            if (surface_get(&back, (uint32_t)x, (uint32_t)y) != want) {
                CHECK(0);
                return;
            }
        }
    CHECK(1);
}

static void test_layer_limit(void)
{
    setup();
    for (int i = 2; i < COMP_LAYERS; i++)
        CHECK(comp_add_layer(&comp, layer_a, 0));
    CHECK(!comp_add_layer(&comp, layer_a, 0));
    CHECK_EQ(comp.n_layers, COMP_LAYERS);
}

int main(void)
{
    printf("test_compositor\n");
    RUN(test_nothing);
    RUN(test_order);
    RUN(test_full_list);
    RUN(test_pixels);
    RUN(test_layer_limit);
    return unit_report("test_compositor");
}
