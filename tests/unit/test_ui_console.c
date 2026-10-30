/* Unit tests for kernel/display/ui_console.c (the Console window's client,
 * 1.3 stage 4b) and the key routing of kernel/display/ui_window.c
 * (ui_wm_key): the client's place and the window's size at every scale; a
 * cell away from the bottom corners equal, byte for byte, to
 * fbcon_draw_cell's at the client's place (fbcon_layout_at); the bottom
 * corner cells (with the cursor) drawn only inside the window's inner
 * rounded shape, every other pixel untouched; the whole window with its
 * grid painted on its own surface and composed through ui_blit_round equal
 * to the window painted on the screen directly and to the exposure path
 * in odd tiles, at every scale, at both pixel formats, partly off the
 * screen (docs/plans/1.3.md, C4, A1); where keys go (no window focused,
 * the Console window, the About window; decision 7 as amended in 3b);
 * hiding the Console window with its close button and opening it again
 * with a key. Fonts are made here (a fixed bit pattern). The toolkit's
 * modules and fbcon_core.c are included directly; surface_core.c,
 * wallpaper_core.c and coverage_core.c are linked (the Makefile). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/ui_rect.c"
#include "kernel/display/ui_draw.c"
#include "kernel/display/icons.c"
#include "kernel/display/ui_icon.c"
#include "kernel/display/ui_widget.c"
#include "kernel/display/ui_window.c"
#include "kernel/display/fbcon_core.c"
#include "kernel/display/ui_console.c"

static uint8_t tc_bits[8][256 * 32 * 2];
static struct font tc_store[8];
static const uint32_t tc_scales[4] = { 4, 5, 6, 8 };

static struct ui_fonts tc_fonts(uint32_t s)
{
    static const uint8_t w[4] = { 8, 10, 12, 16 }, h[4] = { 16, 20, 24, 32 };
    int i = s == 4 ? 0 : s == 5 ? 1 : s == 6 ? 2 : 3;
    for (int b = 0; b < 2; b++) {
        struct font *f = &tc_store[2 * i + b];
        if (!f->bits) {
            uint8_t rb = (uint8_t)((w[i] + 7) / 8);
            for (uint32_t k = 0; k < 256u * h[i] * rb; k++)
                tc_bits[2 * i + b][k] = (uint8_t)((k * (37u + 6u * b) + (k >> 3)) & (b ? 0xB5u : 0xA5u));
            *f = (struct font){ w[i], h[i], rb, tc_bits[2 * i + b] };
        }
    }
    return (struct ui_fonts){ &tc_store[2 * i], &tc_store[2 * i + 1] };
}

/* The regular fonts of the four scales, in fbcon_layout's order. */
static const struct font *const *tc_regular(void)
{
    static const struct font *fl[4];
    for (int i = 0; i < 4; i++)
        fl[i] = tc_fonts(tc_scales[i]).regular;
    return fl;
}

/* A grid: every character, colours by row and column, a full block in
 * both bottom corners, the cursor in the bottom-left one. */
static uint16_t tc_cells[UI_CONSOLE_COLS * UI_CONSOLE_ROWS];
static const uint8_t tc_palette[16][3] = {
    { 0, 0, 0 }, { 0, 0, 42 }, { 0, 42, 0 }, { 0, 42, 42 }, { 42, 0, 0 }, { 42, 0, 42 }, { 42, 21, 0 },
    { 42, 42, 42 }, { 21, 21, 21 }, { 21, 21, 63 }, { 21, 63, 21 }, { 21, 63, 63 }, { 63, 21, 21 },
    { 63, 21, 63 }, { 63, 63, 21 }, { 63, 63, 63 },
};

static struct ui_console_grid tc_grid(void)
{
    for (uint32_t i = 0; i < UI_CONSOLE_COLS * UI_CONSOLE_ROWS; i++) {
        uint32_t row = i / UI_CONSOLE_COLS, col = i % UI_CONSOLE_COLS;
        tc_cells[i] = (uint16_t)(((i * 7 + row) & 0xFF) | ((row + col) % 16) << 8 | ((row / 5) % 8) << 12);
    }
    tc_cells[24 * 80] = 0x1FDB;
    tc_cells[24 * 80 + 79] = 0x1FDB;
    return (struct ui_console_grid){ tc_cells, tc_palette, true, 24, 0 };
}

static void tc_paint(const struct ui_canvas *c, struct rect r, void *ctx)
{
    uint32_t s = *(const uint32_t *)ctx;
    struct ui_console_grid g = tc_grid();
    ui_console_paint(c, r, s, tc_fonts(s).regular, &g);
}

static struct ui_window tc_win(const char *name, int32_t w, int32_t h, bool esc)
{
    return (struct ui_window){ .name = name, .title = name, .icon = UI_ICON_CONSOLE, .w = w, .h = h,
                               .esc_closes = esc };
}

static bool tc_same(struct rect a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

static void test_client_and_size(void)
{
    static const int32_t want[4][4] = {     /* edge, title bar, width, height */
        { 1, 32, 642, 433 }, { 1, 40, 802, 541 }, { 2, 48, 964, 650 }, { 2, 64, 1284, 866 },
    };
    for (int i = 0; i < 4; i++) {
        uint32_t s = tc_scales[i];
        const struct font *f = tc_fonts(s).regular;
        int32_t w, h;
        ui_console_size(s, f, &w, &h);
        CHECK(w == want[i][2] && h == want[i][3]);
        struct rect c = ui_console_client((struct rect){ 100, 60, w, h }, s, f);
        CHECK(tc_same(c, 100 + want[i][0], 60 + want[i][1], 80 * (int32_t)f->width, 25 * (int32_t)f->height));
        CHECK(c.x + c.w == 100 + w - want[i][0] && c.y + c.h == 60 + h - want[i][0]);   /* edge to edge */
    }
}

/* A cell away from the corners: fbcon_draw_cell's pixels at the client. */
static void test_cell_as_fbcon(void)
{
    for (int i = 0; i < 4; i++) {
        uint32_t s = tc_scales[i];
        const struct font *f = tc_fonts(s).regular;
        for (uint32_t bpp = 3; bpp <= 4; bpp++) {
            uint32_t W = 1400, H = 950;
            uint8_t *a = malloc((size_t)W * H * bpp), *b = malloc((size_t)W * H * bpp);
            memset(a, 0x3C, (size_t)W * H * bpp);
            memset(b, 0x3C, (size_t)W * H * bpp);
            struct surface sa, sb;
            surface_init(&sa, a, W, H, W * bpp, bpp);
            surface_init(&sb, b, W, H, W * bpp, bpp);
            int32_t w, h;
            ui_console_size(s, f, &w, &h);
            struct rect r = { 37, 21, w, h };
            struct rect cl = ui_console_client(r, s, f);
            struct fbcon_layout lay;
            CHECK(fbcon_layout_at(s, tc_regular(), (uint32_t)cl.x, (uint32_t)cl.y, &lay));
            struct ui_canvas c;
            ui_canvas_init(&c, &sa);
            static const uint32_t cells[5][2] = { { 0, 0 }, { 0, 79 }, { 12, 40 }, { 23, 0 }, { 24, 40 } };
            for (int k = 0; k < 5; k++) {
                ui_console_cell(&c, r, s, f, cells[k][0], cells[k][1], (uint8_t)(0x41 + k), 0xF0E0D0, 0x102030,
                                k == 2);
                fbcon_draw_cell(&sb, &lay, cells[k][0], cells[k][1], (uint8_t)(0x41 + k), 0xF0E0D0, 0x102030,
                                k == 2);
            }
            CHECK(memcmp(a, b, (size_t)W * H * bpp) == 0);
            /* Outside the grid: nothing. */
            ui_console_cell(&c, r, s, f, 25, 0, 'x', 0xFFFFFF, 0, false);
            ui_console_cell(&c, r, s, f, 0, 80, 'x', 0xFFFFFF, 0, false);
            CHECK(memcmp(a, b, (size_t)W * H * bpp) == 0);
            free(a);
            free(b);
        }
    }
}

/* The bottom corner cells: inside the inner rounded shape fbcon's pixels,
 * outside it nothing written (the window's edge stays whole). */
static void test_corner_cells(void)
{
    for (int i = 0; i < 4; i++) {
        uint32_t s = tc_scales[i];
        const struct font *f = tc_fonts(s).regular;
        uint32_t W = 1400, H = 950, bpp = 4;
        uint8_t *a = malloc((size_t)W * H * bpp), *b = malloc((size_t)W * H * bpp);
        memset(a, 0x3C, (size_t)W * H * bpp);
        memset(b, 0x3C, (size_t)W * H * bpp);
        struct surface sa, sb;
        surface_init(&sa, a, W, H, W * bpp, bpp);
        surface_init(&sb, b, W, H, W * bpp, bpp);
        int32_t w, h;
        ui_console_size(s, f, &w, &h);
        struct rect r = { 11, 7, w, h };
        struct rect cl = ui_console_client(r, s, f);
        struct fbcon_layout lay;
        fbcon_layout_at(s, tc_regular(), (uint32_t)cl.x, (uint32_t)cl.y, &lay);
        struct ui_canvas c;
        ui_canvas_init(&c, &sa);
        ui_console_cell(&c, r, s, f, 24, 0, 0xDB, 0xFFFFFF, 0x0000AA, true);
        ui_console_cell(&c, r, s, f, 24, 79, 0xDB, 0xFFFFFF, 0x0000AA, false);
        fbcon_draw_cell(&sb, &lay, 24, 0, 0xDB, 0xFFFFFF, 0x0000AA, true);
        fbcon_draw_cell(&sb, &lay, 24, 79, 0xDB, 0xFFFFFF, 0x0000AA, false);
        int32_t e = ui_window_edge(s);
        struct rect in = ui_rect_inflate(r, -e);
        int32_t rin = ui_window_radius(s) - e;
        uint32_t cut = 0, kept = 0, bad = 0;
        for (int32_t y = cl.y; y < cl.y + cl.h; y++)
            for (int32_t x = cl.x; x < cl.x + cl.w; x++) {
                int64_t x0, x1;
                bool inside = ui_round_span(in, rin, UI_CORNERS_ALL, y, &x0, &x1) && x >= x0 && x < x1;
                uint32_t got = surface_get(&sa, (uint32_t)x, (uint32_t)y);
                uint32_t want = inside ? surface_get(&sb, (uint32_t)x, (uint32_t)y) : 0x3C3C3C;
                bad += got != want;
                if (!inside)
                    cut++;
                else if (surface_get(&sb, (uint32_t)x, (uint32_t)y) != 0x3C3C3C)
                    kept++;
            }
        CHECK(bad == 0);
        CHECK(cut > 0 && kept > 0);                 /* the shape cut some pixels, and drew the rest */
        /* The pixel at the grid's very corner is outside the shape. */
        CHECK_EQ(surface_get(&sa, (uint32_t)cl.x, (uint32_t)(cl.y + cl.h - 1)), 0x3C3C3C);
        CHECK_EQ(surface_get(&sa, (uint32_t)(cl.x + cl.w - 1), (uint32_t)(cl.y + cl.h - 1)), 0x3C3C3C);
        free(a);
        free(b);
    }
}

/* The Console window drawn directly, from its surface through ui_blit_round
 * (tiles) and on exposure (other tiles): the same pixels. */
static void tc_check_window(uint32_t s, uint32_t bpp, int32_t x, int32_t y, bool active)
{
    uint32_t W = 1500, H = 1000;
    struct ui_fonts f = tc_fonts(s);
    int32_t w, h;
    ui_console_size(s, f.regular, &w, &h);
    struct rect r = { x, y, w, h };
    struct ui_window win = tc_win("Console", w, h, false);
    win.paint = tc_paint;
    win.ctx = &s;
    uint8_t *a = malloc((size_t)W * H * bpp), *b = malloc((size_t)W * H * bpp);
    uint8_t *sp = malloc((size_t)w * h * bpp);
    for (size_t i = 0; i < (size_t)W * H * bpp; i++)
        a[i] = b[i] = (uint8_t)(i * 11 + (i >> 10));
    memset(sp, 0x5A, (size_t)w * h * bpp);
    struct surface sa, sb, ss;
    surface_init(&sa, a, W, H, W * bpp, bpp);
    surface_init(&sb, b, W, H, W * bpp, bpp);
    surface_init(&ss, sp, (uint32_t)w, (uint32_t)h, (uint32_t)w * bpp, bpp);
    struct ui_canvas ca, cs;
    ui_canvas_init(&ca, &sa);
    ui_window_draw(&ca, &win, r, active, UI_BUTTON_NORMAL, s, &f);
    ui_canvas_init(&cs, &ss);
    ui_window_draw(&cs, &win, (struct rect){ 0, 0, w, h }, active, UI_BUTTON_NORMAL, s, &f);
    for (int32_t ty = -13; ty < (int32_t)H; ty += 61)
        for (int32_t tx = -7; tx < (int32_t)W; tx += 97) {
            struct ui_canvas cb;
            ui_canvas_init(&cb, &sb);
            ui_canvas_clip(&cb, (struct rect){ tx, ty, 97, 61 });
            ui_blit_round(&cb, r, &ss, ui_window_radius(s), UI_CORNERS_ALL);
        }
    CHECK(memcmp(a, b, (size_t)W * H * bpp) == 0);
    for (size_t i = 0; i < (size_t)W * H * bpp; i++)
        b[i] = (uint8_t)(i * 11 + (i >> 10));
    for (int32_t ty = -5; ty < (int32_t)H; ty += 53)
        for (int32_t tx = -11; tx < (int32_t)W; tx += 89) {
            struct ui_canvas cb;
            ui_canvas_init(&cb, &sb);
            ui_canvas_clip(&cb, (struct rect){ tx, ty, 89, 53 });
            ui_window_draw(&cb, &win, r, active, UI_BUTTON_NORMAL, s, &f);
        }
    CHECK(memcmp(a, b, (size_t)W * H * bpp) == 0);
    /* The window's edge whole at the bottom corners: the bottom row of the
     * edge, R pixels in from each side, in the edge's colour. */
    int32_t R = ui_window_radius(s), by = y + h - 1;
    if (by >= 0 && by < (int32_t)H && x + R >= 0 && x + w - 1 - R < (int32_t)W) {
        uint32_t edge = active ? UI_BORDER_WINDOW : UI_BORDER_INACTIVE;
        CHECK_EQ(surface_get(&sa, (uint32_t)(x + R), (uint32_t)by), edge);
        CHECK_EQ(surface_get(&sa, (uint32_t)(x + w - 1 - R), (uint32_t)by), edge);
    }
    free(a);
    free(b);
    free(sp);
}

static void test_surface_equals_exposure(void)
{
    for (int i = 0; i < 4; i++) {
        uint32_t s = tc_scales[i];
        for (uint32_t bpp = 3; bpp <= 4; bpp++) {
            tc_check_window(s, bpp, 30, 20, true);
            tc_check_window(s, bpp, 31, 21, false);
            tc_check_window(s, bpp, -45, 7, true);
            tc_check_window(s, bpp, 1500 - 300, 1000 - 200, false);
        }
    }
}

static struct ui_damage tc_dmg;

static struct ui_wm tc_wm(void)
{
    struct ui_wm m;
    ui_damage_init(&tc_dmg, 1024, 768);
    ui_wm_init(&m, 1024, 768, 728, 4, &tc_dmg);
    return m;
}

static int tc_count(const struct ui_wm *m, enum ui_wm_event_kind k)
{
    int n = 0;
    for (uint32_t i = 0; i < m->n_ev; i++)
        n += m->ev[i].kind == k;
    return n;
}

static void test_key_routing(void)
{
    struct ui_wm m = tc_wm();
    struct ui_window con = tc_win("Console", 642, 433, false), about = tc_win("About", 352, 377, true);
    /* No window focused: a release does nothing; a press opens the Console window. */
    CHECK(ui_wm_key(&m, &con, false, false) == UI_KEY_TAKEN && !con.open && m.n == 0);
    CHECK(ui_wm_key(&m, &con, true, false) == UI_KEY_OPENED);
    CHECK(con.open && m.focus == &con && m.z[m.n - 1] == &con);
    CHECK(tc_count(&m, UI_EV_OPEN) == 1 && tc_count(&m, UI_EV_FOCUS) == 1);
    CHECK(tc_same(con.r, 191, 147, 642, 433));      /* centred above the taskbar */
    /* The Console window focused: every key to the console, Esc included. */
    ui_wm_clear_events(&m);
    CHECK(ui_wm_key(&m, &con, true, false) == UI_KEY_TO_CONSOLE);
    CHECK(ui_wm_key(&m, &con, true, true) == UI_KEY_TO_CONSOLE && con.open);
    CHECK(ui_wm_key(&m, &con, false, true) == UI_KEY_TO_CONSOLE && con.open);
    CHECK(m.n_ev == 0);
    /* The About window focused: keys do nothing, Esc pressed closes it. */
    ui_wm_open(&m, &about);
    CHECK(m.focus == &about);
    CHECK(ui_wm_key(&m, &con, true, false) == UI_KEY_TAKEN && about.open && m.focus == &about);
    CHECK(ui_wm_key(&m, &con, false, true) == UI_KEY_TAKEN && about.open);
    ui_wm_clear_events(&m);
    CHECK(ui_wm_key(&m, &con, true, true) == UI_KEY_TAKEN && !about.open);
    CHECK(tc_count(&m, UI_EV_CLOSE_ESC) == 1 && m.focus == &con);   /* the focus passes to the Console window */
    CHECK(ui_wm_key(&m, &con, false, true) == UI_KEY_TO_CONSOLE);   /* Esc's release: the console's */
    /* A click on the desktop: no window focused; a key shows the Console
     * window again (raised, focused, not moved) and is not typed. */
    ui_wm_pointer(&m, 5, 5, true, false);
    ui_wm_pointer(&m, 5, 5, false, true);
    CHECK(m.focus == 0);
    ui_wm_open(&m, &about);
    ui_wm_pointer(&m, 5, 5, true, false);
    ui_wm_pointer(&m, 5, 5, false, true);
    CHECK(m.focus == 0 && m.z[m.n - 1] == &about);
    CHECK(ui_wm_key(&m, &con, true, false) == UI_KEY_OPENED);
    CHECK(m.focus == &con && m.z[m.n - 1] == &con && tc_same(con.r, 191, 147, 642, 433));
    /* Focus moved by a click: keys follow it. */
    ui_wm_pointer(&m, about.r.x + 100, about.r.y + 16, true, false);
    ui_wm_pointer(&m, about.r.x + 100, about.r.y + 16, false, true);
    if (m.focus == &con) {                          /* About covered: move the Console window away first */
        ui_wm_pointer(&m, con.r.x + 100, con.r.y + 16, true, false);
        ui_wm_pointer(&m, con.r.x + 500, con.r.y + 16, false, true);
        ui_wm_pointer(&m, about.r.x + 20, about.r.y + 16, true, false);
        ui_wm_pointer(&m, about.r.x + 20, about.r.y + 16, false, true);
    }
    CHECK(m.focus == &about);
    CHECK(ui_wm_key(&m, &con, true, false) == UI_KEY_TAKEN);
}

static void test_hide_and_reopen(void)
{
    struct ui_wm m = tc_wm();
    struct ui_window con = tc_win("Console", 642, 433, false);
    CHECK(ui_wm_key(&m, &con, true, false) == UI_KEY_OPENED);
    struct rect was = con.r;
    con.dirty = false;
    struct rect cr = ui_window_close_rect(con.r, 4);
    ui_damage_clear(&tc_dmg);
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, cr.x + 16, cr.y + 16, true, false);
    ui_wm_pointer(&m, cr.x + 16, cr.y + 16, false, true);
    CHECK(!con.open && m.n == 0 && m.focus == 0);
    CHECK(tc_count(&m, UI_EV_CLOSE_BUTTON) == 1 && tc_count(&m, UI_EV_UNFOCUS) == 1);
    bool covered = false;
    for (uint32_t i = 0; i < tc_dmg.n; i++)
        covered |= ui_rect_inside(was, tc_dmg.r[i]);
    CHECK(covered);                                 /* its place shows what is under it again */
    /* A key opens it again where it opened, to be painted whole again. */
    ui_damage_clear(&tc_dmg);
    CHECK(ui_wm_key(&m, &con, true, false) == UI_KEY_OPENED);
    CHECK(con.open && m.focus == &con && con.dirty);
    CHECK(tc_same(con.r, was.x, was.y, was.w, was.h));
    covered = false;
    for (uint32_t i = 0; i < tc_dmg.n; i++)
        covered |= ui_rect_inside(was, tc_dmg.r[i]);
    CHECK(covered);
    /* Dragged, closed and opened again: at its opening place (placement). */
    ui_wm_pointer(&m, con.r.x + 100, con.r.y + 16, true, false);
    ui_wm_pointer(&m, con.r.x + 160, con.r.y + 56, false, true);
    CHECK(tc_same(con.r, was.x + 60, was.y + 40, was.w, was.h));
    cr = ui_window_close_rect(con.r, 4);
    ui_wm_pointer(&m, cr.x + 16, cr.y + 16, true, false);
    ui_wm_pointer(&m, cr.x + 16, cr.y + 16, false, true);
    CHECK(!con.open);
    CHECK(ui_wm_key(&m, &con, true, false) == UI_KEY_OPENED && tc_same(con.r, was.x, was.y, was.w, was.h));
}

int main(void)
{
    printf("test_ui_console\n");
    RUN(test_client_and_size);
    RUN(test_cell_as_fbcon);
    RUN(test_corner_cells);
    RUN(test_surface_equals_exposure);
    RUN(test_key_routing);
    RUN(test_hide_and_reopen);
    return unit_report("test_ui_console");
}
