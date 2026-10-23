/*
 * The procedural wallpaper and the splash, the pure part (1.2 stage 8);
 * see wallpaper_core.h. Every step follows scripts/make-wallpaper.py's
 * (wallpaper(), splash_layout(), splash()), in the same order, with the
 * same roundings.
 */
#include "kernel/display/wallpaper_core.h"
#include "splash_data.h"            /* generated: build/gen/ (the Makefile) */

#define FB    8
#define ONE   256
#define HALF  128

/* The curves: centre x (per mille of W), y at the left and right edges
 * (per mille of H); make-wallpaper.py's CURVES. */
static const int32_t curves[WP_CURVES][3] = {
    { 1200, 1000, 400 }, { 1350, 1060, 600 }, { 1120, 560, 40 },
};

/* The logo's x extent in its units (kernel/logo.c's strokes with their
 * radius), and its height: 169 x 20 / 23 + 20, rounded up. */
#define LX0    (59 - SPLASH_LOGO_RX)
#define LX1    (229 + SPLASH_LOGO_RX)
#define LOGO_H 167

int64_t wp_fx(int64_t num, int64_t den)
{
    int64_t a = num < 0 ? -num : num;
    int64_t q = (a * ONE * 2 + den) / (2 * den);
    return num < 0 ? -q : q;
}

int64_t wp_mul(int64_t a, int64_t b)
{
    int64_t p = a * b;
    return p >= 0 ? (p + HALF) >> FB : -((-p + HALF) >> FB);
}

int64_t wp_div(int64_t a, int64_t b)
{
    uint64_t n = (uint64_t)(a < 0 ? -a : a) * ONE, d = (uint64_t)(b < 0 ? -b : b);
    uint64_t q = n / d, r = n % d;
    if (2 * r >= d)
        q++;
    return (a >= 0) == (b > 0) ? (int64_t)q : -(int64_t)q;
}

uint64_t wp_isqrt(uint64_t n)
{
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

uint8_t wp_byte(int64_t v)
{
    int64_t q = v >> FB, r = v & (ONE - 1);
    if (r > HALF || (r == HALF && (q & 1)))
        q++;
    return (uint8_t)(q < 0 ? 0 : q > 255 ? 255 : q);
}

static struct wp_rgb rgb(uint32_t c)
{
    return (struct wp_rgb){ { (int64_t)(c >> 16 & 0xFF) << FB, (int64_t)(c >> 8 & 0xFF) << FB,
                              (int64_t)(c & 0xFF) << FB } };
}

static struct wp_rgb mix(struct wp_rgb a, struct wp_rgb b, int64_t t)
{
    for (int i = 0; i < 3; i++)
        a.c[i] += wp_mul(b.c[i] - a.c[i], t);
    return a;
}

static int64_t abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

static uint32_t dpx(uint32_t dp, uint32_t s)
{
    return (dp * s + 2) / 4;
}

/* ---- Pixels ---- */

static uint8_t *pixel(struct surface *s, int64_t x, int64_t y)
{
    return s->pixels + (uint64_t)y * s->pitch + (uint64_t)x * s->bytes_per_pixel;
}

static void put(struct surface *s, int64_t x, int64_t y, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *p = pixel(s, x, y);
    p[0] = b;
    p[1] = g;
    p[2] = r;
    if (s->bytes_per_pixel == 4)
        p[3] = 0;
}

/* The pixel's bytes mixed towards `c` by coverage a, rounded back. */
static void paint(struct surface *s, int64_t x, int64_t y, struct wp_rgb c, int64_t a)
{
    uint8_t *p = pixel(s, x, y);
    struct wp_rgb v = { { (int64_t)p[2] << FB, (int64_t)p[1] << FB, (int64_t)p[0] << FB } };
    v = mix(v, c, a);
    put(s, x, y, wp_byte(v.c[0]), wp_byte(v.c[1]), wp_byte(v.c[2]));
}

/* ---- The wallpaper ---- */

void wallpaper_setup(struct wallpaper *wp, uint32_t width, uint32_t height, uint32_t scale)
{
    wp->width = width;
    wp->height = height;
    wp->deep = rgb(WP_BG_DEEP);
    wp->low = rgb(WP_BG_LOW);
    wp->base = rgb(WP_BG_BASE);
    wp->glow = rgb(WP_ACCENT_GLOW);
    wp->light = rgb(WP_ACCENT_LIGHT);
    wp->halo_w = (int64_t)dpx(40, scale) << FB;
    wp->core_r = 32 * (int64_t)scale;
    wp->ccx = width / 2;
    wp->ccy = (int64_t)height * 3 / 10;
    wp->rx = (int64_t)width * 3 / 5;
    wp->ry = height / 2;
    for (int i = 0; i < WP_CURVES; i++) {
        int64_t w = width, cx = w * curves[i][0] / 1000;
        int64_t yl = (int64_t)height * curves[i][1] / 1000, yr = (int64_t)height * curves[i][2] / 1000;
        int64_t cy = (cx * cx - (cx - w) * (cx - w) + yl * yl - yr * yr) / (2 * (yl - yr));
        wp->cx[i] = cx << FB;
        wp->cy[i] = cy << FB;
        wp->r[i] = (int64_t)wp_isqrt((uint64_t)(cx * cx + (cy - yl) * (cy - yl))) << FB;
    }
}

/* The columns of row y where curve i can reach (its halo, |distance - r|
 * < halo_w): |dx| from floor(sqrt(lo2)) - 1 to floor(sqrt(hi2)) + 1 on
 * either side of the centre, as pixel columns [x0, x1] and [x2, x3]. A
 * pixel outside them is left to the per-pixel test's verdict: none. */
static void curve_band(const struct wallpaper *wp, int i, int64_t py, int64_t span[4])
{
    int64_t dy = py - wp->cy[i], in = wp->r[i] - wp->halo_w, out = wp->r[i] + wp->halo_w;
    int64_t lo2 = (in > 0 ? in * in : 0) - dy * dy, hi2 = out * out - dy * dy;
    if (hi2 <= 0) {
        span[0] = span[2] = 1;
        span[1] = span[3] = 0;
        return;
    }
    int64_t a = lo2 > 0 ? (int64_t)wp_isqrt((uint64_t)lo2) - 1 : 0;
    int64_t b = (int64_t)wp_isqrt((uint64_t)hi2) + 1;
    if (a < 0)
        a = 0;
    /* A pixel's centre: x * 256 + 128 = cx + dx. */
    span[0] = ((wp->cx[i] - b - HALF) >> FB) - 1;
    span[1] = ((wp->cx[i] - a - HALF) >> FB) + 1;
    span[2] = ((wp->cx[i] + a - HALF) >> FB) - 1;
    span[3] = ((wp->cx[i] + b - HALF) >> FB) + 1;
}

void wallpaper_render(struct surface *s, const struct wallpaper *wp, struct rect r)
{
    if (!rect_clip(&r, s->width, s->height))
        return;
    int64_t halo_a = wp_fx(30, 100), core_a = wp_fx(70, 100);
    for (int64_t y = r.y; y < (int64_t)r.y + r.h; y++) {
        struct wp_rgb row = mix(wp->deep, wp->low, wp_fx(y, (int64_t)wp->height - 1));
        int64_t dy = y - wp->ccy, qy = wp_div(dy * dy, wp->ry * wp->ry);
        int64_t py = (y << FB) + HALF;
        int64_t span[WP_CURVES][4];
        for (int i = 0; i < WP_CURVES; i++)
            curve_band(wp, i, py, span[i]);
        for (int64_t x = r.x; x < (int64_t)r.x + r.w; x++) {
            int64_t dx = x - wp->ccx;
            int64_t w = ONE - wp_div(dx * dx, wp->rx * wp->rx) - qy;
            struct wp_rgb c = w > 0 ? mix(row, wp->base, wp_mul(w, w)) : row;
            int64_t px = (x << FB) + HALF;
            for (int i = 0; i < WP_CURVES; i++) {
                if (!((x >= span[i][0] && x <= span[i][1]) || (x >= span[i][2] && x <= span[i][3])))
                    continue;
                int64_t ex = px - wp->cx[i], ey = py - wp->cy[i];
                int64_t d = abs64((int64_t)wp_isqrt((uint64_t)(ex * ex + ey * ey)) - wp->r[i]);
                if (d >= wp->halo_w)
                    continue;
                int64_t t = wp->halo_w - d;
                c = mix(c, wp->glow, wp_mul(halo_a, t * t / wp->halo_w * ONE / wp->halo_w));
                int64_t cov = wp->core_r + HALF - d;
                if (cov > 0)
                    c = mix(c, wp->light, wp_mul(core_a, cov < ONE ? cov : ONE));
            }
            put(s, x, y, wp_byte(c.c[0]), wp_byte(c.c[1]), wp_byte(c.c[2]));
        }
    }
}

/* ---- Capsules ---- */

struct capsule {
    int64_t x1, y1, dx, dy, l2, r;
    int64_t bx0, by0, bx1, by1;     /* the pixels it may cover, clipped */
};

static bool capsule_setup(struct capsule *c, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t r,
                          uint32_t width, uint32_t height)
{
    c->x1 = x1;
    c->y1 = y1;
    c->dx = x2 - x1;
    c->dy = y2 - y1;
    c->l2 = c->dx * c->dx + c->dy * c->dy;
    c->r = r;
    c->bx0 = ((x1 < x2 ? x1 : x2) - r - 2 * ONE) >> FB;
    c->bx1 = ((x1 > x2 ? x1 : x2) + r + 2 * ONE) >> FB;
    c->by0 = ((y1 < y2 ? y1 : y2) - r - 2 * ONE) >> FB;
    c->by1 = ((y1 > y2 ? y1 : y2) + r + 2 * ONE) >> FB;
    if (c->bx0 < 0)
        c->bx0 = 0;
    if (c->by0 < 0)
        c->by0 = 0;
    if (c->bx1 > (int64_t)width - 1)
        c->bx1 = (int64_t)width - 1;
    if (c->by1 > (int64_t)height - 1)
        c->by1 = (int64_t)height - 1;
    return c->bx0 <= c->bx1 && c->by0 <= c->by1;
}

/* The coverage of pixel (x, y), 0 to 256: the radius plus half a pixel
 * minus the distance from the pixel's centre to the segment. */
static int64_t capsule_cov(const struct capsule *c, int64_t x, int64_t y)
{
    int64_t cx = (x << FB) + HALF - c->x1, cy = (y << FB) + HALF - c->y1, t = 0;
    if (c->l2 != 0) {
        int64_t d = cx * c->dx + cy * c->dy;
        t = d <= 0 ? 0 : d >= c->l2 ? ONE : (d << FB) / c->l2;
    }
    int64_t ex = cx - wp_mul(t, c->dx), ey = cy - wp_mul(t, c->dy);
    int64_t a = c->r + HALF - (int64_t)wp_isqrt((uint64_t)(ex * ex + ey * ey));
    return a <= 0 ? 0 : a > ONE ? ONE : a;
}

/* ---- The splash ---- */

bool splash_layout(struct splash_layout *l, uint32_t width, uint32_t height, uint32_t scale,
                   uint32_t cell_w, uint32_t cell_h)
{
    uint32_t s = scale;
    l->scale = s;
    l->cell_w = cell_w;
    l->cell_h = cell_h;
    l->ls = wp_fx(74 * (int64_t)s, 400);
    l->ts = wp_fx(23 * (int64_t)s, 40);
    int64_t logo_h = wp_mul(wp_fx(LOGO_H, 1), l->ls), cap_h = wp_mul(wp_fx(14, 1), l->ts);
    int64_t g1 = (int64_t)dpx(28, s) << FB, g2 = (int64_t)dpx(40, s) << FB;
    int64_t top = (((int64_t)height << FB) - (logo_h + g1 + cap_h + g2 + ((int64_t)cell_h << FB))) / 2;
    l->lox = wp_fx(width, 2) - wp_mul(wp_fx(LX0 + LX1, 2), l->ls);
    l->loy = top - wp_mul(wp_fx(23 * SPLASH_LOGO_RX, SPLASH_LOGO_RY) - wp_fx(SPLASH_LOGO_RX, 1), l->ls);
    l->title_y = top + logo_h + g1;
    l->r_title = wp_fx(25 * (int64_t)s, 40);
    l->r_os = wp_fx(125 * (int64_t)s, 400);
    l->track = wp_fx(6, 1);
    int64_t adv = 0;
    for (size_t i = 0; i < sizeof(splash_wm_glyphs) / sizeof(splash_wm_glyphs[0]); i++)
        adv += splash_wm_glyphs[i].advance + l->track;
    l->title_x = wp_fx(width, 2) - (wp_mul(adv, l->ts) - wp_mul(l->track, l->ts)) / 2;

    int64_t gap = dpx(6, s), spacing = dpx(8, s), tw = (int64_t)(sizeof(SPLASH_TEXT) - 1) * cell_w;
    int64_t total = tw + gap + 2 * spacing + dpx(3, s);
    if (top < 0 || total > (int64_t)width)
        return false;
    l->text_x = (int32_t)(((int64_t)width - total) / 2);
    l->text_y = (int32_t)((l->title_y + cap_h + g2) >> FB);
    l->rd = wp_fx(3 * (int64_t)s, 8);
    int64_t x0 = ((l->text_x + tw + gap) << FB) + l->rd;
    for (int i = 0; i < SPLASH_DOTS; i++)
        l->dot_x[i] = x0 + ((i * spacing) << FB);
    l->dot_y = ((int64_t)l->text_y << FB) + ((int64_t)cell_h << FB) * 11 / 16;
    int64_t bx0 = ((l->dot_x[0] - l->rd) >> FB) - 2, by0 = ((l->dot_y - l->rd) >> FB) - 2;
    int64_t bx1 = ((l->dot_x[SPLASH_DOTS - 1] + l->rd) >> FB) + 2, by1 = ((l->dot_y + l->rd) >> FB) + 2;
    l->dots_box = (struct rect){ (int32_t)bx0, (int32_t)by0, (int32_t)(bx1 - bx0 + 1), (int32_t)(by1 - by0 + 1) };
    return (int64_t)l->text_y + cell_h <= (int64_t)height;
}

/* Glyph g of the wordmark at pen x: its segments' capsules, each through
 * `fn` (from its first to its last point; a one-point polyline is a dot). */
static void glyph_segments(const struct splash_layout *l, size_t g, int64_t pen, int64_t r, uint32_t width,
                           uint32_t height, void (*fn)(const struct capsule *, void *), void *arg)
{
    const struct wp_glyph *gl = &splash_wm_glyphs[g];
    for (uint32_t p = 0; p < gl->polys; p++) {
        const struct wp_poly *poly = &splash_wm_polys[gl->first_poly + p];
        for (uint32_t k = 0; k + 1 < poly->count || (k == 0 && poly->count == 1); k++) {
            const int16_t *a = splash_wm_points[poly->first + k];
            const int16_t *b = splash_wm_points[poly->first + (poly->count == 1 ? k : k + 1)];
            struct capsule c;
            if (capsule_setup(&c, pen + wp_mul(a[0], l->ts), l->title_y + wp_mul(a[1], l->ts),
                              pen + wp_mul(b[0], l->ts), l->title_y + wp_mul(b[1], l->ts), r, width, height))
                fn(&c, arg);
        }
    }
}

struct box {
    int64_t x0, y0, x1, y1;
    bool any;
};

static void grow_box(const struct capsule *c, void *arg)
{
    struct box *b = arg;
    if (!b->any || c->bx0 < b->x0)
        b->x0 = c->bx0;
    if (!b->any || c->by0 < b->y0)
        b->y0 = c->by0;
    if (!b->any || c->bx1 > b->x1)
        b->x1 = c->bx1;
    if (!b->any || c->by1 > b->y1)
        b->y1 = c->by1;
    b->any = true;
}

struct cover {
    struct box box;
    uint16_t *cov;
};

static void add_cover(const struct capsule *c, void *arg)
{
    struct cover *cv = arg;
    int64_t bw = cv->box.x1 - cv->box.x0 + 1;
    for (int64_t y = c->by0; y <= c->by1; y++)
        for (int64_t x = c->bx0; x <= c->bx1; x++) {
            int64_t a = capsule_cov(c, x, y);
            uint16_t *e = &cv->cov[(y - cv->box.y0) * bw + (x - cv->box.x0)];
            if (a > *e)
                *e = (uint16_t)a;
        }
}

static int64_t glyph_radius(const struct splash_layout *l, size_t g)
{
    return g < 6 ? l->r_title : l->r_os;    /* "MADIRE" heavier than "OS" */
}

size_t splash_scratch_entries(const struct splash_layout *l, uint32_t width, uint32_t height)
{
    size_t most = 0;
    int64_t pen = l->title_x;
    for (size_t g = 0; g < sizeof(splash_wm_glyphs) / sizeof(splash_wm_glyphs[0]); g++) {
        struct box b = { 0, 0, 0, 0, false };
        glyph_segments(l, g, pen, glyph_radius(l, g), width, height, grow_box, &b);
        if (b.any && (size_t)((b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1)) > most)
            most = (size_t)((b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1));
        pen += wp_mul(splash_wm_glyphs[g].advance + l->track, l->ts);
    }
    return most;
}

static void draw_logo(struct surface *s, const struct splash_layout *l)
{
    struct wp_rgb deep = rgb(WP_ACCENT_DEEP), light = rgb(WP_ACCENT_LIGHT), bg = rgb(WP_BG_DEEP);
    int64_t ls = l->ls, back_t = wp_fx(35, 100);
    for (int i = 0; i < 4; i++) {
        const int32_t *st = splash_logo_strokes[i];
        struct capsule c;
        if (!capsule_setup(&c, l->lox + st[0] * ls,
                           l->loy + wp_mul(wp_fx(st[1] * SPLASH_LOGO_RX, SPLASH_LOGO_RY), ls),
                           l->lox + st[2] * ls,
                           l->loy + wp_mul(wp_fx(st[3] * SPLASH_LOGO_RX, SPLASH_LOGO_RY), ls),
                           SPLASH_LOGO_RX * ls, s->width, s->height))
            continue;
        for (int64_t y = c.by0; y <= c.by1; y++)
            for (int64_t x = c.bx0; x <= c.bx1; x++) {
                int64_t a = capsule_cov(&c, x, y);
                if (!a)
                    continue;
                int64_t t = wp_div((x << FB) - (l->lox + LX0 * ls), (LX1 - LX0) * ls);
                struct wp_rgb col = mix(deep, light, t < 0 ? 0 : t > ONE ? ONE : t);
                if (i < 2)                  /* the back strokes */
                    col = mix(col, bg, back_t);
                paint(s, x, y, col, a);
            }
    }
}

bool splash_draw(struct surface *s, const struct splash_layout *l, const struct font *f,
                 uint16_t *scratch, size_t entries)
{
    bool all = true;
    draw_logo(s, l);

    struct wp_rgb text = rgb(WP_TEXT);
    int64_t pen = l->title_x;
    for (size_t g = 0; g < sizeof(splash_wm_glyphs) / sizeof(splash_wm_glyphs[0]); g++) {
        struct cover cv = { { 0, 0, 0, 0, false }, scratch };
        int64_t r = glyph_radius(l, g);
        glyph_segments(l, g, pen, r, s->width, s->height, grow_box, &cv.box);
        pen += wp_mul(splash_wm_glyphs[g].advance + l->track, l->ts);
        if (!cv.box.any)
            continue;
        size_t bw = (size_t)(cv.box.x1 - cv.box.x0 + 1), n = bw * (size_t)(cv.box.y1 - cv.box.y0 + 1);
        if (n > entries) {
            all = false;
            continue;
        }
        for (size_t i = 0; i < n; i++)
            scratch[i] = 0;
        glyph_segments(l, g, pen - wp_mul(splash_wm_glyphs[g].advance + l->track, l->ts), r, s->width,
                       s->height, add_cover, &cv);
        for (int64_t y = cv.box.y0; y <= cv.box.y1; y++)
            for (int64_t x = cv.box.x0; x <= cv.box.x1; x++) {
                uint16_t a = scratch[(size_t)(y - cv.box.y0) * bw + (size_t)(x - cv.box.x0)];
                if (a)
                    paint(s, x, y, text, a);
            }
    }

    /* The text line: the font's set pixels, opaque. */
    const char *t = SPLASH_TEXT;
    uint32_t muted = WP_TEXT_MUTED;
    for (uint32_t i = 0; t[i]; i++) {
        const uint8_t *glyph = f->bits + (uint32_t)(uint8_t)t[i] * f->height * f->row_bytes;
        for (uint32_t y = 0; y < f->height; y++)
            for (uint32_t x = 0; x < f->width; x++)
                if (glyph[y * f->row_bytes + (x >> 3)] & (0x80u >> (x & 7)))
                    put(s, l->text_x + (int64_t)(i * f->width + x), l->text_y + (int64_t)y,
                        (uint8_t)(muted >> 16), (uint8_t)(muted >> 8), (uint8_t)muted);
    }
    return all;
}

void splash_draw_dots(struct surface *s, const struct splash_layout *l, uint32_t phase)
{
    struct wp_rgb lit = rgb(WP_TEXT), dim = rgb(WP_SURFACE_ACTIVE);
    for (uint32_t i = 0; i < SPLASH_DOTS; i++) {
        struct capsule c;
        if (!capsule_setup(&c, l->dot_x[i], l->dot_y, l->dot_x[i], l->dot_y, l->rd, s->width, s->height))
            continue;
        for (int64_t y = c.by0; y <= c.by1; y++)
            for (int64_t x = c.bx0; x <= c.bx1; x++) {
                int64_t a = capsule_cov(&c, x, y);
                if (a)
                    paint(s, x, y, i < phase ? lit : dim, a);
            }
    }
}
