/*
 * The toolkit's drawing (1.3 stage 2); see ui_draw.h.
 */
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_trace.h"
#include "kernel/display/wallpaper_core.h"

#define FB 8

int32_t ui_px(int32_t dp, uint32_t s)
{
    return (int32_t)(((int64_t)dp * s + 2) / 4);
}

void ui_canvas_init(struct ui_canvas *c, struct surface *s)
{
    c->s = s;
    c->clip = (struct rect){ 0, 0, (int32_t)s->width, (int32_t)s->height };
}

void ui_canvas_clip(struct ui_canvas *c, struct rect r)
{
    struct rect all = { 0, 0, (int32_t)c->s->width, (int32_t)c->s->height };
    c->clip = ui_rect_intersect(r, all);
}

static uint8_t *canvas_pixel(const struct ui_canvas *c, int64_t x, int64_t y)
{
    return c->s->pixels + (uint64_t)y * c->s->pitch + (uint64_t)x * c->s->bytes_per_pixel;
}

static void canvas_put(const struct ui_canvas *c, int64_t x, int64_t y, uint32_t rgb)
{
    uint8_t *p = canvas_pixel(c, x, y);
    p[0] = (uint8_t)rgb;
    p[1] = (uint8_t)(rgb >> 8);
    p[2] = (uint8_t)(rgb >> 16);
    if (c->s->bytes_per_pixel == 4)
        p[3] = 0;
}

#ifdef UI_TRACE
static uint32_t canvas_get(const struct ui_canvas *c, int64_t x, int64_t y)
{
    const uint8_t *p = canvas_pixel(c, x, y);
    return (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
#endif

/* Fills [x0, x1) of row y, clipped. */
static void fill_row(const struct ui_canvas *c, int64_t y, int64_t x0, int64_t x1, uint32_t rgb)
{
    const struct rect *k = &c->clip;
    if (y < k->y || y >= (int64_t)k->y + k->h)
        return;
    if (x0 < k->x)
        x0 = k->x;
    if (x1 > (int64_t)k->x + k->w)
        x1 = (int64_t)k->x + k->w;
    for (int64_t x = x0; x < x1; x++)
        canvas_put(c, x, y, rgb);
}

void ui_fill(const struct ui_canvas *c, struct rect r, uint32_t rgb)
{
    if (ui_rect_empty(r))
        return;
    for (int64_t y = r.y; y < (int64_t)r.y + r.h; y++)
        fill_row(c, y, r.x, (int64_t)r.x + r.w, rgb);
}

static int32_t clamp_radius(struct rect r, int32_t radius)
{
    if (radius > r.w / 2)
        radius = r.w / 2;
    if (radius > r.h / 2)
        radius = r.h / 2;
    return radius < 0 ? 0 : radius;
}

/* The first column, from the corner, inside the quarter-circle of radius
 * R on the corner square's row j (both counted from the corner). */
static int32_t corner_inset(int32_t R, int32_t j)
{
    int64_t dy = 2 * (int64_t)R - 2 * j - 1, rr = 4 * (int64_t)R * R;
    int32_t i = 0;
    while (i < R) {
        int64_t dx = 2 * (int64_t)R - 2 * i - 1;
        if (dx * dx + dy * dy <= rr)
            break;
        i++;
    }
    return i;
}

/* The pixels [*x0, *x1) of row y inside the rounded rectangle (r, R
 * already clamped); false if the row is outside it. */
static bool round_row(struct rect r, int32_t R, uint32_t corners, int64_t y, int64_t *x0, int64_t *x1)
{
    if (ui_rect_empty(r) || y < r.y || y >= (int64_t)r.y + r.h)
        return false;
    int64_t top = y - r.y, bottom = (int64_t)r.y + r.h - 1 - y;
    int32_t left = 0, right = 0;
    if (top < R) {
        int32_t in = corner_inset(R, (int32_t)top);
        left = corners & UI_CORNER_TL ? in : 0;
        right = corners & UI_CORNER_TR ? in : 0;
    } else if (bottom < R) {
        int32_t in = corner_inset(R, (int32_t)bottom);
        left = corners & UI_CORNER_BL ? in : 0;
        right = corners & UI_CORNER_BR ? in : 0;
    }
    *x0 = (int64_t)r.x + left;
    *x1 = (int64_t)r.x + r.w - right;
    return true;
}

bool ui_round_span(struct rect r, int32_t radius, uint32_t corners, int64_t y, int64_t *x0, int64_t *x1)
{
    return round_row(r, clamp_radius(r, radius), corners, y, x0, x1);
}

void ui_fill_round(const struct ui_canvas *c, struct rect r, int32_t radius, uint32_t corners, uint32_t rgb)
{
    if (ui_rect_empty(r))
        return;
    int32_t R = clamp_radius(r, radius);
    for (int64_t y = r.y; y < (int64_t)r.y + r.h; y++) {
        int64_t x0, x1;
        if (round_row(r, R, corners, y, &x0, &x1))
            fill_row(c, y, x0, x1, rgb);
    }
}

void ui_blit_round(const struct ui_canvas *c, struct rect r, const struct surface *src, int32_t radius,
                   uint32_t corners)
{
    if (ui_rect_empty(r))
        return;
    int32_t R = clamp_radius(r, radius);
    const struct rect *k = &c->clip;
    for (int64_t y = r.y; y < (int64_t)r.y + r.h; y++) {
        int64_t x0, x1;
        if (y < k->y || y >= (int64_t)k->y + k->h || !round_row(r, R, corners, y, &x0, &x1))
            continue;
        if (x0 < k->x)
            x0 = k->x;
        if (x1 > (int64_t)k->x + k->w)
            x1 = (int64_t)k->x + k->w;
        if (x1 > x0)
            surface_blit(c->s, (int32_t)x0, (int32_t)y, src, (int32_t)(x0 - r.x), (int32_t)(y - r.y),
                         (int32_t)(x1 - x0), 1);
    }
}

void ui_frame_round(const struct ui_canvas *c, struct rect r, int32_t radius, int32_t width,
                    uint32_t corners, uint32_t rgb)
{
    if (ui_rect_empty(r) || width <= 0)
        return;
    int32_t R = clamp_radius(r, radius);
    struct rect in = ui_rect_inflate(r, -width);
    int32_t Rin = clamp_radius(in, R - width);
    for (int64_t y = r.y; y < (int64_t)r.y + r.h; y++) {
        int64_t a0, a1, b0, b1;
        if (!round_row(r, R, corners, y, &a0, &a1))
            continue;
        if (round_row(in, Rin, corners, y, &b0, &b1) && b0 < b1) {
            fill_row(c, y, a0, b0, rgb);
            fill_row(c, y, b1, a1, rgb);
        } else {
            fill_row(c, y, a0, a1, rgb);
        }
    }
}

void ui_blend(const struct ui_canvas *c, int64_t x, int64_t y, uint32_t rgb, int64_t a)
{
    const struct rect *k = &c->clip;
    if (a <= 0 || x < k->x || y < k->y || x >= (int64_t)k->x + k->w || y >= (int64_t)k->y + k->h)
        return;
    uint8_t *p = canvas_pixel(c, x, y);
    uint32_t out = 0;
    for (int ch = 0; ch < 3; ch++) {
        int64_t v = (int64_t)p[2 - ch] << FB, to = (int64_t)(rgb >> (16 - 8 * ch) & 0xFF) << FB;
        out = out << 8 | wp_byte(v + wp_mul(to - v, a));
    }
    canvas_put(c, x, y, out);
}

void ui_text(const struct ui_canvas *c, const struct font *f, int32_t x, int32_t y, const char *text,
             uint32_t rgb)
{
    const struct rect *k = &c->clip;
    for (uint32_t i = 0; text[i]; i++) {
        const uint8_t *glyph = f->bits + (uint32_t)(uint8_t)text[i] * f->height * f->row_bytes;
        int64_t gx = (int64_t)x + (int64_t)i * f->width;
        for (uint32_t row = 0; row < f->height; row++) {
            int64_t py = (int64_t)y + row;
            if (py < k->y || py >= (int64_t)k->y + k->h)
                continue;
            for (uint32_t col = 0; col < f->width; col++) {
                int64_t px = gx + col;
                if (px >= k->x && px < (int64_t)k->x + k->w &&
                    glyph[row * f->row_bytes + (col >> 3)] & (0x80u >> (col & 7))) {
                    UI_TRACE_PAIR(UI_TRACE_TEXT, rgb, canvas_get(c, px, py));
                    canvas_put(c, px, py, rgb);
                }
            }
        }
    }
}

int32_t ui_text_width(const struct font *f, const char *text)
{
    int64_t n = 0;
    while (text[n])
        n++;
    return (int32_t)(n * f->width);
}
