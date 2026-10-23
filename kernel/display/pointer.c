/*
 * The mouse on thread 0 (1.2 stage 7); see pointer.h and pointer_core.h.
 */
#include "kernel/display/pointer.h"
#include "drivers/mouse/ps2mouse.h"
#include "kernel/display/display.h"
#include "kernel/display/fbcon.h"
#include "kernel/display/fbcon_core.h"
#include "kernel/display/pointer_core.h"
#include "kernel/kprintf.h"
#include "kernel/panic.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

static bool on_screen;                  /* a pointer on the framebuffer console */
static bool logging;
static bool shown;                      /* drawn (or hidden only around a console copy) */
static bool hidden;                     /* hidden by pointer_hide_over until pointer_unhide */
static struct pointer_sprite sprite;
static struct pointer_under under;
static struct surface *front;
static int32_t px, py;                  /* the hot spot */
static int32_t drawn_x, drawn_y;        /* where it is drawn */
static uint32_t width, height;
static uint32_t reported_dropped;

/* test=mouse's sums */
static int64_t sum_dx, sum_dy;
static uint32_t presses[3], releases[3];

void pointer_start(bool log_packets, bool show)
{
    logging = log_packets;
    if (!mouse_present())
        return;
    if (logging)
        klogf("Mouse: packets logged on serial (boot option 'mouselog' or test=mouse)\n");
    else
        klogf("Mouse: packets not logged (the boot option 'mouselog' logs them on serial)\n");
    if (!fbcon_active())
        return;
    if (!show) {                        /* 1.2 stage 10: the console has no pointer */
        klogf("Mouse: no pointer on the console (the test option 'pointer', or test=mouse, draws one)\n");
        return;
    }
    const struct fb_info *fi = display_info();
    width = fi->width;
    height = fi->height;
    front = display_front_buffer();
    if (!front || !pointer_sprite_make(&sprite, fbcon_scale_for_height(height)))
        return;
    px = (int32_t)width / 2;
    py = (int32_t)height / 2;
    on_screen = true;
    klogf("Mouse: pointer %ux%u pixels (scale %u %%), hidden until the first packet, then at (%d, %d) "
          "moved by it\n", sprite.w, sprite.h, sprite.scale * 25, px, py);
}

/* ---- Drawing (thread 0 or the crash screen; the framebuffer only) ---- */

static void draw_at(int32_t x, int32_t y)
{
    if (shown && !hidden)
        pointer_restore(&under, front);
    pointer_save(&under, front, pointer_area(&sprite, x, y, width, height));
    pointer_paint(front, &sprite, x, y);
    drawn_x = x;
    drawn_y = y;
    shown = true;
    hidden = false;
}

static bool overlap(struct rect a, struct rect b)
{
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 &&
           a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool pointer_hide_over(struct rect r)
{
    if (!on_screen || !shown || hidden || !overlap(r, under.r))
        return false;
    pointer_restore(&under, front);
    hidden = true;
    return true;
}

void pointer_unhide(void)
{
    if (!hidden)
        return;
    if (panic_active) {                 /* the crash screen shows no pointer */
        shown = false;
        hidden = false;
        on_screen = false;
        return;
    }
    draw_at(drawn_x, drawn_y);
}

/* ---- Packets ---- */

static char sign(int32_t v)
{
    return v < 0 ? '-' : '+';
}

static uint32_t magnitude(int32_t v)
{
    return v < 0 ? (uint32_t)-v : (uint32_t)v;
}

static void buttons_str(char *out, uint8_t b)
{
    out[0] = (b & MOUSE_B0_LEFT) ? 'L' : '-';
    out[1] = (b & MOUSE_B0_MIDDLE) ? 'M' : '-';
    out[2] = (b & MOUSE_B0_RIGHT) ? 'R' : '-';
    out[3] = 0;
}

/* " pressed LR" and the like: the letters of `b`, in the order L, M, R. */
static void edge_str(char *out, size_t size, const char *what, uint8_t b)
{
    if (!b) {
        out[0] = 0;
        return;
    }
    ksnprintf(out, size, " %s %s%s%s", what, (b & MOUSE_B0_LEFT) ? "L" : "", (b & MOUSE_B0_MIDDLE) ? "M" : "",
              (b & MOUSE_B0_RIGHT) ? "R" : "");
}

static void count_edges(uint8_t b, uint32_t *n)
{
    if (b & MOUSE_B0_LEFT)
        n[0]++;
    if (b & MOUSE_B0_MIDDLE)
        n[1]++;
    if (b & MOUSE_B0_RIGHT)
        n[2]++;
}

static void log_packet(uint32_t n, const struct mouse_event *e)
{
    char held[4], down[16], up[16], at[32];
    buttons_str(held, e->buttons);
    edge_str(down, sizeof(down), "pressed", e->pressed);
    edge_str(up, sizeof(up), "released", e->released);
    at[0] = 0;
    if (on_screen)
        ksnprintf(at, sizeof(at), " at (%d, %d)", px, py);
    kserialf("mouse: packet %u: dx %c%u dy %c%u buttons %s%s%s%s%s\n", n, sign(e->dx), magnitude(e->dx),
             sign(e->dy), magnitude(e->dy), held, down, up, e->overflow ? " overflow (motion dropped)" : "", at);
}

uint32_t pointer_poll(void)
{
    if (!mouse_present())
        return 0;
    struct mouse_event e;
    struct mouse_counts c;
    uint32_t taken = 0;
    while (mouse_next_event(&e)) {
        taken++;
        sum_dx += e.dx;
        sum_dy += e.dy;
        count_edges(e.pressed, presses);
        count_edges(e.released, releases);
        if (on_screen) {
            px += e.dx;
            py += e.dy;
            pointer_clamp(&px, &py, width, height);
        }
        if (logging) {
            mouse_counts(&c);
            log_packet(c.packets, &e);
        }
    }
    if (taken && on_screen && thread_screen_allowed() && !panic_active) {
        draw_at(px, py);
        if (logging)
            kserialf("pointer: drawn at (%d, %d)\n", px, py);
    }
    mouse_counts(&c);
    if (c.dropped != reported_dropped) {
        klogf("Mouse: %u bytes dropped (queue full)\n", c.dropped - reported_dropped);
        reported_dropped = c.dropped;
    }
    return taken;
}

/* ---- test=mouse ---- */

#define QUIET_TICKS 500                 /* the test ends 5 s after the last packet */
#define WAIT_TICKS  6000                /* ... or after 60 s without any */
#define TIMING_TICKS 100                /* the timing runs at least 1 s */
#define TIMING_MAX 100000               /* ... or this many moves (a stalled timer) */

static uint32_t micros(uint64_t ticks, uint32_t n)
{
    return (uint32_t)(ticks * 10000u / n);
}

/* One move as pointer_poll draws it: the pixels under the old place
 * restored, the new ones saved, the sprite drawn; between two places one
 * pixel apart, at the pointer's own position. */
static void timing(void)
{
    const struct fb_info *fi = display_info();
    int32_t x = px, y = py, x2 = x + 1 < (int32_t)width ? x + 1 : x - 1;
    uint64_t t0 = timer_ticks(), t;
    uint32_t n = 0;
    do {
        draw_at((n & 1) ? x : x2, y);
        n++;
        t = timer_ticks() - t0;
    } while ((t < TIMING_TICKS || (n & 1)) && n < TIMING_MAX);
    klogf("pointer timing: one move (%ux%ux%u, the %ux%u sprite at %u %%: the pixels under it restored, the "
          "new ones saved, the sprite drawn): %u in %u ticks, about %u us each%s\n", fi->width, fi->height,
          fi->bpp, sprite.w, sprite.h, sprite.scale * 25, n, (uint32_t)t, micros(t, n),
          t ? "" : " (the timer did not advance)");
    if (logging)
        kserialf("pointer: drawn at (%d, %d)\n", drawn_x, drawn_y);
}

void mouse_test(void)
{
    if (!mouse_present()) {
        klogf("Mouse test: no mouse (see the Mouse: lines); nothing to do\n");
        return;
    }
    klogf("Mouse test: ready: every packet is logged; the test ends %u s after the last packet, or after %u s "
          "without any\n", QUIET_TICKS / 100, WAIT_TICKS / 100);
    uint64_t start = timer_ticks(), last = start;
    uint32_t packets = 0;
    for (;;) {
        uint32_t n = pointer_poll();
        uint64_t now = timer_ticks();
        if (n) {
            packets += n;
            last = now;
        }
        if (packets ? now - last >= QUIET_TICKS : now - start >= WAIT_TICKS)
            break;
        if (!diag_sleep(1))
            break;                      /* a stalled wait (logged): the counts so far */
    }
    struct mouse_counts c;
    mouse_counts(&c);
    klogf("Mouse test: done: %u packets from %u bytes (%u bad bytes, %u overflows, %u dropped: queue full); "
          "IRQ12 delivered %u times: %u bytes read, %u with nothing waiting, %u with a port 1 byte\n",
          c.packets, c.irq_bytes, c.bad_bytes, c.overflows, c.dropped, c.irqs, c.irq_bytes, c.irq_empty,
          c.irq_keyboard);
    klogf("Mouse test: motion dx %c%u dy %c%u; pressed L %u M %u R %u, released L %u M %u R %u\n",
          sum_dx < 0 ? '-' : '+', (uint32_t)(sum_dx < 0 ? -sum_dx : sum_dx), sum_dy < 0 ? '-' : '+',
          (uint32_t)(sum_dy < 0 ? -sum_dy : sum_dy), presses[0], presses[1], presses[2], releases[0],
          releases[1], releases[2]);
    if (on_screen && shown)
        timing();
}
