/*
 * The framebuffer console, the pure part (1.2 stage 6); see fbcon_core.h.
 */
#include "kernel/display/fbcon_core.h"

uint32_t fbcon_scale_for_height(uint32_t height)
{
    if (height < 900)
        return 4;
    if (height < 1080)
        return 5;
    if (height < 1440)
        return 6;
    return 8;
}

bool fbcon_layout(uint32_t width, uint32_t height, const struct font *const fonts[4],
                  struct fbcon_layout *out)
{
    static const uint32_t scales[4] = { 4, 5, 6, 8 };
    uint32_t want = fbcon_scale_for_height(height);
    int i = 3;
    while (i > 0 && scales[i] > want)
        i--;
    for (; i >= 0; i--) {
        const struct font *f = fonts[i];
        uint64_t gw = (uint64_t)FBCON_COLS * f->width, gh = (uint64_t)FBCON_ROWS * f->height;
        if (gw <= width && gh <= height) {
            out->font = f;
            out->scale = scales[i];
            out->cell_w = f->width;
            out->cell_h = f->height;
            out->x = (uint32_t)((width - gw) / 2);
            out->y = (uint32_t)((height - gh) / 2);
            return true;
        }
    }
    return false;
}

static uint32_t widen(uint8_t v)
{
    v &= 0x3F;
    return (uint32_t)(v << 2 | v >> 4);
}

uint32_t fbcon_rgb6(const uint8_t rgb6[3])
{
    return widen(rgb6[0]) << 16 | widen(rgb6[1]) << 8 | widen(rgb6[2]);
}

void fbcon_cursor_rows(uint32_t height, uint32_t *first, uint32_t *end)
{
    *first = height * 13 / 16;
    *end = height * 15 / 16;
}

struct rect fbcon_cell_rect(const struct fbcon_layout *l, uint32_t row, uint32_t col)
{
    return (struct rect){ (int32_t)(l->x + col * l->cell_w), (int32_t)(l->y + row * l->cell_h),
                          (int32_t)l->cell_w, (int32_t)l->cell_h };
}

void fbcon_draw_cell(struct surface *s, const struct fbcon_layout *l, uint32_t row, uint32_t col,
                     uint8_t ch, uint32_t fg, uint32_t bg, bool cursor)
{
    if (row >= FBCON_ROWS || col >= FBCON_COLS)
        return;
    uint64_t x0 = l->x + (uint64_t)col * l->cell_w, y0 = l->y + (uint64_t)row * l->cell_h;
    if (x0 + l->cell_w > s->width || y0 + l->cell_h > s->height)
        return;
    const struct font *f = l->font;
    const uint8_t *glyph = f->bits + (uint32_t)ch * f->height * f->row_bytes;
    uint32_t c_first, c_end, bpp = s->bytes_per_pixel;
    fbcon_cursor_rows(f->height, &c_first, &c_end);
    for (uint32_t y = 0; y < f->height; y++) {
        const uint8_t *bits = glyph + y * f->row_bytes;
        bool under = cursor && y >= c_first && y < c_end;
        uint8_t *p = s->pixels + (y0 + y) * s->pitch + x0 * bpp;
        for (uint32_t x = 0; x < f->width; x++, p += bpp) {
            bool on = under || (bits[x >> 3] & (0x80u >> (x & 7)));
            uint32_t rgb = on ? fg : bg;
            p[0] = (uint8_t)rgb;
            p[1] = (uint8_t)(rgb >> 8);
            p[2] = (uint8_t)(rgb >> 16);
            if (bpp == 4)
                p[3] = 0;
        }
    }
}

bool fbcon_layout_at(uint32_t scale, const struct font *const fonts[4], uint32_t x, uint32_t y,
                     struct fbcon_layout *out)
{
    int i = scale == 4 ? 0 : scale == 5 ? 1 : scale == 6 ? 2 : scale == 8 ? 3 : -1;
    if (i < 0)
        return false;
    out->font = fonts[i];
    out->scale = scale;
    out->cell_w = fonts[i]->width;
    out->cell_h = fonts[i]->height;
    out->x = x;
    out->y = y;
    return true;
}

void fbcon_draw_cell_at(struct surface *s, const struct font *f, int32_t x, int32_t y, uint8_t ch, uint32_t fg,
                        uint32_t bg, bool cursor, struct rect clip)
{
    int64_t cx0 = clip.x, cy0 = clip.y, cx1 = (int64_t)clip.x + clip.w, cy1 = (int64_t)clip.y + clip.h;
    if (cx0 < 0)
        cx0 = 0;
    if (cy0 < 0)
        cy0 = 0;
    if (cx1 > s->width)
        cx1 = s->width;
    if (cy1 > s->height)
        cy1 = s->height;
    const uint8_t *glyph = f->bits + (uint32_t)ch * f->height * f->row_bytes;
    uint32_t c_first, c_end, bpp = s->bytes_per_pixel;
    fbcon_cursor_rows(f->height, &c_first, &c_end);
    for (uint32_t gy = 0; gy < f->height; gy++) {
        int64_t py = (int64_t)y + gy;
        if (py < cy0 || py >= cy1)
            continue;
        const uint8_t *bits = glyph + gy * f->row_bytes;
        bool under = cursor && gy >= c_first && gy < c_end;
        for (uint32_t gx = 0; gx < f->width; gx++) {
            int64_t px = (int64_t)x + gx;
            if (px < cx0 || px >= cx1)
                continue;
            bool on = under || (bits[gx >> 3] & (0x80u >> (gx & 7)));
            uint32_t rgb = on ? fg : bg;
            uint8_t *p = s->pixels + (uint64_t)py * s->pitch + (uint64_t)px * bpp;
            p[0] = (uint8_t)rgb;
            p[1] = (uint8_t)(rgb >> 8);
            p[2] = (uint8_t)(rgb >> 16);
            if (bpp == 4)
                p[3] = 0;
        }
    }
}
