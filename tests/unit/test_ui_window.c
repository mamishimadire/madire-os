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
 * screen and in odd tiles (docs/plans/1.3.md, C4, A1). Fonts are made
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
    /* At most UI_WINDOWS_MAX. */
    struct ui_window c = win("C", 100, 100, false), d = win("D", 100, 100, false), e = win("E", 100, 100, false);
    CHECK(ui_wm_open(&m, &c) && ui_wm_open(&m, &d));
    CHECK(!ui_wm_open(&m, &e) && m.n == 4);
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
    return unit_report("test_ui_window");
}
