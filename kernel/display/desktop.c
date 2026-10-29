/*
 * The desktop, the kernel side (1.3 stage 3); see desktop.h.
 */
#include "kernel/display/desktop.h"
#include "drivers/mouse/ps2mouse.h"
#include "drivers/vga/vga_text.h"
#include "kernel/arch/x86/cmos.h"
#include "kernel/display/compositor.h"
#include "kernel/display/desktop_core.h"
#include "kernel/display/display.h"
#include "kernel/display/fbcon.h"
#include "kernel/display/fbcon_core.h"
#include "kernel/display/pointer.h"
#include "kernel/display/ui_about.h"
#include "kernel/display/ui_console.h"
#include "kernel/display/ui_icon.h"
#include "kernel/display/ui_window.h"
#include "kernel/kernel.h"
#include "kernel/kprintf.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/panic.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/thread.h"
#include "kernel/rtc_time.h"
#include "kernel/timer.h"

typedef unsigned long long ull;

static bool wanted, logging, nocache;
static bool showing;
static struct desktop_layout lay;
static struct wallpaper wp;
static struct compositor comp;
static struct surface *back;
static struct surface cache;
static bool cached;
static char clock[DESKTOP_CLOCK_SIZE];
static uint32_t clock_second = 0xFFFFFFFFu;

/* 1.3 stage 4a: the windows (ui_window.h) and their surfaces. */
struct desk_window {
    struct ui_window w;         /* first: the manager's windows are these */
    struct surface surf;
    bool has_surface;
    bool painted;               /* the surface painted whole once */
};

static bool nosurface, about_at_start;
static bool desk_panic;                 /* 1.3 stage 4b: the test option desktoppanic */
static struct ui_wm wm;
static struct ui_fonts fonts;
static uint32_t scale;
static struct desk_window console_win, about_win;
static struct ui_about about;
static uint16_t about_scratch[UI_ABOUT_SCRATCH];
static char loader_name[UI_ABOUT_VALUE_MAX] = "-";
static char about_display[24], about_memory[24];
static uint32_t usable_mb;

/* 1.3 stage 4b: the frame log, throttled (desktop.h): the last frame
 * composed, whether its marker is still to be logged, the frames composed
 * since the last marker and the longest composition among them (TSC
 * cycles, turned into microseconds by the TSC's rate since the desktop
 * started), and the mouse bytes last seen (desk_settled). */
static uint32_t desk_frame, desk_burst_frames, desk_burst_first;
static bool desk_unlogged;
/* ... and the frames composed since a drag began, until the first settled
 * frame after it ended (desktoplog: one report-only line then). */
static uint32_t desk_drag_frames;
static uint64_t desk_drag_longest;
static bool desk_in_drag;
static uint64_t desk_burst_longest, desk_tsc0, desk_tick0, desk_bytes_tick;
static uint32_t desk_bytes_seen;

void desktop_options(bool w, bool l, bool n, bool ns, bool about_opt, bool panic_opt)
{
    wanted = w;
    logging = l;
    nocache = n;
    nosurface = ns;
    about_at_start = about_opt;
    desk_panic = panic_opt;
}

void desktop_about_info(const char *loader, uint32_t mb)
{
    if (loader && loader[0])
        ksnprintf(loader_name, sizeof(loader_name), "%s", loader);
    usable_mb = mb;
}

bool desktop_showing(void)
{
    return showing;
}

static void read_clock(char out[DESKTOP_CLOCK_SIZE])
{
    struct rtc_raw raw;
    bool ok = cmos_rtc_read(&raw);
    rtc_clock_text(&raw, ok, out);
}

/* ---- The layers and the present step (compositor.h) ---- */

static void background_layer(void *ctx, struct surface *b, struct rect r)
{
    (void)ctx;
    if (cached)
        surface_blit(b, r.x, r.y, &cache, r.x, r.y, r.w, r.h);
    else
        desktop_draw_background(b, &lay, &wp, r);
}

/* 1.3 stage 4a: the open windows, back to front: each one's surface through
 * its rounded shape, or, without a surface, the window painted here,
 * clipped to the damaged rectangle (docs/plans/1.3.md, C4, A1). */
static void windows_layer(void *ctx, struct surface *b, struct rect r)
{
    (void)ctx;
    for (uint32_t i = 0; i < wm.n; i++) {
        struct desk_window *d = (struct desk_window *)wm.z[i];
        struct ui_canvas c = { b, ui_rect_intersect(r, d->w.r) };
        if (ui_rect_empty(c.clip))
            continue;
        if (d->has_surface)
            ui_blit_round(&c, d->w.r, &d->surf, ui_window_radius(scale), UI_CORNERS_ALL);
        else
            ui_window_draw(&c, &d->w, d->w.r, wm.focus == &d->w, d->w.close, scale, &fonts);
    }
}

static void taskbar_layer(void *ctx, struct surface *b, struct rect r)
{
    (void)ctx;
    desktop_draw_taskbar(b, &lay, r, clock);
}

/* The only copy to the framebuffer while the desktop shows; the pointer
 * stays on top (1.2's hooks). */
static void present(void *ctx, struct rect r)
{
    (void)ctx;
    bool hid = pointer_hide_over(r);
    surface_blit(display_front_buffer(), r.x, r.y, back, r.x, r.y, r.w, r.h);
    if (hid)
        pointer_unhide();
}

/* 1.3 stage 4a: a window's surface painted again where its look changed:
 * whole the first time, then its title bar and edges (with the corners'
 * squares). Each pixel gets the same value whatever the clip. */
static void refresh_surfaces(void)
{
    for (uint32_t i = 0; i < wm.n; i++) {
        struct desk_window *d = (struct desk_window *)wm.z[i];
        if (!d->w.dirty)
            continue;
        d->w.dirty = false;
        if (!d->has_surface)
            continue;
        int32_t w = d->w.w, h = d->w.h, R = ui_window_radius(scale), tb = ui_window_title_h(scale);
        struct rect whole = { 0, 0, w, h };
        struct rect parts[4] = {
            { 0, 0, w, tb > R ? tb : R }, { 0, 0, R, h }, { w - R, 0, R, h }, { 0, h - R, w, R },
        };
        int n = d->painted ? 4 : 1;
        for (int k = 0; k < n; k++) {
            struct ui_canvas c;
            ui_canvas_init(&c, &d->surf);
            ui_canvas_clip(&c, d->painted ? parts[k] : whole);
            ui_window_draw(&c, &d->w, whole, wm.focus == &d->w, d->w.close, scale, &fonts);
        }
        d->painted = true;
    }
}

/* 1.3 stage 4b: no mouse byte waiting: every byte queued is in a decoded
 * packet or was a bad byte (the queue empty, no packet begun); or no byte
 * has come for half a second (a dropped byte may leave a packet begun). */
static bool desk_settled(void)
{
    if (!mouse_present())
        return true;
    struct mouse_counts c;
    mouse_counts(&c);
    uint64_t now = timer_ticks();
    if (c.irq_bytes != desk_bytes_seen) {
        desk_bytes_seen = c.irq_bytes;
        desk_bytes_tick = now;
    }
    return c.queued == 3u * c.packets + c.bad_bytes || now - desk_bytes_tick >= 50;
}

/* The marker of the last frame composed (test=desktop boots only, stage
 * 3) and the clock it shows; with desktoplog, first the burst's line when
 * more than one frame was composed since the last marker (report only). */
static void desk_log_frame(void)
{
    if (logging && desk_burst_frames > 1) {
        uint64_t ticks = timer_ticks() - desk_tick0;
        uint64_t per = ticks ? (fd_tsc() - desk_tsc0) / ticks : 0;
        kserialf("desktop: burst: %u frames composed (frames %u to %u), the longest composition %llu us (%llu "
                 "TSC cycles; %llu cycles per tick over %llu ticks)\n", desk_burst_frames, desk_burst_first,
                 desk_frame, (ull)fd_cycles_us(desk_burst_longest, per), (ull)desk_burst_longest, (ull)per,
                 (ull)ticks);
    }
    if (desk_in_drag && !wm.drag) {
        if (logging) {
            uint64_t ticks = timer_ticks() - desk_tick0;
            uint64_t per = ticks ? (fd_tsc() - desk_tsc0) / ticks : 0;
            kserialf("desktop: frames during the drag: %u composed, the longest composition %llu us (%llu TSC "
                     "cycles)\n", desk_drag_frames, (ull)fd_cycles_us(desk_drag_longest, per),
                     (ull)desk_drag_longest);
        }
        desk_in_drag = false;
        desk_drag_frames = 0;
        desk_drag_longest = 0;
    }
    desk_burst_frames = 0;
    desk_burst_longest = 0;
    desk_unlogged = false;
    klogf("display: frame %u\n", desk_frame);
    kserialf("desktop: clock %s\n", clock);
    /* 1.3 stage 4b: desktoppanic, once the Console window's frame is shown. */
    if (desk_panic && console_win.w.open) {
        klogf("Desktop fault test: a panic with the Console window open (test option desktoppanic)\n");
        panic("Desktop test: a panic with the Console window open (on purpose).");
    }
}

/* One composition of whatever is damaged; its frame is logged only once
 * settled (1.3 stage 4b): frames composed while mouse bytes wait are
 * counted, and the marker comes with the first settled pass. */
static void compose(void)
{
    refresh_surfaces();
    if (comp_pending(&comp)) {
        struct rect first = comp.damage.r[0];
        uint64_t t0 = fd_tsc();
        uint32_t n = comp_compose(&comp);
        uint64_t took = fd_tsc() - t0;
        desk_frame = display_frame_done();
        if (!desk_burst_frames)
            desk_burst_first = desk_frame;
        desk_burst_frames++;
        if (took > desk_burst_longest)
            desk_burst_longest = took;
        if (wm.drag)
            desk_in_drag = true;
        if (desk_in_drag) {
            desk_drag_frames++;
            if (took > desk_drag_longest)
                desk_drag_longest = took;
        }
        desk_unlogged = true;
        if (logging)
            kserialf("desktop: composed %u rectangle%s, the first at (%d, %d) %dx%d\n", n, n == 1 ? "" : "s",
                     first.x, first.y, first.w, first.h);
    }
    if (desk_unlogged && desk_settled())
        desk_log_frame();
}

/* ---- The window events (desktoplog) ---- */

static const char *const close_states[UI_BUTTON_STATES] = { "normal", "hover", "pressed" };

static void log_events(void)
{
    if (!logging) {
        ui_wm_clear_events(&wm);
        return;
    }
    for (uint32_t i = 0; i < wm.n_ev; i++) {
        const struct ui_wm_event *e = &wm.ev[i];
        const char *name = e->w ? e->w->name : "";
        switch (e->kind) {
        case UI_EV_OPEN:
            kserialf("desktop: open %s at (%d, %d) %dx%d\n", name, e->r.x, e->r.y, e->r.w, e->r.h);
            break;
        case UI_EV_RAISE:
            kserialf("desktop: raise %s\n", name);
            break;
        case UI_EV_FOCUS:
            kserialf("desktop: focus %s\n", name);
            break;
        case UI_EV_UNFOCUS:
            kserialf("desktop: focus none\n");
            break;
        case UI_EV_DRAG:
            kserialf("desktop: drag %s to (%d, %d) %dx%d\n", name, e->r.x, e->r.y, e->r.w, e->r.h);
            break;
        case UI_EV_CLOSE_BUTTON:
            kserialf("desktop: close %s (the close button)\n", name);
            break;
        case UI_EV_CLOSE_ESC:
            kserialf("desktop: close %s (Esc)\n", name);
            break;
        case UI_EV_CLOSE_STATE:
            kserialf("desktop: close button of %s: %s\n", name, close_states[e->state]);
            break;
        }
    }
    if (wm.lost_ev) {
        kserialf("desktop: %u window events not logged (the list was full)\n", wm.lost_ev);
        wm.lost_ev = 0;
    }
    ui_wm_clear_events(&wm);
}

/* ---- The background cache (slot 506's third area) ---- */

/* Maps `pages` frames of free RAM from page `first` of the area; on a
 * failure every frame mapped is returned and *why says why. */
static bool map_pages(uint32_t first, uint32_t pages, uint32_t *tables, const char **why, uint32_t *done)
{
    uint32_t before = paging_stats()->page_tables;
    uint64_t flags = PTE_WRITE | paging_nx_flag(), base = VM_DESKTOP_BASE + (uint64_t)first * VM_PAGE_SIZE;
    *why = 0;
    for (*done = 0; *done < pages; (*done)++) {
        uint32_t frame = memory_alloc_page();
        if (!frame) {
            *why = "no free page";
            break;
        }
        enum vmm_result r = paging_map(base + (uint64_t)*done * VM_PAGE_SIZE, frame, flags);
        if (r != VMM_OK) {
            memory_free_page(frame);
            *why = vmm_result_str(r);
            break;
        }
    }
    *tables = paging_stats()->page_tables - before;
    if (!*why)
        return true;
    for (uint32_t i = 0; i < *done; i++) {
        uint64_t phys = 0;
        if (paging_unmap(base + (uint64_t)i * VM_PAGE_SIZE, &phys) == VMM_OK)
            memory_free_page((uint32_t)phys);
    }
    return false;
}

static bool map_cache(const struct desktop_cache_plan *p, uint32_t *tables)
{
    const char *why;
    uint32_t done;
    if (map_pages(0, p->pages, tables, &why, &done))
        return true;
    klogf("Desktop: background not cached (page %u of %u not mapped: %s; %u frames returned): drawn per "
          "damaged rectangle\n", done + 1, p->pages, why, done);
    return false;
}

static void plan_cache(uint32_t free_pages)
{
    const struct fb_info *fi = display_info();
    const struct fb_plan *fp = display_slot_plan();
    if (nocache || !fp) {
        klogf("Desktop: background not cached (test option desktopnocache): drawn per damaged rectangle\n");
        return;
    }
    struct desktop_cache_plan p;
    if (!desktop_plan_cache(fi->width, fi->height, fi->bytes_per_pixel, fp->bb_pages + fp->tables,
                            free_pages, &p)) {
        klogf("Desktop: background not cached (%u pages over the share): drawn per damaged rectangle\n", p.over);
        return;
    }
    uint32_t tables;
    if (!map_cache(&p, &tables))
        return;
    cached = surface_init(&cache, (void *)(uintptr_t)VM_DESKTOP_BASE, fi->width, fi->height, p.pitch,
                          fi->bytes_per_pixel);
    if (!cached)
        return;
    desktop_draw_background(&cache, &lay, &wp, (struct rect){ 0, 0, (int32_t)fi->width, (int32_t)fi->height });
    klogf("Desktop: slot 506: background cache at 0x%016llx (%u pages of free RAM, pitch %u), %u page tables; "
          "the display's pages %u of a share of %u (a quarter of %u free pages)\n", (ull)VM_DESKTOP_BASE,
          p.pages, p.pitch, tables, p.display, p.share, p.free_pages);
}

/* ---- The window surfaces (1.3 stage 4a; after the cache, C4) ---- */

static void plan_surface(struct desktop_area *area, struct desk_window *d)
{
    const struct fb_info *fi = display_info();
    uint32_t pitch = (uint32_t)d->w.w * fi->bytes_per_pixel;
    uint32_t pages = desktop_surface_pages((uint32_t)d->w.w, (uint32_t)d->w.h, fi->bytes_per_pixel);
    if (nosurface) {
        klogf("Desktop: window %s drawn on exposure (test option desktopnosurface)\n", d->w.name);
        return;
    }
    struct desktop_area_item it;
    if (!desktop_area_plan(area, pages, &it)) {
        klogf("Desktop: window %s drawn on exposure (its surface, %u pages, does not fit the share: %u pages "
              "over)\n", d->w.name, pages, it.over);
        return;
    }
    uint32_t tables, done;
    const char *why;
    if (!map_pages(it.first, pages, &tables, &why, &done)) {
        klogf("Desktop: window %s drawn on exposure (page %u of %u of its surface not mapped: %s; %u frames "
              "returned)\n", d->w.name, done + 1, pages, why, done);
        return;
    }
    desktop_area_take(area, &it);
    uint64_t va = VM_DESKTOP_BASE + (uint64_t)it.first * VM_PAGE_SIZE;
    d->has_surface = surface_init(&d->surf, (void *)(uintptr_t)va, (uint32_t)d->w.w, (uint32_t)d->w.h, pitch,
                                  fi->bytes_per_pixel);
    klogf("Desktop: slot 506: window surface %s at 0x%016llx (%u pages of free RAM, %dx%d, pitch %u), %u page "
          "tables; the display's pages %u of a share of %u\n", d->w.name, (ull)va, pages, d->w.w, d->w.h, pitch,
          tables, it.display, area->share);
}

static void plan_surfaces(uint32_t free_pages)
{
    const struct fb_plan *fp = display_slot_plan();
    if (!fp) {
        klogf("Desktop: windows drawn on exposure (no display plan)\n");
        return;
    }
    struct desktop_area area;
    desktop_area_init(&area, fp->bb_pages + fp->tables, free_pages);
    if (cached)
        area.pages = desktop_surface_pages(cache.width, cache.height, cache.bytes_per_pixel);
    plan_surface(&area, &console_win);
    plan_surface(&area, &about_win);
    uint32_t tables, pages;
    paging_slot_figures(VM_DISPLAY_INDEX, &tables, &pages);
    klogf("Desktop: slot 506 with the window surfaces: %u page tables, %u pages mapped (walked)\n", tables, pages);
}

/* ---- The windows ---- */

/* 1.3 stage 4b: the Console window's client, the grid as vga_shadow holds
 * it (ui_console.h): on its surface when painted whole or its edges
 * again, and on the back buffer when it has no surface (A1). */
static void desk_console_paint(const struct ui_canvas *c, struct rect r, void *ctx)
{
    (void)ctx;
    const struct vga_shadow *v = vga_text_shadow();
    struct ui_console_grid g = { v->cells, v->palette, v->cursor_visible, v->cursor_row, v->cursor_col };
    ui_console_paint(c, r, scale, fonts.regular, &g);
}

/* 1.3 stage 4b: fbcon's changed cells (fbcon_window), in the window's
 * coordinates; nothing while the window is hidden (its surface keeps
 * them, and opening it damages it whole). */
static void desk_console_damage(struct rect r)
{
    if (!console_win.w.open)
        return;
    r.x += console_win.w.r.x;
    r.y += console_win.w.r.y;
    comp_damage(&comp, r);
}

static void setup_windows(const struct fb_info *fi, const struct fbcon_layout *cl)
{
    int32_t w, h;
    ui_fonts_for_scale(scale, &fonts);
    ui_console_size(scale, cl->font, &w, &h);
    /* Field by field (the structs are static, zero already): no struct
     * copy the compiler could turn into a library call. */
    console_win.w.name = "Console";
    console_win.w.title = "Console";
    console_win.w.icon = UI_ICON_CONSOLE;
    console_win.w.w = w;
    console_win.w.h = h;
    console_win.w.paint = desk_console_paint;   /* 1.3 stage 4b */
    ksnprintf(about_display, sizeof(about_display), "%ux%ux%u", fi->width, fi->height, fi->bpp);
    ksnprintf(about_memory, sizeof(about_memory), "%u MB", usable_mb);
    ui_about_setup(&about, scale, &fonts, MADIRE_OS_VERSION, loader_name, about_display, about_memory,
                   about_scratch, UI_ABOUT_SCRATCH);
    ui_about_size(scale, &w, &h);
    about_win.w.name = "About";
    about_win.w.title = UI_ABOUT_TITLE;
    about_win.w.icon = UI_ICON_ABOUT;
    about_win.w.w = w;
    about_win.w.h = h;
    about_win.w.esc_closes = true;
    about_win.w.paint = ui_about_paint;
    about_win.w.ctx = &about;
    size_t need = ui_about_scratch(scale, fi->width, fi->height);
    if (need > UI_ABOUT_SCRATCH)
        klogf("Desktop: the About window's wordmark needs %u coverage entries (%u kept): not drawn whole\n",
              (uint32_t)need, (uint32_t)UI_ABOUT_SCRATCH);
}

/* The test-only way to open a window in stage 4a (stage 5's menus open it). */
static void open_about(void)
{
    if (!ui_wm_open(&wm, &about_win.w))
        return;
    /* Each value in full (the window may show it cut, ui_about.h). */
    const char *full[UI_ABOUT_ROWS] = { MADIRE_OS_VERSION, loader_name, about_display, about_memory,
                                        UI_ABOUT_MODE };
    if (logging)
        for (int i = 0; i < UI_ABOUT_ROWS; i++)
            kserialf("desktop: about %s: %s\n", ui_about_labels[i], full[i]);
    log_events();
}

/* ---- Start, poll, close ---- */

static bool refuse(const char *why)
{
    klogf("Desktop: not started (%s): the text boot screen kept\n", why);
    return false;
}

bool desktop_start(bool console_offered)
{
    if (!wanted)
        return false;
    if (!console_offered)
        return refuse("the console is not offered");
    const struct fbcon_layout *cl = fbcon_current_layout();
    const struct fb_info *fi = display_info();
    back = display_back_buffer();
    if (!back || !cl || fbcon_held())
        return refuse("the console is not on the framebuffer");
    if (!desktop_layout(&lay, fi->width, fi->height, cl->scale, cl->font))
        return refuse("no layout for this screen");
    wallpaper_setup(&wp, fi->width, fi->height, cl->scale);
    read_clock(clock);
    clock_second = timer_uptime_seconds();
    klogf("Desktop: shown on %ux%ux%u (scale %u %%): the wallpaper with the M, the taskbar, the clock %s; "
          "a key opens the Console window (test=desktop, 1.3 stage 4b)\n", fi->width, fi->height, fi->bpp,
          cl->scale * 25, clock);
    scale = cl->scale;
    setup_windows(fi, cl);
    uint32_t free_pages = pmm_free_pages();
    plan_cache(free_pages);
    uint32_t tables, pages;
    paging_slot_figures(VM_DISPLAY_INDEX, &tables, &pages);
    klogf("Desktop: slot 506 now: %u page tables, %u pages mapped (walked)\n", tables, pages);
    plan_surfaces(free_pages);
    /* 1.3 stage 4b: from here the console draws into its window (C2). */
    if (!fbcon_window(console_win.has_surface ? &console_win.surf : 0, console_win.w.w, console_win.w.h,
                      desk_console_damage))
        return refuse("the console could not move into its window");
    desk_tsc0 = fd_tsc();
    desk_tick0 = timer_ticks();
    comp_init(&comp, back, present, 0);
    comp_add_layer(&comp, background_layer, 0);
    comp_add_layer(&comp, windows_layer, 0);
    comp_add_layer(&comp, taskbar_layer, 0);
    ui_wm_init(&wm, (int32_t)fi->width, (int32_t)fi->height, lay.bar.y, scale, &comp.damage);
    showing = true;
    comp_damage(&comp, (struct rect){ 0, 0, (int32_t)fi->width, (int32_t)fi->height });
    if (about_at_start) {
        klogf("Desktop: the About window opened at start (test option desktopabout; stage 5's menus open it)\n");
        open_about();
    }
    compose();
    return true;
}

static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void desktop_poll(void)
{
    if (!showing || panic_active || !thread_screen_allowed())
        return;
    uint32_t s = timer_uptime_seconds();
    if (s != clock_second) {
        clock_second = s;
        char now[DESKTOP_CLOCK_SIZE];
        read_clock(now);
        if (!same(now, clock)) {
            for (int i = 0; i < DESKTOP_CLOCK_SIZE; i++)
                clock[i] = now[i];
            comp_damage(&comp, lay.clock_box);
        }
    }
    compose();
}

void desktop_pointer(int32_t x, int32_t y, bool left_down, bool left_up)
{
    if (!showing)
        return;
    ui_wm_pointer(&wm, x, y, left_down, left_up);
    log_events();
}

enum desktop_key_route desktop_key(bool pressed, bool esc)
{
    if (!showing)
        return DESKTOP_KEY_TAKEN;
    enum ui_key_route r = ui_wm_key(&wm, &console_win.w, pressed, esc);
    if (r == UI_KEY_OPENED)
        klogf("Desktop: a key opened the Console window (the key is not typed)\n");
    log_events();
    return r == UI_KEY_OPENED ? DESKTOP_KEY_OPENED : r == UI_KEY_TO_CONSOLE ? DESKTOP_KEY_CONSOLE
                                                                            : DESKTOP_KEY_TAKEN;
}
