/* Unit tests for kernel/display/ui_list.c (1.5 stage 10): the list's and
 * the grid's geometry (the body, the scroll bar's 24 dp column, the page,
 * the items' rectangles, hit testing), the scroll bar's thumb (its least
 * length, its place, a click above or below it, a drag rounded to the
 * nearest line, none when every line fits), the selection (a click, Ctrl+
 * click, Shift+click, a click on no item), the keys in the list and the
 * grid (with Shift ranges; none with Ctrl or Alt), scrolling the focus into
 * view, the wheel (3 rows or one line of cells), the double click (500 ms,
 * 4 dp, the same item) and the picture's marks (the selection's fill and
 * bar, the ring, the divider, the thumb; made-up fonts with no pixels).
 * The pixel-exact comparison with tests/desktop_ref.py is part static's.
 * The modules are included directly, except surface_core.c,
 * wallpaper_core.c and coverage_core.c, linked as separate files (the
 * Makefile). */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/cicons.c"            /* 1.5 stage 12: ui_icon.c draws the colour icons */
#include "kernel/display/ui_cicon.c"
#include "kernel/display/ui_about.c"
#include "kernel/display/ui_textfield.c"
#include "kernel/display/ui_list.c"

#define SW 640
#define SH 300

static uint8_t buf[SH * SW * 4];
static struct surface surf;
static struct ui_canvas cv;

static uint8_t blank_bits[256 * 16];
static const struct font blank = { 8, 16, 1, blank_bits };
static const struct ui_fonts fonts = { &blank, &blank };
static const char *const titles[4] = { "Name", "Size", "Type", "Modified" };

static struct ui_list_item items[20];

static void make_items(uint32_t n)
{
    for (uint32_t i = 0; i < 20; i++) {
        items[i].name = "item";
        items[i].size = "1 KiB";
        items[i].type = "File";
        items[i].modified = "2026-11-20 14:05";
        items[i].icon = UI_ICON_NONE;
        items[i].selected = false;
    }
    (void)n;
}

static int selected(uint32_t n)
{
    int k = 0;
    for (uint32_t i = 0; i < n; i++)
        k += items[i].selected;
    return k;
}

static uint32_t at(int x, int y)
{
    return surface_get(&surf, (uint32_t)x, (uint32_t)y);
}

static struct key_event key(uint8_t code, uint8_t mods)
{
    return (struct key_event){ code, 0, (uint8_t)(KEY_PRESSED | mods), 0 };
}

static enum ui_list_result press(struct ui_list *l, uint8_t code, uint8_t mods)
{
    struct key_event e = key(code, mods);
    return ui_list_key(l, &e);
}

static void list_geometry(void)
{
    struct ui_list l;
    make_items(20);
    ui_list_init(&l, UI_LIST_DETAILS, items, 20, titles, (struct rect){ 10, 20, 600, 266 }, 4);
    struct rect b = ui_list_body(&l), sb = ui_list_sb_rect(&l), r3 = ui_list_item_rect(&l, 3);
    CHECK_EQ(b.x, 10);
    CHECK_EQ(b.y, 48);                      /* below the 28 dp header */
    CHECK_EQ(b.w, 576);                     /* less the 24 dp scroll column */
    CHECK_EQ(b.h, 238);
    CHECK_EQ(sb.x, 586);
    CHECK_EQ(sb.y, 48);
    CHECK_EQ(sb.w, 24);
    CHECK_EQ(sb.h, 238);
    CHECK_EQ(ui_list_columns(&l), 1);
    CHECK_EQ(ui_list_lines(&l), 20);
    CHECK_EQ(ui_list_page(&l), 8);          /* 238 / 28 full rows */
    CHECK_EQ(ui_list_max_top(&l), 12);
    CHECK_EQ(r3.x, 10);
    CHECK_EQ(r3.y, 132);
    CHECK_EQ(r3.w, 576);
    CHECK_EQ(r3.h, 28);
    CHECK_EQ(ui_list_item_at(&l, 20, 49), 0);
    CHECK_EQ(ui_list_item_at(&l, 20, 277), 8);      /* the partly shown row */
    CHECK_EQ(ui_list_item_at(&l, 590, 60), -1);     /* the scroll column */
    CHECK_EQ(ui_list_item_at(&l, 20, 30), -1);      /* the header */
    l.top = 12;
    CHECK_EQ(ui_list_item_at(&l, 20, 49), 12);
    /* At 125 %: the header 35, rows 35, the column 30. */
    ui_list_init(&l, UI_LIST_DETAILS, items, 20, titles, (struct rect){ 0, 0, 600, 300 }, 5);
    b = ui_list_body(&l);
    CHECK_EQ(b.y, 35);
    CHECK_EQ(b.w, 570);
    CHECK_EQ(ui_list_page(&l), 7);          /* 265 / 35 */
}

static void grid_geometry(void)
{
    struct ui_list l;
    make_items(14);
    ui_list_init(&l, UI_LIST_GRID, items, 14, 0, (struct rect){ 0, 0, 504, 216 }, 4);
    struct rect r7 = ui_list_item_rect(&l, 7);
    CHECK_EQ(ui_list_body(&l).y, 0);        /* no header */
    CHECK_EQ(ui_list_columns(&l), 5);       /* 480 / 96 */
    CHECK_EQ(ui_list_lines(&l), 3);
    CHECK_EQ(ui_list_page(&l), 2);          /* 216 / 88 */
    CHECK_EQ(ui_list_max_top(&l), 1);
    CHECK_EQ(r7.x, 192);
    CHECK_EQ(r7.y, 88);
    CHECK_EQ(r7.w, 96);
    CHECK_EQ(r7.h, 88);
    CHECK_EQ(ui_list_item_at(&l, 200, 100), 7);
    CHECK_EQ(ui_list_item_at(&l, 479, 0), 4);
    CHECK_EQ(ui_list_item_at(&l, 490, 0), -1);
    CHECK_EQ(ui_list_item_at(&l, 0, 180), 10);
    CHECK_EQ(ui_list_item_at(&l, 400, 180), -1);    /* past the last item */
    ui_list_init(&l, UI_LIST_GRID, items, 14, 0, (struct rect){ 0, 0, 500, 216 }, 4);
    CHECK_EQ(ui_list_columns(&l), 4);       /* 476 / 96 */
    ui_list_init(&l, UI_LIST_GRID, items, 14, 0, (struct rect){ 0, 0, 60, 216 }, 4);
    CHECK_EQ(ui_list_columns(&l), 1);       /* at least one */
}

static void scrollbar(void)
{
    struct rect sb = { 0, 0, 24, 160 }, th;
    /* The track is 152 px; 10 of 100 lines: 15 px, so the least 24 dp. */
    CHECK(ui_scrollbar_thumb(sb, 100, 10, 0, 4, &th));
    CHECK_EQ(th.x, 8);
    CHECK_EQ(th.y, 4);
    CHECK_EQ(th.w, 8);
    CHECK_EQ(th.h, 24);
    CHECK(ui_scrollbar_thumb(sb, 100, 10, 45, 4, &th));
    CHECK_EQ(th.y, 68);                     /* 4 + 128 x 45 / 90 */
    CHECK(ui_scrollbar_thumb(sb, 100, 10, 90, 4, &th));
    CHECK_EQ(th.y, 132);
    CHECK(ui_scrollbar_thumb(sb, 100, 10, 500, 4, &th));
    CHECK_EQ(th.y, 132);                    /* top clamped */
    CHECK(ui_scrollbar_thumb(sb, 20, 10, 10, 4, &th));
    CHECK_EQ(th.h, 76);                     /* 152 x 10 / 20 */
    CHECK_EQ(th.y, 80);
    CHECK(ui_scrollbar_thumb(sb, 11, 10, 1, 4, &th));
    CHECK_EQ(th.h, 138);                    /* 1520 / 11 */
    CHECK_EQ(th.y, 18);
    CHECK(!ui_scrollbar_thumb(sb, 10, 10, 0, 4, &th));
    CHECK(!ui_scrollbar_thumb(sb, 5, 10, 0, 4, &th));
    CHECK(!ui_scrollbar_thumb(sb, 5, 0, 0, 4, &th));
    CHECK(!ui_scrollbar_thumb((struct rect){ 0, 0, 24, 8 }, 100, 10, 0, 4, &th));
    /* At 200 %: the column 48, the thumb 16 wide, at least 48 long. */
    CHECK(ui_scrollbar_thumb((struct rect){ 0, 0, 48, 400 }, 100, 10, 0, 8, &th));
    CHECK_EQ(th.x, 16);
    CHECK_EQ(th.y, 8);
    CHECK_EQ(th.w, 16);
    CHECK_EQ(th.h, 48);
    CHECK_EQ(ui_scrollbar_hit(sb, 100, 10, 0, 4, 12, 5), UI_SB_HIT_THUMB);
    CHECK_EQ(ui_scrollbar_hit(sb, 100, 10, 0, 4, 1, 27), UI_SB_HIT_THUMB);   /* the whole column's width */
    CHECK_EQ(ui_scrollbar_hit(sb, 100, 10, 0, 4, 23, 28), UI_SB_HIT_BELOW);
    CHECK_EQ(ui_scrollbar_hit(sb, 100, 10, 0, 4, 12, 100), UI_SB_HIT_BELOW);
    CHECK_EQ(ui_scrollbar_hit(sb, 100, 10, 90, 4, 12, 10), UI_SB_HIT_ABOVE);
    CHECK_EQ(ui_scrollbar_hit(sb, 100, 10, 90, 4, 24, 10), UI_SB_HIT_NONE);
    CHECK_EQ(ui_scrollbar_hit(sb, 10, 10, 0, 4, 12, 10), UI_SB_HIT_NONE);
    CHECK_EQ(ui_scrollbar_drag(sb, 100, 10, 4, 68), 45);    /* (64 x 90 + 64) / 128 */
    CHECK_EQ(ui_scrollbar_drag(sb, 100, 10, 4, 0), 0);
    CHECK_EQ(ui_scrollbar_drag(sb, 100, 10, 4, 132), 90);
    CHECK_EQ(ui_scrollbar_drag(sb, 100, 10, 4, 500), 90);
    CHECK_EQ(ui_scrollbar_drag(sb, 100, 10, 4, 5), 1);      /* (1 x 90 + 64) / 128 */
    CHECK_EQ(ui_scrollbar_drag(sb, 10, 10, 4, 50), 0);
}

static void selection(void)
{
    struct ui_list l;
    make_items(10);
    ui_list_init(&l, UI_LIST_DETAILS, items, 10, titles, (struct rect){ 10, 20, 600, 266 }, 4);
    l.ring = true;
    CHECK(ui_list_click(&l, 3, 0));
    CHECK_EQ(selected(10), 1);
    CHECK(items[3].selected);
    CHECK_EQ(l.cursor, 3);
    CHECK_EQ(l.anchor, 3);
    CHECK(!l.ring);
    CHECK(ui_list_click(&l, 6, KEY_SHIFT));
    CHECK_EQ(selected(10), 4);              /* 3 to 6 */
    CHECK(items[3].selected && items[6].selected && !items[7].selected);
    CHECK_EQ(l.anchor, 3);
    CHECK_EQ(l.cursor, 6);
    CHECK(ui_list_click(&l, 8, KEY_CTRL));
    CHECK_EQ(selected(10), 5);
    CHECK_EQ(l.anchor, 8);
    CHECK(ui_list_click(&l, 8, KEY_CTRL));
    CHECK_EQ(selected(10), 4);
    CHECK(!items[8].selected);
    CHECK(ui_list_click(&l, 5, KEY_SHIFT));
    CHECK_EQ(selected(10), 4);              /* 5 to 8 */
    CHECK(items[5].selected && items[8].selected && !items[4].selected);
    CHECK(!ui_list_click(&l, -1, KEY_CTRL));
    CHECK_EQ(selected(10), 4);
    CHECK(ui_list_click(&l, -1, 0));
    CHECK_EQ(selected(10), 0);
    CHECK(!ui_list_click(&l, -1, 0));
    CHECK(!ui_list_click(&l, 10, KEY_SHIFT));
    ui_list_select_all(&l);
    CHECK_EQ(selected(10), 10);
}

static void list_keys(void)
{
    struct ui_list l;
    make_items(20);
    ui_list_init(&l, UI_LIST_DETAILS, items, 20, titles, (struct rect){ 10, 20, 600, 266 }, 4);
    CHECK_EQ(press(&l, KEY_DOWN, 0), UI_LIST_MOVED);
    CHECK_EQ(l.cursor, 1);
    CHECK(l.ring);
    CHECK(items[1].selected && selected(20) == 1);
    press(&l, KEY_DOWN, KEY_SHIFT);
    press(&l, KEY_DOWN, KEY_SHIFT);
    CHECK_EQ(l.cursor, 3);
    CHECK_EQ(l.anchor, 1);
    CHECK_EQ(selected(20), 3);
    press(&l, KEY_END, 0);
    CHECK_EQ(l.cursor, 19);
    CHECK_EQ(selected(20), 1);
    CHECK_EQ(l.top, 12);                    /* scrolled into view */
    press(&l, KEY_HOME, 0);
    CHECK_EQ(l.cursor, 0);
    CHECK_EQ(l.top, 0);
    press(&l, KEY_PGDN, 0);
    CHECK_EQ(l.cursor, 8);
    CHECK_EQ(l.top, 1);
    press(&l, KEY_PGUP, 0);
    CHECK_EQ(l.cursor, 0);
    CHECK_EQ(l.top, 0);
    CHECK_EQ(press(&l, KEY_UP, 0), UI_LIST_MOVED);
    CHECK_EQ(l.cursor, 0);
    CHECK_EQ(press(&l, KEY_LEFT, 0), UI_LIST_NONE);
    CHECK_EQ(press(&l, KEY_RIGHT, 0), UI_LIST_NONE);
    CHECK_EQ(press(&l, KEY_ENTER, 0), UI_LIST_OPEN);
    CHECK_EQ(press(&l, KEY_KP_ENTER, 0), UI_LIST_OPEN);
    CHECK_EQ(press(&l, KEY_DOWN, KEY_CTRL), UI_LIST_NONE);  /* the caller's (Files, S13) */
    CHECK_EQ(press(&l, KEY_DOWN, KEY_ALT), UI_LIST_NONE);
    CHECK_EQ(press(&l, KEY_F2, 0), UI_LIST_NONE);
    struct key_event rel = { KEY_DOWN, 0, 0, 0 };
    CHECK_EQ(ui_list_key(&l, &rel), UI_LIST_NONE);
    CHECK_EQ(l.cursor, 0);
    ui_list_init(&l, UI_LIST_DETAILS, items, 0, titles, (struct rect){ 10, 20, 600, 266 }, 4);
    CHECK_EQ(press(&l, KEY_DOWN, 0), UI_LIST_NONE);         /* an empty folder */
    CHECK_EQ(press(&l, KEY_ENTER, 0), UI_LIST_NONE);
}

static void grid_keys(void)
{
    struct ui_list l;
    make_items(14);
    ui_list_init(&l, UI_LIST_GRID, items, 14, 0, (struct rect){ 0, 0, 504, 216 }, 4);
    press(&l, KEY_RIGHT, 0);
    CHECK_EQ(l.cursor, 1);
    press(&l, KEY_DOWN, 0);
    CHECK_EQ(l.cursor, 6);
    press(&l, KEY_DOWN, 0);
    CHECK_EQ(l.cursor, 11);
    CHECK_EQ(l.top, 1);
    press(&l, KEY_DOWN, 0);
    CHECK_EQ(l.cursor, 11);                 /* no item below */
    press(&l, KEY_LEFT, 0);
    CHECK_EQ(l.cursor, 10);
    press(&l, KEY_UP, 0);
    CHECK_EQ(l.cursor, 5);
    CHECK_EQ(l.top, 1);
    press(&l, KEY_PGUP, 0);
    CHECK_EQ(l.cursor, 0);                  /* 5 < 10: the first line, the same column */
    CHECK_EQ(l.top, 0);
    press(&l, KEY_PGDN, 0);
    CHECK_EQ(l.cursor, 10);
    press(&l, KEY_END, 0);
    CHECK_EQ(l.cursor, 13);
    press(&l, KEY_RIGHT, 0);
    CHECK_EQ(l.cursor, 13);
    press(&l, KEY_LEFT, KEY_SHIFT);
    press(&l, KEY_LEFT, KEY_SHIFT);
    CHECK_EQ(selected(14), 3);              /* 11 to 13 */
    press(&l, KEY_HOME, 0);
    CHECK_EQ(l.cursor, 0);
    CHECK_EQ(selected(14), 1);
}

static void wheel_and_double(void)
{
    struct ui_list l;
    make_items(20);
    ui_list_init(&l, UI_LIST_DETAILS, items, 20, titles, (struct rect){ 10, 20, 600, 266 }, 4);
    ui_list_wheel(&l, 1);
    CHECK_EQ(l.top, 3);
    ui_list_wheel(&l, 5);
    CHECK_EQ(l.top, 12);
    ui_list_wheel(&l, -10);
    CHECK_EQ(l.top, 0);
    ui_list_init(&l, UI_LIST_GRID, items, 14, 0, (struct rect){ 0, 0, 504, 216 }, 4);
    ui_list_wheel(&l, 1);
    CHECK_EQ(l.top, 1);
    ui_list_wheel(&l, 1);
    CHECK_EQ(l.top, 1);
    struct ui_list_clicks k = { 0, 0, 0, -1, false };
    CHECK(!ui_list_double(&k, 0, 10, 10, 2, 4));
    CHECK(ui_list_double(&k, 400, 13, 14, 2, 4));           /* 400 ms, 3 and 4 px */
    CHECK(!ui_list_double(&k, 500, 13, 14, 2, 4));          /* the memory was cleared */
    CHECK(!ui_list_double(&k, 1001, 13, 14, 2, 4));         /* 501 ms */
    CHECK(!ui_list_double(&k, 1100, 18, 14, 2, 4));         /* 5 px */
    CHECK(!ui_list_double(&k, 1200, 18, 14, 3, 4));         /* another item */
    CHECK(ui_list_double(&k, 1700, 18, 14, 3, 4));          /* exactly 500 ms */
    CHECK(!ui_list_double(&k, 1800, 18, 14, -1, 4));
    CHECK(!ui_list_double(&k, 1900, 18, 14, -1, 4));        /* no item: never a double click */
}

static void draw_marks(void)
{
    struct ui_list l;
    surface_init(&surf, buf, SW, SH, SW * 4, 4);
    ui_canvas_init(&cv, &surf);
    ui_fill(&cv, (struct rect){ 0, 0, SW, SH }, UI_BG_DEEP);
    make_items(3);
    items[1].selected = true;
    ui_list_init(&l, UI_LIST_DETAILS, items, 3, titles, (struct rect){ 10, 20, 600, 266 }, 4);
    l.cursor = 2;
    l.ring = true;
    ui_list_draw(&cv, &l, &fonts);
    CHECK_EQ(at(300, 25), UI_SURFACE_WINDOW);
    CHECK_EQ(at(9, 25), UI_BG_DEEP);
    CHECK_EQ(at(300, 47), UI_BORDER);       /* the header's divider */
    CHECK_EQ(at(585, 47), UI_BORDER);
    CHECK_EQ(at(586, 47), UI_SURFACE_WINDOW);
    CHECK_EQ(at(300, 90), UI_SURFACE_ACTIVE);       /* row 1: 76 to 103 */
    CHECK_EQ(at(10, 90), UI_ACCENT);        /* its bar, 2 px, 80 to 99 */
    CHECK_EQ(at(11, 80), UI_ACCENT);
    CHECK_EQ(at(12, 90), UI_SURFACE_ACTIVE);
    CHECK_EQ(at(10, 78), UI_SURFACE_ACTIVE);
    CHECK_EQ(at(10, 100), UI_SURFACE_ACTIVE);
    CHECK_EQ(at(300, 104), UI_ACCENT);      /* row 2's ring */
    CHECK_EQ(at(300, 105), UI_ACCENT);
    CHECK_EQ(at(300, 106), UI_SURFACE_WINDOW);
    CHECK_EQ(at(12, 117), UI_ACCENT);
    CHECK_EQ(at(10, 117), UI_SURFACE_WINDOW);
    CHECK_EQ(at(598, 60), UI_SURFACE_WINDOW);       /* every row fits: no thumb */
    /* 20 rows: the thumb, 230 x 8 / 20 = 92 px from y 52, x 594 to 601. */
    make_items(20);
    ui_list_init(&l, UI_LIST_DETAILS, items, 20, titles, (struct rect){ 10, 20, 600, 266 }, 4);
    ui_list_draw(&cv, &l, &fonts);
    CHECK_EQ(at(597, 100), UI_TEXT_MUTED);
    CHECK_EQ(at(594, 100), UI_TEXT_MUTED);
    CHECK_EQ(at(593, 100), UI_SURFACE_WINDOW);
    CHECK_EQ(at(602, 100), UI_SURFACE_WINDOW);
    CHECK_EQ(at(597, 143), UI_TEXT_MUTED);
    CHECK_EQ(at(597, 145), UI_SURFACE_WINDOW);
    CHECK_EQ(at(594, 52), UI_SURFACE_WINDOW);       /* the rounded end */
    l.sb = UI_SB_DRAG;
    ui_list_draw(&cv, &l, &fonts);
    CHECK_EQ(at(597, 100), UI_TEXT);
    /* The grid: cell 1 selected, the ring on cell 7. */
    ui_fill(&cv, (struct rect){ 0, 0, SW, SH }, UI_BG_DEEP);
    make_items(14);
    items[1].selected = true;
    ui_list_init(&l, UI_LIST_GRID, items, 14, 0, (struct rect){ 10, 20, 504, 216 }, 4);
    l.cursor = 7;
    l.ring = true;
    ui_list_draw(&cv, &l, &fonts);
    CHECK_EQ(at(150, 60), UI_SURFACE_ACTIVE);       /* the shape (108, 22) 92 x 84 */
    CHECK_EQ(at(107, 60), UI_SURFACE_WINDOW);
    CHECK_EQ(at(150, 102), UI_ACCENT);      /* its bar: y 102 and 103 */
    CHECK_EQ(at(150, 103), UI_ACCENT);
    CHECK_EQ(at(150, 101), UI_SURFACE_ACTIVE);
    CHECK_EQ(at(150, 104), UI_SURFACE_ACTIVE);
    CHECK_EQ(at(111, 102), UI_SURFACE_ACTIVE);
    CHECK_EQ(at(112, 102), UI_ACCENT);
    CHECK_EQ(at(250, 110), UI_ACCENT);      /* cell 7's ring */
    CHECK_EQ(at(250, 111), UI_ACCENT);
    CHECK_EQ(at(250, 112), UI_SURFACE_WINDOW);
    CHECK_EQ(at(501, 90), UI_TEXT_MUTED);   /* the thumb: 208 x 2 / 3 = 138 px from y 24 */
    CHECK_EQ(at(501, 161), UI_TEXT_MUTED);
    CHECK_EQ(at(501, 163), UI_SURFACE_WINDOW);
}

int main(void)
{
    printf("test_ui_list\n");
    RUN(list_geometry);
    RUN(grid_geometry);
    RUN(scrollbar);
    RUN(selection);
    RUN(list_keys);
    RUN(grid_keys);
    RUN(wheel_and_double);
    RUN(draw_marks);
    return unit_report("test_ui_list");
}
