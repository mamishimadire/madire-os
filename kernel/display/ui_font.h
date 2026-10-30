#ifndef MADIRE_DISPLAY_UI_FONT_H
#define MADIRE_DISPLAY_UI_FONT_H

/*
 * The toolkit's fonts (1.3 stage 2): Terminus regular and bold, one size
 * per scale step (Design Guide, section 3): 8x16 at 4 (100 %), 10x20 at 5,
 * 12x24 at 6, 16x32 at 8 (200 %). Kept apart from ui_draw.c, which takes
 * any font, so the host unit tests can draw with fonts of their own.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/font.h"

struct ui_fonts {
    const struct font *regular;
    const struct font *bold;
};

/* The fonts of scale step s; false (and *out unchanged) for any s but 4, 5, 6 or 8. */
bool ui_fonts_for_scale(uint32_t s, struct ui_fonts *out);

#endif
