/* Unit tests for kernel/display/surface_core.c (drawing surfaces, 1.2
 * stage 5b): fill, blit and clipping at 4 and 3 bytes per pixel, the
 * conversions between them, and the CRC-32 of the RGB bytes. Every
 * surface lives inside a larger buffer filled with a guard byte, so a write
 * outside it is caught. The module is included directly. */
#include <stdint.h>
#include <string.h>
#include "tests/unit/unit.h"
#include "kernel/display/surface_core.c"

#define GUARD 0xA5
#define MAXW  40
#define MAXH  30
#define PAD   64                /* guard bytes before and after */

struct buf {
    uint8_t mem[PAD + MAXH * (MAXW * 4 + 8) + PAD];
    struct surface s;
};

static struct buf a, b;

/* A surface of w x h at `bpp` bytes per pixel, a pitch `extra` bytes over
 * the minimum, every byte (pixels and gaps) set to the guard byte. */
static struct surface *mk(struct buf *x, uint32_t w, uint32_t h, uint32_t bpp, uint32_t extra)
{
    memset(x->mem, GUARD, sizeof(x->mem));
    CHECK(surface_init(&x->s, x->mem + PAD, w, h, w * bpp + extra, bpp));
    return &x->s;
}

/* True if every byte outside the pixels of rows [0, h) is still the guard
 * byte (the gaps at the row ends included). */
static int guards_intact(const struct buf *x)
{
    const struct surface *s = &x->s;
    for (size_t i = 0; i < sizeof(x->mem); i++) {
        int64_t off = (int64_t)i - PAD;
        int inside = 0;
        if (off >= 0 && off < (int64_t)s->pitch * s->height) {
            uint32_t col = (uint32_t)(off % s->pitch);
            inside = col < s->width * s->bytes_per_pixel;
        }
        if (!inside && x->mem[i] != GUARD) {
            printf("    guard byte %zu changed to 0x%02x\n", i, x->mem[i]);
            return 0;
        }
    }
    return 1;
}

/* Counts the pixels equal to rgb inside rectangle r (already inside s). */
static uint32_t count_in(const struct surface *s, int32_t x0, int32_t y0, int32_t w, int32_t h, uint32_t rgb)
{
    uint32_t n = 0;
    for (int32_t y = y0; y < y0 + h; y++)
        for (int32_t x = x0; x < x0 + w; x++)
            n += surface_get(s, (uint32_t)x, (uint32_t)y) == rgb;
    return n;
}

static void test_init(void)
{
    struct surface s;
    memset(&s, 0, sizeof(s));
    CHECK(!surface_init(&s, a.mem, 10, 10, 40, 2));
    CHECK(!surface_init(&s, a.mem, 10, 10, 39, 4));
    CHECK(!surface_init(&s, a.mem, 10, 10, 29, 3));
    CHECK(!surface_init(&s, a.mem, 0, 10, 40, 4));
    CHECK(!surface_init(&s, a.mem, 10, 0, 40, 4));
    CHECK(!surface_init(&s, NULL, 10, 10, 40, 4));
    CHECK(s.pixels == NULL);                /* unchanged on refusal */
    CHECK(surface_init(&s, a.mem, 10, 10, 30, 3));
    CHECK(surface_init(&s, a.mem, 10, 10, 44, 4));
    CHECK_EQ(s.pitch, 44);
    CHECK_EQ(s.bytes_per_pixel, 4);
}

static void test_rect_clip(void)
{
    struct rect r = { -5, -3, 10, 10 };
    CHECK(rect_clip(&r, 20, 20));
    CHECK(r.x == 0 && r.y == 0 && r.w == 5 && r.h == 7);
    r = (struct rect){ 15, 18, 10, 10 };
    CHECK(rect_clip(&r, 20, 20));
    CHECK(r.x == 15 && r.y == 18 && r.w == 5 && r.h == 2);
    r = (struct rect){ 20, 0, 5, 5 };       /* just past the right edge */
    CHECK(!rect_clip(&r, 20, 20));
    CHECK(r.x == 0 && r.y == 0 && r.w == 0 && r.h == 0);
    r = (struct rect){ -5, 0, 5, 5 };       /* just before the left edge */
    CHECK(!rect_clip(&r, 20, 20));
    r = (struct rect){ 2, 2, 0, 5 };
    CHECK(!rect_clip(&r, 20, 20));
    r = (struct rect){ 2, 2, -4, 5 };
    CHECK(!rect_clip(&r, 20, 20));
    /* Extremes: no overflow. */
    r = (struct rect){ INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX };
    CHECK(!rect_clip(&r, 20, 20));
    r = (struct rect){ INT32_MAX, 0, INT32_MAX, 5 };
    CHECK(!rect_clip(&r, 20, 20));
    r = (struct rect){ -10, -10, INT32_MAX, INT32_MAX };
    CHECK(rect_clip(&r, 20, 20));
    CHECK(r.x == 0 && r.y == 0 && r.w == 20 && r.h == 20);
}

/* The byte layout: 0xRRGGBB stored blue, green, red (and 0 at 4 bytes). */
static void test_pixel_bytes(void)
{
    struct surface *s = mk(&a, 3, 2, 4, 0);
    surface_fill(s, (struct rect){ 1, 0, 1, 1 }, 0x123456);
    CHECK(memcmp(s->pixels + 4, "\x56\x34\x12\x00", 4) == 0);
    CHECK_EQ(surface_get(s, 1, 0), 0x123456);
    surface_fill(s, (struct rect){ 0, 0, 1, 1 }, 0xFF123456u);     /* the top byte is dropped */
    CHECK(memcmp(s->pixels, "\x56\x34\x12\x00", 4) == 0);
    s = mk(&b, 3, 2, 3, 0);
    surface_fill(s, (struct rect){ 2, 1, 1, 1 }, 0xABCDEF);
    CHECK(memcmp(s->pixels + 9 + 6, "\xEF\xCD\xAB", 3) == 0);
    CHECK_EQ(surface_get(s, 2, 1), 0xABCDEF);
    CHECK(guards_intact(&b));
}

static void test_fill(void)
{
    for (uint32_t bpp = 3; bpp <= 4; bpp++) {
        for (uint32_t extra = 0; extra <= 5; extra += 5) {
            struct surface *s = mk(&a, 20, 15, bpp, extra);
            surface_fill(s, (struct rect){ 0, 0, 20, 15 }, 0x000000);
            CHECK_EQ(count_in(s, 0, 0, 20, 15, 0), 300);
            surface_fill(s, (struct rect){ 3, 4, 5, 6 }, 0x102030);
            CHECK_EQ(count_in(s, 3, 4, 5, 6, 0x102030), 30);
            CHECK_EQ(count_in(s, 0, 0, 20, 15, 0x102030), 30);
            /* Clipped at every edge, and wholly outside. */
            surface_fill(s, (struct rect){ -4, -2, 6, 5 }, 0x111111);
            CHECK_EQ(count_in(s, 0, 0, 20, 15, 0x111111), 2 * 3);
            surface_fill(s, (struct rect){ 17, 12, 10, 10 }, 0x222222);
            CHECK_EQ(count_in(s, 0, 0, 20, 15, 0x222222), 3 * 3);
            surface_fill(s, (struct rect){ -100, 5, 1000, 1 }, 0x333333);
            CHECK_EQ(count_in(s, 0, 5, 20, 1, 0x333333), 20);
            surface_fill(s, (struct rect){ 20, 0, 5, 5 }, 0x444444);
            surface_fill(s, (struct rect){ 0, -9, 5, 9 }, 0x444444);
            surface_fill(s, (struct rect){ INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX }, 0x444444);
            CHECK_EQ(count_in(s, 0, 0, 20, 15, 0x444444), 0);
            CHECK(guards_intact(&a));
        }
    }
    /* A 4-byte surface whose rows are not 4-byte aligned (pitch 81): the
     * byte path, the same result. */
    struct surface *s = mk(&a, 20, 3, 4, 1);
    surface_fill(s, (struct rect){ 0, 0, 20, 3 }, 0x0A0B0C);
    CHECK_EQ(count_in(s, 0, 0, 20, 3, 0x0A0B0C), 60);
    CHECK(guards_intact(&a));
}

/* src: a gradient where every pixel has its own colour. */
static void gradient(struct surface *s)
{
    for (uint32_t y = 0; y < s->height; y++)
        for (uint32_t x = 0; x < s->width; x++)
            surface_fill(s, (struct rect){ (int32_t)x, (int32_t)y, 1, 1 }, (x * 5 + 1) << 16 | (y * 7 + 2) << 8 | (x ^ y));
}

static uint32_t grad_at(uint32_t x, uint32_t y)
{
    return (x * 5 + 1) << 16 | (y * 7 + 2) << 8 | (x ^ y);
}

/* dst pixel (x, y) must be src (x - dx + sx, y - dy + sy) where that lies
 * in the copied, clipped rectangle, and the background elsewhere. */
static int blit_matches(const struct surface *d, int32_t dx, int32_t dy, const struct surface *s,
                        int32_t sx, int32_t sy, int32_t w, int32_t h, uint32_t bg)
{
    for (int32_t y = 0; y < (int32_t)d->height; y++)
        for (int32_t x = 0; x < (int32_t)d->width; x++) {
            int64_t rx = (int64_t)x - dx, ry = (int64_t)y - dy, fx = rx + sx, fy = ry + sy;
            int in = rx >= 0 && rx < w && ry >= 0 && ry < h && fx >= 0 && fy >= 0 &&
                     fx < s->width && fy < s->height;
            uint32_t want = in ? grad_at((uint32_t)fx, (uint32_t)fy) : bg;
            if (surface_get(d, (uint32_t)x, (uint32_t)y) != want) {
                printf("    pixel %d,%d: 0x%06x, expected 0x%06x\n", x, y,
                       surface_get(d, (uint32_t)x, (uint32_t)y), want);
                return 0;
            }
        }
    return 1;
}

static void test_blit(void)
{
    static const int32_t cases[][6] = {     /* dx, dy, sx, sy, w, h */
        { 2, 3, 0, 0, 12, 9 },              /* whole source, inside */
        { -5, -4, 0, 0, 12, 9 },            /* clipped at the destination's top-left */
        { 33, 24, 0, 0, 12, 9 },            /* at its bottom-right */
        { 5, 5, 4, 2, 6, 5 },               /* part of the source */
        { 5, 5, -3, -2, 20, 20 },           /* a source rectangle past every source edge */
        { 0, 0, 12, 0, 5, 5 },              /* wholly outside the source */
        { 40, 0, 0, 0, 12, 9 },             /* wholly outside the destination */
        { -12, 0, 0, 0, 12, 9 },
        { 3, 3, 0, 0, 0, 9 },               /* empty */
        { INT32_MIN, INT32_MIN, 0, 0, INT32_MAX, INT32_MAX },
        { 1, 1, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX },
    };
    for (uint32_t dbpp = 3; dbpp <= 4; dbpp++)
        for (uint32_t sbpp = 3; sbpp <= 4; sbpp++)
            for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                const int32_t *c = cases[i];
                struct surface *s = mk(&b, 12, 9, sbpp, sbpp == 4 ? 0 : 2);
                gradient(s);
                struct surface *d = mk(&a, MAXW, MAXH, dbpp, 3);
                surface_fill(d, (struct rect){ 0, 0, MAXW, MAXH }, 0x5A5A5A);
                surface_blit(d, c[0], c[1], s, c[2], c[3], c[4], c[5]);
                int ok = blit_matches(d, c[0], c[1], s, c[2], c[3], c[4], c[5], 0x5A5A5A);
                if (!ok)
                    printf("    case %zu (dst %u, src %u bytes per pixel)\n", i, dbpp, sbpp);
                CHECK(ok);
                CHECK(guards_intact(&a));
                CHECK(guards_intact(&b));
            }
    /* Within one surface, rectangles apart. */
    struct surface *d = mk(&a, MAXW, MAXH, 4, 0);
    surface_fill(d, (struct rect){ 0, 0, MAXW, MAXH }, 0);
    gradient(d);
    surface_blit(d, 20, 15, d, 0, 0, 10, 10);
    CHECK_EQ(surface_get(d, 20, 15), grad_at(0, 0));
    CHECK_EQ(surface_get(d, 29, 24), grad_at(9, 9));
    CHECK_EQ(surface_get(d, 19, 15), grad_at(19, 15));
    /* 4 to 4 bytes: the unused byte is written as 0. */
    struct surface *s = mk(&b, 2, 1, 4, 0);
    memset(s->pixels, 0xFF, 8);
    d = mk(&a, 2, 1, 4, 0);
    surface_blit(d, 0, 0, s, 0, 0, 2, 1);
    CHECK(memcmp(d->pixels, "\xFF\xFF\xFF\x00\xFF\xFF\xFF\x00", 8) == 0);
}

/* The CRC of the RGB bytes equals zlib's crc32 of those bytes: the check
 * value of "123456789" (0xCBF43926), as three pixels. */
static void test_crc32(void)
{
    for (uint32_t bpp = 3; bpp <= 4; bpp++) {
        struct surface *s = mk(&a, 3, 1, bpp, 0);
        surface_fill(s, (struct rect){ 0, 0, 1, 1 }, 0x313233);      /* "123" */
        surface_fill(s, (struct rect){ 1, 0, 1, 1 }, 0x343536);
        surface_fill(s, (struct rect){ 2, 0, 1, 1 }, 0x373839);
        CHECK_EQ(surface_crc32(s), 0xCBF43926u);
        /* The same pixels as one column: the same bytes in the same order. */
        s = mk(&b, 1, 3, bpp, 7);
        surface_fill(s, (struct rect){ 0, 0, 1, 1 }, 0x313233);
        surface_fill(s, (struct rect){ 0, 1, 1, 1 }, 0x343536);
        surface_fill(s, (struct rect){ 0, 2, 1, 1 }, 0x373839);
        CHECK_EQ(surface_crc32(s), 0xCBF43926u);
    }
    /* The gaps at the row ends and the unused byte are not part of it. */
    struct surface *s = mk(&a, 2, 2, 4, 9);
    surface_fill(s, (struct rect){ 0, 0, 2, 2 }, 0);
    uint32_t c = surface_crc32(s);
    s->pixels[3] = 0x77;
    s->pixels[8] = 0x77;                    /* in the row's gap */
    CHECK_EQ(surface_crc32(s), c);
    CHECK_EQ(c, 0x7BD5C66Fu);               /* zlib.crc32(bytes(12)) */
}

int main(void)
{
    RUN(test_init);
    RUN(test_rect_clip);
    RUN(test_pixel_bytes);
    RUN(test_fill);
    RUN(test_blit);
    RUN(test_crc32);
    return unit_report("surface");
}
