#ifndef MADIRE_DISPLAY_UI_WIDGET_H
#define MADIRE_DISPLAY_UI_WIDGET_H

/*
 * The toolkit's label and button, the pure part (1.3 stage 2; host unit
 * tests in tests/unit/test_ui_widget.c).
 *
 * The rules (tests/desktop_ref.py draws the same pictures from them):
 *   - a label: its text in one font and colour, placed in its rectangle r
 *     (left: at r.x; centre: at r.x + floor((r.w - text width) / 2);
 *     right: at r.x + r.w - text width), vertically at
 *     r.y + floor((r.h - font height) / 2), clipped to r;
 *   - a button in state st (normal, hover or pressed) at scale s: its
 *     rectangle filled (unless the style's fill is UI_COLOUR_NONE) as a
 *     rounded rectangle of radius ui_px(radius dp, s) with the style's
 *     corners; then its icon (ui_px(icon dp, s) square, centred as a
 *     label is) or else its label (centred, the style's weight), both in
 *     the style's foreground colour for st; focused, the focus ring
 *     (Design Guide, section 9): a frame g + w outside the rectangle,
 *     w wide, in `accent`, with radius R + g + w (g = w = ui_px(2, s), R
 *     the button's clamped radius) and the style's corners;
 *   - a state whose colour the Design Guide does not define is
 *     UI_COLOUR_UNSET in the style: such a button is not drawn at all
 *     (ui_button_draw returns false), so no colour is ever drawn that the
 *     guide's contrast table (section 12) does not list.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"

#define UI_COLOUR_NONE  0xFF000000u     /* a fill left out (what is under it shows) */
#define UI_COLOUR_UNSET 0xFE000000u     /* a state the Design Guide gives no colour */

enum ui_align { UI_ALIGN_LEFT, UI_ALIGN_CENTRE, UI_ALIGN_RIGHT };

enum ui_button_state { UI_BUTTON_NORMAL, UI_BUTTON_HOVER, UI_BUTTON_PRESSED, UI_BUTTON_STATES };

struct ui_button_style {
    uint32_t fill[UI_BUTTON_STATES];
    uint32_t fg[UI_BUTTON_STATES];
    bool bold;
    int32_t radius_dp;
    uint32_t corners;
};

struct ui_button {
    struct rect r;
    const char *label;          /* drawn if icon is UI_ICON_NONE */
    int icon;                   /* ui_icon.h, or UI_ICON_NONE */
    int32_t icon_dp;
};

/* The Design Guide's buttons (sections 8 and 9). */
extern const struct ui_button_style ui_style_close;         /* a window's close button */
extern const struct ui_button_style ui_style_secondary;     /* the dialog's Cancel */
extern const struct ui_button_style ui_style_primary;       /* the dialog's Restart */
extern const struct ui_button_style ui_style_key;           /* a Calculator key */
extern const struct ui_button_style ui_style_key_equals;    /* the Calculator's = key */

/* Draws a label (the rules above). */
void ui_label_draw(const struct ui_canvas *c, struct rect r, const struct font *f, const char *text,
                   uint32_t rgb, enum ui_align align);

/* Draws button b in state st (the rules above) with the fonts of scale s.
 * False (nothing drawn) for a state outside the three, an unset colour,
 * or an unknown icon. */
bool ui_button_draw(const struct ui_canvas *c, const struct ui_button *b, const struct ui_button_style *style,
                    enum ui_button_state st, bool focused, uint32_t s, const struct ui_fonts *f);

#endif
