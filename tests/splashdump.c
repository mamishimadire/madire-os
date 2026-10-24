/*
 * The kernel's splash as a host tool (1.2 stage 8): kernel/display/
 * wallpaper_core.c draws it with the console fonts (build/font/), as the
 * kernel does on its back buffer, and the screen is written as a PPM (P6).
 * Part static compares it with scripts/make-wallpaper.py --splash, the
 * reference, pixel for pixel.
 *
 *     splashdump WIDTH HEIGHT BYTES_PER_PIXEL PHASE OUT.ppm
 *
 * BYTES_PER_PIXEL is 4 or 3 (the framebuffer formats); PHASE the number of
 * dots lit (0-3), or "ready" (1.2 stage 10): the ready state drawn as the
 * kernel does at System ready, over the splash with no dot lit. Prints one
 * line: the scale, the dots' box and the coverage scratch it needed. Exit
 * status 1 on any error.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "kernel/display/surface_core.c"
#include "kernel/display/fbcon_core.c"
#include "kernel/display/coverage_core.c"
#include "kernel/display/wallpaper_core.c"

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: splashdump WIDTH HEIGHT BYTES_PER_PIXEL PHASE OUT.ppm\n");
        return 1;
    }
    uint32_t w = (uint32_t)atoi(argv[1]), h = (uint32_t)atoi(argv[2]), bpp = (uint32_t)atoi(argv[3]);
    int is_ready = strcmp(argv[4], "ready") == 0;
    uint32_t phase = is_ready ? 0 : (uint32_t)atoi(argv[4]);
    static const struct font *const fonts[4] = { &font_8x16, &font_10x20, &font_12x24, &font_16x32 };
    struct fbcon_layout cl;
    struct splash_layout l;
    struct wallpaper wp;
    struct surface s;
    uint8_t *pix = calloc((size_t)w * h, bpp);
    if (!pix || phase > SPLASH_DOTS || !surface_init(&s, pix, w, h, w * bpp, bpp) ||
        !fbcon_layout(w, h, fonts, &cl) || !splash_layout(&l, w, h, cl.scale, cl.cell_w, cl.cell_h)) {
        fprintf(stderr, "splashdump: no splash for %ux%ux%u, phase %u\n", w, h, bpp * 8, phase);
        return 1;
    }
    size_t entries = splash_scratch_entries(&l, w, h);
    uint16_t *scratch = malloc(entries * sizeof(uint16_t));
    wallpaper_setup(&wp, w, h, cl.scale);
    wallpaper_render(&s, &wp, (struct rect){ 0, 0, (int32_t)w, (int32_t)h });
    if (!scratch || !splash_draw(&s, &l, cl.font, scratch, entries)) {
        fprintf(stderr, "splashdump: the wordmark was not drawn\n");
        return 1;
    }
    splash_draw_dots(&s, &l, phase);
    if (is_ready)
        splash_draw_ready(&s, &l, cl.font, &wp);
    FILE *f = fopen(argv[5], "wb");
    if (!f)
        return 1;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t c = surface_get(&s, x, y);
            fputc((int)(c >> 16 & 0xFF), f);
            fputc((int)(c >> 8 & 0xFF), f);
            fputc((int)(c & 0xFF), f);
        }
    if (fclose(f) != 0)
        return 1;
    printf("SPLASHDUMP %ux%ux%u phase %s: scale %u %%, dots' box (%d, %d) %dx%d, scratch %zu entries", w, h,
           bpp * 8, argv[4], cl.scale * 25, l.dots_box.x, l.dots_box.y, l.dots_box.w, l.dots_box.h, entries);
    if (is_ready)
        printf(", ready box (%d, %d) %dx%d", l.ready_box.x, l.ready_box.y, l.ready_box.w, l.ready_box.h);
    printf("\n");
    return 0;
}
