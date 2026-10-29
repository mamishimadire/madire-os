#ifndef MADIRE_DISPLAY_DESKTOP_CORE_H
#define MADIRE_DISPLAY_DESKTOP_CORE_H

/*
 * The desktop's pictures and its memory plan, the pure part (1.3 stage 3;
 * host unit tests in tests/unit/test_ui_desktop.c; the kernel side is
 * desktop.c). tests/desktop_ref.py draws the same pictures from the rules
 * below, independently.
 *
 * The background (the owner's decision in stage 3b, in place of the Design
 * Guide v0.2's section 6 composition): the wallpaper (wallpaper_core.h)
 * with the M logo alone, at the splash's place and size (splash_layout
 * with the console's scale and cell size; the logo's gradient as on the
 * splash). No wordmark, tagline or hint: no text lies on the wallpaper.
 *
 * The taskbar (Design Guide, section 10): bar = 40 dp along the bottom,
 * full width, in `surface.bar`; its top 1 dp row in `surface.bar.edge`;
 * the M button's cell the bar's first 40 x 40 dp, in its normal state
 * (nothing drawn under the M, so the bar shows); the M (24 dp) centred in
 * the cell (at (bar - P) / 2, rounded down, from its corner) in `accent`,
 * as ui_icon.h draws it; the clock "HH:MM" in the console's font in
 * `text`, its right edge 16 dp from the screen's, at y = the bar's top +
 * (bar - font height) / 2. The clock's box is the five cells. Sizes in
 * dp become pixels as (dp x s + 2) / 4.
 *
 * Every drawing call paints only inside its clip, and each pixel gets the
 * same value whatever the clip (so a damaged rectangle drawn again gives
 * the pixels of the whole screen drawn at once).
 *
 * The background cache (docs/plans/1.3.md, C4; slot 506's third area,
 * VM_DESKTOP_BASE): a W x H surface in the framebuffer's format, pitch
 * W x bytes per pixel, in whole pages, with its page tables (one page
 * directory, the area being 1 GiB aligned, and one page table per 512
 * pages). It is planned when the desktop starts: it fits if the display's
 * pages in all (the back buffer's and slot 506's tables, already taken,
 * plus the cache's pages and tables) stay at or below a quarter of the
 * free pages counted then (FBPLAN_SHARE). Otherwise the background is
 * drawn again for every damaged rectangle.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/font.h"
#include "kernel/display/surface_core.h"
#include "kernel/display/wallpaper_core.h"

#define DESKTOP_CLOCK_SIZE 6        /* "HH:MM" + NUL */

struct desktop_layout {
    uint32_t width, height, scale;
    const struct font *font;        /* the console's (regular) */
    int64_t ls, lox, loy;           /* the logo, as the splash places it */
    struct rect bar, bar_edge, m_cell, clock_box;
    int32_t m_px;                   /* the M's box */
    int32_t m_x, m_y;
};

/* The desktop's places on a width x height screen at scale step `scale`
 * with the console's font `f`. False if the splash has no layout there or
 * the logo or the clock does not fit above or in the taskbar. */
bool desktop_layout(struct desktop_layout *l, uint32_t width, uint32_t height, uint32_t scale,
                    const struct font *f);

/* Draws the background inside clip on s (a screen of the layout's size). */
void desktop_draw_background(struct surface *s, const struct desktop_layout *l, const struct wallpaper *wp,
                             struct rect clip);

/* Draws the taskbar inside clip, the clock showing `clock` (5 characters). */
void desktop_draw_taskbar(struct surface *s, const struct desktop_layout *l, struct rect clip,
                          const char *clock);

struct desktop_cache_plan {
    uint32_t pitch;                 /* width x bytes per pixel */
    uint32_t pages;                 /* the cache's frames */
    uint32_t tables;                /* its page directory and page tables */
    uint32_t free_pages;            /* counted when the desktop starts */
    uint32_t share;                 /* free_pages / FBPLAN_SHARE */
    uint32_t display;               /* the display's pages in all, with the cache */
    uint32_t over;                  /* display - share when it does not fit, else 0 */
    bool fits;
};

/* The plan for a width x height cache at `bytes_per_pixel`, with
 * `display_pages` (the back buffer's pages and slot 506's tables) already
 * taken and `free_pages` free. Returns plan->fits. */
bool desktop_plan_cache(uint32_t width, uint32_t height, uint32_t bytes_per_pixel, uint32_t display_pages,
                        uint32_t free_pages, struct desktop_cache_plan *plan);

#endif
