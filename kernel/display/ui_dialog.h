#ifndef MADIRE_DISPLAY_UI_DIALOG_H
#define MADIRE_DISPLAY_UI_DIALOG_H

/*
 * The Restart dialog, the pure part (1.3 stage 5; decision 9; the Design
 * Guide, section 9; host unit tests in tests/unit/test_ui_dialog.c).
 * tests/desktop_ref.py draws the same picture from these rules.
 *
 * A window of ui_window.h's style (its frame, title bar and close button),
 * always drawn active: 360 x 172 dp at scale s, titled "Restart" with the
 * restart icon, centred in the area above the taskbar (x = (W - w) / 2,
 * y = (bar - h) / 2, 0 if negative). Inside it, places in dp from the
 * window's top-left corner as ui_menu.h says ("at a dp" is ui_px(a) from
 * the corner, "from a to b dp" is [ui_px(a), ui_px(b))):
 *   - the text in `text`, in the console's (regular) font, at 24 dp
 *     across: "Restart Madire OS now?" at 56 dp down and "Files in Live
 *     mode are lost." at 76 dp (the guide's text, broken after the
 *     question: 39 characters of 8 dp fit the 312 dp between the paddings);
 *   - the buttons from 116 to 148 dp down: Cancel from 136 to 232 dp
 *     across (ui_style_cancel), Restart from 240 to 336 dp
 *     (ui_style_restart, bold), each ui_widget.h's button in its state, the
 *     focused one with the focus ring when the ring is on.
 * The focus is on Cancel when it opens; the ring is on if it was opened by
 * the keyboard, and turns on when Tab moves the focus to the other button.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/display/ui_draw.h"
#include "kernel/display/ui_font.h"
#include "kernel/display/ui_widget.h"
#include "kernel/display/ui_window.h"

#define UI_DIALOG_TITLE "Restart"
#define UI_DIALOG_LINE1 "Restart Madire OS now?"
#define UI_DIALOG_LINE2 "Files in Live mode are lost."

enum ui_dialog_button { UI_DLG_CANCEL, UI_DLG_RESTART, UI_DLG_BUTTONS };
enum ui_dialog_part { UI_DLG_NONE, UI_DLG_BODY, UI_DLG_PART_CANCEL, UI_DLG_PART_RESTART, UI_DLG_PART_CLOSE };

struct ui_dialog {
    struct ui_window w;         /* its frame: w.r where it lies, w.close its close button's state */
    uint32_t scale;
    struct ui_fonts fonts;
    enum ui_dialog_button focus;
    bool ring;
    enum ui_button_state btn[UI_DLG_BUTTONS];
};

/* The dialog's size at scale s. */
void ui_dialog_size(uint32_t s, int32_t *w, int32_t *h);

/* d set up at scale s with fonts f, placed on a screen `width` wide with
 * the taskbar's top at bar_y; Cancel focused, the ring as given, every
 * button normal. d's window paints d itself: d is not to be copied. */
void ui_dialog_init(struct ui_dialog *d, uint32_t s, const struct ui_fonts *f, int32_t width, int32_t bar_y,
                    bool ring);

/* Button b's rectangle on the screen. */
struct rect ui_dialog_button_rect(const struct ui_dialog *d, enum ui_dialog_button b);

/* What is at (x, y): a button, the close button, the rest of the dialog, or none. */
enum ui_dialog_part ui_dialog_hit(const struct ui_dialog *d, int32_t x, int32_t y);

/* The window's client paint (struct ui_window's paint; ctx the dialog). */
void ui_dialog_paint(const struct ui_canvas *c, struct rect r, void *ctx);

/* Draws the whole dialog (ui_window_draw, active, with its close button's state). */
void ui_dialog_draw(const struct ui_canvas *c, const struct ui_dialog *d);

#endif
