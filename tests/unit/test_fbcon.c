/* Unit tests for kernel/display/fbcon_core.c (the framebuffer console's
 * pure part, 1.2 stage 6): the scale steps (decision 19) at their
 * boundaries, the layout (centred, stepping down when the grid does not
 * fit), the 6-bit colours, the cursor rows, and the cell drawing at 4 and
 * 3 bytes per pixel with fonts made up here (1 and 2 bytes per row): every
 * pixel checked, and nothing written outside the cell. The modules are
 * included directly. */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/surface_core.c"
#include "kernel/display/fbcon_core.c"

#define GUARD 0xA5

/* Made-up fonts: glyph c's row y is (c + y) * 37, masked to the width. */
static uint8_t bits_a[256 * 16], bits_b[256 * 20 * 2], bits_c[256 * 24 * 2], bits_d[256 * 32 * 2];
static const struct font fa = { 8, 16, 1, bits_a };
static const struct font fb = { 10, 20, 2, bits_b };
static const struct font fc = { 12, 24, 2, bits_c };
static const struct font fd = { 16, 32, 2, bits_d };
static const struct font *const fonts[4] = { &fa, &fb, &fc, &fd };

static void make_font(const struct font *f, uint8_t *bits)
{
    for (uint32_t c = 0; c < 256; c++)
        for (uint32_t y = 0; y < f->height; y++) {
            uint32_t v = ((c + y) * 37u) & ((1u << f->width) - 1);
            v <<= f->row_bytes * 8 - f->width;           /* leftmost pixel in the top bit */
            for (uint32_t k = 0; k < f->row_bytes; k++)
                bits[(c * f->height + y) * f->row_bytes + k] = (uint8_t)(v >> (8 * (f->row_bytes - 1 - k)));
        }
}

/* Is pixel x of glyph c's row y set, read back from the made-up rule. */
static int glyph_bit(const struct font *f, uint32_t c, uint32_t y, uint32_t x)
{
    uint32_t v = ((c + y) * 37u) & ((1u << f->width) - 1);
    return (v >> (f->width - 1 - x)) & 1;
}

static void test_scale_steps(void)
{
    CHECK_EQ(fbcon_scale_for_height(600), 4);
    CHECK_EQ(fbcon_scale_for_height(768), 4);
    CHECK_EQ(fbcon_scale_for_height(899), 4);
    CHECK_EQ(fbcon_scale_for_height(900), 5);
    CHECK_EQ(fbcon_scale_for_height(1079), 5);
    CHECK_EQ(fbcon_scale_for_height(1080), 6);
    CHECK_EQ(fbcon_scale_for_height(1439), 6);
    CHECK_EQ(fbcon_scale_for_height(1440), 8);
    CHECK_EQ(fbcon_scale_for_height(4096), 8);
}

static void test_layout(void)
{
    struct fbcon_layout l;
    CHECK(fbcon_layout(1024, 768, fonts, &l));
    CHECK(l.font == &fa);
    CHECK_EQ(l.scale, 4);
    CHECK_EQ(l.x, 192);                 /* (1024 - 640) / 2 */
    CHECK_EQ(l.y, 184);                 /* (768 - 400) / 2 */
    CHECK(fbcon_layout(800, 600, fonts, &l));
    CHECK_EQ(l.x, 80);
    CHECK_EQ(l.y, 100);
    CHECK(fbcon_layout(1280, 1024, fonts, &l));
    CHECK(l.font == &fb);
    CHECK_EQ(l.scale, 5);
    CHECK_EQ(l.x, 240);                 /* (1280 - 800) / 2 */
    CHECK_EQ(l.y, 262);                 /* (1024 - 500) / 2 */
    CHECK(fbcon_layout(1920, 1080, fonts, &l));
    CHECK(l.font == &fc);
    CHECK_EQ(l.scale, 6);
    CHECK_EQ(l.x, 480);                 /* (1920 - 960) / 2 */
    CHECK_EQ(l.y, 240);                 /* (1080 - 600) / 2 */
    CHECK(fbcon_layout(2560, 1440, fonts, &l));
    CHECK(l.font == &fd);
    CHECK_EQ(l.x, 640);
    CHECK_EQ(l.y, 320);
    CHECK(fbcon_layout(1281, 1081, fonts, &l));     /* odd sizes round down */
    CHECK_EQ(l.x, 160);
    CHECK_EQ(l.y, 240);
    /* Too narrow for the scale's grid: steps down until it fits. */
    CHECK(fbcon_layout(900, 1440, fonts, &l));      /* 1280 and 960 too wide; 800 fits */
    CHECK(l.font == &fb);
    CHECK_EQ(l.scale, 5);
    CHECK_EQ(l.x, 50);
    CHECK(fbcon_layout(640, 400, fonts, &l));       /* exactly the scale 4 grid */
    CHECK_EQ(l.x, 0);
    CHECK_EQ(l.y, 0);
    struct fbcon_layout keep = l;
    CHECK(!fbcon_layout(639, 1080, fonts, &l));     /* nothing fits */
    CHECK(!fbcon_layout(1024, 399, fonts, &l));
    CHECK(memcmp(&keep, &l, sizeof(l)) == 0);       /* unchanged */
}

static void test_colours_and_cursor(void)
{
    const uint8_t black[3] = { 0, 0, 0 }, white[3] = { 0x3F, 0x3F, 0x3F };
    const uint8_t brown[3] = { 0x2A, 0x15, 0x00 }, odd[3] = { 0x01, 0x20, 0xFF };
    CHECK_EQ(fbcon_rgb6(black), 0x000000);
    CHECK_EQ(fbcon_rgb6(white), 0xFFFFFF);
    CHECK_EQ(fbcon_rgb6(brown), 0xAA5500);
    CHECK_EQ(fbcon_rgb6(odd), 0x0482FF);           /* 1 -> 0x04, 0x20 -> 0x82, 0xFF -> 0x3F -> 0xFF */
    uint32_t a, b;
    fbcon_cursor_rows(16, &a, &b);
    CHECK_EQ(a, 13);
    CHECK_EQ(b, 15);
    fbcon_cursor_rows(20, &a, &b);
    CHECK_EQ(a, 16);
    CHECK_EQ(b, 18);
    fbcon_cursor_rows(24, &a, &b);
    CHECK_EQ(a, 19);
    CHECK_EQ(b, 22);
    fbcon_cursor_rows(32, &a, &b);
    CHECK_EQ(a, 26);
    CHECK_EQ(b, 30);
}

/* A screen just big enough for the grid plus a margin, every byte the
 * guard byte; draws cells and checks each pixel and the guard. */
static uint8_t screen[(640 + 2 * 16) * (400 + 2 * 8) * 4 + 64];

static void check_cells(const struct font *f, uint32_t bpp)
{
    struct surface s;
    uint32_t w = 80 * f->width + 7, h = 25 * f->height + 5;
    static uint8_t big[(80 * 16 + 7) * (25 * 32 + 5) * 4];
    uint8_t *mem = f->width == 8 ? screen : big;
    size_t size = f->width == 8 ? sizeof(screen) : sizeof(big);
    memset(mem, GUARD, size);
    CHECK(surface_init(&s, mem, w, h, w * bpp, bpp));
    struct fbcon_layout l = { f, 4, 3, 2, f->width, f->height };
    const uint32_t fg = 0x123456, bg = 0xABCDEF;
    struct { uint32_t row, col; uint8_t ch; bool cursor; } cells[] = {
        { 0, 0, 0x41, false }, { 24, 79, 0xDF, true }, { 12, 40, 0xC9, false }, { 3, 7, 0x00, true },
    };
    for (uint32_t k = 0; k < 4; k++)
        fbcon_draw_cell(&s, &l, cells[k].row, cells[k].col, cells[k].ch, fg, bg, cells[k].cursor);
    fbcon_draw_cell(&s, &l, 25, 0, 'X', fg, bg, false);        /* outside the grid */
    fbcon_draw_cell(&s, &l, 0, 80, 'X', fg, bg, false);
    uint32_t c_first, c_end, bad = 0, drawn = 0;
    fbcon_cursor_rows(f->height, &c_first, &c_end);
    for (uint32_t k = 0; k < 4; k++) {
        uint32_t x0 = 3 + cells[k].col * f->width, y0 = 2 + cells[k].row * f->height;
        for (uint32_t y = 0; y < f->height; y++)
            for (uint32_t x = 0; x < f->width; x++) {
                int on = glyph_bit(f, cells[k].ch, y, x) || (cells[k].cursor && y >= c_first && y < c_end);
                const uint8_t *p = mem + (y0 + y) * s.pitch + (x0 + x) * bpp;
                if (surface_get(&s, x0 + x, y0 + y) != (on ? fg : bg) || (bpp == 4 && p[3] != 0))
                    bad++;
                drawn++;
            }
    }
    CHECK_EQ(bad, 0);
    /* Every other byte is still the guard byte. */
    uint32_t touched = 0;
    for (size_t i = 0; i < size; i++)
        touched += mem[i] != GUARD;
    uint32_t changed = 0;
    for (uint32_t k = 0; k < 4; k++) {
        uint32_t x0 = 3 + cells[k].col * f->width, y0 = 2 + cells[k].row * f->height;
        for (uint32_t y = 0; y < f->height; y++)
            for (uint32_t x = 0; x < f->width; x++)
                for (uint32_t b = 0; b < bpp; b++)
                    changed += mem[(y0 + y) * s.pitch + (x0 + x) * bpp + b] != GUARD;
    }
    CHECK_EQ(touched, changed);
    CHECK_EQ(drawn, 4 * f->width * f->height);
}

static void test_cells(void)
{
    for (uint32_t i = 0; i < 4; i++) {
        check_cells(fonts[i], 4);
        check_cells(fonts[i], 3);
    }
}

static void test_cell_outside_surface(void)
{
    /* A layout whose last column would cross the surface's edge: that cell
     * is not drawn at all; one inside is. */
    struct surface s;
    memset(screen, GUARD, sizeof(screen));
    CHECK(surface_init(&s, screen, 100, 40, 400, 4));
    struct fbcon_layout l = { &fa, 4, 0, 0, 8, 16 };
    fbcon_draw_cell(&s, &l, 0, 12, 'A', 0xFFFFFF, 0, false);    /* x 96-103: crosses 100 */
    fbcon_draw_cell(&s, &l, 2, 0, 'A', 0xFFFFFF, 0, false);     /* y 32-47: crosses 40 */
    uint32_t touched = 0;
    for (size_t i = 0; i < sizeof(screen); i++)
        touched += screen[i] != GUARD;
    CHECK_EQ(touched, 0);
    fbcon_draw_cell(&s, &l, 0, 11, 'A', 0xFFFFFF, 0, false);    /* x 88-95: inside */
    touched = 0;
    for (size_t i = 0; i < sizeof(screen); i++)
        touched += screen[i] != GUARD;
    CHECK_EQ(touched, 8 * 16 * 4);
    struct rect r = fbcon_cell_rect(&l, 2, 11);
    CHECK_EQ(r.x, 88);
    CHECK_EQ(r.y, 32);
    CHECK_EQ(r.w, 8);
    CHECK_EQ(r.h, 16);
}

/* 1.3 stage 4b: the grid at a given scale and place (the Console window's),
 * not centred and whatever the surface's size; any other scale refused. */
static void test_layout_at(void)
{
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    for (uint32_t i = 0; i < 4; i++) {
        struct fbcon_layout l;
        CHECK(fbcon_layout_at(scales[i], fonts, 2 + i, 32 + 8 * i, &l));
        CHECK(l.font == fonts[i] && l.scale == scales[i]);
        CHECK_EQ(l.cell_w, fonts[i]->width);
        CHECK_EQ(l.cell_h, fonts[i]->height);
        CHECK_EQ(l.x, 2 + i);
        CHECK_EQ(l.y, 32 + 8 * i);
        struct rect r = fbcon_cell_rect(&l, 24, 79);
        CHECK(r.x == (int32_t)(2 + i + 79 * fonts[i]->width) && r.y == (int32_t)(32 + 8 * i + 24 * fonts[i]->height));
    }
    static const uint32_t bad[5] = { 0, 3, 7, 9, 16 };
    for (uint32_t i = 0; i < 5; i++) {
        struct fbcon_layout l = { &fa, 99, 1, 2, 3, 4 };
        CHECK(!fbcon_layout_at(bad[i], fonts, 0, 0, &l));
        CHECK(l.scale == 99 && l.x == 1 && l.cell_h == 4);  /* unchanged */
    }
}

/* 1.3 stage 4b: a cell placed at any (x, y) and clipped: inside the clip
 * and the surface the same pixels as fbcon_draw_cell's, nothing else. */
static void test_cell_at_clipped(void)
{
    static uint8_t want[100 * 40 * 4];
    for (uint32_t bpp = 3; bpp <= 4; bpp++) {
        struct surface s, w;
        memset(want, GUARD, sizeof(want));
        CHECK(surface_init(&w, want, 100, 40, 100 * bpp, bpp));
        struct fbcon_layout l = { &fb, 5, 30, 10, 10, 20 };
        fbcon_draw_cell(&w, &l, 0, 0, 'Q', 0xC0FFEE, 0x123456, true);     /* the cell at (30, 10) */
        /* Whole, clipped to the surface only: fbcon_draw_cell's. */
        memset(screen, GUARD, sizeof(screen));
        CHECK(surface_init(&s, screen, 100, 40, 100 * bpp, bpp));
        fbcon_draw_cell_at(&s, &fb, 30, 10, 'Q', 0xC0FFEE, 0x123456, true, (struct rect){ -50, -50, 500, 500 });
        CHECK(memcmp(screen, want, 100 * 40 * bpp) == 0);
        /* Clipped to a rectangle: only its pixels. */
        memset(screen, GUARD, sizeof(screen));
        struct rect k = { 33, 14, 5, 9 };
        fbcon_draw_cell_at(&s, &fb, 30, 10, 'Q', 0xC0FFEE, 0x123456, true, k);
        uint32_t bad = 0, in = 0;
        for (uint32_t y = 0; y < 40; y++)
            for (uint32_t x = 0; x < 100; x++)
                for (uint32_t b = 0; b < bpp; b++) {
                    size_t o = y * 100 * bpp + x * bpp + b;
                    bool inside = (int32_t)x >= k.x && (int32_t)x < k.x + k.w && (int32_t)y >= k.y &&
                                  (int32_t)y < k.y + k.h;
                    bad += screen[o] != (inside ? want[o] : GUARD);
                    in += inside;
                }
        CHECK_EQ(bad, 0);
        CHECK_EQ(in, 5 * 9 * bpp);
        /* Partly off the surface (negative and past the edge): no write outside. */
        memset(screen, GUARD, sizeof(screen));
        fbcon_draw_cell_at(&s, &fb, -4, -7, 'Q', 0xC0FFEE, 0x123456, false, (struct rect){ -50, -50, 500, 500 });
        fbcon_draw_cell_at(&s, &fb, 95, 30, 'Q', 0xC0FFEE, 0x123456, false, (struct rect){ -50, -50, 500, 500 });
        uint32_t touched = 0, past = 0;
        for (size_t i = 0; i < sizeof(screen); i++) {
            touched += screen[i] != GUARD;
            past += i >= 100 * 40 * bpp && screen[i] != GUARD;
        }
        CHECK_EQ(past, 0);
        CHECK_EQ(touched, ((10 - 4) * (20 - 7) + (100 - 95) * (40 - 30)) * bpp);
    }
}

int main(void)
{
    make_font(&fa, bits_a);
    make_font(&fb, bits_b);
    make_font(&fc, bits_c);
    make_font(&fd, bits_d);
    printf("fbcon unit tests\n");
    RUN(test_scale_steps);
    RUN(test_layout);
    RUN(test_colours_and_cursor);
    RUN(test_cells);
    RUN(test_cell_outside_surface);
    RUN(test_layout_at);
    RUN(test_cell_at_clipped);
    return unit_report("fbcon");
}
