#ifndef MADIRE_DISPLAY_UI_ABOUT_H
#define MADIRE_DISPLAY_UI_ABOUT_H

/*
 * The About window's content, the pure part (1.3 stage 4a; the Design
 * Guide, section 8, with the owner's answers of 2026-10-29; host unit tests
 * in tests/unit/test_ui_about.c). tests/desktop_ref.py draws the same
 * picture from these rules.
 *
 * The window is 352 x 377 dp (ui_window.h draws its frame, title bar and
 * close button; the title "About Madire OS", the about icon). Inside it,
 * at scale s, every place in dp from the window's top-left corner, made
 * pixels as ui_px(dp, s), all drawn in the canvas's clip:
 *   - the M in the logo's gradient (wallpaper_core.h's wp_logo_draw, as on
 *     the splash) in a box of P = 64 dp at x = (window width - P) / 2,
 *     y = 56 dp (the 32 dp title bar and 24 dp of padding): the logo's
 *     210-unit width spans 56 of the 64 dp, m = fx(56 P, 64 x 210) pixels
 *     per unit; its origin lox = x * 256 + fx(P, 2) - 144 m, loy =
 *     y * 256 + fx(P, 2) - mul(fx(167, 2), m) (24.8);
 *   - the wordmark "MADIRE OS" in `text`, the splash's capsule letters
 *     (wp_line_draw) with a cap height of 16 dp: ts = fx(4 s, 14) pixels
 *     per unit (14 units), radius fx(50 s, 161) for MADIRE and fx(25 s,
 *     161) for OS (the splash's weights in proportion), tracking 6 units,
 *     its cap top at 136 dp, centred: pen = x * 256 + fx(window width, 2) -
 *     (mul(advances, ts) - mul(6 units, ts)) / 2;
 *   - five rows in the console's font (regular), row i at 176 + 28 i dp:
 *     the label at 24 dp in `text.muted`, the value at 136 dp in `text`;
 *     the labels Version, Boot loader, Display, Memory, Mode; the value
 *     cut to its column (ui_text_fit) of ui_px(328) - ui_px(136) pixels
 *     (the 24 dp right padding is never crossed);
 *   - a 1 dp divider in `border` from 24 dp to 328 dp, at 320 dp;
 *   - the footer, the tagline "SIMPLE . SECURE . FURTHER TOGETHER" (each dot
 *     the middle dot, code page 437's 0xFA) in the console's font in `text.muted`,
 *     centred (x = (window width - text width) / 2), at 337 dp.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"

#define UI_ABOUT_ROWS      5
#define UI_ABOUT_VALUE_MAX 64           /* bytes of a value, its NUL included */
#define UI_ABOUT_TITLE     "About Madire OS"
#define UI_ABOUT_MODE      "Live (memory only)"
#define UI_ABOUT_FOOTER    "SIMPLE \xFA SECURE \xFA FURTHER TOGETHER"
#define UI_ABOUT_SCRATCH   4096         /* 16-bit entries for the wordmark's coverage */

extern const char *const ui_about_labels[UI_ABOUT_ROWS];

struct ui_about {
    uint32_t scale;
    struct ui_fonts fonts;
    char values[UI_ABOUT_ROWS][UI_ABOUT_VALUE_MAX];     /* as drawn (cut to the column) */
    uint16_t *scratch;
    size_t entries;
    bool short_scratch;         /* the wordmark was not drawn whole (scratch too small) */
};

/* The window's size at scale s. */
void ui_about_size(uint32_t s, int32_t *w, int32_t *h);

/* The value column's width in pixels at scale s. */
int32_t ui_about_value_px(uint32_t s);

/* text cut to fit max_px pixels in font f: unchanged if it fits; else
 * its longest beginning that fits followed by "..." (ASCII). out holds at
 * most size - 1 bytes and a NUL (text is cut there too). */
void ui_text_fit(const struct font *f, const char *text, int32_t max_px, char *out, size_t size);

/* Sets a up at scale s with fonts f: the values Version, Boot loader,
 * Display and Memory (Mode is UI_ABOUT_MODE), each cut to its column; the
 * wordmark's coverage scratch. */
void ui_about_setup(struct ui_about *a, uint32_t s, const struct ui_fonts *f, const char *version,
                    const char *loader, const char *display, const char *memory, uint16_t *scratch,
                    size_t entries);

/* The 16-bit entries the wordmark needs at scale s (on a screen of
 * width x height; at most UI_ABOUT_SCRATCH at every scale). */
size_t ui_about_scratch(uint32_t s, uint32_t width, uint32_t height);

/* The window's client paint (struct ui_window's paint; ctx a struct ui_about). */
void ui_about_paint(const struct ui_canvas *c, struct rect r, void *ctx);

#endif
