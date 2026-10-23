/*
 * The framebuffer console (1.2 stage 6); see fbcon.h and fbcon_core.h.
 */
#include "kernel/display/fbcon.h"
#include "drivers/vga/vga_text.h"
#include "kernel/display/display.h"
#include "kernel/display/fbcon_core.h"
#include "kernel/kprintf.h"
#include "kernel/panic.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/thread.h"
#include "kernel/timer.h"

#define CELLS (FBCON_COLS * FBCON_ROWS)
#define NOT_DRAWN 0xFFFFFFFFu
#define WITH_CURSOR 0x10000u

static const struct font *const fonts[4] = { &font_8x16, &font_10x20, &font_12x24, &font_16x32 };

static bool active;
static struct fbcon_layout lay;
static struct surface *back;
static uint32_t colour[16];             /* 0xRRGGBB of each slot */
static uint32_t drawn[CELLS];           /* what each cell shows: entry | WITH_CURSOR, or NOT_DRAWN */
static int32_t cursor_at = -1;          /* the cell the cursor is drawn on */
static uint32_t refused;                /* draws refused off thread 0 */
static bool busy, panic_retried;

bool fbcon_active(void)
{
    return active;
}

/* The screen rule, and no drawing inside a draw: re-entered only by a
 * panic while drawing (a fault in the console itself, or another thread's
 * panic while thread 0 was drawing). The crash screen gets one try; a
 * second re-entry means the console itself faults, so it stops (the
 * crash report stays on serial). */
static bool begin_draw(void)
{
    if (!thread_screen_allowed()) {
        refused++;
        return false;
    }
    if (busy) {
        if (!panic_active || panic_retried) {
            active = false;
            return false;
        }
        panic_retried = true;
    }
    busy = true;
    return true;
}

static void end_draw(void)
{
    busy = false;
}

static uint32_t cursor_index(const struct vga_shadow *v)
{
    return v->cursor_visible ? (uint32_t)v->cursor_row * FBCON_COLS + v->cursor_col : NOT_DRAWN;
}

/* Draws cell i on the back buffer if it shows something else; true if it
 * did (the caller presents it). */
static bool draw_cell(const struct vga_shadow *v, uint32_t i)
{
    uint32_t want = v->cells[i] | (cursor_index(v) == i ? WITH_CURSOR : 0);
    if (drawn[i] == want)
        return false;
    uint8_t attr = (uint8_t)(v->cells[i] >> 8);
    fbcon_draw_cell(back, &lay, i / FBCON_COLS, i % FBCON_COLS, (uint8_t)(v->cells[i] & 0xFF),
                    colour[attr & 0x0F], colour[attr >> 4], want & WITH_CURSOR);
    drawn[i] = want;
    return true;
}

static void present_cell(uint32_t i)
{
    struct rect r = fbcon_cell_rect(&lay, i / FBCON_COLS, i % FBCON_COLS);
    surface_blit(display_front_buffer(), r.x, r.y, back, r.x, r.y, r.w, r.h);
}

static void update_cell(uint32_t i)
{
    if (i < CELLS && draw_cell(vga_text_shadow(), i))
        present_cell(i);
}

static void redraw_all(void)
{
    const struct vga_shadow *v = vga_text_shadow();
    for (uint32_t s = 0; s < 16; s++)
        colour[s] = fbcon_rgb6(v->palette[s]);
    surface_fill(back, (struct rect){ 0, 0, (int32_t)back->width, (int32_t)back->height }, 0);
    for (uint32_t i = 0; i < CELLS; i++) {
        drawn[i] = NOT_DRAWN;
        draw_cell(v, i);
    }
    uint32_t c = cursor_index(v);
    cursor_at = c == NOT_DRAWN ? -1 : (int32_t)c;
    display_present();
}

void fbcon_start(void)
{
    const struct fb_info *fi = display_info();
    back = display_back_buffer();
    if (!back || !fbcon_layout(fi->width, fi->height, fonts, &lay)) {
        klogf("Display: console kept in VGA text memory (no layout for %ux%u)\n", fi->width, fi->height);
        return;
    }
    busy = true;
    redraw_all();
    busy = false;
    active = true;
    klogf("Display: console on the framebuffer: 80x25 cells of %ux%u pixels at (%u, %u), scale %u %%\n",
          lay.cell_w, lay.cell_h, lay.x, lay.y, lay.scale * 25);
}

void fbcon_cell(uint32_t i)
{
    if (!begin_draw())
        return;
    update_cell(i);
    end_draw();
}

void fbcon_redraw(void)
{
    if (!begin_draw())
        return;
    redraw_all();
    end_draw();
}

void fbcon_palette(uint8_t slot)
{
    (void)slot;                         /* any slot may be on any cell: all of them */
    fbcon_redraw();
}

void fbcon_cursor(void)
{
    if (!begin_draw())
        return;
    uint32_t c = cursor_index(vga_text_shadow());
    int32_t now = c == NOT_DRAWN ? -1 : (int32_t)c;
    if (cursor_at >= 0 && cursor_at != now)
        update_cell((uint32_t)cursor_at);
    if (now >= 0)
        update_cell((uint32_t)now);
    cursor_at = now;
    end_draw();
}

/* ---- test=fbcon ---- */

#define HOLD_TICKS 300                  /* 3 s at 100 Hz */
#define TIMING_TICKS 100                /* each timing runs at least 1 s */
#define TIMING_MAX 1000                 /* ... or this many times (a stalled timer) */

static uint16_t saved[CELLS];

static void put_entry(uint32_t i, uint16_t e)
{
    vga_put_cell(i / FBCON_COLS, i % FBCON_COLS, (uint8_t)(e & 0xFF), (uint8_t)((e >> 8) & 0x0F),
                 (uint8_t)(e >> 12));
}

/* The marker tests/qmp_fbcon.py stops the guest at, then nothing drawn
 * for HOLD_TICKS; the "over" line tells the screendump was taken in time. */
static void hold(uint32_t frame)
{
    klogf("display: frame %u\n", frame);
    diag_sleep(HOLD_TICKS);
    klogf("fbcon test: hold %u over\n", frame);
}

static void put_text(uint32_t row, uint32_t col, const char *s, uint8_t fg, uint8_t bg)
{
    for (; *s && col < FBCON_COLS; s++, col++)
        vga_put_cell(row, col, (uint8_t)*s, fg, bg);
}

/* Rows 3-18: the 256 characters (16 per row, every other column) in the
 * colours 1-15 in turn on black, and the 16 x 16 colour pairs (a half
 * block and a shade per pair; foreground by column, background by row). */
static void draw_chart(void)
{
    for (uint32_t i = 0; i < CELLS; i++)
        put_entry(i, 0x0720);
    put_text(1, 2, "Code page 437 and the 16 colours on the framebuffer console", 15, 1);
    for (uint32_t r = 0; r < 16; r++)
        for (uint32_t c = 0; c < 16; c++) {
            vga_put_cell(3 + r, 2 + 2 * c, (uint8_t)(r * 16 + c), (uint8_t)(1 + (r + c) % 15), 0);
            vga_put_cell(3 + r, 40 + 2 * c, 0xDF, (uint8_t)c, (uint8_t)r);
            vga_put_cell(3 + r, 41 + 2 * c, 0xB1, (uint8_t)c, (uint8_t)r);
        }
    put_text(21, 2, "The cursor:", 14, 0);
    vga_cursor_move(21, 14);
    vga_cursor_show(true);
}

static uint32_t micros(uint64_t ticks, uint32_t n)
{
    return (uint32_t)(ticks * 10000u / n);
}

static void timing(const struct fb_info *fi)
{
    uint64_t t0 = timer_ticks(), t;
    uint32_t n = 0;
    do {
        fbcon_redraw();
        n++;
        t = timer_ticks() - t0;
    } while (t < TIMING_TICKS && n < TIMING_MAX);
    klogf("fbcon timing: full-screen redraw (%ux%ux%u: the screen cleared, the 2000 cells drawn on "
          "the back buffer, the whole screen presented): %u in %u ticks, about %u us each%s\n",
          fi->width, fi->height, fi->bpp, n, (uint32_t)t, micros(t, n),
          t ? "" : " (the timer did not advance)");

    /* One scroll line as the console draws it: its whole grid through
     * vga_put_cell, every row now one line up (then back down again). */
    t0 = timer_ticks();
    n = 0;
    do {
        for (uint32_t i = 0; i < CELLS; i++)
            put_entry(i, (n & 1) ? saved[i] : i + FBCON_COLS < CELLS ? saved[i + FBCON_COLS] : 0x0720);
        n++;
        t = timer_ticks() - t0;
    } while ((t < TIMING_TICKS || (n & 1)) && n < TIMING_MAX);
    klogf("fbcon timing: one scroll line (the 2000 cells written through vga_put_cell, as the console "
          "redraws its grid; only the changed cells drawn): %u in %u ticks, about %u us each%s\n",
          n, (uint32_t)t, micros(t, n), t ? "" : " (the timer did not advance)");
}

void fbcon_test(void)
{
    if (!active) {
        klogf("fbcon test: the console is not on the framebuffer (see the Display: lines); nothing drawn\n");
        return;
    }
    const struct fb_info *fi = display_info();
    const struct vga_shadow *v = vga_text_shadow();
    klogf("fbcon test: %ux%ux%u, 80x25 cells of %ux%u at (%u, %u); screen 1: the boot screen\n",
          fi->width, fi->height, fi->bpp, lay.cell_w, lay.cell_h, lay.x, lay.y);
    hold(1);

    for (uint32_t i = 0; i < CELLS; i++)
        saved[i] = v->cells[i];
    bool cur_on = v->cursor_visible;
    uint8_t cur_row = v->cursor_row, cur_col = v->cursor_col;
    draw_chart();
    klogf("fbcon test: screen 2: the 256 characters, the 16 x 16 colour pairs, the cursor at row 21, "
          "column 14\n");
    hold(2);

    vga_cursor_show(false);
    timing(fi);
    for (uint32_t i = 0; i < CELLS; i++)
        put_entry(i, saved[i]);
    vga_cursor_move(cur_row, cur_col);
    vga_cursor_show(cur_on);
    klogf("fbcon test: done: 2 screens held, the boot screen back; %u draws refused off thread 0\n", refused);
}
