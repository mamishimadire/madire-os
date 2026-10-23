/* The mouse pointer, the pure part (see pointer_core.h). */
#include "kernel/display/pointer_core.h"

/* Madire's own pointer, one character per dp (see pointer_core.h). */
const char *const pointer_shape[POINTER_DP_H] = {
    "o...........",
    "oo..........",
    "oao.........",
    "oawo........",
    "oawwo.......",
    "oawwwo......",
    "oawwwwo.....",
    "oawwwwwo....",
    "oawwwwwwo...",
    "oawwwwwwwo..",
    "oawwwwwwwwo.",
    "oawwwwwwwwwo",
    "oawwwwoooooo",
    "oawwwo......",
    "oawwo.......",
    "oawo........",
    "oao.........",
    "oo..........",
};

static uint8_t shape_px(char c)
{
    switch (c) {
    case 'o': return POINTER_PX_EDGE;
    case 'w': return POINTER_PX_FILL;
    case 'a': return POINTER_PX_SPINE;
    }
    return POINTER_NONE;
}

bool pointer_sprite_make(struct pointer_sprite *s, uint32_t scale)
{
    if (scale != 4 && scale != 5 && scale != 6 && scale != 8)
        return false;
    s->scale = scale;
    s->w = (POINTER_DP_W * scale + 2) / 4;
    s->h = (POINTER_DP_H * scale + 2) / 4;
    for (uint32_t y = 0; y < s->h; y++)
        for (uint32_t x = 0; x < s->w; x++)
            s->px[y * s->w + x] = shape_px(pointer_shape[y * POINTER_DP_H / s->h][x * POINTER_DP_W / s->w]);
    return true;
}

uint32_t pointer_colour(uint8_t px)
{
    switch (px) {
    case POINTER_PX_EDGE:  return POINTER_EDGE;
    case POINTER_PX_FILL:  return POINTER_FILL;
    case POINTER_PX_SPINE: return POINTER_SPINE;
    }
    return 0;
}

void pointer_clamp(int32_t *x, int32_t *y, uint32_t width, uint32_t height)
{
    if (*x < 0)
        *x = 0;
    if (*y < 0)
        *y = 0;
    if (width && *x > (int32_t)width - 1)
        *x = (int32_t)width - 1;
    if (height && *y > (int32_t)height - 1)
        *y = (int32_t)height - 1;
}

struct rect pointer_area(const struct pointer_sprite *s, int32_t x, int32_t y, uint32_t width,
                         uint32_t height)
{
    struct rect r = { x, y, (int32_t)s->w, (int32_t)s->h };
    rect_clip(&r, width, height);
    return r;
}

static bool under_surface(struct surface *u, struct pointer_under *p, const struct surface *screen)
{
    return p->r.w > 0 && p->r.h > 0 && p->r.w <= POINTER_MAX_W && p->r.h <= POINTER_MAX_H &&
           surface_init(u, p->pixels, (uint32_t)p->r.w, (uint32_t)p->r.h,
                        (uint32_t)p->r.w * screen->bytes_per_pixel, screen->bytes_per_pixel);
}

void pointer_save(struct pointer_under *u, const struct surface *screen, struct rect r)
{
    struct surface us;
    u->r = r;
    if (!under_surface(&us, u, screen)) {
        u->r = (struct rect){ 0, 0, 0, 0 };
        return;
    }
    surface_blit(&us, 0, 0, screen, r.x, r.y, r.w, r.h);
}

void pointer_restore(struct pointer_under *u, struct surface *screen)
{
    struct surface us;
    if (under_surface(&us, u, screen))
        surface_blit(screen, u->r.x, u->r.y, &us, 0, 0, u->r.w, u->r.h);
    u->r = (struct rect){ 0, 0, 0, 0 };
}

/* Each row in runs of one colour, each run one fill (clipped by it). */
void pointer_paint(struct surface *screen, const struct pointer_sprite *s, int32_t x, int32_t y)
{
    for (uint32_t row = 0; row < s->h; row++) {
        const uint8_t *px = &s->px[row * s->w];
        uint32_t i = 0;
        while (i < s->w) {
            uint32_t j = i + 1;
            while (j < s->w && px[j] == px[i])
                j++;
            if (px[i] != POINTER_NONE)
                surface_fill(screen, (struct rect){ x + (int32_t)i, y + (int32_t)row, (int32_t)(j - i), 1 },
                             pointer_colour(px[i]));
            i = j;
        }
    }
}
