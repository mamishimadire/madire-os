#ifndef MADIRE_DISPLAY_SURFACE_CORE_H
#define MADIRE_DISPLAY_SURFACE_CORE_H

/*
 * Drawing surfaces, the pure part (1.2 stage 5b; host unit tests in
 * tests/unit/test_surface.c). A surface is a block of pixels in memory:
 * the framebuffer, the back buffer or any buffer of the kernel's own.
 * Nothing here knows where its pixels are; the kernel side is display.c.
 *
 * Formats (the framebuffer's, fbinfo_core.h): 4 bytes per pixel, the
 * colour 0x00RRGGBB stored little-endian (blue, green, red, unused), or
 * 3 bytes per pixel (blue, green, red). Colours are given as 0xRRGGBB in
 * both; the unused byte of a 4-byte pixel is written as 0.
 *
 * Every operation clips: a rectangle may lie partly or wholly outside the
 * surface (negative coordinates included); only the pixels inside are
 * touched, and nothing outside the surface is ever read or written. Sizes
 * and coordinates are signed 32-bit; the arithmetic is done in 64 bits, so
 * no value overflows.
 *
 * Plain loops only: the kernel is built with -fno-tree-loop-distribute-
 * patterns, so none of them becomes a memset or memcpy call (there is no
 * C library in the kernel).
 */

#include <stdbool.h>
#include <stdint.h>

struct surface {
    uint8_t *pixels;            /* the top-left pixel */
    uint32_t width;
    uint32_t height;
    uint32_t pitch;             /* bytes from one row to the next */
    uint32_t bytes_per_pixel;   /* 4 or 3 */
};

/* A rectangle: x, y its top-left corner, w x h pixels (w or h <= 0: empty). */
struct rect {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
};

/* Sets up *s; false (and *s unchanged) for a bytes_per_pixel other than
 * 3 or 4, a zero size, a null pointer or a pitch below width x bytes per
 * pixel. */
bool surface_init(struct surface *s, void *pixels, uint32_t width, uint32_t height,
                  uint32_t pitch, uint32_t bytes_per_pixel);

/* Clips *r to [0, width) x [0, height); false if nothing is left (then *r
 * is the empty rectangle at 0, 0). */
bool rect_clip(struct rect *r, uint32_t width, uint32_t height);

/* The colour 0xRRGGBB at (x, y), which must lie inside the surface. */
uint32_t surface_get(const struct surface *s, uint32_t x, uint32_t y);

/* Fills the part of `r` inside the surface with `rgb` (0xRRGGBB). */
void surface_fill(struct surface *s, struct rect r, uint32_t rgb);

/* Copies the w x h pixels at (sx, sy) of `src` to (dx, dy) of `dst`,
 * clipped against both surfaces (a source pixel outside `src` is not
 * copied, nor is one landing outside `dst`). Converts between 4 and 3 bytes
 * per pixel. The two may be the same surface only if the rectangles don't
 * overlap. */
void surface_blit(struct surface *dst, int32_t dx, int32_t dy, const struct surface *src,
                  int32_t sx, int32_t sy, int32_t w, int32_t h);

/* CRC-32 (the one of zlib and PNG: polynomial 0xEDB88320, reflected,
 * initial and final value 0xFFFFFFFF) of the surface's pixels as red,
 * green, blue bytes, row by row from the top, each row from the left: the
 * same bytes as a screendump of the screen (PPM P6, 8 bits per colour). */
uint32_t surface_crc32(const struct surface *s);

#endif
