/* Unit tests for kernel/display/ui_cicon.c and cicons.c (the toolkit's
 * colour icons, 1.5 stage 12; docs/plans/1.5.md, note (12)(a)): the
 * tables' shape (each design's shapes contiguous, 436 points and 149
 * shapes in all, every point on its grid, a rectangle two points, a stroke
 * at most CICON_STROKE_MAX, every colour and trace known); refusals; every
 * icon at both sizes and every scale drawn inside its box only, with
 * something drawn; the 16 dp design up to 40 pixels, the 48 dp one above;
 * the colours at known points (the folder's front `accent`, its tab
 * `accent.hover`, the Trash full's sheets, none in the empty one); a
 * clipped icon equal to the unclipped one inside the clip; ui_icon_draw
 * hands a colour icon over (its colour argument unused). The pixel-exact
 * comparison with tests/desktop_ref.py is part static's. The modules are
 * included directly, except surface_core.c, wallpaper_core.c and
 * coverage_core.c, linked as separate files (the Makefile). */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/cicons.c"
#include "kernel/display/ui_cicon.c"

#define SW  120
#define SH  120
#define BG  0x0A1325u

static uint8_t buf[SH * SW * 4], buf2[SH * SW * 4];
static struct surface surf, surf2;
static struct ui_canvas cv, cv2;

static void setup(void)
{
    surface_init(&surf, buf, SW, SH, SW * 4, 4);
    ui_canvas_init(&cv, &surf);
    ui_fill(&cv, (struct rect){ 0, 0, SW, SH }, BG);
}

static uint32_t at(int x, int y)
{
    return surface_get(&surf, (uint32_t)x, (uint32_t)y);
}

static void test_table(void)
{
    uint32_t next = 0, points = 0;
    for (int i = 0; i < UI_CICON_COUNT; i++)
        for (int z = 0; z < 2; z++) {
            const struct cicon_def *d = &cicon_defs[i][z];
            int32_t grid = z ? CICON_GRID_48 : CICON_GRID_16;
            CHECK_EQ(d->first, next);
            CHECK(d->shapes > 0);
            for (uint32_t k = 0; k < d->shapes; k++) {
                const struct cicon_shape *sh = &cicon_shapes[d->first + k];
                CHECK_EQ(sh->first, points);
                CHECK(sh->kind <= CICON_STROKE);
                CHECK(sh->colour < CI_COLOURS);
                CHECK(sh->trace == CI_TRACE_NONE || sh->trace == CI_TRACE_BG || sh->trace < CI_COLOURS);
                if (sh->kind == CICON_RRECT)
                    CHECK_EQ(sh->count, 2);
                else if (sh->kind == CICON_POLY)
                    CHECK(sh->count >= 3);
                else
                    CHECK(sh->count >= 1 && sh->count <= CICON_STROKE_MAX);
                CHECK(sh->radius >= 0);
                for (uint32_t p = 0; p < sh->count; p++) {
                    const int16_t *pt = cicon_points[sh->first + p];
                    CHECK(pt[0] >= 0 && pt[0] <= grid && pt[1] >= 0 && pt[1] <= grid);
                }
                points += sh->count;
            }
            next += d->shapes;
        }
    CHECK_EQ(next, cicon_shape_count);
    CHECK_EQ(points, cicon_point_count);
    CHECK_EQ(cicon_point_count, 436);       /* T4: counted from the tables by hand */
    CHECK_EQ(cicon_shape_count, 149);
    CHECK_EQ(UI_CICON_COUNT, 14);
    CHECK(UI_CICON_FIRST >= UI_ICON_COUNT);
}

/* The Files window's stroke icons (icons.h's second table, 1.5 stage 12):
 * contiguous, every point with room for its stroke on the 24 dp grid, 177
 * points and 51 polylines in 16 icons; each drawn inside its box; the 1.3
 * list and its count unchanged. */
static void test_stroke2(void)
{
    uint32_t next_poly = 0, next_point = 0;
    for (int i = 0; i < ICON_S12_COUNT; i++) {
        CHECK_EQ(icon_defs2[i].first_poly, next_poly);
        CHECK(icon_defs2[i].polys > 0);
        for (uint32_t p = 0; p < icon_defs2[i].polys; p++) {
            const struct icon_poly *poly = &icon_polys2[next_poly + p];
            CHECK_EQ(poly->first, next_point);
            CHECK(poly->count > 0);
            for (uint32_t k = 0; k < poly->count; k++) {
                const int16_t *pt = icon_points2[poly->first + k];
                CHECK(pt[0] >= ICON_RADIUS && pt[0] <= ICON_GRID - ICON_RADIUS);
                CHECK(pt[1] >= ICON_RADIUS && pt[1] <= ICON_GRID - ICON_RADIUS);
            }
            next_point += poly->count;
        }
        next_poly += icon_defs2[i].polys;
    }
    CHECK_EQ(next_poly, icon_polys2_count);
    CHECK_EQ(next_point, icon_points2_count);
    CHECK_EQ(icon_points2_count, 242);      /* T4: counted from the table by hand (1.5 stage 13: 177 + 65) */
    CHECK_EQ(icon_polys2_count, 63);        /* 51 + cut 4, copy 2, paste 4, restore 2 */
    CHECK_EQ(ICON_S12_COUNT, 20);           /* 16 + cut, copy, paste, restore */
    CHECK_EQ(UI_SICON_END - UI_SICON_FIRST, ICON_S12_COUNT);
    CHECK(UI_SICON_FIRST >= UI_ICON_COUNT && UI_SICON_END <= UI_CICON_FIRST);
    CHECK_EQ(UI_ICON_COUNT, ICON_STROKED + 1);
    for (int icon = UI_SICON_FIRST; icon < UI_SICON_END; icon++) {
        setup();
        CHECK(ui_icon_draw(&cv, icon, 10, 7, 32, UI_TEXT));
        int in = 0, out = 0;
        for (int y = 0; y < SH; y++)
            for (int x = 0; x < SW; x++)
                if (at(x, y) != BG) {
                    if (x >= 10 && x < 42 && y >= 7 && y < 39)
                        in++;
                    else
                        out++;
                }
        CHECK(in > 0);
        CHECK_EQ(out, 0);
    }
    CHECK(!ui_icon_draw(&cv, UI_SICON_END, 0, 0, 16, UI_TEXT));
}

static void test_refusals(void)
{
    setup();
    CHECK(!ui_cicon_draw(&cv, UI_CICON_FIRST - 1, 0, 0, 16));
    CHECK(!ui_cicon_draw(&cv, UI_CICON_END, 0, 0, 16));
    CHECK(!ui_cicon_draw(&cv, UI_ICON_FOLDER, 0, 0, 0));
    CHECK(!ui_cicon_draw(&cv, UI_ICON_FOLDER, 0, 0, UI_ICON_MAX_PX + 1));
    CHECK(!ui_icon_draw(&cv, UI_CICON_END, 0, 0, 16, 0));
    CHECK(ui_cicon_draw(&cv, UI_ICON_FOLDER, -500, -500, 48));     /* off the surface: nothing */
    int changed = 0;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++)
            changed += at(x, y) != BG;
    CHECK_EQ(changed, 0);
}

static void test_all_in_box(void)
{
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    static const int32_t sizes[2] = { 16, 48 };
    int bad = 0, empty = 0, drawn = 0;
    for (int icon = UI_CICON_FIRST; icon < UI_CICON_END; icon++)
        for (int s = 0; s < 4; s++)
            for (int z = 0; z < 2; z++) {
                int32_t p = ui_px(sizes[z], scales[s]);
                setup();
                drawn += ui_cicon_draw(&cv, icon, 10, 7, p);
                int in = 0;
                for (int y = 0; y < SH; y++)
                    for (int x = 0; x < SW; x++) {
                        if (at(x, y) == BG)
                            continue;
                        if (x >= 10 && x < 10 + p && y >= 7 && y < 7 + p)
                            in++;
                        else
                            bad++;
                    }
                empty += in == 0;
            }
    CHECK_EQ(drawn, UI_CICON_COUNT * 8);
    CHECK_EQ(bad, 0);
    CHECK_EQ(empty, 0);
}

static void test_colours(void)
{
    /* The 48 dp folder at 48 px (1 px a dp): the front fills (4, 15)-(44,
     * 40), the tab holds (38, 10), its light line runs along y 15. */
    setup();
    ui_cicon_draw(&cv, UI_ICON_FOLDER, 0, 0, 48);
    CHECK_EQ(at(24, 30), UI_ACCENT);
    CHECK_EQ(at(38, 10), UI_ACCENT_HOVER);
    CHECK_EQ(at(24, 15), UI_ACCENT_LIGHT);
    CHECK_EQ(at(2, 30), BG);
    /* The 16 dp design at 40 px (2.5 px a dp): the front (1.5, 5.5)-(14.5,
     * 13.5); at 41 px the 48 dp design: (4, 15)-(44, 40) scaled. */
    setup();
    ui_cicon_draw(&cv, UI_ICON_FOLDER, 0, 0, 40);
    CHECK_EQ(at(20, 25), UI_ACCENT);
    CHECK_EQ(at(20, 14), UI_ACCENT_LIGHT);          /* y 5.5-6.5 dp: 13.75-16.25 px */
    setup();
    ui_cicon_draw(&cv, UI_ICON_FOLDER, 0, 0, 41);
    CHECK_EQ(at(20, 11), UI_ACCENT_HOVER);          /* the back: y 12-15 dp = 10.25-12.8 px */
    CHECK_EQ(at(20, 14), UI_ACCENT);                /* the front, below its line (12.8-13.7 px) */
    /* The Trash: the full one's sheets, none in the empty one. */
    int light = 0, warn = 0;
    setup();
    ui_cicon_draw(&cv, UI_ICON_TRASH_FULL, 0, 0, 96);
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 96; x++) {
            light += at(x, y) == UI_ACCENT_LIGHT;
            warn += at(x, y) == UI_WARNING;
        }
    CHECK(light > 50 && warn > 50);
    light = warn = 0;
    setup();
    ui_cicon_draw(&cv, UI_ICON_TRASH, 0, 0, 96);
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 96; x++) {
            light += at(x, y) == UI_ACCENT_LIGHT;
            warn += at(x, y) == UI_WARNING;
        }
    CHECK_EQ(light, 0);
    CHECK_EQ(warn, 0);
    /* The link: the file's page and the badge's disc and arrow. */
    setup();
    ui_cicon_draw(&cv, UI_ICON_LINK, 0, 0, 48);
    CHECK_EQ(at(24, 24), UI_TEXT);                  /* the page */
    CHECK_EQ(at(4, 38), UI_SURFACE);                /* the disc, off the arrow */
}

static void test_clip_and_dispatch(void)
{
    surface_init(&surf2, buf2, SW, SH, SW * 4, 4);
    ui_canvas_init(&cv2, &surf2);
    for (int icon = UI_CICON_FIRST; icon < UI_CICON_END; icon++) {
        setup();
        ui_fill(&cv2, (struct rect){ 0, 0, SW, SH }, BG);
        ui_cicon_draw(&cv, icon, 5, 5, 96);
        ui_canvas_clip(&cv2, (struct rect){ 30, 20, 40, 50 });
        ui_icon_draw(&cv2, icon, 5, 5, 96, 0x123456u);  /* handed over; the colour unused */
        int diff = 0, outside = 0;
        for (int y = 0; y < SH; y++)
            for (int x = 0; x < SW; x++) {
                uint32_t b = surface_get(&surf2, (uint32_t)x, (uint32_t)y);
                if (x >= 30 && x < 70 && y >= 20 && y < 70)
                    diff += b != at(x, y);
                else
                    outside += b != BG;
            }
        CHECK_EQ(diff, 0);
        CHECK_EQ(outside, 0);
    }
}

int main(void)
{
    printf("test_ui_cicon\n");
    RUN(test_table);
    RUN(test_stroke2);
    RUN(test_refusals);
    RUN(test_all_in_box);
    RUN(test_colours);
    RUN(test_clip_and_dispatch);
    return unit_report("test_ui_cicon");
}
