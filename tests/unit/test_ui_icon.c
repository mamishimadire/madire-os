/* Unit tests for kernel/display/ui_icon.c and icons.c (the toolkit's
 * stroke icons, 1.3 stage 2): the stroke table's shape (contiguous, every
 * point on the 24 dp grid with room for its stroke); refusals; every icon
 * at every size and scale drawn inside its box only, with something drawn;
 * the plus equal to the minus away from its vertical stroke, and full
 * coverage where strokes cross (the largest coverage, not a sum); the same colours
 * at 4 and 3 bytes per pixel; a clipped icon equal to the unclipped one
 * inside the clip; the M from the splash's copy of kernel/logo.c's
 * strokes. The pixel-exact comparison with tests/desktop_ref.py is part
 * static's. The modules are included directly, except surface_core.c,
 * wallpaper_core.c and coverage_core.c (since 1.3 stage 3), linked as
 * separate files (the Makefile), so they never share a translation unit
 * with the toolkit (their statics). */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"

#define SW  120
#define SH  120
#define BG  0x0A1325u
#define FG  0xFAF9FEu

static uint8_t buf[SH * SW * 4];
static struct surface surf;
static struct ui_canvas cv;

static void setup(uint32_t bpp)
{
    surface_init(&surf, buf, SW, SH, SW * bpp, bpp);
    ui_canvas_init(&cv, &surf);
    ui_fill(&cv, (struct rect){ 0, 0, SW, SH }, BG);
}

static uint32_t at(int x, int y)
{
    return surface_get(&surf, (uint32_t)x, (uint32_t)y);
}

static void test_table(void)
{
    uint32_t next_poly = 0, next_point = 0;
    for (int i = 0; i < ICON_STROKED; i++) {
        CHECK_EQ(icon_defs[i].first_poly, next_poly);
        CHECK(icon_defs[i].polys > 0);
        for (uint32_t p = 0; p < icon_defs[i].polys; p++) {
            const struct icon_poly *poly = &icon_polys[next_poly + p];
            CHECK_EQ(poly->first, next_point);
            CHECK(poly->count > 0);
            for (uint32_t k = 0; k < poly->count; k++) {
                const int16_t *pt = icon_points[poly->first + k];
                CHECK(pt[0] >= ICON_RADIUS && pt[0] <= ICON_GRID - ICON_RADIUS);
                CHECK(pt[1] >= ICON_RADIUS && pt[1] <= ICON_GRID - ICON_RADIUS);
            }
            next_point += poly->count;
        }
        next_poly += icon_defs[i].polys;
    }
    CHECK_EQ(next_poly, sizeof(icon_polys) / sizeof(icon_polys[0]));
    CHECK_EQ(next_point, 104);
    CHECK_EQ(UI_ICON_COUNT, ICON_STROKED + 1);
    /* The M's strokes are kernel/logo.c's (part static checks the copy). */
    CHECK_EQ(splash_logo_strokes[0][0], 95);
    CHECK_EQ(splash_logo_strokes[3][3], 23);
}

static void test_refusals(void)
{
    setup(4);
    CHECK(!ui_icon_draw(&cv, -1, 0, 0, 24, FG));
    CHECK(!ui_icon_draw(&cv, UI_ICON_COUNT, 0, 0, 24, FG));
    CHECK(!ui_icon_draw(&cv, UI_ICON_CLOSE, 0, 0, 0, FG));
    CHECK(!ui_icon_draw(&cv, UI_ICON_CLOSE, 0, 0, UI_ICON_MAX_PX + 1, FG));
    CHECK(ui_icon_draw(&cv, UI_ICON_CLOSE, -500, -500, 24, FG));   /* off the surface: drawn as nothing */
    int changed = 0;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++)
            changed += at(x, y) != BG;
    CHECK_EQ(changed, 0);
}

static void test_all_in_box(void)
{
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    static const int32_t sizes[4] = { 16, 24, 32, 48 };
    int bad = 0, empty = 0, drawn = 0;
    for (int icon = 0; icon < UI_ICON_COUNT; icon++)
        for (int s = 0; s < 4; s++)
            for (int z = 0; z < 4; z++) {
                int32_t p = ui_px(sizes[z], scales[s]);
                setup(4);
                drawn += ui_icon_draw(&cv, icon, 10, 7, p, FG);
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
    CHECK_EQ(drawn, UI_ICON_COUNT * 16);
    CHECK_EQ(bad, 0);
    CHECK_EQ(empty, 0);
}

static void test_compose(void)
{
    /* The plus is the minus and a vertical stroke at the centre, the
     * largest coverage of the two: away from the vertical stroke (its
     * radius P / 24 and 2 pixels' margin) the two icons are equal. */
    static const int32_t px_[6] = { 16, 20, 24, 30, 48, 96 };
    static uint32_t minus[96 * 96];
    int diff = 0, compared = 0;
    for (int k = 0; k < 6; k++) {
        int32_t p = px_[k];
        setup(4);
        ui_icon_draw(&cv, UI_ICON_MINUS, 0, 0, p, FG);
        for (int y = 0; y < p; y++)
            for (int x = 0; x < p; x++)
                minus[y * 96 + x] = at(x, y);
        setup(4);
        ui_icon_draw(&cv, UI_ICON_PLUS, 0, 0, p, FG);
        for (int y = 0; y < p; y++)
            for (int x = 0; x < p; x++) {
                int64_t off = 2 * x + 1 - p;            /* twice the column's distance from the centre */
                if (off < 0)
                    off = -off;
                if (off * 12 <= p + 48)                 /* |off| / 2 <= P / 24 + 2 */
                    continue;
                compared++;
                diff += at(x, y) != minus[y * 96 + x];
            }
    }
    CHECK(compared > 1000);
    CHECK_EQ(diff, 0);
}

static void test_coverage_max(void)
{
    /* The close icon's strokes cross at the centre: full colour there (the
     * largest coverage, 256, not a sum); anti-aliased edges elsewhere. */
    setup(4);
    ui_icon_draw(&cv, UI_ICON_CLOSE, 0, 0, 48, FG);
    CHECK_EQ(at(23, 23), FG);
    CHECK_EQ(at(24, 24), FG);
    int partial = 0;
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++)
            partial += at(x, y) != FG && at(x, y) != BG;
    CHECK(partial > 0);
}

static void test_formats(void)
{
    static uint32_t four[48 * 48];
    setup(4);
    ui_icon_draw(&cv, UI_ICON_ABOUT, 3, 3, 40, FG);
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++)
            four[y * 48 + x] = at(x, y);
    setup(3);
    ui_icon_draw(&cv, UI_ICON_ABOUT, 3, 3, 40, FG);
    int diff = 0;
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++)
            diff += four[y * 48 + x] != at(x, y);
    CHECK_EQ(diff, 0);
}

static void test_clipped(void)
{
    static uint32_t whole[64 * 64];
    setup(4);
    ui_icon_draw(&cv, UI_ICON_M, 5, 5, 48, 0x2987F3);
    int lit = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            whole[y * 64 + x] = at(x, y);
            lit += at(x, y) != BG;
        }
    CHECK(lit > 200);
    setup(4);
    ui_canvas_clip(&cv, (struct rect){ 20, 0, 13, 40 });
    ui_icon_draw(&cv, UI_ICON_M, 5, 5, 48, 0x2987F3);
    int diff = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int in = x >= 20 && x < 33 && y < 40;
            diff += at(x, y) != (in ? whole[y * 64 + x] : BG);
        }
    CHECK_EQ(diff, 0);
    /* Off the left edge: the visible part equals the same columns drawn whole. */
    setup(4);
    ui_icon_draw(&cv, UI_ICON_RESTART, 40, 0, 48, FG);
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++)
            whole[y * 64 + x] = at(40 + x, y);
    setup(4);
    ui_icon_draw(&cv, UI_ICON_RESTART, -20, 0, 48, FG);
    diff = 0;
    for (int y = 0; y < 48; y++)
        for (int x = 20; x < 48; x++)
            diff += at(x - 20, y) != whole[y * 64 + x];
    CHECK_EQ(diff, 0);
}

int main(void)
{
    printf("test_ui_icon\n");
    RUN(test_table);
    RUN(test_refusals);
    RUN(test_all_in_box);
    RUN(test_compose);
    RUN(test_coverage_max);
    RUN(test_formats);
    RUN(test_clipped);
    return unit_report("test_ui_icon");
}
