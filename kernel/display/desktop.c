/*
 * The desktop, the kernel side (1.3 stage 3); see desktop.h.
 */
#include "kernel/display/desktop.h"
#include "kernel/arch/x86/cmos.h"
#include "kernel/display/compositor.h"
#include "kernel/display/desktop_core.h"
#include "kernel/display/display.h"
#include "kernel/display/fbcon.h"
#include "kernel/display/fbcon_core.h"
#include "kernel/display/pointer.h"
#include "kernel/kprintf.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/panic.h"
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

void desktop_options(bool w, bool l, bool n)
{
    wanted = w;
    logging = l;
    nocache = n;
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

static void compose(void)
{
    if (!comp_pending(&comp))
        return;
    struct rect first = comp.damage.r[0];
    uint32_t n = comp_compose(&comp);
    uint32_t frame = display_frame_done();
    if (logging)
        kserialf("desktop: composed %u rectangle%s, the first at (%d, %d) %dx%d\n", n, n == 1 ? "" : "s",
                 first.x, first.y, first.w, first.h);
    klogf("display: frame %u\n", frame);        /* test=desktop boots only (stage 3) */
    kserialf("desktop: clock %s\n", clock);
}

/* ---- The background cache (slot 506's third area) ---- */

static bool map_cache(const struct desktop_cache_plan *p, uint32_t *tables)
{
    uint32_t before = paging_stats()->page_tables, done = 0;
    uint64_t flags = PTE_WRITE | paging_nx_flag();
    const char *why = 0;
    for (; done < p->pages; done++) {
        uint32_t frame = memory_alloc_page();
        if (!frame) {
            why = "no free page";
            break;
        }
        enum vmm_result r = paging_map(VM_DESKTOP_BASE + (uint64_t)done * VM_PAGE_SIZE, frame, flags);
        if (r != VMM_OK) {
            memory_free_page(frame);
            why = vmm_result_str(r);
            break;
        }
    }
    *tables = paging_stats()->page_tables - before;
    if (!why)
        return true;
    for (uint32_t i = 0; i < done; i++) {
        uint64_t phys = 0;
        if (paging_unmap(VM_DESKTOP_BASE + (uint64_t)i * VM_PAGE_SIZE, &phys) == VMM_OK)
            memory_free_page((uint32_t)phys);
    }
    klogf("Desktop: background not cached (page %u of %u not mapped: %s; %u frames returned): drawn per "
          "damaged rectangle\n", done + 1, p->pages, why, done);
    return false;
}

static void plan_cache(void)
{
    const struct fb_info *fi = display_info();
    const struct fb_plan *fp = display_slot_plan();
    if (nocache || !fp) {
        klogf("Desktop: background not cached (test option desktopnocache): drawn per damaged rectangle\n");
        return;
    }
    struct desktop_cache_plan p;
    if (!desktop_plan_cache(fi->width, fi->height, fi->bytes_per_pixel, fp->bb_pages + fp->tables,
                            pmm_free_pages(), &p)) {
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
    if (!fbcon_hold())
        return refuse("the console could not be held");
    wallpaper_setup(&wp, fi->width, fi->height, cl->scale);
    read_clock(clock);
    clock_second = timer_uptime_seconds();
    klogf("Desktop: shown on %ux%ux%u (scale %u %%): the wallpaper with the M, the taskbar, the clock %s; "
          "a key hands the screen back to the console (test=desktop, 1.3 stage 3)\n", fi->width, fi->height,
          fi->bpp, cl->scale * 25, clock);
    plan_cache();
    uint32_t tables, pages;
    paging_slot_figures(VM_DISPLAY_INDEX, &tables, &pages);
    klogf("Desktop: slot 506 now: %u page tables, %u pages mapped (walked)\n", tables, pages);
    comp_init(&comp, back, present, 0);
    comp_add_layer(&comp, background_layer, 0);
    comp_add_layer(&comp, taskbar_layer, 0);
    showing = true;
    comp_damage(&comp, (struct rect){ 0, 0, (int32_t)fi->width, (int32_t)fi->height });
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

void desktop_close_key(void)
{
    if (!showing)
        return;
    showing = false;
    klogf("Desktop: closed (a key): the screen handed back to the console; the key is not typed\n");
    fbcon_release();
}
