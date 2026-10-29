/* Unit tests for slot 506's third area (kernel/display/desktop_core.c,
 * 1.3 stage 4a; docs/plans/1.3.md, C4): a surface's pages; the area's
 * plan for the cache alone equal to desktop_plan_cache's (stage 3); the
 * fixed order cache, Console, About, each after the ones mapped, the
 * tables each adds; at the quarter share exactly and one page over; the
 * fallbacks: the cache refused and the surfaces placed from page 0, the
 * Console refused and About still placed (smaller), nothing placed; an
 * item of no pages never fits. The toolkit's modules are included
 * directly; surface_core.c, wallpaper_core.c and coverage_core.c are
 * linked (the Makefile). */
#include <stdint.h>
#include "tests/unit/unit.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/desktop_core.c"

static void test_surface_pages(void)
{
    CHECK_EQ(desktop_surface_pages(1024, 768, 4), 768);
    CHECK_EQ(desktop_surface_pages(642, 433, 4), 272);          /* 1,111,944 bytes */
    CHECK_EQ(desktop_surface_pages(352, 377, 4), 130);          /* 530,816 bytes */
    CHECK_EQ(desktop_surface_pages(352, 377, 3), 98);           /* 398,112 bytes */
    CHECK_EQ(desktop_surface_pages(1, 1, 3), 1);
}

/* The cache alone: the same figures as stage 3's plan. */
static void test_cache_alone(void)
{
    static const uint32_t cases[5][5] = {
        { 1024, 768, 4, 771, 31293 }, { 800, 600, 3, 359, 31400 }, { 2560, 1440, 4, 3610, 28000 },
        { 2560, 1440, 4, 3610, 4 * 7219 }, { 1024, 768, 4, 771, 4 * (771 + 768 + 3) - 1 },
    };
    for (int i = 0; i < 5; i++) {
        const uint32_t *c = cases[i];
        struct desktop_cache_plan p;
        struct desktop_area a;
        struct desktop_area_item it;
        bool fits = desktop_plan_cache(c[0], c[1], c[2], c[3], c[4], &p);
        desktop_area_init(&a, c[3], c[4]);
        CHECK_EQ(desktop_area_plan(&a, desktop_surface_pages(c[0], c[1], c[2]), &it), fits);
        CHECK_EQ(it.pages, p.pages);
        CHECK_EQ(it.tables, p.tables);
        CHECK_EQ(it.display, p.display);
        CHECK_EQ(a.share, p.share);
        CHECK_EQ(it.over, p.over);
        CHECK_EQ(it.first, 0);
    }
}

static void test_order(void)
{
    struct desktop_area a;
    struct desktop_area_item cache, console, about;
    desktop_area_init(&a, 771, 31293);                          /* 1024 x 768 x 32 at 128 MiB (stage 3b's run) */
    CHECK(desktop_area_plan(&a, 768, &cache));
    CHECK(cache.first == 0 && cache.tables == 3 && cache.display == 771 + 768 + 3);
    desktop_area_take(&a, &cache);
    CHECK(desktop_area_plan(&a, 272, &console));
    CHECK(console.first == 768 && console.tables == 1);         /* 1040 pages: a third page table */
    desktop_area_take(&a, &console);
    CHECK(desktop_area_plan(&a, 130, &about));
    CHECK(about.first == 1040 && about.tables == 0);            /* 1170 pages: still three */
    CHECK_EQ(about.display, 771 + 1170 + 4);
    desktop_area_take(&a, &about);
    CHECK_EQ(a.pages, 1170);
    /* A refused item takes no place. */
    struct desktop_area_item big;
    CHECK(!desktop_area_plan(&a, 100000, &big));
    desktop_area_take(&a, &big);
    CHECK_EQ(a.pages, 1170);
    CHECK_EQ(big.over, 771 + 1170 + 100000 + 1 + (101170 + 511) / 512 - a.share);
    struct desktop_area_item none;
    CHECK(!desktop_area_plan(&a, 0, &none));
}

static void test_share_edges(void)
{
    /* Exactly at the share, then one page over. */
    uint32_t taken = 771, want = taken + 768 + 3 + 272 + 1;
    struct desktop_area a;
    struct desktop_area_item it;
    desktop_area_init(&a, taken, 4 * want);
    desktop_area_plan(&a, 768, &it);
    desktop_area_take(&a, &it);
    CHECK(desktop_area_plan(&a, 272, &it) && it.display == want && it.over == 0);
    desktop_area_init(&a, taken, 4 * want - 1);
    desktop_area_plan(&a, 768, &it);
    desktop_area_take(&a, &it);
    CHECK(!desktop_area_plan(&a, 272, &it) && it.over == 1);
}

static void test_fallbacks(void)
{
    struct desktop_area a;
    struct desktop_area_item it;
    /* The cache refused (2560 x 1440 at 200 % on a small machine), the surfaces placed from page 0. */
    desktop_area_init(&a, 3610, 4 * (3610 + 1086 + 2 + 500));
    CHECK(!desktop_area_plan(&a, 3600, &it));
    desktop_area_take(&a, &it);
    CHECK(desktop_area_plan(&a, 1086, &it) && it.first == 0 && it.tables == 4);
    desktop_area_take(&a, &it);
    CHECK(desktop_area_plan(&a, 497, &it) && it.first == 1086 && it.tables == 1);
    /* The Console refused, About still placed. */
    desktop_area_init(&a, 3610, 4 * (3610 + 3600 + 9 + 600));
    CHECK(desktop_area_plan(&a, 3600, &it));
    desktop_area_take(&a, &it);
    CHECK(!desktop_area_plan(&a, 1086, &it));
    desktop_area_take(&a, &it);
    CHECK(desktop_area_plan(&a, 497, &it) && it.first == 3600);
    /* Nothing fits. */
    desktop_area_init(&a, 3610, 4 * 3610);
    CHECK(!desktop_area_plan(&a, 1, &it) && it.over == 3);      /* 1 page, its directory and table */
}

int main(void)
{
    printf("test_ui_plan\n");
    RUN(test_surface_pages);
    RUN(test_cache_alone);
    RUN(test_order);
    RUN(test_share_edges);
    RUN(test_fallbacks);
    return unit_report("test_ui_plan");
}
