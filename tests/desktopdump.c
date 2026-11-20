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
 *
 * 1.3 stage 4a: the window scenes (kernel/display/ui_window.h, ui_about.h):
 * on bg.deep, two About windows placed by the window manager (the first
 * centred above the taskbar, the second 24 dp further: the place taken),
 * the first inactive, the second active with its close button normal
 * (windows), on hover (windows_hover) or pressed (windows_pressed), each
 * window painted on its own surface and composed through ui_blit_round,
 * the first then the second, as the compositor does; windows_exposed is
 * the windows scene painted directly in tiles of 97 x 61 pixels from
 * (-7, -13) (the exposure path without surfaces, docs/plans/1.3.md, C4,
 * A1). The values: Version 1.3, a boot loader's name longer than the
 * column (cut, ui_about.h), 2560x1440x32, 127 MB. Built with -DUI_TRACE: with
 * the environment variable DESKTOP_PAIRS set to a file, every scene
 * appends to it the colour pairs it drew (kernel/display/ui_trace.h), one
 * line each: PAIR <text|icon|state|carried-bar|carried-pointer>
 * <foreground RRGGBB> <background RRGGBB> <pixels> <scene> (the carried
 * kinds since 1.3 stage 5).
 *
 * 1.3 stage 4b: the Console window's scenes (kernel/display/ui_console.h):
 * on bg.deep, the Console window placed by the window manager with a fixed
 * grid in it (desktop_ref.py's DUMP_GRID: cell i, row r, column c, holds
 * character (7i + r) mod 256 in colour slot (r + c) mod 16 on slot (r div
 * 5) mod 8, the VGA's default palette; both bottom corner cells a full
 * block, white on blue; the cursor in the bottom-left one), active
 * (console) or inactive (console_inactive); console_about: the Console
 * window, then an About window opened over it (the Console window
 * inactive, the About window active), each painted on its own surface and
 * composed through ui_blit_round; console_exposed: console_about painted
 * directly in tiles of 97 x 61 pixels from (-7, -13) (the exposure path).
 *
 * 1.5 stage 8: chrome, chrome_hover, chrome_max (the title-bar buttons,
 * a maximised window) and taskbuttons (the taskbar's window buttons in
 * each state, the Alt+Tab ring); see scene_chrome and scene_taskbuttons.
 *
 * 1.5 stage 10: textfield, list, grid and scrollbar (kernel/display/
 * ui_textfield.h, ui_list.h): eight text fields on a surface.window panel;
 * two lists (20 items scrolled, a range selected, the ring on an item not
 * selected, the thumb; 3 items, the first selected with the ring); two
 * grids (14 items; the second scrolled a line, the thumb on hover); eight
 * scroll bars in their states; see scene_textfield and the three after it
 * (tests/desktop_ref.py builds the same items).
 *
 * 1.3 stage 5: the menus, the M button and the Restart dialog
 * (kernel/display/ui_menu.h, ui_dialog.h, desktop_core.h), each on bg.deep
 * with the taskbar (the clock 09:41) and its M button: mbutton_hover,
 * mbutton_pressed (the button alone); menu_mouse (the M menu as the mouse
 * opens it, no row selected, the M button open), menu_key (About selected
 * with the focus ring, as the Windows key opens it), menu_hover (Restart
 * selected by the pointer; 1.5 stage 6: the row Power...); menu_desktop
 * (the desktop menu for a hot spot 10 dp left of the screen's right edge
 * and above the taskbar, so moved left and up, its second row selected);
 * the dialog (1.5 stage 6: the power dialog, Cancel, Restart, Shut down):
 * dialog (Cancel focused, no ring, its close button on hover), dialog_key
 * (the ring on Cancel, Shut down on hover; Restart until 1.5),
 * dialog_tab (Restart focused with the ring, Cancel on hover),
 * dialog_pressed_cancel (Cancel pressed, the ring on Restart),
 * dialog_pressed_restart (Restart pressed, the ring on Cancel),
 * dialog_pressed_shutdown (1.5 stage 6: Shut down pressed, the ring on
 * Cancel).
 * menu_tiles and dialog_tiles are menu_key and dialog_tab painted in tiles
 * of 97 x 61 pixels from (-7, -13) (as the compositor paints them, clipped
 * to each damaged rectangle); they record no colour pair (a mark's colour
 * under it is read before the tile that holds it is drawn).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/display/desktop_core.h"
#include "kernel/display/fbcon_core.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_about.h"
#include "kernel/display/ui_console.h"
#include "kernel/display/ui_dialog.h"
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_files.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_list.h"
#include "kernel/display/ui_menu.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_textfield.h"
#include "kernel/display/ui_trace.h"
#include "kernel/display/ui_widget.h"
#include "kernel/display/ui_window.h"

/* ---- The colour pairs drawn (ui_trace.h) ---- */

struct pair {
    int kind;
    uint32_t fg, bg;
    unsigned long n;
};
static struct pair pairs[512];
static int n_pairs, pairs_lost;
static bool trace_off;                  /* 1.3 stage 5: the tiled scenes record nothing */

void ui_trace(int kind, uint32_t fg, uint32_t bg)
{
    if (trace_off)
        return;
    for (int i = 0; i < n_pairs; i++)
        if (pairs[i].kind == kind && pairs[i].fg == fg && pairs[i].bg == bg) {
            pairs[i].n++;
            return;
        }
    if (n_pairs == (int)(sizeof(pairs) / sizeof(pairs[0]))) {
        pairs_lost++;
        return;
    }
    pairs[n_pairs++] = (struct pair){ kind, fg, bg, 1 };
}

static int write_pairs(const char *scene)
{
    const char *path = getenv("DESKTOP_PAIRS");
    static const char *const kinds[5] = { "text", "icon", "state", "carried-bar", "carried-pointer" };
    if (!path)
        return 0;
    FILE *f = fopen(path, "a");
    if (!f)
        return 1;
    for (int i = 0; i < n_pairs; i++)
        fprintf(f, "PAIR %s %06X %06X %lu %s\n", kinds[pairs[i].kind], pairs[i].fg, pairs[i].bg, pairs[i].n, scene);
    if (pairs_lost)
        fprintf(f, "PAIRS LOST %d %s\n", pairs_lost, scene);
    return fclose(f) != 0 || pairs_lost;
}

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

/* ---- 1.3 stage 4a: the windows ---- */

static const char dump_loader[] = "Limine 12.9.3 (a boot loader name much longer than the column)";
/* 1.4 stage 12: a scene named <base>_disk or <base>_diskro draws <base>
 * with Disk mode's About Mode row (and Restart line, ui_dialog_set_line2). */
static const char *dump_mode;
static uint16_t about_scratch[2][UI_ABOUT_SCRATCH];

static int scene_windows(struct surface *s, const struct fbcon_layout *cl, enum ui_button_state close,
                         bool exposed)
{
    struct desktop_layout l;
    struct ui_fonts f;
    if (!desktop_layout(&l, s->width, s->height, cl->scale, cl->font) || !ui_fonts_for_scale(cl->scale, &f))
        return 1;
    uint32_t sc = cl->scale;
    static const char display[] = "2560x1440x32";      /* a fixed value (desktop_ref.py's DUMP_DISPLAY) */
    struct ui_about about[2];
    struct ui_window win[2];
    struct ui_wm wm;
    ui_wm_init(&wm, (int32_t)s->width, (int32_t)s->height, l.bar.y, sc, 0);
    for (int i = 0; i < 2; i++) {
        int32_t w, h;
        ui_about_setup(&about[i], sc, &f, "1.3", dump_loader, display, "127 MB", about_scratch[i], UI_ABOUT_SCRATCH);
        if (dump_mode)
            ui_about_set_mode(&about[i], dump_mode);
        ui_about_size(sc, &w, &h);
        win[i] = (struct ui_window){ .name = "About", .title = UI_ABOUT_TITLE, .icon = UI_ICON_ABOUT, .w = w, .h = h,
                                     .esc_closes = true, .paint = ui_about_paint, .ctx = &about[i] };
        if (!ui_wm_open(&wm, &win[i]))
            return 1;
    }
    enum ui_button_state st[2] = { UI_BUTTON_NORMAL, close };
    int rc = 0;
    if (exposed) {
        for (int32_t y = -13; y < (int32_t)s->height; y += 61)
            for (int32_t x = -7; x < (int32_t)s->width; x += 97)
                for (int i = 0; i < 2; i++) {
                    struct ui_canvas c;
                    ui_canvas_init(&c, s);
                    ui_canvas_clip(&c, ui_rect_intersect((struct rect){ x, y, 97, 61 }, win[i].r));
                    if (!ui_rect_empty(c.clip))
                        ui_window_draw(&c, &win[i], win[i].r, i == 1, st[i], sc, &f);
                }
    } else {
        for (int i = 0; i < 2; i++) {
            uint8_t *px = calloc((size_t)win[i].w * win[i].h, s->bytes_per_pixel);
            struct surface ws;
            struct ui_canvas c;
            if (!px || !surface_init(&ws, px, (uint32_t)win[i].w, (uint32_t)win[i].h,
                                     (uint32_t)win[i].w * s->bytes_per_pixel, s->bytes_per_pixel))
                return 1;
            ui_canvas_init(&c, &ws);
            ui_window_draw(&c, &win[i], (struct rect){ 0, 0, win[i].w, win[i].h }, i == 1, st[i], sc, &f);
            ui_canvas_init(&c, s);
            ui_blit_round(&c, win[i].r, &ws, ui_window_radius(sc), UI_CORNERS_ALL);
            free(px);
        }
    }
    for (int i = 0; i < 2; i++)
        rc |= about[i].short_scratch;
    printf("SCENE windows: scale %u %%, %s, close %s; the windows at (%d, %d) and (%d, %d), %dx%d; the boot "
           "loader's value '%s'\n", sc * 25, exposed ? "painted in tiles" : "composed from surfaces",
           close == UI_BUTTON_HOVER ? "hover" : close == UI_BUTTON_PRESSED ? "pressed" : "normal", win[0].r.x,
           win[0].r.y, win[1].r.x, win[1].r.y, win[0].w, win[0].h, about[0].values[1]);
    return rc;
}

/* ---- 1.3 stage 4b: the Console window ---- */

static uint16_t dump_cells[UI_CONSOLE_COLS * UI_CONSOLE_ROWS];
static const uint8_t dump_palette[16][3] = {      /* the VGA's 16 default colours, 6-bit */
    { 0, 0, 0 }, { 0, 0, 42 }, { 0, 42, 0 }, { 0, 42, 42 }, { 42, 0, 0 }, { 42, 0, 42 }, { 42, 21, 0 },
    { 42, 42, 42 }, { 21, 21, 21 }, { 21, 21, 63 }, { 21, 63, 21 }, { 21, 63, 63 }, { 63, 21, 21 },
    { 63, 21, 63 }, { 63, 63, 21 }, { 63, 63, 63 },
};

static void dump_console_paint(const struct ui_canvas *c, struct rect r, void *ctx)
{
    const struct ui_fonts *f = ctx;
    struct ui_console_grid g = { dump_cells, dump_palette, true, 24, 0 };
    ui_console_paint(c, r, S, f->regular, &g);
}

/* kind 0: console (active), 1: console_inactive, 2: console_about. */
static int scene_console(struct surface *s, const struct fbcon_layout *cl, int kind, bool exposed)
{
    struct desktop_layout l;
    struct ui_fonts f;
    if (!desktop_layout(&l, s->width, s->height, cl->scale, cl->font) || !ui_fonts_for_scale(cl->scale, &f))
        return 1;
    uint32_t sc = cl->scale;
    for (uint32_t i = 0; i < UI_CONSOLE_COLS * UI_CONSOLE_ROWS; i++) {
        uint32_t row = i / UI_CONSOLE_COLS, col = i % UI_CONSOLE_COLS;
        dump_cells[i] = (uint16_t)(((i * 7 + row) & 0xFF) | ((row + col) % 16) << 8 | ((row / 5) % 8) << 12);
    }
    dump_cells[24 * 80] = 0x1FDB;
    dump_cells[24 * 80 + 79] = 0x1FDB;
    static const char display[] = "2560x1440x32";
    struct ui_about about = { 0 };
    struct ui_window win[2];
    struct ui_wm wm;
    int32_t w, h;
    ui_wm_init(&wm, (int32_t)s->width, (int32_t)s->height, l.bar.y, sc, 0);
    ui_console_size(sc, f.regular, &w, &h);
    win[0] = (struct ui_window){ .name = "Console", .title = "Console", .icon = UI_ICON_CONSOLE, .w = w, .h = h,
                                 .paint = dump_console_paint, .ctx = &f };
    if (!ui_wm_open(&wm, &win[0]))
        return 1;
    int n = kind == 2 ? 2 : 1;
    if (n == 2) {
        ui_about_setup(&about, sc, &f, "1.3", dump_loader, display, "127 MB", about_scratch[0], UI_ABOUT_SCRATCH);
        ui_about_size(sc, &w, &h);
        win[1] = (struct ui_window){ .name = "About", .title = UI_ABOUT_TITLE, .icon = UI_ICON_ABOUT, .w = w,
                                     .h = h, .esc_closes = true, .paint = ui_about_paint, .ctx = &about };
        if (!ui_wm_open(&wm, &win[1]))
            return 1;
    }
    bool active[2] = { kind == 0, true };
    if (exposed) {
        for (int32_t y = -13; y < (int32_t)s->height; y += 61)
            for (int32_t x = -7; x < (int32_t)s->width; x += 97)
                for (int i = 0; i < n; i++) {
                    struct ui_canvas c;
                    ui_canvas_init(&c, s);
                    ui_canvas_clip(&c, ui_rect_intersect((struct rect){ x, y, 97, 61 }, win[i].r));
                    if (!ui_rect_empty(c.clip))
                        ui_window_draw(&c, &win[i], win[i].r, active[i], UI_BUTTON_NORMAL, sc, &f);
                }
    } else {
        for (int i = 0; i < n; i++) {
            uint8_t *px = calloc((size_t)win[i].w * win[i].h, s->bytes_per_pixel);
            struct surface ws;
            struct ui_canvas c;
            if (!px || !surface_init(&ws, px, (uint32_t)win[i].w, (uint32_t)win[i].h,
                                     (uint32_t)win[i].w * s->bytes_per_pixel, s->bytes_per_pixel))
                return 1;
            ui_canvas_init(&c, &ws);
            ui_window_draw(&c, &win[i], (struct rect){ 0, 0, win[i].w, win[i].h }, active[i], UI_BUTTON_NORMAL, sc,
                           &f);
            ui_canvas_init(&c, s);
            ui_blit_round(&c, win[i].r, &ws, ui_window_radius(sc), UI_CORNERS_ALL);
            free(px);
        }
    }
    printf("SCENE console: scale %u %%, %s, %s; the Console window at (%d, %d), %dx%d", sc * 25,
           exposed ? "painted in tiles" : "composed from surfaces",
           kind == 0 ? "active" : kind == 1 ? "inactive" : "under an About window", win[0].r.x, win[0].r.y,
           win[0].w, win[0].h);
    if (n == 2)
        printf(", the About window at (%d, %d)", win[1].r.x, win[1].r.y);
    printf("\n");
    return n == 2 && about.short_scratch ? 1 : 0;
}

/* ---- 1.5 stage 8: the title-bar buttons and the taskbar's window buttons ---- */

/* variant 0 chrome: on bg.deep the Console window (minimise only) and an
 * About window opened over it (minimise and maximise), every button
 * normal; 1 chrome_hover: the About window's minimise button on hover, its
 * maximise button pressed, the Console window's minimise on hover; 2
 * chrome_max: an About window alone, maximised (the restore icon; its
 * content in its own size, centred across). Each window painted whole, in
 * one pass, as the traced 1.3 window scenes (the colour pairs are recorded
 * from a button's centre whatever the clip, so tiles would record pixels
 * not yet painted or already painted; the exposure path is proven by
 * windows_exposed and console_exposed). */
static int scene_chrome(struct surface *s, const struct fbcon_layout *cl, int variant)
{
    struct desktop_layout l;
    struct ui_fonts f;
    if (!desktop_layout(&l, s->width, s->height, cl->scale, cl->font) || !ui_fonts_for_scale(cl->scale, &f))
        return 1;
    uint32_t sc = cl->scale;
    for (uint32_t i = 0; i < UI_CONSOLE_COLS * UI_CONSOLE_ROWS; i++) {
        uint32_t row = i / UI_CONSOLE_COLS, col = i % UI_CONSOLE_COLS;
        dump_cells[i] = (uint16_t)(((i * 7 + row) & 0xFF) | ((row + col) % 16) << 8 | ((row / 5) % 8) << 12);
    }
    dump_cells[24 * 80] = 0x1FDB;
    dump_cells[24 * 80 + 79] = 0x1FDB;
    static const char display[] = "2560x1440x32";
    static struct ui_about about;
    struct ui_window win[2];
    struct ui_wm wm;
    int32_t w, h;
    int n = 0;
    ui_wm_init(&wm, (int32_t)s->width, (int32_t)s->height, l.bar.y, sc, 0);
    if (variant != 2) {
        ui_console_size(sc, f.regular, &w, &h);
        win[n] = (struct ui_window){ .name = "Console", .title = "Console", .icon = UI_ICON_CONSOLE, .w = w, .h = h,
                                     .buttons = UI_WIN_MINIMISE, .paint = dump_console_paint, .ctx = &f };
        if (!ui_wm_open(&wm, &win[n++]))
            return 1;
    }
    ui_about_setup(&about, sc, &f, "1.3", dump_loader, display, "127 MB", about_scratch[0], UI_ABOUT_SCRATCH);
    ui_about_size(sc, &w, &h);
    win[n] = (struct ui_window){ .name = "About", .title = UI_ABOUT_TITLE, .icon = UI_ICON_ABOUT, .w = w, .h = h,
                                 .esc_closes = true, .buttons = UI_WIN_MINIMISE | UI_WIN_MAXIMISE,
                                 .paint = ui_about_paint, .ctx = &about };
    if (!ui_wm_open(&wm, &win[n++]))
        return 1;
    struct ui_window *a = &win[n - 1];
    if (variant == 1) {
        a->min_st = UI_BUTTON_HOVER;
        a->max_st = UI_BUTTON_PRESSED;
        win[0].min_st = UI_BUTTON_HOVER;
    } else if (variant == 2) {
        ui_wm_toggle_maximise(&wm, a);
    }
    for (int i = 0; i < n; i++) {
        struct ui_canvas c;
        ui_canvas_init(&c, s);
        ui_canvas_clip(&c, win[i].r);
        ui_window_draw(&c, &win[i], win[i].r, &win[i] == a, UI_BUTTON_NORMAL, sc, &f);
    }
    printf("SCENE chrome: scale %u %%, %s; the About window at (%d, %d) %dx%d%s\n", sc * 25,
           variant == 0 ? "every button normal" : variant == 1 ? "minimise on hover, maximise pressed" : "maximised",
           a->r.x, a->r.y, a->r.w, a->r.h, variant != 2 ? ", over the Console window" : "");
    return about.short_scratch ? 1 : 0;
}

/* The taskbar (its clock "09:41") with six window buttons, in the order
 * they opened: About (focused: the fill and the bar), Console (hover),
 * a long title clipped (pressed), Power (minimised: normal), Restart (the
 * Alt+Tab ring), Close (normal); the six share the span. */
static int scene_taskbuttons(struct surface *s, const struct fbcon_layout *cl)
{
    struct desktop_layout l;
    struct ui_fonts f;
    if (!desktop_layout(&l, s->width, s->height, cl->scale, cl->font) || !ui_fonts_for_scale(cl->scale, &f))
        return 1;
    uint32_t sc = cl->scale;
    static const char *const titles[6] = { UI_ABOUT_TITLE, "Console", "A title much longer than its button",
                                           "Power", "Restart", "Close" };
    static const int icons[6] = { UI_ICON_ABOUT, UI_ICON_CONSOLE, UI_ICON_CALCULATOR, UI_ICON_POWER,
                                  UI_ICON_RESTART, UI_ICON_CLOSE };
    struct ui_window win[6];
    struct ui_wm wm;
    desktop_draw_taskbar(s, &l, (struct rect){ 0, 0, (int32_t)s->width, (int32_t)s->height }, "09:41");
    ui_wm_init(&wm, (int32_t)s->width, (int32_t)s->height, l.bar.y, sc, 0);
    ui_wm_set_taskbar(&wm, l.m_cell.x + l.m_cell.w + ui_px(4, sc), l.clock_box.x - ui_px(16, sc));
    for (int i = 0; i < 6; i++) {
        win[i] = (struct ui_window){ .name = titles[i], .title = titles[i], .icon = icons[i], .w = 100, .h = 100 };
        if (!ui_wm_open(&wm, &win[i]))
            return 1;
    }
    ui_wm_activate(&wm, &win[0]);
    win[1].task_st = UI_BUTTON_HOVER;
    win[2].task_st = UI_BUTTON_PRESSED;
    ui_wm_minimise(&wm, &win[3]);
    wm.switching = true;
    wm.target = &win[4];
    struct ui_canvas c;
    ui_canvas_init(&c, s);
    ui_wm_draw_taskbar(&c, &wm, &f);
    struct rect b0 = ui_wm_task_rect(&wm, 0);
    printf("SCENE taskbuttons: scale %u %%, six buttons %dx%d from (%d, %d), 4 dp apart\n", sc * 25, b0.w, b0.h,
           b0.x, b0.y);
    return 0;
}

/* ---- 1.3 stage 5: the menus, the M button and the Restart dialog ---- */

static uint16_t menu_scratch[UI_ABOUT_SCRATCH];
static struct ui_dialog dump_dialog;            /* its window paints it: not copied */

static int scene_menus(struct surface *s, const struct fbcon_layout *cl, const char *name)
{
    struct desktop_layout l;
    struct ui_fonts f;
    if (!desktop_layout(&l, s->width, s->height, cl->scale, cl->font) || !ui_fonts_for_scale(cl->scale, &f))
        return 1;
    uint32_t sc = cl->scale;
    int32_t W = (int32_t)s->width;
    bool tiled = strcmp(name, "menu_tiles") == 0 || strcmp(name, "dialog_tiles") == 0;
    enum ui_m_state m = UI_M_NORMAL;
    struct ui_menu menu;
    bool has_menu = false, has_dialog = false;
    ui_menu_init(&menu, UI_MENU_M, sc, (struct rect){ 0, 0, 0, 0 });
    if (strcmp(name, "mbutton_hover") == 0) {
        m = UI_M_HOVER;
    } else if (strcmp(name, "mbutton_pressed") == 0) {
        m = UI_M_PRESSED;
    } else if (strcmp(name, "menu_desktop") == 0) {
        ui_menu_init(&menu, UI_MENU_DESKTOP, sc,
                     ui_menu_place_at(sc, W, l.bar.y, W - ui_px(10, sc), l.bar.y - ui_px(10, sc)));
        menu.sel = 1;
        has_menu = true;
    } else if (strncmp(name, "menu_", 5) == 0) {
        m = UI_M_OPEN;
        ui_menu_init(&menu, UI_MENU_M, sc, ui_menu_place_m(sc, l.bar.y));
        if (strcmp(name, "menu_key") == 0 || strcmp(name, "menu_tiles") == 0) {
            menu.sel = 0;
            menu.ring = true;
        } else if (strcmp(name, "menu_hover") == 0) {
            menu.sel = 2;
        }
        has_menu = true;
    } else {
        struct ui_dialog *d = &dump_dialog;
        ui_dialog_init(d, sc, &f, W, l.bar.y, false);
        if (strcmp(name, "dialog") == 0) {
            d->w.close = UI_BUTTON_HOVER;
        } else if (strcmp(name, "dialog_key") == 0) {
            d->ring = true;
            d->btn[UI_DLG_SHUTDOWN] = UI_BUTTON_HOVER;  /* 1.5 stage 6: the accent button (was Restart) */
        } else if (strcmp(name, "dialog_tab") == 0 || strcmp(name, "dialog_tiles") == 0) {
            d->focus = UI_DLG_RESTART;
            d->ring = true;
            d->btn[UI_DLG_CANCEL] = UI_BUTTON_HOVER;
        } else if (strcmp(name, "dialog_pressed_cancel") == 0) {
            d->focus = UI_DLG_RESTART;
            d->ring = true;
            d->btn[UI_DLG_CANCEL] = UI_BUTTON_PRESSED;
        } else if (strcmp(name, "dialog_pressed_restart") == 0) {
            d->ring = true;
            d->btn[UI_DLG_RESTART] = UI_BUTTON_PRESSED;
        } else if (strcmp(name, "dialog_pressed_shutdown") == 0) {     /* 1.5 stage 6 */
            d->ring = true;
            d->btn[UI_DLG_SHUTDOWN] = UI_BUTTON_PRESSED;
        } else {
            return 2;
        }
        has_dialog = true;
    }
    bool whole = true;
    trace_off = tiled;
    for (int32_t y = tiled ? -13 : 0; y < (int32_t)s->height; y += tiled ? 61 : (int32_t)s->height)
        for (int32_t x = tiled ? -7 : 0; x < W; x += tiled ? 97 : W) {
            struct rect clip = tiled ? (struct rect){ x, y, 97, 61 } : (struct rect){ 0, 0, W, (int32_t)s->height };
            struct ui_canvas c;
            ui_canvas_init(&c, s);
            ui_canvas_clip(&c, clip);
            desktop_draw_taskbar_m(s, &l, clip, "09:41", m);
            if (has_menu)
                whole &= ui_menu_draw(&c, &menu, &f, menu_scratch, UI_ABOUT_SCRATCH);
            if (has_dialog)
                ui_dialog_draw(&c, &dump_dialog);
        }
    trace_off = false;
    printf("SCENE %s: scale %u %%, %s, the M button %d", name, sc * 25, tiled ? "painted in tiles" : "drawn whole",
           (int)m);
    if (has_menu)
        printf("; the menu at (%d, %d) %dx%d, row %d selected%s", menu.r.x, menu.r.y, menu.r.w, menu.r.h, menu.sel,
               menu.ring ? ", the ring on" : "");
    if (has_dialog)
        printf("; the dialog at (%d, %d) %dx%d", dump_dialog.w.r.x, dump_dialog.w.r.y, dump_dialog.w.r.w,
               dump_dialog.w.r.h);
    printf("\n");
    return whole ? 0 : 1;
}

/* ---- 1.5 stage 10: the text field, the list and grid views, the scroll bar ---- */

static const char *const dump_titles[4] = { "Name", "Size", "Type", "Modified" };
static const char dump_long[] = "A very long file name that cannot fit in the name column at all.txt";
static const char dump_odd[] = "caf\xe9 \x01menu.txt";
static char dump_names[20][32], dump_sizes[20][32], dump_mods[20][48];   /* room for any uint32_t */

/* desktop_ref.py's scene_items: n items, those in sel selected. */
static void dump_items(struct ui_list_item *it, uint32_t n, const uint32_t *sel, int nsel)
{
    static const int icons[4] = { UI_ICON_CONSOLE, UI_ICON_ABOUT, UI_ICON_CALCULATOR, UI_ICON_RESTART };
    for (uint32_t i = 0; i < n; i++) {
        if (i == 4) {
            it[i].name = dump_long;
        } else if (i == 5) {
            it[i].name = dump_odd;
        } else {
            if (i < 4)
                snprintf(dump_names[i], sizeof(dump_names[i]), "Folder %u", i + 1);
            else
                snprintf(dump_names[i], sizeof(dump_names[i]), "file-%02u.txt", i);
            it[i].name = dump_names[i];
        }
        if (i < 4) {
            it[i].size = 0;
        } else if (i == 7) {
            it[i].size = "123456789 KiB";
        } else {
            snprintf(dump_sizes[i], sizeof(dump_sizes[i]), "%u KiB", 3 * i + 1);
            it[i].size = dump_sizes[i];
        }
        it[i].type = i < 4 ? "Folder" : "Text file";
        snprintf(dump_mods[i], sizeof(dump_mods[i]), "2026-11-%02u 14:%02u", i + 1, 3 * i);
        it[i].modified = dump_mods[i];
        it[i].icon = i == 2 ? UI_ICON_NONE : icons[i % 4];
        it[i].selected = false;
        for (int k = 0; k < nsel; k++)
            if (sel[k] == i)
                it[i].selected = true;
    }
}

static int scene_textfield(struct ui_canvas *c, int32_t W, int32_t H)
{
    static const struct {
        const char *text;
        int pos;                /* -1: the end */
        int32_t w_dp;
        bool focused;
    } cases[8] = {
        { "New Folder", -1, 240, false },
        { "New Folder", -1, 240, true },
        { "", -1, 240, true },
        { "report-2026.txt", 6, 240, true },
        { "A long name that does not fit its field at all, so it scrolls.txt", -1, 240, true },
        { "A long name that does not fit its field at all, so it scrolls.txt", 0, 240, true },
        { "caf\xe9 \x01name.txt", -1, 240, false },
        { "abcdef", 1, 40, true },
    };
    static struct ui_textfield t;
    struct ui_fonts f;
    struct flow fl;
    if (!ui_fonts_for_scale(S, &f))
        return 1;
    ui_fill(c, (struct rect){ d(16), d(16), W - 2 * d(16), H - 2 * d(16) }, UI_SURFACE_WINDOW);
    flow_init(&fl, W, d(32), d(16));
    fl.y = d(32);
    for (int i = 0; i < 8; i++) {
        struct rect r = flow_place(&fl, d(cases[i].w_dp), d(32));
        ui_textfield_set(&t, cases[i].text);
        if (cases[i].pos >= 0)
            t.pos = (uint16_t)cases[i].pos;
        ui_textfield_fit(&t, ui_textfield_cols(r, S, f.regular));
        ui_textfield_draw(c, &t, r, S, &f, cases[i].focused);
    }
    printf("SCENE textfield: scale %u %%, 8 fields, bottom %d\n", S * 25, flow_bottom(&fl));
    return 0;
}

static int scene_list(struct ui_canvas *c)
{
    static struct ui_list_item a_items[20], b_items[3];
    static const uint32_t a_sel[2] = { 4, 5 }, b_sel[1] = { 0 };
    struct ui_fonts f;
    struct ui_list a, b;
    if (!ui_fonts_for_scale(S, &f))
        return 1;
    dump_items(a_items, 20, a_sel, 2);
    ui_list_init(&a, UI_LIST_DETAILS, a_items, 20, dump_titles, (struct rect){ d(16), d(16), d(600), d(266) }, S);
    a.top = 3;
    a.cursor = 6;
    a.anchor = 4;
    a.ring = true;
    ui_list_draw(c, &a, &f);
    dump_items(b_items, 3, b_sel, 1);
    ui_list_init(&b, UI_LIST_DETAILS, b_items, 3, dump_titles,
                 (struct rect){ d(16), d(16) + d(266) + d(16), d(600), d(140) }, S);
    b.ring = true;
    ui_list_draw(c, &b, &f);
    printf("SCENE list: scale %u %%, page %u of %u lines; page %u of %u\n", S * 25, ui_list_page(&a),
           ui_list_lines(&a), ui_list_page(&b), ui_list_lines(&b));
    return 0;
}

static int scene_grid(struct ui_canvas *c)
{
    static struct ui_list_item a_items[14], b_items[14];
    static const uint32_t a_sel[2] = { 1, 2 }, b_sel[1] = { 8 };
    struct ui_fonts f;
    struct ui_list a, b;
    if (!ui_fonts_for_scale(S, &f))
        return 1;
    dump_items(a_items, 14, a_sel, 2);
    ui_list_init(&a, UI_LIST_GRID, a_items, 14, 0, (struct rect){ d(16), d(16), d(504), d(216) }, S);
    a.cursor = 7;
    a.ring = true;
    ui_list_draw(c, &a, &f);
    dump_items(b_items, 14, b_sel, 1);
    ui_list_init(&b, UI_LIST_GRID, b_items, 14, 0, (struct rect){ d(16), d(16) + d(216) + d(16), d(504), d(216) },
                 S);
    b.top = 1;
    b.cursor = 8;
    b.anchor = 8;
    b.ring = true;
    b.sb = UI_SB_HOVER;
    ui_list_draw(c, &b, &f);
    printf("SCENE grid: scale %u %%, %u columns, page %u of %u lines\n", S * 25, ui_list_columns(&a),
           ui_list_page(&a), ui_list_lines(&a));
    return 0;
}

static int scene_scrollbar(struct ui_canvas *c, int32_t W)
{
    static const struct {
        uint32_t lines, page, top;
        enum ui_sb_state st;
    } cases[8] = {
        { 100, 10, 0, UI_SB_NORMAL }, { 100, 10, 45, UI_SB_HOVER }, { 100, 10, 90, UI_SB_DRAG },
        { 1000, 10, 500, UI_SB_NORMAL }, { 20, 10, 10, UI_SB_NORMAL }, { 11, 10, 1, UI_SB_HOVER },
        { 10, 10, 0, UI_SB_NORMAL }, { 3, 10, 0, UI_SB_NORMAL },
    };
    struct flow fl;
    flow_init(&fl, W, d(16), d(16));
    for (int i = 0; i < 8; i++) {
        struct rect r = flow_place(&fl, d(24), d(160));
        ui_fill(c, r, UI_SURFACE_WINDOW);
        ui_scrollbar_draw(c, r, cases[i].lines, cases[i].page, cases[i].top, cases[i].st, S);
    }
    printf("SCENE scrollbar: scale %u %%, 8 scroll bars, bottom %d\n", S * 25, flow_bottom(&fl));
    return 0;
}

/* ---- 1.5 stage 12: the Files window's icons and the window ---- */

static const uint32_t fileicon_pairs[8][2] = {     /* foreground, background (tests/desktop_ref.py's) */
    { UI_TEXT, UI_SURFACE }, { UI_TEXT_MUTED, UI_SURFACE }, { UI_ACCENT, UI_SURFACE }, { UI_ACCENT, UI_SURFACE_ACTIVE },
    { UI_TEXT_ON_ACCENT, UI_ACCENT }, { UI_TEXT, UI_SURFACE_HOVER }, { UI_TEXT, UI_ACCENT_PRESSED },
    { UI_TEXT, UI_SURFACE_ACTIVE },
};

/* Every colour icon at 16 and 48 dp on surface.window, surface and
 * surface.active; the Files window's stroke icons at 16 and 48 dp in the
 * pairs above. */
static int scene_fileicons(struct ui_canvas *c, int32_t W)
{
    static const uint32_t bgs[3] = { UI_SURFACE_WINDOW, UI_SURFACE, UI_SURFACE_ACTIVE };
    struct flow f;
    flow_init(&f, W, 8, 4);
    uint32_t k = 0, j = 0;
    for (int b = 0; b < 3; b++)
        for (int z = 0; z < 2; z++) {
            int32_t p = d(z ? 48 : 16);
            for (int i = UI_CICON_FIRST; i < UI_CICON_END; i++, k++) {
                struct rect cell = flow_place(&f, p + 4, p + 4);
                ui_fill(c, cell, bgs[b]);
                ui_icon_draw(c, i, cell.x + 2, cell.y + 2, p, UI_TEXT);
            }
            flow_break(&f);
        }
    for (int z = 0; z < 2; z++) {
        int32_t p = d(z ? 48 : 16);
        for (int i = UI_SICON_FIRST; i < UI_SICON_END; i++, j++) {
            struct rect cell = flow_place(&f, p + 4, p + 4);
            ui_fill(c, cell, fileicon_pairs[j % 8][1]);
            ui_icon_draw(c, i, cell.x + 2, cell.y + 2, p, fileicon_pairs[j % 8][0]);
        }
        flow_break(&f);
    }
    printf("SCENE fileicons: scale %u %%, %u colour icons, %u stroke icons, bottom %d\n", S * 25, k, j,
           flow_bottom(&f));
    return 0;
}

static struct ui_files_view files_scene_view;
static struct ui_fonts files_scene_fonts;

static void files_scene_paint(const struct ui_canvas *c, struct rect r, void *ctx)
{
    (void)ctx;
    ui_files_draw(c, r, S, &files_scene_fonts, &files_scene_view);
}

/* The Files window at (16, 16) dp, 720 x 480 dp, active (desktop_ref.py's
 * scene_files: the same view). */
static int scene_files(struct ui_canvas *c, bool grid)
{
    static const struct {
        const char *name, *size, *type, *mod;
        int icon;
    } rows[7] = {
        { "Projects", 0, "Folder", "2026-11-20 09:15", UI_ICON_FOLDER },
        { "Work", 0, "Folder", "2026-11-19 17:02", UI_ICON_FOLDER },
        { ".hidden-notes", "0 B", "File", "2026-11-18 08:00", UI_ICON_FILE },
        { "Notes.txt", "1.2 KiB", "Text file", "2026-11-20 10:41", UI_ICON_TEXTFILE },
        { "Plan.md", "3.4 KiB", "Text file", "2026-11-20 11:05", UI_ICON_TEXTFILE },
        { "Project Link", "24 B", "Link", "2026-11-17 12:30", UI_ICON_LINK },
        { "data.bin", "12.0 MiB", "File", "2026-11-16 22:59", UI_ICON_FILE },
    };
    static struct ui_list_item items[7];
    static struct ui_list list;
    static struct ui_textfield field;
    static struct files_pathbar pb;
    static struct ui_window w;
    if (!ui_fonts_for_scale(S, &files_scene_fonts))
        return 1;
    struct rect r = { d(16), d(16), d(720), d(480) };
    struct ui_files_layout lay;
    ui_files_layout(r, S, files_scene_fonts.regular, true, &lay);
    for (int i = 0; i < 7; i++) {
        items[i].name = rows[i].name;
        items[i].size = rows[i].size;
        items[i].type = rows[i].type;
        items[i].modified = rows[i].mod;
        items[i].icon = rows[i].icon;
        items[i].selected = grid ? i == 3 : (i == 3 || i == 4);
    }
    ui_list_init(&list, grid ? UI_LIST_GRID : UI_LIST_DETAILS, items, 7, dump_titles, lay.content, S);
    list.cursor = grid ? 3 : 4;
    list.ring = grid;
    ui_textfield_set(&field, "Plan-2026.md");
    ui_textfield_fit(&field, ui_textfield_cols(ui_files_field_rect(&list, 4), S, files_scene_fonts.regular));
    files_pathbar(FILES_VIEW_FOLDER, "/home/user/Documents", 20, ui_files_path_cells(&lay, S, files_scene_fonts.regular),
                  &pb);
    struct ui_files_view *v = &files_scene_view;
    v->mode = UI_FILES_LIST;
    v->bar = UI_FILES_BAR_LIVE;
    for (int i = 0; i < UF_BUTTONS; i++) {
        v->enabled[i] = i != UF_FORWARD;
        v->chosen[i] = i == UF_LIST || i == UF_HIDDEN;      /* files_grid: List stale on purpose */
    }
    v->hover = grid ? UF_NONE : UF_NEWFILE;
    v->pressed = grid ? UF_NONE : UF_OPEN;
    v->tip = grid ? UF_NONE : UF_BACK;
    v->place = 2;
    v->pb = &pb;
    v->list = &list;
    v->renaming = !grid;
    v->field = &field;
    v->panel_icon = UI_ICON_NONE;
    v->status = "7 items, 300.0 MiB free";
    v->status_error = false;
    w.name = "Files";
    w.title = "Files";
    w.icon = UI_ICON_FOLDER;
    w.w = r.w;
    w.h = r.h;
    w.buttons = UI_WIN_MINIMISE | UI_WIN_MAXIMISE;
    w.resizable = true;
    w.grows = true;
    w.paint = files_scene_paint;
    ui_window_draw(c, &w, r, true, UI_BUTTON_NORMAL, S, &files_scene_fonts);
    printf("SCENE %s: scale %u %%, the window %dx%d, %u path cells\n", grid ? "files_grid" : "files", S * 25, r.w,
           r.h, ui_files_path_cells(&lay, S, files_scene_fonts.regular));
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
    /* 1.4 stage 12: <base>_disk and <base>_diskro draw <base> with Disk
     * mode's texts (a pairs file, if asked for, names <base>). */
    static char base[64];
    const char *scene = argv[4];
    size_t len = strlen(scene);
    if (len < sizeof(base)) {
        bool ro = len > 7 && strcmp(scene + len - 7, "_diskro") == 0;
        bool rw = !ro && len > 5 && strcmp(scene + len - 5, "_disk") == 0;
        if (ro || rw) {
            memcpy(base, scene, len - (ro ? 7 : 5));
            base[len - (ro ? 7 : 5)] = 0;
            dump_mode = ro ? UI_ABOUT_MODE_DISKRO : UI_ABOUT_MODE_DISK;
            ui_dialog_set_line2(ro ? UI_DIALOG_LINE2_DISKRO : UI_DIALOG_LINE2_DISK);
            argv[4] = base;
        }
    }
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
    } else if (strcmp(argv[4], "windows") == 0) {
        rc = scene_windows(&s, &cl, UI_BUTTON_NORMAL, false);
    } else if (strcmp(argv[4], "windows_hover") == 0) {
        rc = scene_windows(&s, &cl, UI_BUTTON_HOVER, false);
    } else if (strcmp(argv[4], "windows_pressed") == 0) {
        rc = scene_windows(&s, &cl, UI_BUTTON_PRESSED, false);
    } else if (strcmp(argv[4], "windows_exposed") == 0) {
        rc = scene_windows(&s, &cl, UI_BUTTON_NORMAL, true);
    } else if (strcmp(argv[4], "console") == 0) {                 /* 1.3 stage 4b */
        rc = scene_console(&s, &cl, 0, false);
    } else if (strcmp(argv[4], "console_inactive") == 0) {
        rc = scene_console(&s, &cl, 1, false);
    } else if (strcmp(argv[4], "console_about") == 0) {
        rc = scene_console(&s, &cl, 2, false);
    } else if (strcmp(argv[4], "console_exposed") == 0) {
        rc = scene_console(&s, &cl, 2, true);
    } else if (strcmp(argv[4], "chrome") == 0) {                  /* 1.5 stage 8 */
        rc = scene_chrome(&s, &cl, 0);
    } else if (strcmp(argv[4], "chrome_hover") == 0) {
        rc = scene_chrome(&s, &cl, 1);
    } else if (strcmp(argv[4], "chrome_max") == 0) {
        rc = scene_chrome(&s, &cl, 2);
    } else if (strcmp(argv[4], "taskbuttons") == 0) {
        rc = scene_taskbuttons(&s, &cl);
    } else if (strcmp(argv[4], "textfield") == 0) {               /* 1.5 stage 10 */
        rc = scene_textfield(&c, (int32_t)w, (int32_t)h);
    } else if (strcmp(argv[4], "list") == 0) {
        rc = scene_list(&c);
    } else if (strcmp(argv[4], "grid") == 0) {
        rc = scene_grid(&c);
    } else if (strcmp(argv[4], "scrollbar") == 0) {
        rc = scene_scrollbar(&c, (int32_t)w);
    } else if (strcmp(argv[4], "fileicons") == 0) {               /* 1.5 stage 12 */
        rc = scene_fileicons(&c, (int32_t)w);
    } else if (strcmp(argv[4], "files") == 0) {
        rc = scene_files(&c, false);
    } else if (strcmp(argv[4], "files_grid") == 0) {
        rc = scene_files(&c, true);
    } else if (strncmp(argv[4], "mbutton_", 8) == 0 || strncmp(argv[4], "menu_", 5) == 0 ||
               strncmp(argv[4], "dialog", 6) == 0) {          /* 1.3 stage 5 */
        rc = scene_menus(&s, &cl, argv[4]);
        if (rc == 2) {
            fprintf(stderr, "desktopdump: unknown scene %s\n", argv[4]);
            return 1;
        }
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
    return write_pairs(argv[4]) ? 1 : rc;
}
