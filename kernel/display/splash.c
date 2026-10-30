/*
 * The splash (1.2 stage 8); see splash.h and wallpaper_core.h.
 */
#include "kernel/display/splash.h"
#include "kernel/display/display.h"
#include "kernel/display/fbcon.h"
#include "kernel/display/fbcon_core.h"
#include "kernel/display/pointer.h"
#include "kernel/display/wallpaper_core.h"
#include "kernel/kprintf.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

#define DOTS_TICKS  40                  /* 400 ms at 100 Hz */
#define HOLD_TICKS  300                 /* 3 s */
#define TIMING_TICKS 100                /* each timing runs at least 1 s */
#define TIMING_MAX  1000                /* ... or this many times */

static bool verbose, still, allowed, logging, failed;
static bool showing;
static bool ready;                      /* 1.2 stage 10: the ready state, no dots */
static struct wallpaper wp;
static struct splash_layout lay;
static const struct font *font;
static struct surface *back;
/* Static buffers, not the heap: the kernel heap's figures (logged on
 * serial) stay what they are without the splash. The largest need is at
 * scale 8 (200 %): a 43 x 11 dots' box and 5550 coverage entries. */
#define SAVED_BYTES     (64 * 16 * 4)
#define SCRATCH_ENTRIES 8192
static struct surface saved;            /* the dots' box before any dot */
static uint8_t saved_px[SAVED_BYTES];
static uint16_t scratch[SCRATCH_ENTRIES];
static uint32_t phase;
static uint64_t last_tick;              /* the last timed step (0: the timer not seen yet) */

void splash_options(bool v, bool s, bool a, bool l)
{
    verbose = v;
    still = s;
    allowed = a;
    logging = l;
}

bool splash_wanted(void)
{
    if (logging && verbose)
        klogf("Splash: not shown (boot option 'verbose'): the text boot screen\n");
    else if (logging && failed)
        klogf("Splash: not shown (a FAILED status line): the text boot screen\n");
    return allowed && !verbose && !failed && display_active();
}

bool splash_showing(void)
{
    return showing && fbcon_held();
}

/* Copies r of the back buffer to the screen, the pointer kept on top. */
static void present(struct rect r)
{
    bool hid = pointer_hide_over(r);
    surface_blit(display_front_buffer(), r.x, r.y, back, r.x, r.y, r.w, r.h);
    if (hid)
        pointer_unhide();
}

static void draw_dots(void)
{
    struct rect b = lay.dots_box;
    surface_blit(back, b.x, b.y, &saved, 0, 0, b.w, b.h);
    splash_draw_dots(back, &lay, phase);
    present(b);
}

/* The whole splash on the back buffer, the dots' box saved first. */
static void draw_all(void)
{
    struct rect b = lay.dots_box;
    wallpaper_render(back, &wp, (struct rect){ 0, 0, (int32_t)back->width, (int32_t)back->height });
    splash_draw(back, &lay, font, scratch, SCRATCH_ENTRIES);
    surface_blit(&saved, 0, 0, back, b.x, b.y, b.w, b.h);
    splash_draw_dots(back, &lay, phase);
}

/* The console takes the screen back (the text boot screen). */
static void close_splash(const char *why)
{
    if (!showing)
        return;
    showing = false;
    fbcon_release();
    if (logging)
        klogf("Splash: closed (%s): the text boot screen shown\n", why);
}

void splash_start(void)
{
    const struct fb_info *fi = display_info();
    const struct fbcon_layout *cl = fbcon_current_layout();
    back = display_back_buffer();
    bool laid = back && cl && splash_layout(&lay, fi->width, fi->height, cl->scale, cl->cell_w, cl->cell_h);
    if (laid) {
        font = cl->font;
        wallpaper_setup(&wp, fi->width, fi->height, cl->scale);
        uint32_t bpp = back->bytes_per_pixel, w = (uint32_t)lay.dots_box.w, h = (uint32_t)lay.dots_box.h;
        laid = splash_scratch_entries(&lay, fi->width, fi->height) <= SCRATCH_ENTRIES &&
               (uint64_t)w * h * bpp <= SAVED_BYTES && surface_init(&saved, saved_px, w, h, w * bpp, bpp);
    }
    if (!laid) {
        fbcon_release();
        if (logging)
            klogf("Splash: not shown (no layout for it, or larger than its buffers): the text boot screen\n");
        return;
    }
    phase = 0;
    draw_all();
    showing = true;
    uint32_t frame = display_present();
    if (logging)
        klogf("Splash: shown on %ux%ux%u (scale %u %%, frame %u); the dots %s\n", fi->width, fi->height,
              fi->bpp, lay.scale * 25, frame, still ? "frozen (boot option 'still')" : "move with the boot");
}

static void advance(void)
{
    phase = (phase + 1) % (SPLASH_DOTS + 1);
    draw_dots();
}

void splash_step(bool f)
{
    if (f) {
        failed = true;
        close_splash("a FAILED status line");
        return;
    }
    if (!splash_showing() || still)
        return;
    if (timer_ticks() == 0)             /* the timer does not run yet */
        advance();
    else
        splash_tick();
}

void splash_tick(void)
{
    if (!splash_showing() || still || ready || !thread_screen_allowed())
        return;
    uint64_t now = timer_ticks();
    if (now == 0)
        return;
    if (last_tick == 0) {
        last_tick = now;
        return;
    }
    if (now - last_tick >= DOTS_TICKS) {
        last_tick = now;
        advance();
    }
}

void splash_close_key(bool esc)
{
    close_splash(esc ? "Esc" : "a key");
}

void splash_ready(bool console_offered)
{
    if (!splash_showing())
        return;
    if (!console_offered) {
        close_splash("the console is not offered");
        return;
    }
    ready = true;
    splash_draw_ready(back, &lay, font, &wp);
    present(lay.ready_box);
    if (logging)
        klogf("Splash: ready: the dots stopped, \"%s\" shown\n", SPLASH_READY_TEXT);
}

bool splash_handover(void)
{
    if (!splash_showing())
        return false;
    showing = false;
    fbcon_unhold();
    if (logging)
        klogf("Splash: handed over to the desktop: the M kept, the splash's text gone\n");
    return true;
}

/* ---- test=splash ---- */

static uint32_t micros(uint64_t ticks, uint32_t n)
{
    return (uint32_t)(ticks * 10000u / n);
}

static void hold(uint32_t n)
{
    klogf("display: frame %u\n", n);
    diag_sleep(HOLD_TICKS);
    klogf("splash test: hold %u over\n", n);
}

/* Runs `what` until TIMING_TICKS passed (or TIMING_MAX times); logs it. */
static void timing(const char *what, void (*fn)(void))
{
    const struct fb_info *fi = display_info();
    uint64_t t0 = timer_ticks(), t;
    uint32_t n = 0;
    do {
        fn();
        n++;
        t = timer_ticks() - t0;
    } while (t < TIMING_TICKS && n < TIMING_MAX);
    klogf("splash timing: %s (%ux%ux%u, scale %u %%): %u in %u ticks, about %u us each%s\n", what,
          fi->width, fi->height, fi->bpp, lay.scale * 25, n, (uint32_t)t, micros(t, n),
          t ? "" : " (the timer did not advance)");
}

static void time_wallpaper(void)
{
    wallpaper_render(back, &wp, (struct rect){ 0, 0, (int32_t)back->width, (int32_t)back->height });
}

static void time_splash(void)
{
    draw_all();
    present((struct rect){ 0, 0, (int32_t)back->width, (int32_t)back->height });
}

static void time_dots(void)
{
    advance();
}

void splash_test(void)
{
    if (!splash_showing()) {
        klogf("splash test: the splash is not shown (see the Splash: lines); nothing drawn\n");
        return;
    }
    const struct fb_info *fi = display_info();
    klogf("splash test: %ux%ux%u, scale %u %%; the dots' box at (%d, %d), %dx%d\n", fi->width, fi->height,
          fi->bpp, lay.scale * 25, lay.dots_box.x, lay.dots_box.y, lay.dots_box.w, lay.dots_box.h);
    for (uint32_t p = 0; p <= SPLASH_DOTS; p++) {
        phase = p;
        draw_dots();
        klogf("splash test: screen %u: the splash with %u of the 3 dots lit\n", p + 1, p);
        hold(p + 1);
    }
    timing("the wallpaper (every pixel computed into the back buffer)", time_wallpaper);
    timing("the whole splash (the wallpaper, the logo, the wordmark, the text and the dots drawn, the "
           "screen presented)", time_splash);
    timing("one dots update (the dots' box restored, the dots drawn, the box presented)", time_dots);
    klogf("splash test: done: 4 screens held; the splash still shown\n");
}
