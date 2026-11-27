/* Unit tests for the v1.5 pointer set (1.5 stage 14b; kernel/display/
 * pointer_core.c, docs/plans/1.5.md, notes (11), (12)(b), (14), (15)):
 *   - the arrow pixel-identical: pointer_sprite_kind's arrow is
 *     pointer_sprite_make's, and at every scale its pixels' FNV-1a hash is
 *     the one computed from kernel/display/pointer_core.c at f21524a (the
 *     last commit before this stage; T4: computed by hand from the 18 rows);
 *   - every kind's size and hot spot at every scale (the rules in
 *     pointer_core.h), every cell a known character, at most 44 x 44;
 *   - the busy pointer: frame f lights dot f + 1 alone, 8 frames a turn,
 *     one frame every 125 ms (pointer_busy_frame);
 *   - pointer_edges_kind for every edge combination and pointer_choose's
 *     order (a drag first, then move, resize, a menu, the grab band, Files,
 *     the arrow);
 *   - a kind drawn with its hot spot at the screen's corners (the sprite
 *     starting off the screen) touches nothing outside the screen, and its
 *     save-under puts the screen back byte for byte.
 * The modules are included directly. */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/surface_core.c"
#include "kernel/display/pointer_core.c"

#define GUARD 0xA5
#define W     40
#define H     30
#define PAD   64

static uint32_t ps_fnv(const struct pointer_sprite *s)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < s->w * s->h; i++)
        h = (h ^ s->px[i]) * 16777619u;
    return h;
}

static void test_arrow_identical(void)
{
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    static const uint32_t fnv[4] = { 0x94D3909Eu, 0x58780071u, 0x85E3F493u, 0x34C98B39u };   /* at f21524a */
    static const uint32_t ws[4] = { 12, 15, 18, 24 }, hs[4] = { 18, 23, 27, 36 };
    static struct pointer_sprite a, b;
    for (int i = 0; i < 4; i++) {
        CHECK(pointer_sprite_make(&a, scales[i]));
        CHECK(pointer_sprite_kind(&b, POINTER_ARROW, scales[i], 5));
        CHECK_EQ(a.w, ws[i]);
        CHECK_EQ(a.h, hs[i]);
        CHECK(a.w == b.w && a.h == b.h && memcmp(a.px, b.px, a.w * a.h) == 0);
        CHECK_EQ(ps_fnv(&a), fnv[i]);
        int32_t hx = 9, hy = 9;
        pointer_hot(POINTER_ARROW, scales[i], &hx, &hy);
        CHECK(hx == 0 && hy == 0);
    }
    CHECK(!pointer_sprite_kind(&b, POINTER_ARROW, 7, 0));
    CHECK(!pointer_sprite_kind(&b, POINTER_KINDS, 4, 0));
}

static void test_kinds(void)
{
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    static const char *const names[POINTER_KINDS] = { "arrow", "resize-ew", "resize-ns", "resize-nwse", "resize-nesw",
                                                      "move", "ibeam", "hand", "busy", "drag-move", "drag-copy",
                                                      "no-drop" };
    static const uint8_t dims[POINTER_KINDS][4] = {   /* w, h, hx, hy (pointer_core.h) */
        { 12, 18, 0, 0 }, { 16, 9, 8, 4 }, { 9, 16, 4, 8 }, { 13, 13, 6, 6 }, { 13, 13, 6, 6 }, { 17, 17, 8, 8 },
        { 7, 18, 3, 9 }, { 14, 18, 5, 0 }, { 22, 22, 0, 0 }, { 22, 22, 0, 0 }, { 22, 22, 0, 0 }, { 22, 22, 0, 0 },
    };
    static struct pointer_sprite s;
    for (int k = 0; k < (int)POINTER_KINDS; k++) {
        const struct pointer_def *d = &pointer_defs[k];
        CHECK(strcmp(d->name, names[k]) == 0);
        CHECK(d->w == dims[k][0] && d->h == dims[k][1] && d->hx == dims[k][2] && d->hy == dims[k][3]);
        for (uint32_t y = 0; y < d->h; y++) {
            CHECK_EQ(strlen(d->rows[y]), d->w);
            for (uint32_t x = 0; x < d->w; x++)
                CHECK(strchr(".owad12345678", d->rows[y][x]) != 0);
        }
        for (int i = 0; i < 4; i++) {
            uint32_t sc = scales[i];
            CHECK(pointer_sprite_kind(&s, (enum pointer_kind)k, sc, 0));
            CHECK_EQ(s.w, (d->w * sc + 2) / 4);
            CHECK_EQ(s.h, (d->h * sc + 2) / 4);
            CHECK(s.w <= POINTER_MAX_W && s.h <= POINTER_MAX_H);
            int32_t hx = -1, hy = -1;
            pointer_hot((enum pointer_kind)k, sc, &hx, &hy);
            CHECK_EQ(hx, (int32_t)((d->hx * sc + 2) / 4));
            CHECK_EQ(hy, (int32_t)((d->hy * sc + 2) / 4));
            CHECK(hx < (int32_t)s.w && hy < (int32_t)s.h);
        }
    }
    /* T4 by hand: the east-west pointer at 125 % is 20 x 11 pixels, hot
     * spot (10, 5); the hand at 200 % is 28 x 36, hot spot (10, 0). */
    int32_t hx, hy;
    CHECK(pointer_sprite_kind(&s, POINTER_RESIZE_EW, 5, 0) && s.w == 20 && s.h == 11);
    pointer_hot(POINTER_RESIZE_EW, 5, &hx, &hy);
    CHECK(hx == 10 && hy == 5);
    CHECK(pointer_sprite_kind(&s, POINTER_HAND, 8, 0) && s.w == 28 && s.h == 36);
    pointer_hot(POINTER_HAND, 8, &hx, &hy);
    CHECK(hx == 10 && hy == 0);
    CHECK_EQ(pointer_colour(POINTER_PX_DANGER), UI_DANGER);
    CHECK_EQ(pointer_colour(POINTER_PX_SPINE), UI_ACCENT);
}

static void test_busy(void)
{
    static struct pointer_sprite s;
    uint32_t seen[8] = { 0 };
    for (uint32_t f = 0; f < 16; f++) {
        CHECK(pointer_sprite_kind(&s, POINTER_BUSY, 4, f));
        /* at 100 % one pixel a cell: dot k + 1's cells are accent only in frame k */
        for (uint32_t y = 0; y < 22; y++)
            for (uint32_t x = 0; x < 22; x++) {
                char c = pointer_rows_busy[y][x];
                if (c >= '1' && c <= '8')
                    CHECK_EQ(s.px[y * 22 + x], (uint32_t)(c - '1') == f % 8 ? POINTER_PX_SPINE : POINTER_PX_FILL);
            }
        seen[f % 8] = ps_fnv(&s);
    }
    for (int a = 0; a < 8; a++)
        for (int b = a + 1; b < 8; b++)
            CHECK(seen[a] != seen[b]);          /* 8 different frames */
    /* one frame every 125 ms at 100 Hz (12.5 ticks), 8 a turn */
    CHECK_EQ(pointer_busy_frame(0), 0);
    CHECK_EQ(pointer_busy_frame(12), 0);
    CHECK_EQ(pointer_busy_frame(13), 1);
    CHECK_EQ(pointer_busy_frame(25), 2);
    CHECK_EQ(pointer_busy_frame(87), 6);
    CHECK_EQ(pointer_busy_frame(88), 7);
    CHECK_EQ(pointer_busy_frame(100), 0);
    CHECK_EQ(pointer_busy_frame(125), 2);
    CHECK_EQ(pointer_busy_frame(10000000000ull), 0);
}

static void test_choose(void)
{
    CHECK_EQ(pointer_edges_kind(0), POINTER_ARROW);
    CHECK_EQ(pointer_edges_kind(0x1), POINTER_RESIZE_EW);
    CHECK_EQ(pointer_edges_kind(0x2), POINTER_RESIZE_EW);
    CHECK_EQ(pointer_edges_kind(0x4), POINTER_RESIZE_NS);
    CHECK_EQ(pointer_edges_kind(0x8), POINTER_RESIZE_NS);
    CHECK_EQ(pointer_edges_kind(0x4 | 0x1), POINTER_RESIZE_NWSE);
    CHECK_EQ(pointer_edges_kind(0x8 | 0x2), POINTER_RESIZE_NWSE);
    CHECK_EQ(pointer_edges_kind(0x4 | 0x2), POINTER_RESIZE_NESW);
    CHECK_EQ(pointer_edges_kind(0x8 | 0x1), POINTER_RESIZE_NESW);
    struct pointer_wants w = { false, false, 0, false, 0, -1 };
    CHECK_EQ(pointer_choose(&w), POINTER_ARROW);
    w.files = POINTER_IBEAM;
    CHECK_EQ(pointer_choose(&w), POINTER_IBEAM);    /* 6: Files over its window */
    w.band = 0x2;
    CHECK_EQ(pointer_choose(&w), POINTER_RESIZE_EW);    /* 5: the grab band before Files */
    w.menu = true;
    CHECK_EQ(pointer_choose(&w), POINTER_ARROW);    /* 4: a menu before the band */
    w.resizing = 0x8 | 0x2;
    CHECK_EQ(pointer_choose(&w), POINTER_RESIZE_NWSE);  /* 3: a resize before a menu */
    w.moving = true;
    CHECK_EQ(pointer_choose(&w), POINTER_MOVE);     /* 2 */
    w.files = POINTER_NO_DROP;
    w.files_drag = true;
    CHECK_EQ(pointer_choose(&w), POINTER_NO_DROP);  /* 1: a drag first */
    w.files = 99;                                   /* a kind that does not exist: not taken */
    CHECK_EQ(pointer_choose(&w), POINTER_MOVE);
    struct pointer_wants busy = { false, false, 0, false, 0, POINTER_BUSY };
    CHECK_EQ(pointer_choose(&busy), POINTER_BUSY);
}

struct ps_buf {
    uint8_t mem[PAD + H * W * 4 + PAD];
    struct surface s;
};

static void ps_screen(struct ps_buf *b)
{
    memset(b->mem, GUARD, sizeof(b->mem));
    surface_init(&b->s, b->mem + PAD, W, H, W * 4, 4);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            surface_fill(&b->s, (struct rect){ (int32_t)x, (int32_t)y, 1, 1 }, (x * 6) << 16 | (y * 8) << 8 | 7);
}

static bool ps_guards(const struct ps_buf *b)
{
    for (uint32_t i = 0; i < PAD; i++)
        if (b->mem[i] != GUARD || b->mem[sizeof(b->mem) - 1 - i] != GUARD)
            return false;
    return true;
}

/* Each kind with its hot spot at each corner of a 40 x 30 screen: the
 * sprite's top left is the position less the hot spot (off the screen at
 * the top left for a centred hot spot); the pixels drawn are exactly the
 * sprite's inside the screen, and restoring puts every byte back. */
static void test_corners(void)
{
    static struct ps_buf scr, orig;
    static struct pointer_sprite sp;
    static struct pointer_under u;
    static const int32_t at[4][2] = { { 0, 0 }, { W - 1, 0 }, { 0, H - 1 }, { W - 1, H - 1 } };
    for (int k = 1; k < (int)POINTER_KINDS; k++) {
        CHECK(pointer_sprite_kind(&sp, (enum pointer_kind)k, 4, 2));
        int32_t hx, hy;
        pointer_hot((enum pointer_kind)k, 4, &hx, &hy);
        for (int c = 0; c < 4; c++) {
            ps_screen(&scr);
            ps_screen(&orig);
            int32_t x0 = at[c][0] - hx, y0 = at[c][1] - hy;
            pointer_save(&u, &scr.s, pointer_area(&sp, x0, y0, W, H));
            pointer_paint(&scr.s, &sp, x0, y0);
            CHECK(ps_guards(&scr));
            int wrong = 0;
            for (int32_t y = 0; y < H; y++)
                for (int32_t x = 0; x < W; x++) {
                    int32_t sx = x - x0, sy = y - y0;
                    uint8_t px = sx >= 0 && sy >= 0 && sx < (int32_t)sp.w && sy < (int32_t)sp.h ?
                                 sp.px[sy * sp.w + sx] : POINTER_NONE;
                    uint32_t want = px == POINTER_NONE ? surface_get(&orig.s, (uint32_t)x, (uint32_t)y)
                                                       : pointer_colour(px);
                    if (surface_get(&scr.s, (uint32_t)x, (uint32_t)y) != want)
                        wrong++;
                }
            CHECK_EQ(wrong, 0);
            pointer_restore(&u, &scr.s);
            CHECK(memcmp(scr.mem, orig.mem, sizeof(scr.mem)) == 0);
        }
    }
}

int main(void)
{
    RUN(test_arrow_identical);
    RUN(test_kinds);
    RUN(test_busy);
    RUN(test_choose);
    RUN(test_corners);
    return unit_report("pointer_shapes");
}
