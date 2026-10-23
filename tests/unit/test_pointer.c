/* Unit tests for kernel/display/pointer_core.c (the mouse pointer's pure
 * part, 1.2 stage 7): the sprite's rows, its sizes at the four scale steps
 * and its nearest-neighbour scaling, the clamp, the clipped area, drawing
 * (every pixel checked, the transparent ones untouched) and save-under (a
 * screen drawn on and restored is byte for byte the screen before, at 4
 * and 3 bytes per pixel, clipped at every edge). Every surface lives inside
 * a larger buffer filled with a guard byte, so a write outside it is
 * caught. The modules are included directly. */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/surface_core.c"
#include "kernel/display/pointer_core.c"

#define GUARD 0xA5
#define W     40
#define H     30
#define PAD   64

struct buf {
    uint8_t mem[PAD + H * W * 4 + PAD];
    struct surface s;
};

/* A screen with a different colour at every pixel, and guards around it. */
static void screen_init(struct buf *b, uint32_t bpp)
{
    memset(b->mem, GUARD, sizeof(b->mem));
    surface_init(&b->s, b->mem + PAD, W, H, W * bpp, bpp);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            surface_fill(&b->s, (struct rect){ (int32_t)x, (int32_t)y, 1, 1 },
                         (x * 6) << 16 | (y * 8) << 8 | ((x + y) & 0xFF));
}

static bool guards_ok(const struct buf *b)
{
    for (uint32_t i = 0; i < PAD; i++)
        if (b->mem[i] != GUARD || b->mem[sizeof(b->mem) - 1 - i] != GUARD)
            return false;
    return true;
}

static void test_shape(void)
{
    int ok = 1;
    for (uint32_t y = 0; y < POINTER_DP_H; y++) {
        if (strlen(pointer_shape[y]) != POINTER_DP_W)
            ok = 0;
        for (uint32_t x = 0; x < POINTER_DP_W && pointer_shape[y][x]; x++)
            if (!strchr(".oaw", pointer_shape[y][x]))
                ok = 0;
    }
    CHECK(ok);
    CHECK_EQ(pointer_shape[0][0], 'o');                 /* the hot spot is drawn */
    CHECK_EQ(POINTER_EDGE, 0x011239);                   /* bg.deep */
    CHECK_EQ(POINTER_FILL, 0xFAF9FE);                   /* text */
    CHECK_EQ(POINTER_SPINE, 0x2987F3);                  /* accent */
    CHECK_EQ(pointer_colour(POINTER_NONE), 0);
    CHECK_EQ(pointer_colour(POINTER_PX_EDGE), POINTER_EDGE);
    CHECK_EQ(pointer_colour(POINTER_PX_FILL), POINTER_FILL);
    CHECK_EQ(pointer_colour(POINTER_PX_SPINE), POINTER_SPINE);
}

static void test_sizes(void)
{
    static struct pointer_sprite s;
    static const uint32_t scale[4] = { 4, 5, 6, 8 }, w[4] = { 12, 15, 18, 24 }, h[4] = { 18, 23, 27, 36 };
    for (int i = 0; i < 4; i++) {
        CHECK(pointer_sprite_make(&s, scale[i]));
        CHECK_EQ(s.w, w[i]);
        CHECK_EQ(s.h, h[i]);
        int ok = 1;
        for (uint32_t y = 0; y < s.h; y++)
            for (uint32_t x = 0; x < s.w; x++)
                if (s.px[y * s.w + x] != shape_px(pointer_shape[y * 18 / s.h][x * 12 / s.w]))
                    ok = 0;
        CHECK(ok);
    }
    /* 100 %: the grid itself; 200 %: each dp a 2 x 2 block. */
    CHECK(pointer_sprite_make(&s, 8));
    CHECK_EQ(s.px[0], POINTER_PX_EDGE);                 /* pixels (0-1, 0-1): dp (0, 0), 'o' */
    CHECK_EQ(s.px[1], POINTER_PX_EDGE);
    CHECK_EQ(s.px[24 + 1], POINTER_PX_EDGE);
    CHECK_EQ(s.px[2], POINTER_NONE);                    /* dp (1, 0), '.' */
    CHECK_EQ(s.px[5 * 24 + 2], POINTER_PX_SPINE);       /* dp (1, 2), 'a' */
    CHECK_EQ(s.px[5 * 24 + 4], POINTER_PX_EDGE);        /* dp (2, 2), 'o' */
    CHECK_EQ(s.px[7 * 24 + 4], POINTER_PX_FILL);        /* dp (2, 3), 'w' */
    s.scale = 99;
    CHECK(!pointer_sprite_make(&s, 3));
    CHECK(!pointer_sprite_make(&s, 7));
    CHECK_EQ(s.scale, 99);
}

static void test_clamp_and_area(void)
{
    int32_t x = -5, y = -1;
    pointer_clamp(&x, &y, 1024, 768);
    CHECK(x == 0 && y == 0);
    x = 1024; y = 5000;
    pointer_clamp(&x, &y, 1024, 768);
    CHECK(x == 1023 && y == 767);
    x = 512; y = 384;
    pointer_clamp(&x, &y, 1024, 768);
    CHECK(x == 512 && y == 384);

    static struct pointer_sprite s;
    pointer_sprite_make(&s, 4);
    struct rect r = pointer_area(&s, 0, 0, W, H);
    CHECK(r.x == 0 && r.y == 0 && r.w == 12 && r.h == 18);
    r = pointer_area(&s, W - 5, H - 3, W, H);
    CHECK(r.x == W - 5 && r.y == H - 3 && r.w == 5 && r.h == 3);
    r = pointer_area(&s, W - 1, H - 1, W, H);
    CHECK(r.w == 1 && r.h == 1);
}

/* Draws the sprite on a copy by hand: the reference. */
static void reference(struct buf *ref, const struct pointer_sprite *s, int32_t x, int32_t y)
{
    for (uint32_t r = 0; r < s->h; r++)
        for (uint32_t c = 0; c < s->w; c++)
            if (s->px[r * s->w + c] != POINTER_NONE)
                surface_fill(&ref->s, (struct rect){ x + (int32_t)c, y + (int32_t)r, 1, 1 },
                             pointer_colour(s->px[r * s->w + c]));
}

static void test_draw_and_save_under(uint32_t bpp)
{
    static struct buf scr, orig, ref;
    static struct pointer_sprite s;
    static struct pointer_under u;
    static const int32_t at[][2] = { { 3, 2 }, { 0, 0 }, { W - 1, H - 1 }, { W - 7, 4 }, { 10, H - 10 }, { 20, 8 } };
    for (uint32_t sc = 4; sc <= 8; sc += 4) {
        pointer_sprite_make(&s, sc);
        screen_init(&scr, bpp);
        screen_init(&orig, bpp);
        u.r = (struct rect){ 0, 0, 0, 0 };
        for (uint32_t i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
            int32_t x = at[i][0], y = at[i][1];
            /* A move: the old place restored, the new one saved, drawn. */
            pointer_restore(&u, &scr.s);
            pointer_save(&u, &scr.s, pointer_area(&s, x, y, W, H));
            pointer_paint(&scr.s, &s, x, y);
            screen_init(&ref, bpp);
            reference(&ref, &s, x, y);
            CHECK(memcmp(scr.mem, ref.mem, sizeof(scr.mem)) == 0);
            CHECK(guards_ok(&scr));
        }
        pointer_restore(&u, &scr.s);
        CHECK(memcmp(scr.mem, orig.mem, sizeof(scr.mem)) == 0);    /* save-under exact */
        CHECK_EQ(u.r.w, 0);
        pointer_restore(&u, &scr.s);                                /* nothing saved: nothing done */
        CHECK(memcmp(scr.mem, orig.mem, sizeof(scr.mem)) == 0);
    }
}

static void test_draw_4(void) { test_draw_and_save_under(4); }
static void test_draw_3(void) { test_draw_and_save_under(3); }

int main(void)
{
    RUN(test_shape);
    RUN(test_sizes);
    RUN(test_clamp_and_area);
    RUN(test_draw_4);
    RUN(test_draw_3);
    return unit_report("pointer");
}
