/*
 * Drawing surfaces, the pure part (1.2 stage 5b); see surface_core.h.
 */
#include "kernel/display/surface_core.h"

bool surface_init(struct surface *s, void *pixels, uint32_t width, uint32_t height,
                  uint32_t pitch, uint32_t bytes_per_pixel)
{
    if (!pixels || width == 0 || height == 0 || (bytes_per_pixel != 3 && bytes_per_pixel != 4) ||
        (uint64_t)pitch < (uint64_t)width * bytes_per_pixel)
        return false;
    s->pixels = pixels;
    s->width = width;
    s->height = height;
    s->pitch = pitch;
    s->bytes_per_pixel = bytes_per_pixel;
    return true;
}

bool rect_clip(struct rect *r, uint32_t width, uint32_t height)
{
    int64_t x0 = r->x, y0 = r->y;
    int64_t x1 = x0 + (r->w > 0 ? r->w : 0), y1 = y0 + (r->h > 0 ? r->h : 0);
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int64_t)width)
        x1 = width;
    if (y1 > (int64_t)height)
        y1 = height;
    if (x0 >= x1 || y0 >= y1) {
        r->x = r->y = r->w = r->h = 0;
        return false;
    }
    r->x = (int32_t)x0;
    r->y = (int32_t)y0;
    r->w = (int32_t)(x1 - x0);
    r->h = (int32_t)(y1 - y0);
    return true;
}

static uint8_t *pixel_at(const struct surface *s, uint32_t x, uint32_t y)
{
    return s->pixels + (uint64_t)y * s->pitch + (uint64_t)x * s->bytes_per_pixel;
}

static uint32_t read_pixel(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
}

static void write_pixel(uint8_t *p, uint32_t bytes_per_pixel, uint32_t rgb)
{
    p[0] = (uint8_t)rgb;
    p[1] = (uint8_t)(rgb >> 8);
    p[2] = (uint8_t)(rgb >> 16);
    if (bytes_per_pixel == 4)
        p[3] = 0;
}

uint32_t surface_get(const struct surface *s, uint32_t x, uint32_t y)
{
    return read_pixel(pixel_at(s, x, y));
}

void surface_fill(struct surface *s, struct rect r, uint32_t rgb)
{
    if (!rect_clip(&r, s->width, s->height))
        return;
    rgb &= 0xFFFFFFu;
    for (int32_t y = 0; y < r.h; y++) {
        uint8_t *p = pixel_at(s, (uint32_t)r.x, (uint32_t)(r.y + y));
        if (s->bytes_per_pixel == 4) {
            /* One aligned store per pixel when the row allows it. */
            if (((uintptr_t)p & 3) == 0) {
                uint32_t *q = (uint32_t *)(void *)p;
                for (int32_t x = 0; x < r.w; x++)
                    q[x] = rgb;
                continue;
            }
        }
        for (int32_t x = 0; x < r.w; x++, p += s->bytes_per_pixel)
            write_pixel(p, s->bytes_per_pixel, rgb);
    }
}

void surface_blit(struct surface *dst, int32_t dx, int32_t dy, const struct surface *src,
                  int32_t sx, int32_t sy, int32_t w, int32_t h)
{
    /* Clip against the source first, moving the destination with it. */
    struct rect s = { sx, sy, w, h };
    if (!rect_clip(&s, src->width, src->height))
        return;
    int64_t ddx = (int64_t)dx + ((int64_t)s.x - sx), ddy = (int64_t)dy + ((int64_t)s.y - sy);
    /* Then against the destination, moving the source with it. */
    int64_t x0 = ddx, y0 = ddy, x1 = ddx + s.w, y1 = ddy + s.h;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int64_t)dst->width)
        x1 = dst->width;
    if (y1 > (int64_t)dst->height)
        y1 = dst->height;
    if (x0 >= x1 || y0 >= y1)
        return;
    uint32_t cw = (uint32_t)(x1 - x0), ch = (uint32_t)(y1 - y0);
    uint32_t fx = (uint32_t)(s.x + (x0 - ddx)), fy = (uint32_t)(s.y + (y0 - ddy));
    bool words = dst->bytes_per_pixel == 4 && src->bytes_per_pixel == 4;
    for (uint32_t y = 0; y < ch; y++) {
        uint8_t *d = pixel_at(dst, (uint32_t)x0, (uint32_t)y0 + y);
        const uint8_t *p = pixel_at(src, fx, fy + y);
        if (words && ((uintptr_t)d & 3) == 0 && ((uintptr_t)p & 3) == 0) {
            uint32_t *dw = (uint32_t *)(void *)d;
            const uint32_t *sw = (const uint32_t *)(const void *)p;
            for (uint32_t x = 0; x < cw; x++)
                dw[x] = sw[x] & 0xFFFFFFu;
            continue;
        }
        for (uint32_t x = 0; x < cw; x++, d += dst->bytes_per_pixel, p += src->bytes_per_pixel)
            write_pixel(d, dst->bytes_per_pixel, read_pixel(p));
    }
}

static uint32_t crc32_byte(uint32_t crc, uint8_t b)
{
    crc ^= b;
    for (int i = 0; i < 8; i++)
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    return crc;
}

uint32_t surface_crc32(const struct surface *s)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t y = 0; y < s->height; y++) {
        const uint8_t *p = pixel_at(s, 0, y);
        for (uint32_t x = 0; x < s->width; x++, p += s->bytes_per_pixel) {
            crc = crc32_byte(crc, p[2]);        /* red */
            crc = crc32_byte(crc, p[1]);        /* green */
            crc = crc32_byte(crc, p[0]);        /* blue */
        }
    }
    return crc ^ 0xFFFFFFFFu;
}
