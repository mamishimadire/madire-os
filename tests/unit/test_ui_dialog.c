/* Unit tests for kernel/display/ui_dialog.c (the Restart dialog, 1.3 stage
 * 5): its size and place (centred above the taskbar, never above the
 * screen), its window (titled Restart, the restart icon, open), its
 * buttons' rectangles, hit testing (the close button, Cancel, Restart, the
 * rest, outside), the state when it opens (Cancel focused, the ring as
 * asked, every button normal), and the picture: each button's fill in each
 * state (the Design Guide, section 9: Cancel surface.active, surface.hover,
 * accent.pressed; Restart accent, accent.hover, accent.pressed), the focus
 * ring on the focused button only and only with the ring on, the close
 * button on hover, and the dialog painted in tiles equal, byte for byte, to
 * the dialog painted whole. Fonts are made here (a fixed bit pattern). The
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
#include "kernel/display/ui_dialog.c"

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

static bool same_rect(struct rect a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return a.x == x && a.y == y && a.w == w && a.h == h;
}

static struct ui_dialog dlg;

static void test_place_and_state(void)
{
    struct ui_fonts f = fonts_for(4);
    int32_t w = 0, h = 0;
    ui_dialog_size(4, &w, &h);
    CHECK(w == 360 && h == 172);
    ui_dialog_size(5, &w, &h);
    CHECK(w == 450 && h == 215);
    ui_dialog_init(&dlg, 4, &f, 1024, 728, false);
    CHECK(same_rect(dlg.w.r, 332, 278, 360, 172));
    CHECK(dlg.w.open && strcmp(dlg.w.title, "Restart") == 0 && dlg.w.icon == UI_ICON_RESTART);
    CHECK(dlg.focus == UI_DLG_CANCEL && !dlg.ring);
    CHECK(dlg.btn[UI_DLG_CANCEL] == UI_BUTTON_NORMAL && dlg.btn[UI_DLG_RESTART] == UI_BUTTON_NORMAL);
    CHECK(dlg.w.close == UI_BUTTON_NORMAL && dlg.w.ctx == &dlg);
    CHECK(same_rect(ui_dialog_button_rect(&dlg, UI_DLG_CANCEL), 468, 394, 96, 32));
    CHECK(same_rect(ui_dialog_button_rect(&dlg, UI_DLG_RESTART), 572, 394, 96, 32));
    ui_dialog_init(&dlg, 4, &f, 800, 120, true);
    CHECK(same_rect(dlg.w.r, 220, 0, 360, 172) && dlg.ring);       /* never above the screen */
    struct ui_fonts f8 = fonts_for(8);
    ui_dialog_init(&dlg, 8, &f8, 2560, 1360, false);
    CHECK(same_rect(dlg.w.r, 920, 508, 720, 344));
    CHECK(same_rect(ui_dialog_button_rect(&dlg, UI_DLG_RESTART), 920 + 480, 508 + 232, 192, 64));
}

static void test_hit(void)
{
    struct ui_fonts f = fonts_for(4);
    ui_dialog_init(&dlg, 4, &f, 1024, 728, false);
    CHECK_EQ(ui_dialog_hit(&dlg, 516, 410), UI_DLG_PART_CANCEL);
    CHECK_EQ(ui_dialog_hit(&dlg, 468, 394), UI_DLG_PART_CANCEL);
    CHECK_EQ(ui_dialog_hit(&dlg, 567, 410), UI_DLG_BODY);           /* between the buttons */
    CHECK_EQ(ui_dialog_hit(&dlg, 620, 410), UI_DLG_PART_RESTART);
    CHECK_EQ(ui_dialog_hit(&dlg, 676, 294), UI_DLG_PART_CLOSE);
    CHECK_EQ(ui_dialog_hit(&dlg, 400, 300), UI_DLG_BODY);
    CHECK_EQ(ui_dialog_hit(&dlg, 331, 300), UI_DLG_NONE);
    CHECK_EQ(ui_dialog_hit(&dlg, 500, 450), UI_DLG_NONE);
}

static struct surface screen(uint32_t w, uint32_t h, uint32_t bpp)
{
    struct surface s;
    uint8_t *px = calloc((size_t)w * h, bpp);
    surface_init(&s, px, w, h, w * bpp, bpp);
    struct ui_canvas c;
    ui_canvas_init(&c, &s);
    ui_fill(&c, (struct rect){ 0, 0, (int32_t)w, (int32_t)h }, UI_BG_DEEP);
    return s;
}

/* The fill of each button (a pixel inside it, clear of its label) and the
 * ring above it, after drawing the dialog. */
static uint32_t fill_at(struct surface *s, enum ui_dialog_button b)
{
    struct rect r = ui_dialog_button_rect(&dlg, b);
    return surface_get(s, (uint32_t)r.x + 5, (uint32_t)r.y + 16);
}

static uint32_t ring_at(struct surface *s, enum ui_dialog_button b)
{
    struct rect r = ui_dialog_button_rect(&dlg, b);
    return surface_get(s, (uint32_t)r.x + 48, (uint32_t)r.y - 4);
}

static void test_picture(void)
{
    struct ui_fonts f = fonts_for(4);
    struct surface s = screen(1024, 768, 4);
    struct ui_canvas c;
    ui_canvas_init(&c, &s);
    ui_dialog_init(&dlg, 4, &f, 1024, 728, false);
    ui_dialog_draw(&c, &dlg);
    CHECK_EQ(fill_at(&s, UI_DLG_CANCEL), UI_SURFACE_ACTIVE);
    CHECK_EQ(fill_at(&s, UI_DLG_RESTART), UI_ACCENT);
    CHECK_EQ(ring_at(&s, UI_DLG_CANCEL), UI_SURFACE_WINDOW);      /* focused, but the ring off */
    CHECK_EQ(surface_get(&s, 400, 278), UI_BORDER_WINDOW);        /* drawn active: the active edge */
    CHECK_EQ(surface_get(&s, 662, 280), UI_SURFACE_WINDOW);       /* the close button, normal */
    static const uint32_t cancel[3] = { UI_SURFACE_ACTIVE, UI_SURFACE_HOVER, UI_ACCENT_PRESSED };
    static const uint32_t restart[3] = { UI_ACCENT, UI_ACCENT_HOVER, UI_ACCENT_PRESSED };
    for (int st = 0; st < 3; st++) {
        dlg.btn[UI_DLG_CANCEL] = (enum ui_button_state)st;
        dlg.btn[UI_DLG_RESTART] = (enum ui_button_state)(2 - st);
        ui_dialog_draw(&c, &dlg);
        CHECK_EQ(fill_at(&s, UI_DLG_CANCEL), cancel[st]);
        CHECK_EQ(fill_at(&s, UI_DLG_RESTART), restart[2 - st]);
    }
    dlg.btn[UI_DLG_CANCEL] = dlg.btn[UI_DLG_RESTART] = UI_BUTTON_NORMAL;
    dlg.ring = true;
    ui_dialog_draw(&c, &dlg);
    CHECK_EQ(ring_at(&s, UI_DLG_CANCEL), UI_ACCENT);
    CHECK_EQ(ring_at(&s, UI_DLG_RESTART), UI_SURFACE_WINDOW);
    dlg.focus = UI_DLG_RESTART;
    dlg.w.close = UI_BUTTON_HOVER;
    dlg.btn[UI_DLG_CANCEL] = UI_BUTTON_HOVER;
    ui_dialog_draw(&c, &dlg);
    CHECK_EQ(ring_at(&s, UI_DLG_CANCEL), UI_SURFACE_WINDOW);
    CHECK_EQ(ring_at(&s, UI_DLG_RESTART), UI_ACCENT);
    CHECK_EQ(surface_get(&s, 662, 280), UI_DANGER);              /* the close button on hover */
    /* The same dialog painted in tiles of 37 x 29 from (-3, -11). */
    struct surface t = screen(1024, 768, 4);
    for (int32_t y = -11; y < 768; y += 29)
        for (int32_t x = -3; x < 1024; x += 37) {
            struct ui_canvas k;
            ui_canvas_init(&k, &t);
            ui_canvas_clip(&k, (struct rect){ x, y, 37, 29 });
            ui_dialog_draw(&k, &dlg);
        }
    CHECK(memcmp(s.pixels, t.pixels, (size_t)1024 * 768 * 4) == 0);
    free(s.pixels);
    free(t.pixels);
}

int main(void)
{
    printf("test_ui_dialog\n");
    RUN(test_place_and_state);
    RUN(test_hit);
    RUN(test_picture);
    return unit_report("test_ui_dialog");
}
