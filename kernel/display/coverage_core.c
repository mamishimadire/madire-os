/*
 * Coverage of capsules, the pure part (1.3 stage 2); see coverage_core.h.
 */
#include "kernel/display/coverage_core.h"
#include "kernel/display/wallpaper_core.h"

#define FB    8
#define ONE   256
#define HALF  128

static int64_t min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

bool cov_capsule_setup(struct cov_capsule *c, int64_t x1, int64_t y1, int64_t x2, int64_t y2,
                       int64_t r, uint32_t width, uint32_t height)
{
    int64_t pad = r + 2 * ONE;
    c->x1 = x1;
    c->y1 = y1;
    c->dx = x2 - x1;
    c->dy = y2 - y1;
    c->l2 = c->dx * c->dx + c->dy * c->dy;
    c->r = r;
    c->bx0 = max64((min64(x1, x2) - pad) >> FB, 0);
    c->by0 = max64((min64(y1, y2) - pad) >> FB, 0);
    c->bx1 = min64((max64(x1, x2) + pad) >> FB, (int64_t)width - 1);
    c->by1 = min64((max64(y1, y2) + pad) >> FB, (int64_t)height - 1);
    return c->bx0 <= c->bx1 && c->by0 <= c->by1;
}

int64_t cov_capsule_at(const struct cov_capsule *c, int64_t x, int64_t y)
{
    int64_t px = (x << FB) + HALF - c->x1, py = (y << FB) + HALF - c->y1, t = 0;
    if (c->l2 != 0) {
        int64_t d = px * c->dx + py * c->dy;
        if (d >= c->l2)
            t = ONE;
        else if (d > 0)
            t = (d << FB) / c->l2;
    }
    int64_t ox = px - wp_mul(t, c->dx), oy = py - wp_mul(t, c->dy);
    int64_t a = c->r + HALF - (int64_t)wp_isqrt((uint64_t)(ox * ox + oy * oy));
    return a <= 0 ? 0 : a > ONE ? ONE : a;
}
