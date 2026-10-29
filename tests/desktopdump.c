/*
 * The desktop toolkit as a host tool (1.3 stage 2): the pure toolkit code
 * (kernel/display/ui_*.c, icons.c, coverage_core.c, with the console
 * fonts, regular and bold) draws one of the stage's test scenes on a
 * screen, as the kernel will on its back buffer, and the screen is written
 * as a PPM (P6). Part static compares it with tests/desktop_ref.py, the
 * independent reference, pixel for pixel.
 *
 *     desktopdump WIDTH HEIGHT BYTES_PER_PIXEL SCENE OUT.ppm
 *
 * SCENE is prims (rectangles, clipping, rounded rectangles and frames,
 * text in both weights at every scale, two damage lists), icons (every
 * icon at every scale at 16 and 24 dp and at the screen's scale at 32 and
 * 48 dp, on the backgrounds of the Design Guide's contrast table, and
 * clipped) or widgets (labels in every alignment and both weights, and
 * every button in each state the Design Guide defines, at the screen's
 * scale and at 125 %). 1.3 stage 3b: also background (the wallpaper with
 * the M) and taskbar, the desktop's own pictures (kernel/display/desktop_core.h; the functions
 * below say how each is drawn). The first three scenes are test
 * arrangements, not the desktop's layout; desktop_ref.py lays them out
 * from the same description:
 *   - the scale s is the console's (fbcon_layout); d(n) = ui_px(n, s);
 *   - the screen starts filled with bg.deep;
 *   - a flow places boxes left to right from (margin, margin) with `gap`
 *     between them; a box that would cross the right margin (unless it is
 *     the first of its row) starts a new row, `gap` below the row's
 *     tallest box; a break starts a new row likewise.
 * Prints one line (SCENE ...: the scale, the flow's last row's bottom and
 * the damage lists). Exit status 1 on any error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/display/desktop_core.h"
#include "kernel/display/fbcon_core.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_widget.h"

struct flow {
    int32_t w, margin, gap;
    int32_t x, y, row_h;
};

static void flow_init(struct flow *f, int32_t w, int32_t margin, int32_t gap)
{
    *f = (struct flow){ w, margin, gap, margin, margin, 0 };
}

static void flow_break(struct flow *f)
{
    if (f->x != f->margin) {
        f->x = f->margin;
        f->y += f->row_h + f->gap;
        f->row_h = 0;
    }
}

static struct rect flow_place(struct flow *f, int32_t w, int32_t h)
{
    if (f->x != f->margin && f->x + w > f->w - f->margin)
        flow_break(f);
    struct rect r = { f->x, f->y, w, h };
    f->x += w + f->gap;
    if (h > f->row_h)
        f->row_h = h;
    return r;
}

static int32_t flow_bottom(const struct flow *f)
{
    return f->y + f->row_h;
}

static uint32_t S;              /* the screen's scale */

static int32_t d(int32_t dp)
{
    return ui_px(dp, S);
}

/* ---- prims ---- */

static void frame_list(const struct ui_canvas *c, const struct ui_damage *dl, uint32_t rgb)
{
    for (uint32_t i = 0; i < dl->n; i++)
        ui_frame_round(c, dl->r[i], 0, 1, 0, rgb);
}

static void scene_prims(struct ui_canvas *c, int32_t W, int32_t H)
{
    struct ui_fonts fonts[4];
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    for (int i = 0; i < 4; i++)
        ui_fonts_for_scale(scales[i], &fonts[i]);

    /* Fills reaching past the left, right and top edges. */
    ui_fill(c, (struct rect){ -d(8), d(8), d(40), d(24) }, UI_SURFACE);
    ui_fill(c, (struct rect){ W - d(24), d(8), d(48), d(24) }, UI_SURFACE_WINDOW);
    ui_fill(c, (struct rect){ d(48), -d(10), d(32), d(24) }, UI_SURFACE_ACTIVE);

    struct flow f;
    flow_init(&f, W, d(16), d(8));
    f.y = d(40);
    /* Rounded rectangles: radii, then corners, then odd sizes. */
    static const int32_t radii[6] = { 0, 2, 4, 6, 12, 40 };
    for (int i = 0; i < 6; i++)
        ui_fill_round(c, flow_place(&f, d(64), d(40)), d(radii[i]), UI_CORNERS_ALL, UI_SURFACE_WINDOW);
    static const uint32_t corners[6] = { UI_CORNER_TL, UI_CORNER_TR, UI_CORNER_BL, UI_CORNER_BR,
                                         UI_CORNER_TL | UI_CORNER_BR, 0 };
    for (int i = 0; i < 6; i++)
        ui_fill_round(c, flow_place(&f, d(64), d(40)), d(12), corners[i], UI_SURFACE);
    ui_fill_round(c, flow_place(&f, d(33) + 1, d(21) + 1), d(8), UI_CORNERS_ALL, UI_SURFACE_ACTIVE);
    ui_fill_round(c, flow_place(&f, d(7), d(40)), d(6), UI_CORNERS_ALL, UI_SURFACE_ACTIVE);
    flow_break(&f);
    /* Frames: radius and width in dp. */
    static const int32_t frames[7][2] = { { 0, 1 }, { 6, 1 }, { 6, 2 }, { 12, 3 }, { 40, 2 }, { 2, 4 }, { 6, 30 } };
    static const uint32_t edge[3] = { UI_BORDER_WINDOW, UI_BORDER_PANEL, UI_BORDER };
    for (int i = 0; i < 7; i++)
        ui_frame_round(c, flow_place(&f, d(64), d(40)), d(frames[i][0]), d(frames[i][1]), UI_CORNERS_ALL,
                       edge[i % 3]);
    ui_frame_round(c, flow_place(&f, d(64), d(40)), d(12), d(2), UI_CORNER_TR | UI_CORNER_BL, UI_BORDER_INACTIVE);
    flow_break(&f);

    /* Text: every font, both weights, on surface.window. */
    static const char line[] = "Madire 1.3 \xFA Ag{}|_@ 0189 \x10";
    int32_t ph = d(8);
    for (int i = 0; i < 4; i++)
        ph += 2 * ((int32_t)fonts[i].regular->height + d(4));
    struct rect panel = flow_place(&f, W - 2 * d(16), ph);
    ui_fill(c, panel, UI_SURFACE_WINDOW);
    int32_t ty = panel.y + d(4);
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 2; b++) {
            const struct font *ft = b ? fonts[i].bold : fonts[i].regular;
            ui_text(c, ft, panel.x + d(8), ty, line, b ? UI_TEXT : UI_TEXT_MUTED);
            ty += (int32_t)ft->height + d(4);
        }
    flow_break(&f);

    /* Clipping: a rounded rectangle and text drawn through a clip. */
    struct rect k = flow_place(&f, d(120), d(30));
    ui_fill(c, k, UI_SURFACE);
    struct ui_canvas in = *c;
    ui_canvas_clip(&in, k);
    ui_fill_round(&in, (struct rect){ k.x - d(20), k.y - d(10), d(100), d(50) }, d(12), UI_CORNERS_ALL,
                  UI_SURFACE_ACTIVE);
    ui_text(&in, fonts[0].regular, k.x + d(60), k.y + d(4), "clipped text here", UI_TEXT);
    flow_break(&f);

    /* Damage lists: one with a contained, two clipped and an empty
     * rectangle; one past full (33 rectangles: the bounding rectangle). */
    struct ui_damage a, b;
    int32_t ya = flow_place(&f, d(100), d(52)).y;
    ui_damage_init(&a, (uint32_t)W, (uint32_t)H);
    ui_damage_add(&a, (struct rect){ d(16), ya, d(40), d(20) });
    ui_damage_add(&a, (struct rect){ d(26), ya + d(5), d(10), d(5) });
    ui_damage_add(&a, (struct rect){ d(76), ya, d(30), d(30) });
    ui_damage_add(&a, (struct rect){ -d(10), ya + d(40), d(30), d(10) });
    ui_damage_add(&a, (struct rect){ W - d(20), ya, d(60), d(10) });
    ui_damage_add(&a, (struct rect){ 0, 0, 0, 5 });
    frame_list(c, &a, UI_BORDER_PANEL);
    flow_break(&f);
    int32_t yb = flow_place(&f, d(210), d(20)).y;
    ui_damage_init(&b, (uint32_t)W, (uint32_t)H);
    for (int i = 0; i < 33; i++)
        ui_damage_add(&b, (struct rect){ d(16) + i * d(6), yb + (i % 3) * d(6), d(4), d(4) });
    frame_list(c, &b, UI_BORDER_WINDOW);
    printf("SCENE prims %dx%d: scale %u %%, bottom %d, damage lists %u and %u rectangles", W, H, S * 25,
           flow_bottom(&f), a.n, b.n);
    printf(", the full list's (%d, %d) %dx%d\n", b.r[0].x, b.r[0].y, b.r[0].w, b.r[0].h);
}

/* ---- icons ---- */

static const uint32_t icon_pairs[9][2] = {     /* foreground, background (Design Guide, section 12) */
    { UI_TEXT, UI_SURFACE_WINDOW }, { UI_TEXT, UI_SURFACE }, { UI_TEXT, UI_SURFACE_ACTIVE },
    { UI_TEXT, UI_DANGER }, { UI_TEXT, UI_DANGER_PRESSED }, { UI_TEXT_ON_ACCENT, UI_ACCENT },
    { UI_ACCENT, UI_SURFACE_BAR }, { UI_ACCENT, UI_SURFACE_ACTIVE }, { UI_ACCENT, UI_SURFACE },
};

static void scene_icons(struct ui_canvas *c, int32_t W)
{
    /* Rows of (scale, size in dp); 0 is the screen's scale. */
    static const uint32_t rows[10][2] = { { 4, 16 }, { 4, 24 }, { 5, 16 }, { 5, 24 }, { 6, 16 },
                                          { 6, 24 }, { 8, 16 }, { 8, 24 }, { 0, 32 }, { 0, 48 } };
    struct flow f;
    flow_init(&f, W, 8, 4);
    uint32_t k = 0;
    for (int row = 0; row < 10; row++) {
        int32_t p = ui_px((int32_t)rows[row][1], rows[row][0] ? rows[row][0] : S);
        for (int i = 0; i < UI_ICON_COUNT; i++, k++) {
            struct rect cell = flow_place(&f, p + 4, p + 4);
            ui_fill(c, cell, icon_pairs[k % 9][1]);
            ui_icon_draw(c, i, cell.x + 2, cell.y + 2, p, icon_pairs[k % 9][0]);
        }
        flow_break(&f);
    }
    /* Clipped: past the left and the right edge, and through a clip. */
    int32_t p = d(24), y = flow_place(&f, p + 4, p + 4).y;
    ui_fill(c, (struct rect){ -p / 2 - 2, y, p + 4, p + 4 }, UI_SURFACE_WINDOW);
    ui_icon_draw(c, UI_ICON_ABOUT, -p / 2, y + 2, p, UI_TEXT);
    ui_fill(c, (struct rect){ W - p / 2 - 2, y, p + 4, p + 4 }, UI_SURFACE_WINDOW);
    ui_icon_draw(c, UI_ICON_RESTART, W - p / 2, y + 2, p, UI_TEXT);
    struct rect cell = { W / 2, y, p + 4, p + 4 };
    ui_fill(c, cell, UI_SURFACE);
    struct ui_canvas in = *c;
    ui_canvas_clip(&in, (struct rect){ cell.x, cell.y, cell.w / 2, cell.h });
    ui_icon_draw(&in, UI_ICON_M, cell.x + 2, cell.y + 2, p, UI_ACCENT);
    printf("SCENE icons: scale %u %%, %u icons, bottom %d\n", S * 25, k, flow_bottom(&f));
}

/* ---- widgets ---- */

static const uint32_t label_pairs[6][2] = {
    { UI_TEXT, UI_SURFACE_WINDOW }, { UI_TEXT_MUTED, UI_SURFACE_WINDOW }, { UI_TEXT, UI_SURFACE },
    { UI_TEXT, UI_SURFACE_BAR }, { UI_TEXT, UI_SURFACE_ACTIVE }, { UI_TEXT_ON_ACCENT, UI_ACCENT },
};

struct button_case {
    const struct ui_button_style *style;
    int32_t w_dp, h_dp;
    const char *label;
    int icon;
    int32_t icon_dp;
    enum ui_button_state st;
    bool focused;
};

static const struct button_case buttons[16] = {
    { &ui_style_close, 32, 32, NULL, UI_ICON_CLOSE, 16, UI_BUTTON_NORMAL, false },
    { &ui_style_close, 32, 32, NULL, UI_ICON_CLOSE, 16, UI_BUTTON_HOVER, false },
    { &ui_style_close, 32, 32, NULL, UI_ICON_CLOSE, 16, UI_BUTTON_PRESSED, false },
    { &ui_style_secondary, 96, 32, "Cancel", UI_ICON_NONE, 0, UI_BUTTON_NORMAL, false },
    { &ui_style_secondary, 96, 32, "Cancel", UI_ICON_NONE, 0, UI_BUTTON_NORMAL, true },
    { &ui_style_primary, 96, 32, "Restart", UI_ICON_NONE, 0, UI_BUTTON_NORMAL, false },
    { &ui_style_primary, 96, 32, "Restart", UI_ICON_NONE, 0, UI_BUTTON_NORMAL, true },
    { &ui_style_key, 52, 40, "7", UI_ICON_NONE, 0, UI_BUTTON_NORMAL, false },
    { &ui_style_key, 52, 40, "7", UI_ICON_NONE, 0, UI_BUTTON_PRESSED, false },
    { &ui_style_key, 52, 40, "7", UI_ICON_NONE, 0, UI_BUTTON_NORMAL, true },
    { &ui_style_key, 52, 40, NULL, UI_ICON_TIMES, 16, UI_BUTTON_NORMAL, false },
    { &ui_style_key, 52, 40, NULL, UI_ICON_TIMES, 16, UI_BUTTON_PRESSED, false },
    { &ui_style_key, 52, 40, NULL, UI_ICON_BACKSPACE, 16, UI_BUTTON_NORMAL, false },
    { &ui_style_key, 52, 40, NULL, UI_ICON_BACKSPACE, 16, UI_BUTTON_PRESSED, true },
    { &ui_style_key_equals, 52, 40, "=", UI_ICON_NONE, 0, UI_BUTTON_NORMAL, false },
    { &ui_style_key_equals, 52, 40, "=", UI_ICON_NONE, 0, UI_BUTTON_PRESSED, false },
};

static int scene_widgets(struct ui_canvas *c, int32_t W)
{
    struct ui_fonts mode;
    ui_fonts_for_scale(S, &mode);
    struct flow f;
    flow_init(&f, W, d(16), d(8));
    /* Labels: three alignments in both weights, then one too wide for its
     * box and one in a box lower than the font. */
    for (int i = 0; i < 8; i++) {
        struct rect r = flow_place(&f, d(160), i < 7 ? d(32) : d(8));
        ui_fill(c, r, label_pairs[i % 6][1]);
        const struct font *ft = i % 2 ? mode.bold : mode.regular;
        const char *text = i == 6 ? "A label longer than its box" : i == 7 ? "Low" : "Label";
        ui_label_draw(c, r, ft, text, label_pairs[i % 6][0], (enum ui_align)(i < 6 ? i / 2 : 1));
    }
    flow_break(&f);
    /* Buttons, each in a surface.window cell, at the screen's scale and at
     * 125 %. */
    int drawn = 0;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t s = pass ? 5 : S;
        struct ui_fonts ft;
        ui_fonts_for_scale(s, &ft);
        int32_t pad = ui_px(6, s);
        for (int i = 0; i < 16; i++) {
            const struct button_case *b = &buttons[i];
            int32_t bw = ui_px(b->w_dp, s), bh = ui_px(b->h_dp, s);
            struct rect cell = flow_place(&f, bw + 2 * pad, bh + 2 * pad);
            ui_fill(c, cell, UI_SURFACE_WINDOW);
            struct ui_button btn = { { cell.x + pad, cell.y + pad, bw, bh }, b->label, b->icon, b->icon_dp };
            drawn += ui_button_draw(c, &btn, b->style, b->st, b->focused, s, &ft);
        }
        flow_break(&f);
    }
    printf("SCENE widgets: scale %u %%, %d buttons drawn, bottom %d\n", S * 25, drawn, flow_bottom(&f));
    return drawn == 32 ? 0 : 1;
}

/* 1.3 stage 3b: the desktop's background (the wallpaper with the M at the
 * splash's place), drawn as the compositor does when there is no cache:
 * tile by tile, each tile drawn again from scratch, 97 x 61 pixels from
 * (-7, -13); the result must be the whole background. */
static int scene_background(struct surface *s, const struct fbcon_layout *cl)
{
    struct desktop_layout l;
    struct wallpaper wp;
    if (!desktop_layout(&l, s->width, s->height, cl->scale, cl->font))
        return 1;
    wallpaper_setup(&wp, s->width, s->height, cl->scale);
    uint32_t tiles = 0;
    for (int32_t y = -13; y < (int32_t)s->height; y += 61)
        for (int32_t x = -7; x < (int32_t)s->width; x += 97, tiles++)
            desktop_draw_background(s, &l, &wp, (struct rect){ x, y, 97, 61 });
    printf("SCENE background: scale %u %%, drawn in %u tiles of 97x61; the M's origin (%lld, %lld)/256\n",
           cl->scale * 25, tiles, (long long)l.lox, (long long)l.loy);
    return 0;
}

/* 1.3 stage 3b: the taskbar on bg.deep with the clock "88:88", then its
 * clock's box alone again with the fixed time "09:41" (a minute changed):
 * the result must be the taskbar with "09:41". */
static int scene_taskbar(struct surface *s, const struct fbcon_layout *cl)
{
    struct desktop_layout l;
    if (!desktop_layout(&l, s->width, s->height, cl->scale, cl->font))
        return 1;
    desktop_draw_taskbar(s, &l, (struct rect){ 0, 0, (int32_t)s->width, (int32_t)s->height }, "88:88");
    desktop_draw_taskbar(s, &l, l.clock_box, "09:41");
    printf("SCENE taskbar: scale %u %%, the bar at y %d (%d high), the clock's box at (%d, %d) %dx%d\n",
           cl->scale * 25, l.bar.y, l.bar.h, l.clock_box.x, l.clock_box.y, l.clock_box.w, l.clock_box.h);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: desktopdump WIDTH HEIGHT BYTES_PER_PIXEL SCENE OUT.ppm\n");
        return 1;
    }
    uint32_t w = (uint32_t)atoi(argv[1]), h = (uint32_t)atoi(argv[2]), bpp = (uint32_t)atoi(argv[3]);
    static const struct font *const fonts[4] = { &font_8x16, &font_10x20, &font_12x24, &font_16x32 };
    struct fbcon_layout cl;
    struct surface s;
    struct ui_canvas c;
    uint8_t *pix = calloc((size_t)w * h, bpp);
    if (!pix || !surface_init(&s, pix, w, h, w * bpp, bpp) || !fbcon_layout(w, h, fonts, &cl)) {
        fprintf(stderr, "desktopdump: no screen %ux%ux%u\n", w, h, bpp * 8);
        return 1;
    }
    S = cl.scale;
    ui_canvas_init(&c, &s);
    ui_fill(&c, (struct rect){ 0, 0, (int32_t)w, (int32_t)h }, UI_BG_DEEP);
    int rc = 0;
    if (strcmp(argv[4], "prims") == 0) {
        scene_prims(&c, (int32_t)w, (int32_t)h);
    } else if (strcmp(argv[4], "icons") == 0) {
        scene_icons(&c, (int32_t)w);
    } else if (strcmp(argv[4], "widgets") == 0) {
        rc = scene_widgets(&c, (int32_t)w);
    } else if (strcmp(argv[4], "background") == 0) {
        rc = scene_background(&s, &cl);
    } else if (strcmp(argv[4], "taskbar") == 0) {
        rc = scene_taskbar(&s, &cl);
    } else {
        fprintf(stderr, "desktopdump: unknown scene %s\n", argv[4]);
        return 1;
    }
    FILE *f = fopen(argv[5], "wb");
    if (!f)
        return 1;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v = surface_get(&s, x, y);
            fputc((int)(v >> 16 & 0xFF), f);
            fputc((int)(v >> 8 & 0xFF), f);
            fputc((int)(v & 0xFF), f);
        }
    if (fclose(f) != 0)
        return 1;
    return rc;
}
