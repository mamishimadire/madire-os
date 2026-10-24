/*
 * The toolkit's fonts (1.3 stage 2); see ui_font.h.
 */
#include "kernel/display/ui_font.h"

bool ui_fonts_for_scale(uint32_t s, struct ui_fonts *out)
{
    switch (s) {
    case 4:
        *out = (struct ui_fonts){ &font_8x16, &font_8x16_bold };
        return true;
    case 5:
        *out = (struct ui_fonts){ &font_10x20, &font_10x20_bold };
        return true;
    case 6:
        *out = (struct ui_fonts){ &font_12x24, &font_12x24_bold };
        return true;
    case 8:
        *out = (struct ui_fonts){ &font_16x32, &font_16x32_bold };
        return true;
    default:
        return false;
    }
}
