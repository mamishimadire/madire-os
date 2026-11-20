/* Unit tests for kernel/display/ui_window.c (windows and the window
 * manager, 1.3 stage 4a): placement (centred above the taskbar, 24 dp
 * further for each window on the same place, clamped); the clamp (never
 * under the taskbar, the title bar on the screen, 48 dp kept on the screen
 * at each side); hit testing from the top down (close button, title bar,
 * body, the taskbar, the desktop); z-order and focus (open, a click
 * raises and focuses, a click on the desktop focuses none, the taskbar
 * does nothing, closing passes the focus to the top window); the drag
 * (capture, 1:1 from the grab, clamped, the event only if it moved); the
 * close button's states (hover, pressed, normal when the pointer leaves,
 * closing only when released on it); Esc; the damage reported; and a
 * window painted on its own surface and composed through ui_blit_round
 * equal, byte for byte, to the window painted on the screen directly, in
 * every state, at every scale, at both pixel formats, partly off the
 * screen and in odd tiles (docs/plans/1.3.md, C4, A1). 1.5 stage 9: the
 * grab band (8 dp, each edge and corner, the buttons first, none when not
 * resizable, maximised or minimised), the resize (capture, 1:1, the
 * minimum, the event, maximise and restore, cancel), extreme pointer
 * values for every edge and corner, and a resized window on a larger
 * surface equal to the window painted directly. 1.5 stage 9b: snapping (the
 * 4 dp zones above the taskbar, the halves, the kept rectangle, no snap for
 * a click, without the maximise button, when not resizable or when the
 * minimum does not fit), dragging away from a maximised or snapped window
 * (with extreme pointer values), and the shortcuts (Super+Up, Down, Left,
 * Right, Pause, Alt+F4, Super+D with its reset, Super+M and Shift+M). Fonts are made
 * here (a fixed bit pattern), so the suite needs no generated font. The
 * toolkit's modules are included directly; surface_core.c,
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

static uint8_t font_bits[8][256 * 32 * 2];
static struct font font_store[8];

static struct ui_fonts fonts_for(uint32_t s)
{
    static const uint8_t w[4] = { 8, 10, 12, 16 }, h[4] = { 16, 20, 24, 32 };
    int i = s == 4 ? 0 : s == 5 ? 1 : s == 6 ? 2 : 3;
    for (int b = 0; b < 2; b++) {
        struct font *f = &font_store[2 * i + b];
        if (!f->bits) {
            uint8_t rb = (uint8_t)((w[i] + 7) / 8);
            for (uint32_t k = 0; k < 256u * h[i] * rb; k++)
                font_bits[2 * i + b][k] = (uint8_t)((k * (37u + 6u * b) + (k >> 3)) & (b ? 0xB5u : 0xA5u));
            *f = (struct font){ w[i], h[i], rb, font_bits[2 * i + b] };
        }
    }
    return (struct ui_fonts){ &font_store[2 * i], &font_store[2 * i + 1] };
}

/* A client: a filled box and a line of text. */
static void client(const struct ui_canvas *c, struct rect r, void *ctx)
{
    const struct ui_fonts *f = ctx;
    ui_fill(c, (struct rect){ r.x + 20, r.y + 50, 40, 30 }, UI_BORDER);
    ui_text(c, f->regular, r.x + 24, r.y + 90, "client text", UI_TEXT);
}

static struct ui_window win(const char *name, int32_t w, int32_t h, bool esc)
{
    return (struct ui_window){ .name = name, .title = name, .icon = UI_ICON_ABOUT, .w = w, .h = h,
                               .esc_closes = esc };
}

static bool same_rect(struct rect a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

static struct ui_damage dmg;

static struct ui_wm wm_1024(void)
{
    struct ui_wm m;
    ui_damage_init(&dmg, 1024, 768);
    ui_wm_init(&m, 1024, 768, 728, 4, &dmg);
    return m;
}

static int count(const struct ui_wm *m, enum ui_wm_event_kind k)
{
    int n = 0;
    for (uint32_t i = 0; i < m->n_ev; i++)
        n += m->ev[i].kind == k;
    return n;
}

static void test_frame_sizes(void)
{
    int32_t w, h;
    ui_window_size_for_client(4, 640, 400, &w, &h);
    CHECK(w == 642 && h == 433);                    /* the Console window at 100 % (C2) */
    ui_window_size_for_client(8, 1280, 800, &w, &h);
    CHECK(w == 1284 && h == 866);
    CHECK_EQ(ui_window_title_h(5), 40);
    CHECK_EQ(ui_window_radius(6), 9);
    struct rect c = ui_window_close_rect((struct rect){ 100, 50, 352, 377 }, 4);
    CHECK(same_rect(c, 420, 50, 32, 32));
}

static void test_place_clamp(void)
{
    struct ui_wm m = wm_1024();
    CHECK(same_rect(ui_wm_place(&m, 352, 377), 336, 175, 352, 377));
    struct ui_window a = win("A", 352, 377, false), b = win("B", 352, 377, false), c = win("C", 352, 377, false);
    CHECK(ui_wm_open(&m, &a));
    CHECK(same_rect(a.r, 336, 175, 352, 377));
    CHECK(ui_wm_open(&m, &b));
    CHECK(same_rect(b.r, 360, 199, 352, 377));      /* 24 dp further: the place was taken */
    CHECK(ui_wm_open(&m, &c));
    CHECK(same_rect(c.r, 384, 223, 352, 377));
    /* The clamp. */
    CHECK(same_rect(ui_wm_clamp(&m, (struct rect){ -1000, -50, 352, 377 }), 48 - 352, 0, 352, 377));
    CHECK(same_rect(ui_wm_clamp(&m, (struct rect){ 2000, 900, 352, 377 }), 1024 - 48, 728 - 377, 352, 377));
    CHECK(same_rect(ui_wm_clamp(&m, (struct rect){ 10, 10, 352, 800 }), 10, 0, 352, 800));  /* taller than the area */
    /* A window larger than the area opens at the top. */
    struct ui_wm m2 = wm_1024();
    CHECK(same_rect(ui_wm_place(&m2, 1100, 900), (1024 - 1100) / 2, 0, 1100, 900));
    /* Placement clamped: a 2560 x 1440 screen at 200 %. */
    struct ui_wm big;
    ui_wm_init(&big, 2560, 1440, 1440 - 80, 8, 0);
    CHECK(same_rect(ui_wm_place(&big, 704, 754), 928, 303, 704, 754));
}

static void test_hit_z_focus(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window a = win("A", 352, 377, false), b = win("B", 352, 377, false), *at;
    ui_wm_open(&m, &a);
    CHECK(m.focus == &a && count(&m, UI_EV_OPEN) == 1 && count(&m, UI_EV_FOCUS) == 1);
    ui_wm_clear_events(&m);
    ui_wm_open(&m, &b);
    CHECK(m.n == 2 && m.z[0] == &a && m.z[1] == &b && m.focus == &b);
    /* Top down: where they overlap, B; A's own corner, A. */
    CHECK(ui_wm_hit(&m, 400, 250, &at) == UI_HIT_BODY && at == &b);
    CHECK(ui_wm_hit(&m, 340, 180, &at) == UI_HIT_TITLE && at == &a);
    CHECK(ui_wm_hit(&m, 336 + 351 - 5, 176, &at) == UI_HIT_CLOSE && at == &a);  /* A's close, left of B */
    CHECK(ui_wm_hit(&m, 360 + 351 - 5, 200, &at) == UI_HIT_CLOSE && at == &b);
    CHECK(ui_wm_hit(&m, 370, 199 + 31, &at) == UI_HIT_TITLE && at == &b);
    CHECK(ui_wm_hit(&m, 370, 199 + 32, &at) == UI_HIT_BODY && at == &b);
    CHECK(ui_wm_hit(&m, 5, 5, &at) == UI_HIT_DESKTOP && !at);
    CHECK(ui_wm_hit(&m, 5, 728, &at) == UI_HIT_TASKBAR && !at);
    CHECK(ui_wm_hit(&m, 5, 727, &at) == UI_HIT_DESKTOP);
    /* A click on A's visible part raises and focuses it. */
    ui_wm_clear_events(&m);
    ui_damage_clear(&dmg);
    ui_wm_pointer(&m, 340, 300, true, false);
    ui_wm_pointer(&m, 340, 300, false, true);
    CHECK(m.z[0] == &b && m.z[1] == &a && m.focus == &a);
    CHECK(count(&m, UI_EV_RAISE) == 1 && count(&m, UI_EV_FOCUS) == 1 && m.ev[0].kind == UI_EV_RAISE);
    CHECK(a.dirty && b.dirty && dmg.n >= 1);
    /* A click on the taskbar does nothing; on the desktop, no window is focused. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 500, 740, true, false);
    ui_wm_pointer(&m, 500, 740, false, true);
    CHECK(m.n_ev == 0 && m.focus == &a);
    ui_wm_pointer(&m, 10, 10, true, false);
    ui_wm_pointer(&m, 10, 10, false, true);
    CHECK(m.focus == 0 && count(&m, UI_EV_UNFOCUS) == 1 && m.z[1] == &a);
    /* A click on the focused top window's body changes nothing. */
    ui_wm_pointer(&m, 400, 300, true, false);
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 400, 300, false, true);
    ui_wm_pointer(&m, 401, 300, true, false);
    ui_wm_pointer(&m, 401, 300, false, true);
    CHECK(m.n_ev == 0);
    /* Opening an open window raises and focuses it. */
    ui_wm_open(&m, &b);
    CHECK(m.z[1] == &b && m.focus == &b && count(&m, UI_EV_OPEN) == 0 && count(&m, UI_EV_RAISE) == 1);
    /* At most UI_WINDOWS_MAX (1.5 stage 8: 6). */
    struct ui_window c = win("C", 100, 100, false), d = win("D", 100, 100, false), e = win("E", 100, 100, false);
    struct ui_window f = win("F", 100, 100, false), g = win("G", 100, 100, false);
    CHECK(ui_wm_open(&m, &c) && ui_wm_open(&m, &d) && ui_wm_open(&m, &e) && ui_wm_open(&m, &f));
    CHECK(!ui_wm_open(&m, &g) && m.n == 6);
}

static void test_drag(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window a = win("A", 352, 377, false), b = win("B", 352, 377, false);
    ui_wm_open(&m, &a);                             /* (336, 175) */
    ui_wm_open(&m, &b);                             /* (360, 199) */
    ui_wm_clear_events(&m);
    /* Press on A's title bar (visible: B starts 24 px lower): A raised, then dragged 1:1. */
    ui_wm_pointer(&m, 400, 180, true, false);
    CHECK(m.drag == &a && m.z[1] == &a);
    ui_damage_clear(&dmg);
    ui_wm_pointer(&m, 410, 185, false, false);
    CHECK(same_rect(a.r, 346, 180, 352, 377));
    CHECK(dmg.n == 2);                              /* where it was and where it is */
    /* Captured: over B's close button nothing hovers. */
    ui_wm_pointer(&m, 360 + 340, 210, false, false);
    CHECK(b.close == UI_BUTTON_NORMAL && a.close == UI_BUTTON_NORMAL);
    /* Down past the taskbar and far left: clamped; back: 1:1 from the grab again. */
    ui_wm_pointer(&m, -500, 760, false, false);
    CHECK(same_rect(a.r, 48 - 352, 728 - 377, 352, 377));
    ui_wm_pointer(&m, 380, 170, false, false);
    CHECK(same_rect(a.r, 316, 165, 352, 377));
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 380, 170, false, true);
    CHECK(!m.drag && count(&m, UI_EV_DRAG) == 1 && same_rect(m.ev[0].r, 316, 165, 352, 377));
    /* A press and release on the title bar without a move: no drag event. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 380, 170, true, false);
    ui_wm_pointer(&m, 380, 170, false, true);
    CHECK(count(&m, UI_EV_DRAG) == 0 && m.n_ev == 0);
    /* Up past the top: the title bar stays on the screen. */
    ui_wm_pointer(&m, 380, 170, true, false);
    ui_wm_pointer(&m, 380, 0, false, false);
    ui_wm_pointer(&m, 380, 0, false, true);
    CHECK(a.r.y == 0);
}

static void test_close_button(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window a = win("A", 352, 377, true), b = win("B", 352, 377, false);
    ui_wm_open(&m, &a);
    ui_wm_open(&m, &b);
    struct rect cb = ui_window_close_rect(b.r, 4);
    int32_t cx = cb.x + cb.w / 2, cy = cb.y + cb.h / 2;
    ui_wm_clear_events(&m);
    ui_damage_clear(&dmg);
    ui_wm_pointer(&m, cx, cy, false, false);
    CHECK(b.close == UI_BUTTON_HOVER && count(&m, UI_EV_CLOSE_STATE) == 1 && m.ev[0].state == UI_BUTTON_HOVER);
    CHECK(dmg.n == 1 && same_rect(dmg.r[0], cb.x, cb.y, cb.w, cb.h));
    ui_wm_pointer(&m, cx, cy, true, false);
    CHECK(b.close == UI_BUTTON_PRESSED && m.pressed == &b);
    ui_wm_pointer(&m, cx - 100, cy, false, false);                  /* the pointer leaves: normal */
    CHECK(b.close == UI_BUTTON_NORMAL && b.open);
    ui_wm_pointer(&m, cx, cy, false, false);
    CHECK(b.close == UI_BUTTON_PRESSED);
    /* Released elsewhere: not closed. */
    ui_wm_pointer(&m, cx - 100, cy, false, true);
    CHECK(b.open && !m.pressed && b.close == UI_BUTTON_NORMAL);
    /* Pressed elsewhere, then moved over it: not pressed (nor hovered while
     * the button is held); released there: not closed, then hovered. The
     * press is in B's body (B: x 360-711, y 199-575, its title bar rows
     * 199-230, its close button x 680-711), below A (rows 175-551): on no
     * title bar, no close button, no other window. */
    struct ui_window *at;
    int32_t ex = cx - 100, ey = cy + 345;                           /* (596, 560) */
    CHECK(ui_wm_hit(&m, ex, ey, &at) == UI_HIT_BODY && at == &b && !ui_rect_contains(a.r, ex, ey));
    ui_wm_pointer(&m, ex, ey, true, false);
    CHECK(!m.drag && !m.pressed);
    ui_wm_pointer(&m, cx, cy, false, false);
    CHECK(b.close == UI_BUTTON_NORMAL);
    ui_wm_pointer(&m, cx, cy, false, true);
    CHECK(b.open && b.close == UI_BUTTON_HOVER);
    /* Pressed and released on it: closed; the focus passes to A. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, cx, cy, true, false);
    ui_wm_pointer(&m, cx, cy, false, true);
    CHECK(!b.open && m.n == 1 && m.focus == &a);
    CHECK(count(&m, UI_EV_CLOSE_BUTTON) == 1 && count(&m, UI_EV_FOCUS) == 1);
    /* Esc closes A (it closes on Esc); then no window: focus none. */
    ui_wm_clear_events(&m);
    CHECK(ui_wm_esc(&m));
    CHECK(!a.open && m.n == 0 && m.focus == 0 && count(&m, UI_EV_CLOSE_ESC) == 1 &&
          count(&m, UI_EV_UNFOCUS) == 1);
    CHECK(!ui_wm_esc(&m));
    /* A window that does not close on Esc. */
    ui_wm_open(&m, &b);
    CHECK(!ui_wm_esc(&m) && b.open);
}

/* A press on B's title bar left of its close button, then a move to where
 * the close button was: B is dragged (its rectangle follows the pointer
 * 1:1, then clamped, and the drag is logged once, at the release); no
 * close button reaches hover or pressed. */
static void test_title_press_drags(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window a = win("A", 352, 377, true), b = win("B", 352, 377, false), *at;
    ui_wm_open(&m, &a);                             /* (336, 175) */
    ui_wm_open(&m, &b);                             /* (360, 199): its title bar rows 199-230 */
    CHECK(ui_wm_hit(&m, 596, 215, &at) == UI_HIT_TITLE && at == &b);
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 596, 215, true, false);
    CHECK(m.drag == &b && !m.pressed);
    ui_wm_pointer(&m, 696, 215, false, false);      /* the close button's centre before the move */
    CHECK(same_rect(b.r, 460, 199, 352, 377));
    CHECK(b.close == UI_BUTTON_NORMAL && a.close == UI_BUTTON_NORMAL);
    ui_wm_pointer(&m, 2000, 300, false, false);     /* asks x 1764: clamped to 1024 - 48 */
    CHECK(same_rect(b.r, 976, 284, 352, 377));
    CHECK(b.close == UI_BUTTON_NORMAL && a.close == UI_BUTTON_NORMAL);
    ui_wm_pointer(&m, 2000, 300, false, true);
    int drags = 0;
    for (uint32_t i = 0; i < m.n_ev; i++)
        if (m.ev[i].kind == UI_EV_DRAG && m.ev[i].w == &b && same_rect(m.ev[i].r, 976, 284, 352, 377))
            drags++;
    CHECK(drags == 1 && count(&m, UI_EV_DRAG) == 1);
    CHECK(count(&m, UI_EV_CLOSE_STATE) == 0 && count(&m, UI_EV_CLOSE_BUTTON) == 0 && b.open);
}

/* The window painted directly against its surface composed through
 * ui_blit_round: equal everywhere, the pixels outside its corners those
 * of the screen under it. */
static void check_surface(uint32_t s, uint32_t bpp, struct rect r, bool active, enum ui_button_state st)
{
    uint32_t W = 1100, H = 900;
    struct ui_fonts f = fonts_for(s);
    struct ui_window w = win("A", r.w, r.h, false);
    w.paint = client;
    w.ctx = &f;
    uint8_t *a = malloc((size_t)W * H * bpp), *b = malloc((size_t)W * H * bpp);
    uint8_t *sp = malloc((size_t)r.w * r.h * bpp);
    for (size_t i = 0; i < (size_t)W * H * bpp; i++)
        a[i] = b[i] = (uint8_t)(i * 7 + (i >> 9));
    memset(sp, 0x5A, (size_t)r.w * r.h * bpp);      /* the corners' pixels: never copied */
    struct surface sa, sb, ss;
    surface_init(&sa, a, W, H, W * bpp, bpp);
    surface_init(&sb, b, W, H, W * bpp, bpp);
    surface_init(&ss, sp, (uint32_t)r.w, (uint32_t)r.h, (uint32_t)r.w * bpp, bpp);
    struct ui_canvas ca, cs;
    ui_canvas_init(&ca, &sa);
    ui_window_draw(&ca, &w, r, active, st, s, &f);
    ui_canvas_init(&cs, &ss);
    ui_window_draw(&cs, &w, (struct rect){ 0, 0, r.w, r.h }, active, st, s, &f);
    for (int32_t y = -13; y < (int32_t)H; y += 61)
        for (int32_t x = -7; x < (int32_t)W; x += 97) {
            struct ui_canvas cb;
            ui_canvas_init(&cb, &sb);
            ui_canvas_clip(&cb, (struct rect){ x, y, 97, 61 });
            ui_blit_round(&cb, r, &ss, ui_window_radius(s), UI_CORNERS_ALL);
        }
    CHECK(memcmp(a, b, (size_t)W * H * bpp) == 0);
    /* The exposure path in tiles too (A1): the same pixels. */
    for (size_t i = 0; i < (size_t)W * H * bpp; i++)
        b[i] = (uint8_t)(i * 7 + (i >> 9));
    for (int32_t y = -5; y < (int32_t)H; y += 53)
        for (int32_t x = -11; x < (int32_t)W; x += 89) {
            struct ui_canvas cb;
            ui_canvas_init(&cb, &sb);
            ui_canvas_clip(&cb, (struct rect){ x, y, 89, 53 });
            ui_window_draw(&cb, &w, r, active, st, s, &f);
        }
    CHECK(memcmp(a, b, (size_t)W * H * bpp) == 0);
    free(a);
    free(b);
    free(sp);
}

static void test_surface_equals_direct(void)
{
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    for (int i = 0; i < 4; i++) {
        uint32_t s = scales[i];
        int32_t w = ui_px(352, s), h = ui_px(377, s);
        for (uint32_t bpp = 3; bpp <= 4; bpp++) {
            check_surface(s, bpp, (struct rect){ 100, 60, w, h }, true, UI_BUTTON_NORMAL);
            check_surface(s, bpp, (struct rect){ 101, 61, w, h }, false, UI_BUTTON_HOVER);
            check_surface(s, bpp, (struct rect){ -40, 3, w, h }, true, UI_BUTTON_PRESSED);
            check_surface(s, bpp, (struct rect){ 1100 - w / 2, 900 - h / 3, w, h }, false, UI_BUTTON_NORMAL);
        }
    }
}

/* The frame's pixels: the corners outside the rounded rectangle untouched,
 * the edge in the active or inactive colour, the close button's fill. */
static void test_frame_pixels(void)
{
    uint32_t W = 400, H = 400, s = 4;
    struct ui_fonts f = fonts_for(s);
    static uint8_t px[400 * 400 * 4];
    struct surface sf;
    surface_init(&sf, px, W, H, W * 4, 4);
    struct ui_canvas c;
    ui_canvas_init(&c, &sf);
    struct ui_window w = win("A", 352, 377, false);
    ui_fill(&c, (struct rect){ 0, 0, 400, 400 }, 0x123456);
    struct rect r = { 10, 10, 352, 377 };
    ui_window_draw(&c, &w, r, true, UI_BUTTON_NORMAL, s, &f);
    CHECK_EQ(surface_get(&sf, 10, 10), 0x123456);                  /* outside the corner */
    CHECK_EQ(surface_get(&sf, 10, 200), UI_BORDER_WINDOW);         /* the left edge */
    CHECK_EQ(surface_get(&sf, 11, 200), UI_SURFACE_WINDOW);
    CHECK_EQ(surface_get(&sf, 10 + 351 - 1, 10 + 31), UI_SURFACE_WINDOW);   /* the close target, normal */
    ui_window_draw(&c, &w, r, false, UI_BUTTON_HOVER, s, &f);
    CHECK_EQ(surface_get(&sf, 10, 200), UI_BORDER_INACTIVE);
    CHECK_EQ(surface_get(&sf, 10 + 351, 10 + 16), UI_DANGER);      /* the fill covers the edge */
    CHECK_EQ(surface_get(&sf, 10 + 352 - 32, 10 + 31), UI_DANGER);
    CHECK_EQ(surface_get(&sf, 10 + 351, 10), 0x123456);            /* it follows the corner */
    ui_window_draw(&c, &w, r, false, UI_BUTTON_PRESSED, s, &f);
    CHECK_EQ(surface_get(&sf, 10 + 352 - 32, 10 + 31), UI_DANGER_PRESSED);
    CHECK_EQ(surface_get(&sf, 10 + 352 - 33, 10 + 31), UI_SURFACE_WINDOW);
}

/* ---- 1.5 stage 8 ---- */

static void click(struct ui_wm *m, int32_t x, int32_t y)
{
    ui_wm_pointer(m, x, y, true, false);
    ui_wm_pointer(m, x, y, false, true);
}

/* The title-bar buttons, minimise, maximise and restore. */
static void test_min_max_restore(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window a = win("A", 352, 377, false), c = win("C", 642, 433, false);
    a.buttons = UI_WIN_MINIMISE | UI_WIN_MAXIMISE;
    c.buttons = UI_WIN_MINIMISE;
    ui_wm_open(&m, &c);                             /* (191, 147) */
    ui_wm_open(&m, &a);                             /* (336, 175) */
    struct ui_window *at;
    CHECK(same_rect(ui_window_part_rect(&a, a.r, 4, UI_HIT_MAXIMISE), 336 + 352 - 64, 175, 32, 32));
    CHECK(same_rect(ui_window_part_rect(&a, a.r, 4, UI_HIT_MINIMISE), 336 + 352 - 96, 175, 32, 32));
    CHECK(same_rect(ui_window_part_rect(&c, c.r, 4, UI_HIT_MINIMISE), 191 + 642 - 64, 147, 32, 32));
    CHECK(ui_rect_empty(ui_window_part_rect(&c, c.r, 4, UI_HIT_MAXIMISE)));
    CHECK(ui_wm_hit(&m, 600, 190, &at) == UI_HIT_MINIMISE && at == &a);
    CHECK(ui_wm_hit(&m, 630, 190, &at) == UI_HIT_MAXIMISE && at == &a);
    CHECK(ui_wm_hit(&m, 670, 190, &at) == UI_HIT_CLOSE && at == &a);
    CHECK(ui_wm_hit(&m, 780, 160, &at) == UI_HIT_MINIMISE && at == &c);
    CHECK(ui_wm_hit(&m, 740, 160, &at) == UI_HIT_TITLE && at == &c);      /* no maximise button */
    /* Hover and press states, as the close button's. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 630, 190, false, false);
    CHECK(a.max_st == UI_BUTTON_HOVER && count(&m, UI_EV_MAX_STATE) == 1 && a.dirty);
    ui_wm_pointer(&m, 630, 190, true, false);
    CHECK(a.max_st == UI_BUTTON_PRESSED && m.pressed == &a && m.pressed_part == UI_HIT_MAXIMISE);
    /* Maximise: the area above the taskbar; the rectangle kept. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 630, 190, false, true);
    CHECK(a.maximised && same_rect(a.r, 0, 0, 1024, 728) && same_rect(a.normal_r, 336, 175, 352, 377));
    CHECK(count(&m, UI_EV_MAXIMISE) == 1);
    /* A maximised window stays while the pointer moves at most 4 dp from the
     * press (1.5 stage 9b: further, it is dragged away and restored); the
     * press and release make a click. */
    ui_wm_pointer(&m, 500, 10, true, false);
    ui_wm_pointer(&m, 504, 14, false, false);
    CHECK(same_rect(a.r, 0, 0, 1024, 728));
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 504, 14, false, true);
    CHECK(count(&m, UI_EV_DRAG) == 0 && m.click_w == &a);
    /* Restore (the maximise button's place at the right end). */
    CHECK(ui_wm_hit(&m, 970, 10, &at) == UI_HIT_MAXIMISE && at == &a);
    ui_wm_clear_events(&m);
    click(&m, 970, 10);
    CHECK(!a.maximised && same_rect(a.r, 336, 175, 352, 377) && count(&m, UI_EV_UNMAXIMISE) == 1);
    /* Minimise: no hit, the focus to the top window shown. */
    ui_wm_clear_events(&m);
    click(&m, 600, 190);
    CHECK(a.minimised && m.focus == &c && count(&m, UI_EV_MINIMISE) == 1 && count(&m, UI_EV_FOCUS) == 1);
    CHECK(ui_wm_hit(&m, 400, 300, &at) == UI_HIT_BODY && at == &c);
    ui_wm_clear_events(&m);
    click(&m, 780, 160);
    CHECK(c.minimised && m.focus == 0 && count(&m, UI_EV_UNFOCUS) == 1);
    /* No window focused: a key opens the Console window: restored. */
    ui_wm_clear_events(&m);
    CHECK(ui_wm_key(&m, &c, true, false) == UI_KEY_OPENED);
    CHECK(!c.minimised && m.focus == &c && count(&m, UI_EV_RESTORE) == 1 && count(&m, UI_EV_OPEN) == 0);
    /* No maximise for a window without the button. */
    ui_wm_toggle_maximise(&m, &c);
    CHECK(!c.maximised && same_rect(c.r, 191, 147, 642, 433));
    /* Opening a minimised window restores it. */
    CHECK(ui_wm_open(&m, &a) && !a.minimised && m.focus == &a && m.z[1] == &a);
}

/* The taskbar's window buttons: places, states, the click and the middle click. */
static void test_taskbar_buttons(void)
{
    struct ui_wm m = wm_1024();
    ui_wm_set_taskbar(&m, 44, 952);
    struct ui_window a = win("A", 352, 377, false), c = win("C", 642, 433, false);
    a.buttons = UI_WIN_MINIMISE | UI_WIN_MAXIMISE;
    c.buttons = UI_WIN_MINIMISE;
    ui_wm_open(&m, &a);
    ui_wm_open(&m, &c);
    CHECK(m.order[0] == &a && m.order[1] == &c);
    CHECK(same_rect(ui_wm_task_rect(&m, 0), 44, 729, 160, 39) && same_rect(ui_wm_task_rect(&m, 1), 208, 729, 160, 39));
    CHECK(same_rect(ui_wm_task_strip(&m), 44, 729, 908, 39));
    struct ui_window *at;
    CHECK(ui_wm_hit(&m, 100, 740, &at) == UI_HIT_TASKBUTTON && at == &a);
    CHECK(ui_wm_hit(&m, 300, 740, &at) == UI_HIT_TASKBUTTON && at == &c);
    CHECK(ui_wm_hit(&m, 500, 740, &at) == UI_HIT_TASKBAR && !at);
    CHECK(ui_wm_hit(&m, 100, 728, &at) == UI_HIT_TASKBAR);                 /* the top line */
    /* Hover, pressed; the release over it raises and focuses A. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 100, 740, false, false);
    CHECK(a.task_st == UI_BUTTON_HOVER && count(&m, UI_EV_TASK_STATE) == 1);
    ui_wm_pointer(&m, 100, 740, true, false);
    CHECK(a.task_st == UI_BUTTON_PRESSED && m.task_pressed == &a && m.focus == &c);
    ui_wm_pointer(&m, 100, 740, false, true);
    CHECK(m.focus == &a && m.z[1] == &a && a.task_st == UI_BUTTON_HOVER);
    /* The focused window's button minimises it; again restores it. */
    ui_wm_clear_events(&m);
    click(&m, 100, 740);
    CHECK(a.minimised && m.focus == &c && count(&m, UI_EV_MINIMISE) == 1);
    ui_wm_clear_events(&m);
    click(&m, 100, 740);
    CHECK(!a.minimised && m.focus == &a && count(&m, UI_EV_RESTORE) == 1 && m.z[1] == &a);
    /* Released elsewhere: nothing. */
    ui_wm_pointer(&m, 300, 740, true, false);
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 500, 740, false, true);
    CHECK(m.focus == &a && c.task_st == UI_BUTTON_NORMAL && count(&m, UI_EV_FOCUS) == 0);
    /* The middle button closes C; A's button moves to the first place. */
    ui_wm_clear_events(&m);
    ui_wm_middle(&m, 300, 740);
    CHECK(!c.open && m.n == 1 && m.order[0] == &a && count(&m, UI_EV_CLOSE_MIDDLE) == 1);
    ui_wm_middle(&m, 500, 740);                     /* not on a button: nothing */
    CHECK(m.n == 1);
    /* Six windows share the span: 148 px each. */
    struct ui_window w2 = win("W2", 100, 100, false), w3 = win("W3", 100, 100, false);
    struct ui_window w4 = win("W4", 100, 100, false), w5 = win("W5", 100, 100, false);
    struct ui_window w6 = win("W6", 100, 100, false);
    CHECK(ui_wm_open(&m, &w2) && ui_wm_open(&m, &w3) && ui_wm_open(&m, &w4) && ui_wm_open(&m, &w5) &&
          ui_wm_open(&m, &w6) && m.n == 6);
    CHECK(same_rect(ui_wm_task_rect(&m, 5), 44 + 5 * 152, 729, 148, 39));
}

/* Alt+Tab over the most-recently-focused order, minimised windows included. */
static void test_switch(void)
{
    struct ui_wm m = wm_1024();
    CHECK(!ui_wm_switch_step(&m, false));           /* no window */
    struct ui_window a = win("A", 100, 100, false), b = win("B", 100, 100, false), c = win("C", 100, 100, false);
    ui_wm_open(&m, &a);
    ui_wm_open(&m, &b);
    ui_wm_open(&m, &c);
    CHECK(m.mru[0] == &c && m.mru[1] == &b && m.mru[2] == &a);
    ui_wm_clear_events(&m);
    CHECK(ui_wm_switch_step(&m, false) && m.switching && m.target == &b);  /* the previous window */
    CHECK(ui_wm_switch_step(&m, false) && m.target == &a);
    CHECK(ui_wm_switch_step(&m, false) && m.target == &c);                 /* round again */
    CHECK(count(&m, UI_EV_SWITCH_SELECT) == 3 && m.focus == &c);           /* nothing moves yet */
    ui_wm_switch_step(&m, false);
    ui_wm_clear_events(&m);
    ui_wm_switch_commit(&m);
    CHECK(!m.switching && m.focus == &b && m.z[2] == &b && count(&m, UI_EV_SWITCH_CHOSEN) == 1);
    CHECK(m.mru[0] == &b && m.mru[1] == &c && m.mru[2] == &a);
    /* A minimised window is chosen and restored. */
    ui_wm_minimise(&m, &a);
    CHECK(a.minimised && m.focus == &b);
    ui_wm_switch_step(&m, true);                    /* back: the last one, A */
    CHECK(m.target == &a);
    ui_wm_switch_commit(&m);
    CHECK(!a.minimised && m.focus == &a && m.mru[0] == &a);
    /* Back twice (the order A, B, C: C, then B), then Esc: nothing changes. */
    ui_wm_switch_step(&m, true);
    CHECK(m.target == &c);
    ui_wm_switch_step(&m, true);
    CHECK(m.target == &b);
    ui_wm_clear_events(&m);
    ui_wm_switch_cancel(&m);
    CHECK(!m.switching && m.focus == &a && count(&m, UI_EV_SWITCH_CANCEL) == 1 && count(&m, UI_EV_FOCUS) == 0);
    /* The marked window closed (its taskbar button, the middle button): no switch left. */
    ui_wm_switch_step(&m, false);
    CHECK(m.target == &b && m.order[1] == &b);
    ui_wm_middle(&m, -1, -1);                       /* no button there: still marked */
    CHECK(m.switching && m.target == &b);
    ui_wm_middle(&m, ui_wm_task_rect(&m, 1).x + 1, 740);
    CHECK(!b.open && !m.switching && m.target == 0 && m.focus == &a);
}

/* The double click on a title bar toggles maximise (UI_WM_DCLICK_TICKS). */
static void test_double_click(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window a = win("A", 352, 377, false), b = win("B", 352, 377, false);
    a.buttons = UI_WIN_MINIMISE | UI_WIN_MAXIMISE;
    ui_wm_open(&m, &a);                             /* (336, 175) */
    ui_wm_set_now(&m, 100);
    click(&m, 400, 190);
    CHECK(m.click_w == &a && !a.maximised);
    ui_wm_set_now(&m, 150);                         /* 50 ticks: still a double click */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 404, 194, true, false);       /* 4 px away: still */
    CHECK(a.maximised && m.drag == 0 && count(&m, UI_EV_MAXIMISE) == 1);
    ui_wm_pointer(&m, 404, 194, false, true);
    CHECK(m.click_w == 0);                          /* the second press makes no new click */
    ui_wm_set_now(&m, 300);
    click(&m, 400, 10);
    ui_wm_set_now(&m, 351);                         /* 51 ticks: two clicks */
    click(&m, 400, 10);
    CHECK(a.maximised);
    ui_wm_set_now(&m, 360);
    ui_wm_pointer(&m, 405, 10, true, false);        /* 5 px away: not a double click */
    ui_wm_pointer(&m, 405, 10, false, true);
    CHECK(a.maximised);
    ui_wm_set_now(&m, 365);
    click(&m, 405, 10);
    CHECK(!a.maximised && same_rect(a.r, 336, 175, 352, 377));
    /* A window without maximise: a double click only drags (nothing moves). */
    ui_wm_open(&m, &b);                             /* (360, 199) */
    ui_wm_set_now(&m, 400);
    click(&m, 420, 210);
    ui_wm_pointer(&m, 420, 210, true, false);
    CHECK(m.drag == &b && !b.maximised);
    ui_wm_pointer(&m, 420, 210, false, true);
    /* A drag is not a click. */
    ui_wm_pointer(&m, 420, 210, true, false);
    ui_wm_pointer(&m, 430, 220, false, false);
    ui_wm_pointer(&m, 430, 220, false, true);
    CHECK(m.click_w == 0);
}

/* ---- 1.5 stage 9: resizing by the edges and corners ---- */

static struct ui_window resizable_win(const char *name)
{
    struct ui_window a = win(name, 352, 377, false);
    a.buttons = UI_WIN_MINIMISE | UI_WIN_MAXIMISE;
    a.resizable = true;
    a.min_w = 352;
    a.min_h = 377;
    return a;
}

static uint32_t edges_at(const struct ui_wm *m, int32_t x, int32_t y)
{
    struct ui_window *at;
    if (ui_wm_hit(m, x, y, &at) != UI_HIT_RESIZE)
        return 0;
    return ui_window_edges(at, at->r, m->scale, x, y);
}

/* The band: 8 dp inside each edge, the corners, the buttons first; none on
 * a window that is not resizable, maximised or minimised. */
static void test_resize_band(void)
{
    CHECK_EQ(ui_window_band(4), 8);
    CHECK_EQ(ui_window_band(5), 10);
    CHECK_EQ(ui_window_band(6), 12);
    CHECK_EQ(ui_window_band(8), 16);
    struct ui_wm m = wm_1024();
    struct ui_window a = resizable_win("A"), *at;
    ui_wm_open(&m, &a);                             /* (336, 175) 352 x 377: right 688, bottom 552 */
    CHECK_EQ(edges_at(&m, 336, 300), UI_EDGE_LEFT);
    CHECK_EQ(edges_at(&m, 343, 300), UI_EDGE_LEFT);
    CHECK(ui_wm_hit(&m, 344, 300, &at) == UI_HIT_BODY && at == &a);
    CHECK_EQ(edges_at(&m, 687, 300), UI_EDGE_RIGHT);
    CHECK_EQ(edges_at(&m, 680, 300), UI_EDGE_RIGHT);
    CHECK(ui_wm_hit(&m, 679, 300, &at) == UI_HIT_BODY);
    CHECK_EQ(edges_at(&m, 500, 175), UI_EDGE_TOP);
    CHECK_EQ(edges_at(&m, 500, 182), UI_EDGE_TOP);
    CHECK(ui_wm_hit(&m, 500, 183, &at) == UI_HIT_TITLE);
    CHECK_EQ(edges_at(&m, 500, 551), UI_EDGE_BOTTOM);
    CHECK_EQ(edges_at(&m, 500, 544), UI_EDGE_BOTTOM);
    CHECK(ui_wm_hit(&m, 500, 543, &at) == UI_HIT_BODY);
    CHECK_EQ(edges_at(&m, 336, 175), UI_EDGE_LEFT | UI_EDGE_TOP);
    CHECK_EQ(edges_at(&m, 336, 551), UI_EDGE_LEFT | UI_EDGE_BOTTOM);
    CHECK_EQ(edges_at(&m, 687, 551), UI_EDGE_RIGHT | UI_EDGE_BOTTOM);
    CHECK(ui_wm_hit(&m, 687, 175, &at) == UI_HIT_CLOSE);            /* the top-right corner: close */
    CHECK_EQ(edges_at(&m, 591, 175), UI_EDGE_TOP);                  /* just left of minimise */
    CHECK(ui_wm_hit(&m, 592, 175, &at) == UI_HIT_MINIMISE);
    CHECK(ui_wm_hit(&m, 335, 300, &at) == UI_HIT_DESKTOP);          /* outside: the desktop */
    /* The 1.3 checks' title point (8 dp in) and body point stay outside the band. */
    CHECK(ui_wm_hit(&m, 336 + 8, 175 + 16, &at) == UI_HIT_TITLE);
    CHECK(ui_wm_hit(&m, 336 + 40, 175 + 250, &at) == UI_HIT_BODY);
    /* Not resizable, maximised, minimised: no band. */
    a.resizable = false;
    CHECK(ui_wm_hit(&m, 336, 300, &at) == UI_HIT_BODY && ui_window_edges(&a, a.r, 4, 336, 300) == 0);
    a.resizable = true;
    ui_wm_toggle_maximise(&m, &a);
    CHECK(ui_wm_hit(&m, 0, 300, &at) == UI_HIT_BODY && ui_wm_hit(&m, 1023, 300, &at) == UI_HIT_BODY);
    CHECK(ui_wm_hit(&m, 500, 727, &at) == UI_HIT_BODY);
    ui_wm_toggle_maximise(&m, &a);
    ui_wm_minimise(&m, &a);
    CHECK(ui_window_edges(&a, a.r, 4, 336, 300) == 0 && ui_wm_hit(&m, 336, 300, &at) == UI_HIT_DESKTOP);
    /* 200 %: 16 px. */
    struct ui_wm big;
    ui_wm_init(&big, 2560, 1440, 1440 - 80, 8, 0);
    struct ui_window b = resizable_win("B");
    b.r = (struct rect){ 100, 100, 704, 754 };
    CHECK_EQ(ui_window_edges(&b, b.r, 8, 115, 400), UI_EDGE_LEFT);
    CHECK_EQ(ui_window_edges(&b, b.r, 8, 116, 400), 0);
}

/* A resize: capture, 1:1 from the grab, the minimum, the event only if the
 * rectangle changed, raised and focused by the press; each edge. */
static void test_resize_drag(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window a = resizable_win("A"), c = win("C", 642, 433, false);
    c.buttons = UI_WIN_MINIMISE;
    ui_wm_open(&m, &a);                             /* (336, 175) */
    ui_wm_open(&m, &c);                             /* (191, 147) 642 x 433: over A's middle */
    ui_wm_clear_events(&m);
    /* C (191-832 x 147-579, not resizable) covers A whole: no band of A under it. */
    struct ui_window *at;
    CHECK(ui_wm_hit(&m, 687, 300, &at) == UI_HIT_BODY && at == &c);
    CHECK(ui_wm_hit(&m, 340, 178, &at) == UI_HIT_TITLE && at == &c);
    /* C dragged down by its title bar (clamped at y 728 - 433 = 295): A's top shows. */
    ui_wm_pointer(&m, 300, 160, true, false);
    ui_wm_pointer(&m, 300, 560, false, false);
    ui_wm_pointer(&m, 300, 560, false, true);
    CHECK(same_rect(c.r, 191, 295, 642, 433));
    ui_wm_clear_events(&m);
    ui_damage_clear(&dmg);
    /* A's top-left corner: the press raises and focuses A and starts the resize. */
    ui_wm_pointer(&m, 340, 178, true, false);
    CHECK(m.resize == &a && m.resize_edges == (UI_EDGE_LEFT | UI_EDGE_TOP) && m.z[1] == &a && m.focus == &a);
    CHECK(count(&m, UI_EV_RAISE) == 1 && count(&m, UI_EV_FOCUS) == 1 && !m.drag && !m.pressed);
    ui_wm_pointer(&m, 290, 128, false, false);      /* 50 left, 50 up */
    CHECK(same_rect(a.r, 286, 125, 402, 427) && a.dirty && dmg.n >= 1);
    /* Captured: over C's minimise button nothing hovers. */
    ui_wm_pointer(&m, 191 + 642 - 48, 295 + 16, false, false);
    CHECK(c.min_st == UI_BUTTON_NORMAL && c.close == UI_BUTTON_NORMAL);
    ui_wm_pointer(&m, 300, 140, false, false);      /* back: 1:1 from the grab again */
    CHECK(same_rect(a.r, 296, 137, 392, 415));
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 300, 140, false, true);
    CHECK(!m.resize && count(&m, UI_EV_RESIZE) == 1 && same_rect(m.ev[0].r, 296, 137, 392, 415));
    /* The right edge (A at 296-687, now on top): grow by 100, then below the minimum. */
    ui_wm_pointer(&m, 685, 300, true, false);
    CHECK(m.resize == &a && m.resize_edges == UI_EDGE_RIGHT);
    ui_wm_pointer(&m, 785, 330, false, false);      /* the y movement changes nothing */
    CHECK(same_rect(a.r, 296, 137, 492, 415));
    ui_wm_pointer(&m, 100, 330, false, false);      /* asks 392 - 585 + ...: the minimum, 352 */
    CHECK(same_rect(a.r, 296, 137, 352, 415));
    ui_wm_pointer(&m, 735, 330, false, true);
    CHECK(same_rect(a.r, 296, 137, 442, 415));
    /* The bottom edge: 415 -> 377 is the minimum. */
    ui_wm_pointer(&m, 400, 137 + 415 - 1, true, false);
    CHECK(m.resize_edges == UI_EDGE_BOTTOM);
    ui_wm_pointer(&m, 400, 137 + 415 - 1 - 100, false, false);
    CHECK(same_rect(a.r, 296, 137, 442, 377));
    ui_wm_pointer(&m, 400, 137 + 415 - 1 - 100, false, true);
    /* A press and release in the band without a move: no event, no click on the title bar. */
    ui_wm_clear_events(&m);
    click(&m, 298, 300);
    CHECK(count(&m, UI_EV_RESIZE) == 0 && m.n_ev == 0 && m.click_w == 0 && !m.resize);
    /* Maximise keeps the resized rectangle; restore puts it back; no band while maximised. */
    ui_wm_toggle_maximise(&m, &a);
    CHECK(same_rect(a.r, 0, 0, 1024, 728) && same_rect(a.normal_r, 296, 137, 442, 377));
    ui_wm_pointer(&m, 1023, 300, true, false);
    CHECK(!m.resize);
    ui_wm_pointer(&m, 1023, 300, false, true);
    ui_wm_toggle_maximise(&m, &a);
    CHECK(same_rect(a.r, 296, 137, 442, 377));
    /* The menus take the pointer during a resize (ui_wm_cancel): the event, nothing more. */
    ui_wm_pointer(&m, 297, 300, true, false);
    ui_wm_pointer(&m, 247, 300, false, false);
    ui_wm_clear_events(&m);
    ui_wm_cancel(&m);
    CHECK(!m.resize && count(&m, UI_EV_RESIZE) == 1 && same_rect(a.r, 246, 137, 492, 377));
    ui_wm_pointer(&m, 100, 300, false, false);
    CHECK(same_rect(a.r, 246, 137, 492, 377));      /* no longer captured */
    /* Closed, then opened again: its own size. */
    ui_wm_pointer(&m, 247, 300, true, false);
    ui_wm_minimise(&m, &a);
    CHECK(!m.resize);
    ui_wm_pointer(&m, 247, 300, false, true);
    ui_wm_activate(&m, &a);
    click(&m, 246 + 492 - 16, 137 + 16);            /* the close button */
    CHECK(!a.open);
    ui_wm_open(&m, &a);
    CHECK(a.r.w == 352 && a.r.h == 377);
}

/* Extreme pointer values for each edge and corner: the limits hold. */
static void test_resize_extremes(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window a = resizable_win("A");
    ui_wm_open(&m, &a);                             /* (336, 175) 352 x 377 */
    static const struct { int32_t px, py; } far[4] = {
        { INT32_MIN, INT32_MIN }, { INT32_MAX, INT32_MAX }, { INT32_MIN, INT32_MAX }, { INT32_MAX, INT32_MIN },
    };
    struct { int32_t x, y; uint32_t e; } grab[8] = {
        { 336, 300, UI_EDGE_LEFT }, { 687, 300, UI_EDGE_RIGHT }, { 500, 175, UI_EDGE_TOP },
        { 500, 551, UI_EDGE_BOTTOM }, { 336, 175, UI_EDGE_LEFT | UI_EDGE_TOP },
        { 336, 551, UI_EDGE_LEFT | UI_EDGE_BOTTOM }, { 687, 551, UI_EDGE_RIGHT | UI_EDGE_BOTTOM },
        { 690, 551, 0 },                            /* outside: nothing */
    };
    for (int g = 0; g < 8; g++)
        for (int f = 0; f < 4; f++) {
            ui_wm_pointer(&m, grab[g].x, grab[g].y, true, false);
            CHECK(m.resize_edges == grab[g].e || (!grab[g].e && !m.resize));
            ui_wm_pointer(&m, far[f].px, far[f].py, false, false);
            struct rect r = a.r;
            /* The limits: the minimum, the area above the taskbar, 48 dp on the screen. */
            CHECK(r.w >= 352 && r.h >= 377 && r.w <= 1024 && r.h <= 728);
            CHECK(r.x >= 0 && r.y >= 0 && (int64_t)r.x + r.w <= 1024 && (int64_t)r.y + r.h <= 728);
            /* The edges that do not move stay. */
            if (!(grab[g].e & UI_EDGE_LEFT) && grab[g].e)
                CHECK_EQ(r.x, 336);
            if (!(grab[g].e & UI_EDGE_TOP) && grab[g].e)
                CHECK_EQ(r.y, 175);
            if (!(grab[g].e & UI_EDGE_RIGHT) && grab[g].e)
                CHECK_EQ(r.x + r.w, 688);
            if (!(grab[g].e & UI_EDGE_BOTTOM) && grab[g].e)
                CHECK_EQ(r.y + r.h, 552);
            ui_wm_pointer(&m, far[f].px, far[f].py, false, true);
            /* Back to the start for the next case. */
            a.r = (struct rect){ 336, 175, 352, 377 };
        }
    /* The exact figures, edge by edge (ui_wm_resize_rect; g = A's start). */
    struct rect g = { 336, 175, 352, 377 };
    int64_t big = (int64_t)1 << 40;
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_LEFT, -big, 0), 0, 175, 688, 377));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_LEFT, big, 0), 336, 175, 352, 377));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_RIGHT, big, 0), 336, 175, 688, 377));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_RIGHT, -big, 0), 336, 175, 352, 377));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_TOP, 0, -big), 336, 0, 352, 552));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_TOP, 0, big), 336, 175, 352, 377));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_BOTTOM, 0, big), 336, 175, 352, 553));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_BOTTOM, 0, -big), 336, 175, 352, 377));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_RIGHT | UI_EDGE_BOTTOM, big, big), 336, 175, 688, 553));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, UI_EDGE_LEFT | UI_EDGE_TOP, -big, -big), 0, 0, 688, 552));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, g, 0, -big, big), 336, 175, 352, 377));
    /* A window mostly off the left (48 dp left on the screen): its left edge
     * goes no further off, its right edge keeps 48 dp on the screen. */
    struct rect off = { 48 - 600, 100, 600, 400 };
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, off, UI_EDGE_LEFT, -big, 0), -552, 100, 600, 400));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, off, UI_EDGE_RIGHT, -big, 0), -552, 100, 600, 400));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, off, UI_EDGE_LEFT, big, 0), -552 + 248, 100, 352, 400));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, off, UI_EDGE_RIGHT, big, 0), -552, 100, 1024, 400));
    /* Mostly off the right: the right edge goes no further off; the left keeps 48 dp. */
    struct rect offr = { 1024 - 48, 100, 600, 400 };
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, offr, UI_EDGE_RIGHT, big, 0), 976, 100, 600, 400));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, offr, UI_EDGE_LEFT, big, 0), 976, 100, 600, 400));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, offr, UI_EDGE_RIGHT, -big, 0), 976, 100, 352, 400));
    /* Larger than the area at the start: kept, never forced smaller. */
    struct rect huge = { 0, 0, 1100, 900 };
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, huge, UI_EDGE_RIGHT, big, 0), 0, 0, 1100, 900));
    CHECK(same_rect(ui_wm_resize_rect(&m, &a, huge, UI_EDGE_BOTTOM, 0, -big), 0, 0, 1100, 377));
    /* A minimum above the start (or none): the start is the smallest; 1 px at least. */
    struct ui_window z = resizable_win("Z");
    z.min_w = 0;
    z.min_h = 5000;
    CHECK(same_rect(ui_wm_resize_rect(&m, &z, g, UI_EDGE_RIGHT | UI_EDGE_BOTTOM, -big, -big), 336, 175, 1, 377));
}

/* A resized window painted on a larger (screen-wide) surface at (0, 0) and
 * composed through ui_blit_round equals the window painted directly; its
 * client in its own size, centred across. */
static void test_resize_surface_equals_direct(void)
{
    static const uint32_t scales[2] = { 4, 8 };
    for (int i = 0; i < 2; i++) {
        uint32_t s = scales[i], W = 900, H = 700, bpp = 4;
        struct ui_fonts f = fonts_for(s);
        struct ui_window w = win("A", 200, 150, false);
        w.paint = client;
        w.ctx = &f;
        w.resizable = true;
        w.min_w = 200;
        w.min_h = 150;
        struct rect r = { 37, 41, 333, 261 };
        uint8_t *a = malloc((size_t)W * H * bpp), *b = malloc((size_t)W * H * bpp), *sp = malloc((size_t)W * H * bpp);
        for (size_t k = 0; k < (size_t)W * H * bpp; k++)
            a[k] = b[k] = (uint8_t)(k * 5 + (k >> 8));
        memset(sp, 0x5A, (size_t)W * H * bpp);
        struct surface sa, sb, ss;
        surface_init(&sa, a, W, H, W * bpp, bpp);
        surface_init(&sb, b, W, H, W * bpp, bpp);
        surface_init(&ss, sp, W, H, W * bpp, bpp);      /* larger than the window */
        struct ui_canvas ca, cs, cb;
        ui_canvas_init(&ca, &sa);
        ui_window_draw(&ca, &w, r, true, UI_BUTTON_NORMAL, s, &f);
        ui_canvas_init(&cs, &ss);
        ui_window_draw(&cs, &w, (struct rect){ 0, 0, r.w, r.h }, true, UI_BUTTON_NORMAL, s, &f);
        ui_canvas_init(&cb, &sb);
        ui_blit_round(&cb, r, &ss, ui_window_radius(s), UI_CORNERS_ALL);
        CHECK(memcmp(a, b, (size_t)W * H * bpp) == 0);
        /* The client centred: its box at (r.w - 200) / 2 + 20 from the left, 50 down. */
        CHECK_EQ(surface_get(&sa, (uint32_t)(r.x + (r.w - 200) / 2 + 20), (uint32_t)(r.y + 50)), UI_BORDER);
        free(a);
        free(b);
        free(sp);
    }
}

/* ---- 1.5 stage 9b: snapping and the window shortcuts ---- */

static struct ui_wm_event *find_event(struct ui_wm *m, enum ui_wm_event_kind k)
{
    for (uint32_t i = 0; i < m->n_ev; i++)
        if (m->ev[i].kind == k)
            return &m->ev[i];
    return 0;
}

/* A title-bar drag ending in a snap zone; dragging away; the zones' limits. */
static void test_snap_drag(void)
{
    CHECK_EQ(ui_window_snap_zone(4), 4);
    CHECK_EQ(ui_window_snap_zone(5), 5);
    CHECK_EQ(ui_window_snap_zone(6), 6);
    CHECK_EQ(ui_window_snap_zone(8), 8);
    struct ui_wm m = wm_1024();
    struct ui_window a = resizable_win("A"), *at;
    ui_wm_open(&m, &a);                             /* (336, 175) */
    CHECK(same_rect(ui_wm_half_rect(&m, UI_SNAP_LEFT), 0, 0, 512, 728));
    CHECK(same_rect(ui_wm_half_rect(&m, UI_SNAP_RIGHT), 512, 0, 512, 728));
    /* To the top edge (the pointer 2 px down): maximised; the start kept; no drag event. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 400, 190, true, false);
    ui_wm_pointer(&m, 400, 2, false, false);
    CHECK(same_rect(a.r, 336, 0, 352, 377));        /* during the drag: clamped as before */
    ui_wm_pointer(&m, 400, 2, false, true);
    CHECK(a.maximised && same_rect(a.r, 0, 0, 1024, 728) && same_rect(a.normal_r, 336, 175, 352, 377));
    CHECK(count(&m, UI_EV_MAXIMISE) == 1 && count(&m, UI_EV_DRAG) == 0);
    /* Dragged away: 4 dp is not enough; 10 px restores it under the pointer
     * (x 500 - 500 * 352 / 1024 = 329, y 20 - 10), and the drag goes on. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 500, 10, true, false);
    ui_wm_pointer(&m, 504, 6, false, false);
    CHECK(a.maximised && count(&m, UI_EV_UNMAXIMISE) == 0);
    ui_wm_pointer(&m, 500, 20, false, false);
    CHECK(!a.maximised && same_rect(a.r, 329, 10, 352, 377) && count(&m, UI_EV_UNMAXIMISE) == 1);
    ui_wm_pointer(&m, 560, 120, false, false);
    CHECK(same_rect(a.r, 389, 110, 352, 377));
    ui_wm_pointer(&m, 560, 120, false, true);
    CHECK(count(&m, UI_EV_DRAG) == 1 && same_rect(find_event(&m, UI_EV_DRAG)->r, 389, 110, 352, 377));
    /* To the left edge: the left half; the start kept; no band while snapped. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 420, 125, true, false);
    ui_wm_pointer(&m, 2, 300, false, false);
    ui_wm_pointer(&m, 2, 300, false, true);
    CHECK(a.snapped == UI_SNAP_LEFT && !a.maximised && same_rect(a.r, 0, 0, 512, 728));
    CHECK(same_rect(a.normal_r, 389, 110, 352, 377) && count(&m, UI_EV_SNAP) == 1 && count(&m, UI_EV_DRAG) == 0);
    CHECK_EQ(find_event(&m, UI_EV_SNAP)->count, UI_SNAP_LEFT);
    CHECK(ui_window_edges(&a, a.r, 4, 0, 300) == 0 && ui_wm_hit(&m, 0, 300, &at) == UI_HIT_BODY);
    /* Dragged away from the half (256 - 256 * 352 / 512 = 80): unsnapped, a drag event on the release. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 256, 10, true, false);
    ui_wm_pointer(&m, 256, 30, false, false);
    ui_wm_pointer(&m, 256, 30, false, true);
    CHECK(!a.snapped && same_rect(a.r, 80, 20, 352, 377));
    CHECK(count(&m, UI_EV_UNSNAP) == 1 && count(&m, UI_EV_DRAG) == 1);
    /* To the right edge (x 1021 >= 1024 - 4): the right half. */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 100, 30, true, false);
    ui_wm_pointer(&m, 1021, 300, false, false);
    ui_wm_pointer(&m, 1021, 300, false, true);
    CHECK(a.snapped == UI_SNAP_RIGHT && same_rect(a.r, 512, 0, 512, 728) && same_rect(a.normal_r, 80, 20, 352, 377));
    CHECK_EQ(find_event(&m, UI_EV_SNAP)->count, UI_SNAP_RIGHT);
    /* At the right edge but over the taskbar: no snap, a drag (clamped). */
    CHECK(ui_wm_snap(&m, &a, UI_SNAP_NONE) && same_rect(a.r, 80, 20, 352, 377));
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 100, 30, true, false);
    ui_wm_pointer(&m, 1023, 767, false, false);
    ui_wm_pointer(&m, 1023, 767, false, true);
    CHECK(!a.snapped && !a.maximised && same_rect(a.r, 976, 351, 352, 377) && count(&m, UI_EV_DRAG) == 1);
    /* No maximise button: the top zone only drags; not resizable: no half. */
    struct ui_window b = win("B", 352, 377, false);
    b.buttons = UI_WIN_MINIMISE;
    ui_wm_open(&m, &b);                             /* (336, 175) */
    ui_wm_clear_events(&m);
    ui_wm_pointer(&m, 400, 190, true, false);
    ui_wm_pointer(&m, 400, 2, false, false);
    ui_wm_pointer(&m, 400, 2, false, true);
    CHECK(!b.maximised && b.r.y == 0 && count(&m, UI_EV_DRAG) == 1 && count(&m, UI_EV_MAXIMISE) == 0);
    ui_wm_pointer(&m, 400, 10, true, false);
    ui_wm_pointer(&m, 1, 300, false, false);
    ui_wm_pointer(&m, 1, 300, false, true);
    CHECK(!b.snapped && same_rect(b.r, -63, 290, 352, 377));
    /* A minimum wider than the half (600 > 512): no snap. */
    struct ui_window c = resizable_win("C");
    c.min_w = 600;
    ui_wm_open(&m, &c);                             /* (336, 175): free again */
    CHECK(same_rect(c.r, 336, 175, 352, 377));
    ui_wm_pointer(&m, 400, 190, true, false);
    ui_wm_pointer(&m, 1, 300, false, false);
    ui_wm_pointer(&m, 1, 300, false, true);
    CHECK(!c.snapped && count(&m, UI_EV_SNAP) == 0);
    /* A click (no move) in the top zone does not snap. */
    struct ui_window d = win("D", 352, 377, false);
    d.buttons = UI_WIN_MINIMISE | UI_WIN_MAXIMISE;
    ui_wm_open(&m, &d);                             /* C was dragged to (-63, 285): (336, 175) free */
    CHECK(same_rect(c.r, -63, 285, 352, 377) && same_rect(d.r, 336, 175, 352, 377));
    ui_wm_pointer(&m, 400, 190, true, false);
    ui_wm_pointer(&m, 400, 11, false, false);
    ui_wm_pointer(&m, 400, 11, false, true);        /* y 11: outside the zone */
    CHECK(!d.maximised && same_rect(d.r, 336, 0, 352, 377));
    ui_wm_set_now(&m, 1000);
    click(&m, 400, 2);
    CHECK(!d.maximised && m.click_w == &d);
    /* Extreme pointer values while dragging away: the clamp holds. */
    ui_wm_toggle_maximise(&m, &d);
    ui_wm_set_now(&m, 2000);
    ui_wm_pointer(&m, 500, 10, true, false);
    ui_wm_pointer(&m, INT32_MAX, INT32_MAX, false, false);
    CHECK(!d.maximised && same_rect(d.r, 976, 351, 352, 377));
    ui_wm_pointer(&m, INT32_MAX, INT32_MAX, false, true);       /* below the taskbar: no snap */
    CHECK(!d.maximised && !d.snapped && same_rect(d.r, 976, 351, 352, 377));
    ui_wm_toggle_maximise(&m, &d);
    ui_wm_set_now(&m, 3000);
    ui_wm_pointer(&m, 500, 10, true, false);
    ui_wm_pointer(&m, INT32_MIN, INT32_MIN, false, false);
    CHECK(!d.maximised && same_rect(d.r, 48 - 352, 0, 352, 377));
    ui_wm_pointer(&m, INT32_MIN, INT32_MIN, false, true);       /* above the top: the top zone */
    CHECK(d.maximised && same_rect(d.normal_r, 48 - 352, 0, 352, 377));
}

/* Super+Up, Down, Left, Right, Pause and Alt+F4 on the focused window. */
static void test_shortcut_window(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window c = win("C", 642, 433, false), a = resizable_win("A");
    c.buttons = UI_WIN_MINIMISE;
    ui_wm_open(&m, &c);                             /* (191, 147) */
    ui_wm_open(&m, &a);                             /* (336, 175), focused */
    ui_wm_clear_events(&m);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_UP), 1);
    CHECK(a.maximised && m.ev[0].kind == UI_EV_SHORTCUT && m.ev[0].sc == UI_SC_UP && m.ev[0].count == 1);
    CHECK(m.ev[1].kind == UI_EV_MAXIMISE);
    ui_wm_clear_events(&m);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_UP), 0);      /* already maximised */
    CHECK(m.ev[0].kind == UI_EV_SHORTCUT && m.ev[0].count == 0);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_DOWN), 1);    /* restored */
    CHECK(!a.maximised && same_rect(a.r, 336, 175, 352, 377));
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_DOWN), 1);    /* minimised; the focus to C */
    CHECK(a.minimised && m.focus == &c);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_UP), 0);      /* C: no maximise */
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_LEFT), 0);    /* C: not resizable */
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_RIGHT), 0);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_DOWN), 1);
    CHECK(c.minimised && m.focus == 0);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_DOWN), 0);    /* no window focused */
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_CLOSE), 0);
    ui_wm_clear_events(&m);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_ABOUT), 0);
    CHECK(m.n_ev == 1 && m.ev[0].sc == UI_SC_ABOUT);
    /* Left and right halves; the other half restores; Up from a half keeps
     * the rectangle kept before the snap; Down restores it. */
    ui_wm_activate(&m, &a);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_LEFT), 1);
    CHECK(a.snapped == UI_SNAP_LEFT && same_rect(a.r, 0, 0, 512, 728) && same_rect(a.normal_r, 336, 175, 352, 377));
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_LEFT), 0);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_RIGHT), 1);
    CHECK(!a.snapped && same_rect(a.r, 336, 175, 352, 377));
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_RIGHT), 1);
    CHECK(a.snapped == UI_SNAP_RIGHT && same_rect(a.r, 512, 0, 512, 728));
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_UP), 1);
    CHECK(a.maximised && !a.snapped && same_rect(a.normal_r, 336, 175, 352, 377));
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_DOWN), 1);
    CHECK(!a.maximised && same_rect(a.r, 336, 175, 352, 377));
    /* A half from maximised keeps the rectangle too; Down unsnaps. */
    ui_wm_shortcut(&m, UI_SC_UP);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_LEFT), 1);
    CHECK(!a.maximised && a.snapped == UI_SNAP_LEFT && same_rect(a.normal_r, 336, 175, 352, 377));
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_DOWN), 1);
    CHECK(!a.snapped && same_rect(a.r, 336, 175, 352, 377));
    /* Alt+F4 closes the focused window. */
    ui_wm_clear_events(&m);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_CLOSE), 1);
    CHECK(!a.open && count(&m, UI_EV_CLOSE_KEY) == 1 && m.focus == 0 && count(&m, UI_EV_UNFOCUS) == 1);
    CHECK(m.ev[0].kind == UI_EV_SHORTCUT && m.ev[0].sc == UI_SC_CLOSE && m.ev[0].count == 1);
}

/* Super+D (and its reset), Super+M and Super+Shift+M. */
static void test_shortcut_desktop(void)
{
    struct ui_wm m = wm_1024();
    struct ui_window c = win("C", 100, 100, true), a = resizable_win("A"), b = win("B", 100, 100, false);
    c.buttons = UI_WIN_MINIMISE;
    b.buttons = UI_WIN_MINIMISE;
    ui_wm_open(&m, &c);
    ui_wm_open(&m, &a);                             /* z: C, A; A focused */
    ui_wm_clear_events(&m);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP), 2);
    CHECK(a.minimised && c.minimised && m.focus == 0 && m.shown_valid);
    CHECK(m.ev[0].kind == UI_EV_SHORTCUT && m.ev[0].count == 2 && !m.ev[0].restored);
    ui_wm_clear_events(&m);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP), 2);   /* back as they were */
    CHECK(!a.minimised && !c.minimised && m.z[0] == &c && m.z[1] == &a && m.focus == &a);
    CHECK(m.ev[0].restored && !m.shown_valid);
    /* Something else restores a window in between: the next Super+D starts again. */
    ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP);
    ui_wm_activate(&m, &c);
    ui_wm_clear_events(&m);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP), 1);   /* C, the one shown, minimised */
    CHECK(!m.ev[0].restored && c.minimised && a.minimised);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP), 1);   /* C back; A stays minimised */
    CHECK(!c.minimised && a.minimised && m.focus == &c);
    /* A window opened in between: the same. */
    ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP);
    ui_wm_open(&m, &b);
    ui_wm_clear_events(&m);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP), 1);
    CHECK(b.minimised && !m.ev[0].restored);
    /* No window focused when it was kept: none focused after. */
    ui_wm_activate(&m, &a);
    ui_wm_activate(&m, &c);
    ui_wm_activate(&m, &b);
    click(&m, 5, 5);
    CHECK(m.focus == 0);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP), 3);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP), 3);
    CHECK(!a.minimised && !b.minimised && !c.minimised && m.focus == 0);
    /* Nothing shown: nothing kept. */
    ui_wm_minimise(&m, &a);
    ui_wm_minimise(&m, &b);
    ui_wm_minimise(&m, &c);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_SHOW_DESKTOP), 0);
    CHECK(!m.shown_valid);
    /* Super+M and Super+Shift+M. */
    ui_wm_activate(&m, &a);
    ui_wm_activate(&m, &c);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_MINIMISE_ALL), 2);
    CHECK(a.minimised && c.minimised && m.minall_n == 2);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_RESTORE_ALL), 2);
    CHECK(!a.minimised && !c.minimised && b.minimised && m.focus == &c && m.minall_n == 0);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_RESTORE_ALL), 0);
    /* A kept window closed in between is skipped. */
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_MINIMISE_ALL), 2);
    ui_wm_activate(&m, &c);
    CHECK(ui_wm_esc(&m) && !c.open);
    CHECK_EQ(ui_wm_shortcut(&m, UI_SC_RESTORE_ALL), 1);
    CHECK(!a.minimised && m.focus == &a);
}

int main(void)
{
    printf("test_ui_window\n");
    RUN(test_frame_sizes);
    RUN(test_place_clamp);
    RUN(test_hit_z_focus);
    RUN(test_drag);
    RUN(test_close_button);
    RUN(test_title_press_drags);
    RUN(test_surface_equals_direct);
    RUN(test_frame_pixels);
    RUN(test_min_max_restore);
    RUN(test_taskbar_buttons);
    RUN(test_switch);
    RUN(test_double_click);
    RUN(test_resize_band);
    RUN(test_resize_drag);
    RUN(test_resize_extremes);
    RUN(test_resize_surface_equals_direct);
    RUN(test_snap_drag);
    RUN(test_shortcut_window);
    RUN(test_shortcut_desktop);
    return unit_report("test_ui_window");
}
