/*
 * The toolkit's colour icons (1.5 stage 12); see ui_cicon.h. The tables
 * are in cicons.c.
 */
#include "kernel/display/ui_cicon.h"
#include "kernel/display/coverage_core.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_rect.h"
#include "kernel/display/ui_trace.h"
#include "kernel/display/wallpaper_core.h"

#define FB 8

const uint32_t cicon_colours[CI_COLOURS] = {
    [CI_ACCENT] = UI_ACCENT,
    [CI_ACCENT_HOVER] = UI_ACCENT_HOVER,
    [CI_ACCENT_LIGHT] = UI_ACCENT_LIGHT,
    [CI_TEXT] = UI_TEXT,
    [CI_TEXT_MUTED] = UI_TEXT_MUTED,
    [CI_TEXT_ON_ACCENT] = UI_TEXT_ON_ACCENT,
    [CI_SURFACE] = UI_SURFACE,
    [CI_BG_DEEP] = UI_BG_DEEP,
    [CI_WARNING] = UI_WARNING,
};

/* Whether the sample at (X, Y) lies inside filled shape sh (points scaled
 * by S = 8 P). */
static bool ci_inside(const struct cicon_shape *sh, int64_t X, int64_t Y, int64_t S)
{
    if (sh->kind == CICON_RRECT) {
        const int16_t *p = cicon_points[sh->first], *q = cicon_points[sh->first + 1u];
        int64_t X0 = p[0] * S, Y0 = p[1] * S, X1 = q[0] * S, Y1 = q[1] * S, R = (int64_t)sh->radius * S;
        if (X < X0 || X >= X1 || Y < Y0 || Y >= Y1)
            return false;
        if (R <= 0)
            return true;
        bool cx_set = true, cy_set = true;
        int64_t cx = 0, cy = 0;
        if (X < X0 + R)
            cx = X0 + R;
        else if (X >= X1 - R)
            cx = X1 - R;
        else
            cx_set = false;
        if (Y < Y0 + R)
            cy = Y0 + R;
        else if (Y >= Y1 - R)
            cy = Y1 - R;
        else
            cy_set = false;
        if (!cx_set || !cy_set)
            return true;
        return (X - cx) * (X - cx) + (Y - cy) * (Y - cy) <= R * R;
    }
    bool in = false;
    uint32_t n = sh->count;
    for (uint32_t e = 0; e < n; e++) {
        const int16_t *a = cicon_points[sh->first + e];
        const int16_t *b = cicon_points[sh->first + (e + 1u) % n];
        int64_t ax = a[0] * S, ay = a[1] * S, bx = b[0] * S, by = b[1] * S;
        if ((ay > Y) == (by > Y))
            continue;
        int64_t lhs = (X - ax) * (by - ay), t = (Y - ay) * (bx - ax);
        if (by > ay ? lhs < t : lhs > t)
            in = !in;
    }
    return in;
}

/* A filled shape (rectangle or polygon) in the box at (x, y), px pixels,
 * grid G, drawn into the pixels of `area` (the box within the clip). True
 * if it covered a pixel. */
static bool ci_fill(const struct ui_canvas *c, const struct cicon_shape *sh, int32_t x, int32_t y, int32_t px,
                    struct rect area, int64_t G, uint32_t rgb)
{
    int64_t S = 8 * (int64_t)px, minx = 0, miny = 0, maxx = 0, maxy = 0;
    for (uint32_t k = 0; k < sh->count; k++) {
        const int16_t *p = cicon_points[sh->first + k];
        if (k == 0 || p[0] < minx)
            minx = p[0];
        if (k == 0 || p[0] > maxx)
            maxx = p[0];
        if (k == 0 || p[1] < miny)
            miny = p[1];
        if (k == 0 || p[1] > maxy)
            maxy = p[1];
    }
    /* The pixels the shape may cover (one more on each side), clipped. */
    struct rect bb = { x + (int32_t)(minx * px / G) - 1, y + (int32_t)(miny * px / G) - 1, 0, 0 };
    bb.w = x + (int32_t)((maxx * px + G - 1) / G) + 1 - bb.x + 1;
    bb.h = y + (int32_t)((maxy * px + G - 1) / G) + 1 - bb.y + 1;
    bb = ui_rect_intersect(bb, area);
    bool any = false;
    for (int64_t py = bb.y; py < (int64_t)bb.y + bb.h; py++) {
        int64_t ry = py - y;
        for (int64_t qx = bb.x; qx < (int64_t)bb.x + bb.w; qx++) {
            int64_t rx = qx - x, n = 0;
            for (int64_t j = 0; j < 4; j++)
                for (int64_t i = 0; i < 4; i++)
                    n += ci_inside(sh, (8 * rx + 2 * i + 1) * G, (8 * ry + 2 * j + 1) * G, S);
            if (n) {
                ui_blend(c, qx, py, rgb, 16 * n);
                any = true;
            }
        }
    }
    return any;
}

/* A stroke shape: its capsules, a pixel's coverage the largest of them. */
static bool ci_stroke(const struct ui_canvas *c, const struct cicon_shape *sh, int32_t x, int32_t y, int32_t px,
                      struct rect area, int64_t G, uint32_t rgb)
{
    struct cov_capsule cap[CICON_STROKE_MAX];
    bool ok[CICON_STROKE_MAX];
    uint32_t n = sh->count;
    if (n < 1 || n > CICON_STROKE_MAX)
        return false;
    uint32_t segs = n == 1 ? 1u : n - 1u;
    for (uint32_t k = 0; k < CICON_STROKE_MAX; k++)
        ok[k] = false;
    int64_t ox = (int64_t)x << FB, oy = (int64_t)y << FB, r = wp_fx(8 * (int64_t)px, G);
    for (uint32_t k = 0; k < segs && k < CICON_STROKE_MAX; k++) {
        const int16_t *a = cicon_points[sh->first + k];
        const int16_t *b = cicon_points[sh->first + (n == 1 ? k : k + 1u)];
        ok[k] = cov_capsule_setup(&cap[k], ox + wp_fx(a[0] * (int64_t)px, G), oy + wp_fx(a[1] * (int64_t)px, G),
                                  ox + wp_fx(b[0] * (int64_t)px, G), oy + wp_fx(b[1] * (int64_t)px, G), r,
                                  c->s->width, c->s->height);
    }
    uint16_t row[UI_ICON_MAX_PX];
    bool any = false;
    for (int64_t py = area.y; py < (int64_t)area.y + area.h; py++) {
        for (int32_t i = 0; i < area.w && i < UI_ICON_MAX_PX; i++)
            row[i] = 0;
        for (uint32_t k = 0; k < segs && k < CICON_STROKE_MAX; k++) {
            if (!ok[k] || py < cap[k].by0 || py > cap[k].by1)
                continue;
            int64_t x0 = cap[k].bx0 > area.x ? cap[k].bx0 : area.x;
            int64_t x1 = cap[k].bx1 < (int64_t)area.x + area.w - 1 ? cap[k].bx1 : (int64_t)area.x + area.w - 1;
            for (int64_t q = x0; q <= x1; q++) {
                int64_t i = q - area.x;
                if (i < 0 || i >= UI_ICON_MAX_PX)
                    continue;
                int64_t a = cov_capsule_at(&cap[k], q, py);
                if (a > row[i])
                    row[i] = (uint16_t)a;
            }
        }
        for (int32_t i = 0; i < area.w && i < UI_ICON_MAX_PX; i++)
            if (row[i]) {
                ui_blend(c, (int64_t)area.x + i, py, rgb, row[i]);
                any = true;
            }
    }
    return any;
}

bool ui_cicon_draw(const struct ui_canvas *c, int icon, int32_t x, int32_t y, int32_t px)
{
    if (icon < UI_CICON_FIRST || icon >= UI_CICON_END || px < 1 || px > UI_ICON_MAX_PX)
        return false;
    struct rect area = ui_rect_intersect(c->clip, (struct rect){ x, y, px, px });
    if (area.w <= 0 || area.h <= 0)
        return true;
    bool big = px > CICON_SMALL_MAX;
    int64_t G = big ? CICON_GRID_48 : CICON_GRID_16;
    const struct cicon_def *d = &cicon_defs[icon - UI_CICON_FIRST][big ? 1 : 0];
#ifdef UI_TRACE
    bool bg_known = x >= 0 && y >= 0 && (uint32_t)x < c->s->width && (uint32_t)y < c->s->height;
    uint32_t bg = bg_known ? surface_get(c->s, (uint32_t)x, (uint32_t)y) : 0;
#endif
    for (uint32_t k = 0; k < d->shapes; k++) {
        uint32_t si = (uint32_t)d->first + k;
        if (si >= cicon_shape_count)
            break;
        const struct cicon_shape *sh = &cicon_shapes[si];
        if (sh->colour >= CI_COLOURS || (uint32_t)sh->first + sh->count > cicon_point_count)
            continue;
        uint32_t rgb = cicon_colours[sh->colour];
        bool any = sh->kind == CICON_STROKE ? ci_stroke(c, sh, x, y, px, area, G, rgb)
                                            : ci_fill(c, sh, x, y, px, area, G, rgb);
#ifdef UI_TRACE
        if (any && sh->trace == CI_TRACE_BG && bg_known)
            UI_TRACE_PAIR(UI_TRACE_ICON, rgb, bg);
        else if (any && sh->trace < CI_COLOURS)
            UI_TRACE_PAIR(UI_TRACE_ICON, rgb, cicon_colours[sh->trace]);
#else
        (void)any;
#endif
    }
    return true;
}
