/*
 * The toolkit's stroke icons (1.3 stage 2); see ui_icon.h.
 */
#include "kernel/display/ui_icon.h"
#include "kernel/display/coverage_core.h"
#include "kernel/display/icons.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_trace.h"
#include "kernel/display/wallpaper_core.h"
#include "splash_data.h"            /* generated: build/gen/ (the Makefile) */

#define FB 8

/* The logo's box in its units: x 39 to 249 (centre 144), height 167. */
#define LOGO_W  210
#define LOGO_CX 144
#define LOGO_H  167

/* An icon placed in its box: what every capsule needs. */
struct placed {
    const struct ui_canvas *c;
    int icon;
    int64_t ox, oy;             /* the box's top-left, 24.8 */
    int32_t px;
    int64_t m;                  /* the M's pixels per logo unit, 24.8 */
};

static int segments(const struct placed *p)
{
    if (p->icon == UI_ICON_M)
        return 4;
    const struct icon_def *d = &icon_defs[p->icon - 1];
    int n = 0;
    for (uint32_t i = 0; i < d->polys; i++) {
        uint32_t count = icon_polys[d->first_poly + i].count;
        n += count == 1 ? 1 : (int)count - 1;
    }
    return n;
}

/* Capsule k of the placed icon, set up and clipped to the surface. */
static bool capsule(const struct placed *p, int k, struct cov_capsule *cap)
{
    const struct surface *s = p->c->s;
    if (p->icon == UI_ICON_M) {
        const int32_t *st = splash_logo_strokes[k];
        int64_t cx = wp_fx(p->px, 2), half_h = wp_fx(LOGO_H, 2);
        return cov_capsule_setup(cap, p->ox + cx + (st[0] - LOGO_CX) * p->m,
                                 p->oy + cx + wp_mul(wp_fx(st[1] * SPLASH_LOGO_RX, SPLASH_LOGO_RY) - half_h, p->m),
                                 p->ox + cx + (st[2] - LOGO_CX) * p->m,
                                 p->oy + cx + wp_mul(wp_fx(st[3] * SPLASH_LOGO_RX, SPLASH_LOGO_RY) - half_h, p->m),
                                 SPLASH_LOGO_RX * p->m, s->width, s->height);
    }
    const struct icon_def *d = &icon_defs[p->icon - 1];
    for (uint32_t i = 0; i < d->polys; i++) {
        const struct icon_poly *poly = &icon_polys[d->first_poly + i];
        int n = poly->count == 1 ? 1 : (int)poly->count - 1;
        if (k >= n) {
            k -= n;
            continue;
        }
        const int16_t *a = icon_points[poly->first + k];
        const int16_t *b = icon_points[poly->first + (poly->count == 1 ? k : k + 1)];
        return cov_capsule_setup(cap, p->ox + wp_fx(a[0] * p->px, ICON_GRID),
                                 p->oy + wp_fx(a[1] * p->px, ICON_GRID),
                                 p->ox + wp_fx(b[0] * p->px, ICON_GRID),
                                 p->oy + wp_fx(b[1] * p->px, ICON_GRID),
                                 wp_fx(ICON_RADIUS * p->px, ICON_GRID), s->width, s->height);
    }
    return false;
}

bool ui_icon_draw(const struct ui_canvas *c, int icon, int32_t x, int32_t y, int32_t px, uint32_t rgb)
{
    if (icon < 0 || icon >= UI_ICON_COUNT || px < 1 || px > UI_ICON_MAX_PX)
        return false;
    struct placed p = { c, icon, (int64_t)x << FB, (int64_t)y << FB, px, wp_fx(22 * (int64_t)px, 24 * LOGO_W) };
    int n = segments(&p);
    /* The box, clipped. */
    struct rect box = ui_rect_intersect(c->clip, (struct rect){ x, y, px, px });
    if (box.w <= 0 || box.h <= 0)
        return true;
    uint16_t row[UI_ICON_MAX_PX];
    for (int64_t py = box.y; py < (int64_t)box.y + box.h; py++) {
        for (int32_t i = 0; i < box.w; i++)
            row[i] = 0;
        for (int k = 0; k < n; k++) {
            struct cov_capsule cap;
            if (!capsule(&p, k, &cap) || py < cap.by0 || py > cap.by1)
                continue;
            int64_t x0 = cap.bx0 > box.x ? cap.bx0 : box.x;
            int64_t x1 = cap.bx1 < (int64_t)box.x + box.w - 1 ? cap.bx1 : (int64_t)box.x + box.w - 1;
            for (int64_t px_ = x0; px_ <= x1; px_++) {
                int64_t a = cov_capsule_at(&cap, px_, py);
                if (a > row[px_ - box.x])
                    row[px_ - box.x] = (uint16_t)a;
            }
        }
        for (int32_t i = 0; i < box.w; i++)
            if (row[i]) {
                UI_TRACE_PAIR(UI_TRACE_ICON, rgb, surface_get(c->s, (uint32_t)(box.x + i), (uint32_t)py));
                ui_blend(c, (int64_t)box.x + i, py, rgb, row[i]);
            }
    }
    return true;
}
