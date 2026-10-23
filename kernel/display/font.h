#ifndef MADIRE_DISPLAY_FONT_H
#define MADIRE_DISPLAY_FONT_H

/*
 * Bitmap fonts (1.2 stage 6): the 256 glyphs of code page 437, in code
 * page order, converted at build time from the pinned BDF files in
 * third_party/terminus/ by scripts/make-font.py (into build/font/; never
 * edited, never committed). One size per scale step (decision 19; the
 * Design Guide, section 3). Only the regular weight is linked so far;
 * the bold files are generated and checked, and linked by the first stage
 * that draws with them.
 */

#include <stdint.h>

struct font {
    uint8_t width;              /* pixels */
    uint8_t height;             /* pixels (rows) */
    uint8_t row_bytes;          /* bytes per row: (width + 7) / 8 */
    const uint8_t *bits;        /* 256 x height rows; the leftmost pixel in the top bit */
};

extern const struct font font_8x16;     /* 100 % */
extern const struct font font_10x20;    /* 125 % */
extern const struct font font_12x24;    /* 150 % */
extern const struct font font_16x32;    /* 200 % */

#endif
