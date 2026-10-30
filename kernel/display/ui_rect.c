/*
 * The toolkit's rectangles and damage list (1.3 stage 2); see ui_rect.h.
 */
#include "kernel/display/ui_rect.h"

static const struct rect empty = { 0, 0, 0, 0 };

static int64_t rect_min(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static int64_t rect_max(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int32_t clamp32(int64_t v)
{
    return v < INT32_MIN ? INT32_MIN : v > INT32_MAX ? INT32_MAX : (int32_t)v;
}

/* The rectangle [x0, x1) x [y0, y1), or the empty one. */
static struct rect span(int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
    if (x1 <= x0 || y1 <= y0)
        return empty;
    int32_t x = clamp32(x0), y = clamp32(y0);
    return (struct rect){ x, y, clamp32(x1 - x), clamp32(y1 - y) };
}

bool ui_rect_empty(struct rect r)
{
    return r.w <= 0 || r.h <= 0;
}

struct rect ui_rect_intersect(struct rect a, struct rect b)
{
    if (ui_rect_empty(a) || ui_rect_empty(b))
        return empty;
    return span(rect_max(a.x, b.x), rect_max(a.y, b.y), rect_min((int64_t)a.x + a.w, (int64_t)b.x + b.w),
                rect_min((int64_t)a.y + a.h, (int64_t)b.y + b.h));
}

struct rect ui_rect_union(struct rect a, struct rect b)
{
    if (ui_rect_empty(a))
        return ui_rect_empty(b) ? empty : b;
    if (ui_rect_empty(b))
        return a;
    return span(rect_min(a.x, b.x), rect_min(a.y, b.y), rect_max((int64_t)a.x + a.w, (int64_t)b.x + b.w),
                rect_max((int64_t)a.y + a.h, (int64_t)b.y + b.h));
}

bool ui_rect_contains(struct rect r, int32_t x, int32_t y)
{
    return !ui_rect_empty(r) && x >= r.x && y >= r.y && (int64_t)x < (int64_t)r.x + r.w &&
           (int64_t)y < (int64_t)r.y + r.h;
}

bool ui_rect_inside(struct rect inner, struct rect outer)
{
    return !ui_rect_empty(inner) && !ui_rect_empty(outer) && inner.x >= outer.x && inner.y >= outer.y &&
           (int64_t)inner.x + inner.w <= (int64_t)outer.x + outer.w &&
           (int64_t)inner.y + inner.h <= (int64_t)outer.y + outer.h;
}

struct rect ui_rect_inflate(struct rect r, int32_t d)
{
    if (ui_rect_empty(r))
        return empty;
    return span((int64_t)r.x - d, (int64_t)r.y - d, (int64_t)r.x + r.w + d, (int64_t)r.y + r.h + d);
}

void ui_damage_init(struct ui_damage *d, uint32_t width, uint32_t height)
{
    d->width = width;
    d->height = height;
    d->n = 0;
}

void ui_damage_clear(struct ui_damage *d)
{
    d->n = 0;
}

void ui_damage_add(struct ui_damage *d, struct rect r)
{
    struct rect screen = { 0, 0, clamp32(d->width), clamp32(d->height) };
    r = ui_rect_intersect(r, screen);
    if (ui_rect_empty(r))
        return;
    for (uint32_t i = 0; i < d->n; i++)
        if (ui_rect_inside(r, d->r[i]))
            return;
    if (d->n < UI_DAMAGE_MAX) {
        d->r[d->n++] = r;
        return;
    }
    for (uint32_t i = 0; i < d->n; i++)
        r = ui_rect_union(r, d->r[i]);
    d->r[0] = r;
    d->n = 1;
}

uint64_t ui_rect_area(struct rect r)
{
    return ui_rect_empty(r) ? 0 : (uint64_t)r.w * (uint64_t)r.h;
}

/* Removes entry i, the later ones moved down one place. */
static void rect_damage_remove(struct ui_damage *d, uint32_t i)
{
    for (; i + 1 < d->n; i++)
        d->r[i] = d->r[i + 1];
    d->n--;
}

void ui_damage_merge_overlaps(struct ui_damage *d)
{
    for (uint32_t i = 0; i < d->n;) {
        if (ui_rect_empty(d->r[i]))
            rect_damage_remove(d, i);
        else
            i++;
    }
    bool merged = true;
    while (merged) {
        merged = false;
        for (uint32_t i = 0; i < d->n && !merged; i++) {
            for (uint32_t j = i + 1; j < d->n && !merged; j++) {
                struct rect u = ui_rect_union(d->r[i], d->r[j]);
                if (ui_rect_area(u) > ui_rect_area(d->r[i]) + ui_rect_area(d->r[j]))
                    continue;
                d->r[i] = u;
                rect_damage_remove(d, j);
                merged = true;
            }
        }
    }
}
