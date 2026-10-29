/*
 * The desktop's pictures and its memory plan, the pure part (1.3 stage 3);
 * see desktop_core.h.
 */
#include "kernel/display/desktop_core.h"
#include "kernel/display/fbinfo_core.h"
#include "kernel/display/tokens.h"
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_rect.h"

#define FB   8
#define PAGE 4096u

bool desktop_layout(struct desktop_layout *l, uint32_t width, uint32_t height, uint32_t scale,
                    const struct font *f)
{
    struct splash_layout sl;
    int32_t bar = ui_px(40, scale);
    l->width = width;
    l->height = height;
    l->scale = scale;
    l->font = f;
    bool laid = splash_layout(&sl, width, height, scale, f->width, f->height);
    l->ls = sl.ls;
    l->lox = sl.lox;
    l->loy = sl.loy;

    l->bar = (struct rect){ 0, (int32_t)height - bar, (int32_t)width, bar };
    l->bar_edge = (struct rect){ 0, (int32_t)height - bar, (int32_t)width, ui_px(1, scale) };
    l->m_cell = (struct rect){ 0, (int32_t)height - bar, bar, bar };
    l->m_px = ui_px(24, scale);
    l->m_x = l->m_cell.x + (bar - l->m_px) / 2;
    l->m_y = l->m_cell.y + (bar - l->m_px) / 2;
    int32_t clock_w = 5 * f->width;
    l->clock_box = (struct rect){ (int32_t)width - ui_px(16, scale) - clock_w,
                                  (int32_t)height - bar + (bar - f->height) / 2, clock_w, f->height };

    /* The logo's bottom (its height, 167 units) above the taskbar. */
    int64_t logo_bottom = sl.title_y - ((int64_t)ui_px(28, scale) << FB);
    return laid && logo_bottom <= ((int64_t)l->bar.y << FB) &&
           l->clock_box.x > l->m_cell.x + l->m_cell.w && l->clock_box.y >= l->bar_edge.y + l->bar_edge.h;
}

void desktop_draw_background(struct surface *s, const struct desktop_layout *l, const struct wallpaper *wp,
                             struct rect clip)
{
    struct ui_canvas c;
    ui_canvas_init(&c, s);
    ui_canvas_clip(&c, clip);
    if (ui_rect_empty(c.clip))
        return;
    wallpaper_render(s, wp, c.clip);
    wp_logo_draw(s, c.clip, l->lox, l->loy, l->ls);
}

void desktop_draw_taskbar(struct surface *s, const struct desktop_layout *l, struct rect clip,
                          const char *clock)
{
    struct ui_canvas c;
    ui_canvas_init(&c, s);
    ui_canvas_clip(&c, clip);
    if (ui_rect_empty(c.clip))
        return;
    ui_fill(&c, l->bar, UI_SURFACE_BAR);
    ui_fill(&c, l->bar_edge, UI_SURFACE_BAR_EDGE);
    ui_icon_draw(&c, UI_ICON_M, l->m_x, l->m_y, l->m_px, UI_ACCENT);
    ui_text(&c, l->font, l->clock_box.x, l->clock_box.y, clock, UI_TEXT);
}

bool desktop_plan_cache(uint32_t width, uint32_t height, uint32_t bytes_per_pixel, uint32_t display_pages,
                        uint32_t free_pages, struct desktop_cache_plan *p)
{
    uint64_t bytes = (uint64_t)width * bytes_per_pixel * height;
    p->pitch = width * bytes_per_pixel;
    p->pages = (uint32_t)((bytes + PAGE - 1) / PAGE);
    p->tables = 1 + (p->pages + 511) / 512;
    p->free_pages = free_pages;
    p->share = free_pages / FBPLAN_SHARE;
    p->display = display_pages + p->pages + p->tables;
    p->fits = p->display <= p->share;
    p->over = p->fits ? 0 : p->display - p->share;
    return p->fits;
}

/* The area's tables with `pages` pages mapped from its page 0. */
static uint64_t area_tables(uint64_t pages)
{
    return pages ? 1 + (pages + 511) / 512 : 0;
}

void desktop_area_init(struct desktop_area *a, uint32_t taken, uint32_t free_pages)
{
    a->taken = taken;
    a->free_pages = free_pages;
    a->share = free_pages / FBPLAN_SHARE;
    a->pages = 0;
}

uint32_t desktop_surface_pages(uint32_t width, uint32_t height, uint32_t bytes_per_pixel)
{
    uint64_t bytes = (uint64_t)width * bytes_per_pixel * height;
    return (uint32_t)((bytes + PAGE - 1) / PAGE);
}

bool desktop_area_plan(const struct desktop_area *a, uint32_t pages, struct desktop_area_item *it)
{
    uint64_t after = (uint64_t)a->pages + pages, display = (uint64_t)a->taken + after + area_tables(after);
    it->pages = pages;
    it->first = a->pages;
    it->tables = (uint32_t)(area_tables(after) - area_tables(a->pages));
    it->display = display > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)display;
    it->fits = pages > 0 && display <= a->share;
    it->over = display > a->share ? it->display - a->share : 0;
    return it->fits;
}

void desktop_area_take(struct desktop_area *a, const struct desktop_area_item *it)
{
    if (it->fits)
        a->pages += it->pages;
}
